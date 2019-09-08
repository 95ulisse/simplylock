use std::ffi::{CString, CStr};
use std::io::{Write, BufRead};
use std::{mem, ptr};
use std::os::unix::prelude::*;
use nix::libc::{c_int, c_void, calloc, free, size_t, strdup};
use nix::unistd::isatty;
use nix::sys::termios::{Termios, LocalFlags, SetArg, tcgetattr, tcsetattr};
use pam_sys::{PamHandle, PamConversation, PamMessage, PamMessageStyle, PamResponse, PamReturnCode};
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
    fn info(&mut self, msg: &CStr);

    /// Incoming error message from PAM.
    /// No response is expected.
    fn error(&mut self, msg: &CStr);

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
                    handler.error(msg);
                }
                PamMessageStyle::TEXT_INFO => {
                    handler.info(msg);
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

/// Common implementation of the [`Converse`](crate::auth::Converse) trait using stdin/stdout
/// to interface with the user.
pub struct StdioConverse;

impl StdioConverse {

    /// Creates a new `StdioConverse`.
    pub fn new() -> StdioConverse {
        StdioConverse {}
    }

}

impl Converse for StdioConverse {
    
    fn prompt(&mut self, msg: &CStr, blind: bool) -> ::std::result::Result<CString, ()> {
        
        // Print prompt
        print!("{}", msg.to_string_lossy());
        ::std::io::stdout().flush().map_err(|_| ())?;
        
        // Switch off echo if required
        let stdin_fd = ::std::io::stdin().as_raw_fd();
        let mut termios: Option<Termios> = None;
        if blind && isatty(stdin_fd).map_err(|_| ())? {
            let mut t = tcgetattr(stdin_fd).map_err(|_| ())?;
            t.local_flags &= !(LocalFlags::ECHO);
            tcsetattr(stdin_fd, SetArg::TCSANOW, &t).map_err(|_| ())?;
            termios = Some(t);
        }

        // Read line
        let mut line = String::new();
        let stdin = ::std::io::stdin();
        stdin.lock().read_line(&mut line).unwrap();
        let line_without_delimiter = line.trim_end_matches('\n');

        // Switch echo back on
        if let Some(mut t) = termios {
            t.local_flags |= LocalFlags::ECHO;
            tcsetattr(stdin_fd, SetArg::TCSANOW, &t).map_err(|_| ())?;

            // Append manually a newline
            println!();
        }

        CString::new(line_without_delimiter).map_err(|_| ())
    }
    
    fn info(&mut self, msg: &CStr) {
        println!("{}", msg.to_string_lossy());
    }
    
    fn error(&mut self, msg: &CStr) {
        println!("{}", msg.to_string_lossy());
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
pub fn authenticate_user<C: Converse>(user: &str, converse: C) -> Result<()> {

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
    if code != PamReturnCode::SUCCESS {
        return Err(create_pam_error(code));
    }

    // Authorization
    code = pam_sys::acct_mgmt(handle, pam_sys::PamFlag::NONE);
    if code != PamReturnCode::SUCCESS {
        return Err(create_pam_error(code));
    }

    // End pam transaction
    code = pam_sys::end(handle, code);
    if code != PamReturnCode::SUCCESS {
        return Err(create_pam_error(code));
    }

    Ok(())

}