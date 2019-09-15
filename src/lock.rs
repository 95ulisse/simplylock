use std::cell::RefCell;
use std::cmp::min;
use std::fs::{File, OpenOptions};
use std::io::{self, Read, Write};
use std::path::Path;
use std::rc::Rc;
use std::time::Duration;
use atoi::atoi;
use termion::event::Key;
use termion::input::TermRead;
use vt::{Console, Vt, VtNumber, VtFlushType};
use crate::auth;
use crate::options::Opt;
use crate::error::*;
use crate::util;

const SYSRQ_PATH: &str = "/proc/sys/kernel/sysrq";
const PRINTK_PATH: &str = "/proc/sys/kernel/printk";

fn read_u32_from_file<P>(path: P) -> Result<u32>
    where P: AsRef<Path>
{
    // Open the file and read all its contents to a string
    let mut f = File::open(path).context(ErrorKind::Io)?;
    let mut s = String::new();
    f.read_to_string(&mut s).context(ErrorKind::Io)?;

    // Parse the beginning of the string as an integer
    let n = atoi(s.trim_start().as_bytes()).ok_or(ErrorKind::Parse)?;
    
    Ok(n)
}

fn write_u32_to_file<P>(path: P, data: u32) -> Result<()>
    where P: AsRef<Path>
{
    let mut f = OpenOptions::new().write(true).open(path).context(ErrorKind::Io)?;
    write!(f, "{}", data).context(ErrorKind::Io)?;
    Ok(())
}

fn user_selection<'a, W, R>(users: &'a [String], current_user: &'a str, vt: &mut W, input: &mut R) -> Result<&'a String>
    where W: Write,
          R: Iterator<Item = io::Result<termion::event::Key>>
{
    let mut current_index = users.iter().position(|x| x == current_user).unwrap();
    'outer: loop {

        // Clear the terminal
        write!(vt, "{}", termion::clear::All).context(ErrorKind::Io)?;

        // A nice message with the list of users allowed to unlock
        write!(vt,
            "{}The following users are unthorized to unlock:{}",
            termion::cursor::Goto(1, 2),
            termion::cursor::Goto(1, 4)).context(ErrorKind::Io)?;
        for (i, u) in users.iter().enumerate() {
            if current_index == i {
                write!(vt,
                       "{}{}=> {}{}\n\r",
                       termion::style::Bold,
                       termion::color::Fg(termion::color::LightBlue),
                       u,
                       termion::style::Reset).context(ErrorKind::Io)?;
            } else {
                write!(vt, " - {}\n\r", u).context(ErrorKind::Io)?;
            }
        }

        write!(vt, "\nUse the arrow keys to select the user that wants to unlock and press enter.").context(ErrorKind::Io)?;

        while let Some(c) = input.next() {
            match c.context(ErrorKind::Io)? {
                Key::Up => {
                    current_index = current_index.saturating_sub(1);
                    continue 'outer;
                },
                Key::Down => {
                    current_index = min(current_index + 1, users.len() - 1);
                    continue 'outer;
                },
                Key::Char('\n') => {
                    break 'outer;
                }
                _ => ()
            }
        }

        return Err(ErrorKind::Message("Unexpected EOF.").into());

    }

    Ok(&users[current_index])
}

fn repaint_console<W: Write>(opt: &Opt, vt: &mut W, user: &str) -> Result<()> {
    
    // Clear the terminal
    write!(vt, "{}", termion::clear::All).context(ErrorKind::Io)?;

    // Print the unlock message
    let mut line = 2;
    if let Some(ref message) = opt.message {
        writeln!(vt,
                 "{}{}",
                 termion::cursor::Goto(1, line),
                 message).context(ErrorKind::Io)?;
        line += 2;
    }
    write!(vt,
           "{}Press enter to unlock as {}{}{}{}. [Press Ctrl+C to change user] ",
           termion::cursor::Goto(1, line),
           termion::style::Bold,
           termion::color::Fg(termion::color::LightBlue),
           user,
           termion::style::Reset
           ).context(ErrorKind::Io)?;

    Ok(())
}

/// Whole-station lock.
/// 
/// For as long as a `Lock` structure is in scope, the station is locked.
/// When this structure is dropped, the station is unlocked.
/// 
/// Lock the station by calling [`Lock::with_options`](crate::lock::Lock::with_options).
pub struct Lock<'a> {
    opt: Opt,
    console: &'a Console,
    original_vt: VtNumber,
    original_sysrq: Option<u32>,
    original_printk: Option<u32>,
    lock_vt: Rc<RefCell<Vt<'a>>>
}

impl<'a> Lock<'a> {

    /// Lock the current station with the given options.
    /// 
    /// This function switches to a new virtual terminal and inhibits sysrequests
    /// and kernel messages (unless disabled by the options).
    pub fn new(opt: Opt, console: &'a Console, lock_vt: Vt<'a>) -> Result<Lock<'a>> {

        let lock_vt = Rc::new(RefCell::new(lock_vt));

