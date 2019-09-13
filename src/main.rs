mod error;
mod options;
mod lock;
mod auth;
mod util;

use std::cell::RefCell;
use std::cmp::min;
use std::io::{self, Write};
use std::time::Duration;
use std::rc::Rc;
use failure::Fail;
use nix::sys::wait::{WaitStatus, waitpid};
use nix::unistd::{Uid, Gid, ForkResult, geteuid, setresuid, setresgid, setsid, fork};
use vt::VtFlushType;
use termion::event::Key;
use termion::input::TermRead;
use crate::error::*;
use crate::options::Opt;

/// This is because a lower-numbered vt might be actually free, but systemd-logind is managing it,
/// and we don't want to step on systemd, otherwise bad things will happen.
/// We chose 13 as the lower limit because the user can manually switch up to vt number 12.
/// On most systems, the maximum number of vts is 16 or 64, so this should not be a problem.
const MIN_VT_NUMBER: i32 = 13;

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

fn run() -> Result<i32> {

    // Parse the options
    let mut opt = options::parse();

    // We need to run as root or setuid root
    if !geteuid().is_root() {
        return Err(ErrorKind::Message("Please, run simplylock as root or setuid root").into());
    }

    // Now we become fully root, in case we were started as setuid from another user
    setresgid(Gid::from_raw(0), Gid::from_raw(0), Gid::from_raw(0))
        .and_then(|_| setresuid(Uid::from_raw(0), Uid::from_raw(0), Uid::from_raw(0)))
        .context(ErrorKind::Message("Cannot setresuid/setresgid root"))?;

    // Now we fork and move to a new session so that we can be the
    // foreground process for the new terminal to be created
    match fork() {
        Ok(ForkResult::Parent { child, .. }) => {
            
            // Wait for the child to terminate
            if opt.no_detach {
                match waitpid(Some(child), None).context(ErrorKind::Message("waitpid"))? {
                    WaitStatus::Exited(_, code) => return Ok(code),
                    WaitStatus::Signaled(_, sig, _) => return Ok(128 + (sig as i32)),
                    _ => return Ok(1)
                }
            }

            // Terminate
            return Ok(0);

        }
        Ok(ForkResult::Child) => {
            // Continue below
        },
        Err(e) => {
            return Err(e.context(ErrorKind::Message("fork")).into());
        },
    }

    // Become session leader
    setsid().context(ErrorKind::Message("setsid"))?;
    
    // We clear the environment to avoid any possible interaction with PAM modules
    unsafe { nix::libc::clearenv(); }
    
    // Open console
    let console = vt::Console::open().context(ErrorKind::Message("Cannot open console device file"))?;
    
    // Lock station
    let vt = Rc::new(RefCell::new(
        console.new_vt_with_minimum_number(MIN_VT_NUMBER).context(ErrorKind::Message("Cannot allocate new terminal"))?
    ));
    let _lock = lock::Lock::with_options(&opt, &console, Rc::clone(&vt))?;

    // Put the terminal in raw mode
    vt.borrow_mut().raw().context(ErrorKind::Io)?;
    let (reader, mut writer) = util::split_stream(Rc::clone(&vt));
    let mut reader = reader.keys();

    // The auth loop
    let mut user = opt.users.first().unwrap();
    'outer: loop {

        // Flush the terminal buffers
        vt.borrow_mut().flush_buffers(VtFlushType::Both).context(ErrorKind::Io)?;

        // Repaint the console
        repaint_console(&opt, &mut writer, user)?;

        // Wait for enter to be pressed if not in quick mode.
        // If we are in quick mode, instead, jump directly to
        // authentication, and disable quick mode, so that after
        // a failed attempt, it will be requested to press enter.
        //
        // This way, if both quick mode and dark mode are enabled,
        // the user will be able to make a first login attempt
        // with the screen switched off, and then it will be turned on later.
        if !opt.quick {

            // Wait for enter
            for c in &mut reader {
                match c.context(ErrorKind::Io)? {
                    Key::Char('\n') => {
                        break;
                    },
                    Key::Ctrl('c') => {
                        
                        // Switch the screen back on before user selection
                        if opt.dark {
                            vt.borrow_mut().blank(false).context(ErrorKind::Io)?;
                        }
                        user = user_selection(&opt.users, user, &mut writer, &mut reader)?;
                        continue 'outer;

                    },
                    _ => {}
                }
            }

            // Switch the screen back on during authentication
            if opt.dark {
                vt.borrow_mut().blank(false).context(ErrorKind::Io)?;
            }

            // Repaint the console
            repaint_console(&opt, &mut writer, user)?;

        } else {
            opt.quick = false;
        }

        write!(writer, "\n\r").context(ErrorKind::Io)?;

        // Try to authenticate the user
        if auth::authenticate_user(user, auth::VtConverse::new(Rc::clone(&vt)))? {
            break;
        }

        // Switch the screen back on to be sure that the user knows
        // the authentication failed.
        if opt.dark {
            vt.borrow_mut().blank(false).context(ErrorKind::Io)?;
        }

        writeln!(writer, "\nAuthentication failed.").context(ErrorKind::Io)?;
        ::std::thread::sleep(Duration::from_secs(3));
    }

    Ok(0)
}

fn main() {
    ::std::process::exit(match run() {
        Err(err) => {
            let mut is_first = true;
            let mut notes: Vec<&'static str> = Vec::new();
            for f in (&err as &dyn Fail).iter_chain() {
                
                // Accumulate the notes to show them all in the end
                let note =
                    f.downcast_ref::<Error>()
                    .map(Error::kind)
                    .and_then(|k| match *k {
                        ErrorKind::Note(note) => Some(note),
                        _ => None
                    });

                match (note, is_first) {
                    (Some(n), _) => notes.push(n),
                    (None, false) => eprintln!("  => {}", f),
                    (None, true) => {
                        eprintln!("Error: {}", f);
                        is_first = false;
                    }
                }
            }

            for n in notes {
                eprintln!("Note: {}", n);
            }

            1
        },
        Ok(code) => {
            code
        }
    });
}
