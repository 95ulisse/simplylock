use std::cell::RefCell;
use std::ffi::{CString, CStr};
use std::io::Write;
use std::{mem, ptr};
use std::rc::Rc;
use nix::libc::{c_int, c_void, calloc, free, size_t, strdup};
use pam_sys::{PamHandle, PamConversation, PamMessage, PamMessageStyle, PamResponse, PamReturnCode};
use termion::input::TermRead;
use vt::Vt;
use crate::error::*;

const PAM_SERVICE_NAME: &str = "simplylock";

/// A trait representing a callback-style interface to the PAM authentication conversation.
/// PAM asks question to the user through our application, and it's our job to rely the user's anwers.
pub trait Converse {

    /// Incoming question for the user.
    /// The parameter `blind` specifies if PAM wants the user to be able to see what they are typing
    /// (usually, when the user types a password, it should not be echoed back).
    fn prompt(&mut self, msg: &CStr, blind: bool) -> ::std::result::Result<CString, ()>;

    /// Incoming informational message from PAM.
    /// No response is expected.
    fn info(&mut self, msg: &CStr) -> ::std::result::Result<(), ()>;

    /// Incoming error message from PAM.
    /// No response is expected.
    fn error(&mut self, msg: &CStr) -> ::std::result::Result<(), ()>;

}

extern "C" fn conversation_function<C: Converse>(
    num_msg: c_int,
    msg: *mut *mut PamMessage,
    out_resp: *mut *mut PamResponse,
    appdata_ptr: *mut c_void,
) -> c_int {

    // allocate space for responses
    let resp = unsafe {
        calloc(num_msg as usize, mem::size_of::<PamResponse>() as size_t) as *mut PamResponse
    };
    if resp.is_null() {
        return PamReturnCode::CONV_ERR as c_int;
    }

    let handler = unsafe { &mut *(appdata_ptr as *mut C) };

    let mut result: PamReturnCode = PamReturnCode::SUCCESS;
    for i in 0..num_msg as isize {
        unsafe {

            // Some usefult references
            let m: &mut PamMessage = &mut **(msg.offset(i));
            let r: &mut PamResponse = &mut *(resp.offset(i));
            let msg = CStr::from_ptr(m.msg);

            // Invoke the correct callback
            match PamMessageStyle::from(m.msg_style) {
                PamMessageStyle::PROMPT_ECHO_ON |
                PamMessageStyle::PROMPT_ECHO_OFF => {
                    let is_blind = m.msg_style == PamMessageStyle::PROMPT_ECHO_OFF as i32;
                    if let Ok(handler_response) = handler.prompt(msg, is_blind) {
                        r.resp = strdup(handler_response.as_ptr());
                        r.resp_retcode = 0;
                        if r.resp.is_null() {
                            result = PamReturnCode::CONV_ERR;
                        }
                    } else {
                        result = PamReturnCode::CONV_ERR;
                    }
                }
                PamMessageStyle::ERROR_MSG => {
                    if handler.error(msg).is_err() {
                        result = PamReturnCode::CONV_ERR;
                    }
                }
                PamMessageStyle::TEXT_INFO => {
                    if handler.info(msg).is_err() {
                        result = PamReturnCode::CONV_ERR;
                    }
                }
            }

        }
        if result != PamReturnCode::SUCCESS {
            break;
        }
    }

    // free allocated memory if an error occured
    if result != PamReturnCode::SUCCESS {
        unsafe {

            // Free any other string allocated with `strdup` in all the responses
            for i in 0..num_msg as isize {
                let m: &mut PamMessage = &mut **(msg.offset(i));
                let r: &mut PamResponse = &mut *(resp.offset(i));

                if r.resp.is_null() {
                    continue;
                }

                match PamMessageStyle::from(m.msg_style) {
                    PamMessageStyle::PROMPT_ECHO_ON |
                    PamMessageStyle::PROMPT_ECHO_OFF => {
                        free(r.resp as *mut c_void);
                    },
                    _ => {}
                }
            }

            // Free the response array
            free(resp as *mut c_void);

        };
    } else {
        unsafe { *out_resp = resp };
    }

    result as c_int
}

/// Implementation of [`Converse`](crate::auth::Converse) that uses a [`Vt`](vt::Vt) for I/O.
pub struct VtConverse<'a> {
    vt: Rc<RefCell<Vt<'a>>>
}

impl<'a> VtConverse<'a> {

    /// Creates a new `VtConverse` that will use the given `Vt` for I/O.
    pub fn new(vt: Rc<RefCell<Vt<'a>>>) -> VtConverse<'a> {
        VtConverse { vt }
    }

}

impl<'a> Converse for VtConverse<'a> {

    fn prompt(&mut self, msg: &CStr, _blind: bool) -> ::std::result::Result<CString, ()> {

        // Print prompt
        write!(self.vt.borrow_mut(), "{}", msg.to_string_lossy()).map_err(|_| ())?;
        
        // Read line
        let line = self.vt.borrow_mut().read_line().map_err(|_| ())?.unwrap_or_else(String::new);

        // Append manually a newline
        write!(self.vt.borrow_mut(), "\n\r").map_err(|_| ())?;

        CString::new(line).map_err(|_| ())

    }

    fn info(&mut self, msg: &CStr) -> ::std::result::Result<(), ()> {
        write!(self.vt.borrow_mut(),
               "{}\n\r",
               msg.to_string_lossy()).map_err(|_| ())
    }
    
    fn error(&mut self, msg: &CStr) -> ::std::result::Result<(), ()> {
        write!(self.vt.borrow_mut(),
               "{}{}{}{}\n\r",
               termion::style::Bold,
               termion::color::Fg(termion::color::Red),
               msg.to_string_lossy(),
               termion::style::Reset).map_err(|_| ())
    }

}

/// Creates a user-friendly error message from a PAM error code.
fn create_pam_error(code: PamReturnCode) -> Error {
    let null_handle: *mut PamHandle = ptr::null_mut();
    let handle = unsafe { &mut *null_handle }; // `pam_strerror` does not use this parameter
    let message = pam_sys::strerror(handle, code).unwrap_or("Error");
    Error::from(ErrorKind::Pam(message.to_string()))
}

/// Authenticates the user with the given name using PAM.
pub fn authenticate_user<C: Converse>(user: &str, converse: C) -> Result<bool> {

    let mut converse = Box::new(converse);
    let conv = PamConversation {
        conv: Some(conversation_function::<C>),
        data_ptr: (&mut *converse) as *mut C as *mut c_void,
    };

    // Begin pam transaction
    let mut handle: *mut PamHandle = ptr::null_mut();
    let mut code = pam_sys::start(PAM_SERVICE_NAME, Some(user), &conv, &mut handle);
    if code != PamReturnCode::SUCCESS {
        return Err(create_pam_error(code));
    }

    let handle = unsafe { &mut *handle };

    // Authentication
    code = pam_sys::authenticate(handle, pam_sys::PamFlag::NONE);

    // Authorization
    if code == PamReturnCode::SUCCESS {
        code = pam_sys::acct_mgmt(handle, pam_sys::PamFlag::NONE);
    }

    // End pam transaction
    let auth_code = code;
    code = pam_sys::end(handle, code);
    
    if code != PamReturnCode::SUCCESS {
        Err(create_pam_error(code))
    } else if auth_code != PamReturnCode::SUCCESS && auth_code != PamReturnCode::AUTH_ERR {
        Err(create_pam_error(auth_code))
    } else {
        Ok(auth_code == PamReturnCode::SUCCESS)
    }

}