        // Save the current vt
        let original_vt = console.current_vt_number()
            .context(ErrorKind::Message("Cannot get current terminal"))?;

        // Block sysrq and printk while saving their original values
        let original_sysrq: Option<u32> = if opt.no_sysrq {
            None
        } else {
            let value =
                read_u32_from_file(SYSRQ_PATH)
                .and_then(|value|
                    write_u32_to_file(SYSRQ_PATH, 0)
                        .map(|_| value)
                )
                .context(ErrorKind::Path(SYSRQ_PATH.into()))
                .context(ErrorKind::Note("Please, consider running with -s to keep sysrequests enabled."))?;
            Some(value)
        };
        let original_printk: Option<u32> = if opt.no_kernel_messages {
            None
        } else {
            let value =
                read_u32_from_file(PRINTK_PATH)
                .and_then(|value|
                    write_u32_to_file(PRINTK_PATH, 0)
                        .map(|_| value)
                )
                .context(ErrorKind::Path(PRINTK_PATH.into()))
                .context(ErrorKind::Note("Please, consider running with -k to keep kernel messages visible."))?;
            Some(value)
        };

        // Activate the new vt
        lock_vt.borrow().switch().context(ErrorKind::Io)?;

        // Lock vt switching
        if !opt.no_lock {
            console.lock_switch(true).context(ErrorKind::Io)?;
        }

        // Blank the screen
        if opt.dark {
            lock_vt.borrow_mut().blank(true).context(ErrorKind::Io)?;
        }

        Ok(Lock {
            opt,
            console,
            original_vt,
            original_sysrq,
            original_printk,
            lock_vt
        })

    }

    /// Runs the main authentication loop. Returns when the user has successfully unlocked the station.
    pub fn run_loop(&mut self) -> Result<()> {
        
        // Put the terminal in raw mode
        self.lock_vt.borrow_mut().raw().context(ErrorKind::Io)?;
        let (reader, mut writer) = util::split_stream(Rc::clone(&self.lock_vt));
        let mut reader = reader.keys();

        let mut user = self.opt.users.first().unwrap();
        'outer: loop {

            // Flush the terminal buffers
            self.lock_vt.borrow_mut().flush_buffers(VtFlushType::Both).context(ErrorKind::Io)?;

            // Repaint the console
            repaint_console(&self.opt, &mut writer, user)?;

            // Wait for enter to be pressed if not in quick mode.
            // If we are in quick mode, instead, jump directly to
            // authentication, and disable quick mode, so that after
            // a failed attempt, it will be requested to press enter.
            //
            // This way, if both quick mode and dark mode are enabled,
            // the user will be able to make a first login attempt
            // with the screen switched off, and then it will be turned on later.
            if !self.opt.quick {

                // Wait for enter
                for c in &mut reader {
                    match c.context(ErrorKind::Io)? {
                        Key::Char('\n') => {
                            break;
                        },
                        Key::Ctrl('c') => {
                            
                            // Switch the screen back on before user selection
                            if self.opt.dark {
                                self.lock_vt.borrow_mut().blank(false).context(ErrorKind::Io)?;
                            }
                            user = user_selection(&self.opt.users, user, &mut writer, &mut reader)?;
                            continue 'outer;

                        },
                        _ => {}
                    }
                }

                // Switch the screen back on during authentication
                if self.opt.dark {
                    self.lock_vt.borrow_mut().blank(false).context(ErrorKind::Io)?;
                }

                // Repaint the console
                repaint_console(&self.opt, &mut writer, user)?;

            } else {
                self.opt.quick = false;
            }

            write!(writer, "\n\r").context(ErrorKind::Io)?;

            // Try to authenticate the user
            if auth::authenticate_user(user, auth::VtConverse::new(Rc::clone(&self.lock_vt)))? {
                break;
            }

            // Switch the screen back on to be sure that the user knows
            // the authentication failed.
            if self.opt.dark {
                self.lock_vt.borrow_mut().blank(false).context(ErrorKind::Io)?;
            }

            write!(writer, "\nAuthentication failed.\n\r").context(ErrorKind::Io)?;
            ::std::thread::sleep(Duration::from_secs(3));
        }

        Ok(())
    }
}

impl<'a> Drop for Lock<'a> {

    /// Unlocks the station.
    fn drop(&mut self) {

        // Unblank the terminal
        if self.opt.dark {
            let _ = self.lock_vt.borrow_mut().blank(false);
        }

        // Re-enable vt switching
        if !self.opt.no_lock {
            let _ = self.console.lock_switch(false);
        }

        // Switch to the original vt
        let _ = self.console.switch_to(self.original_vt);

        // Restore the original state of sysrq and printk
        if let Some(value) = self.original_sysrq {
            let _ = write_u32_to_file(SYSRQ_PATH, value);
        }
        if let Some(value) = self.original_printk {
            let _ = write_u32_to_file(PRINTK_PATH, value);
        }

    }

}