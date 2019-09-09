mod error;
mod options;
mod lock;
mod auth;

use std::io::{Write, Read};
use std::time::Duration;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use nix::sys::signal::{Signal, SigSet};
use nix::sys::wait::{WaitStatus, waitpid};
use nix::unistd::{Uid, Gid, ForkResult, geteuid, setresuid, setresgid, setsid, fork};
use failure::Fail;
use vt::{Vt, VtSignals, VtFlushType};
use colored::*;
use crate::error::*;
use crate::options::Opt;

fn user_selection(opt: &Opt, vt: &mut Vt, user: &mut &str) -> Result<()> {
    writeln!(vt, "{}", "User selection!".red()).context(ErrorKind::Io)?;
    Ok(())
}

fn repaint_console(opt: &Opt, vt: &mut Vt, user: &str) -> Result<()> {
    
    // Clear the terminal
    vt.clear().context(ErrorKind::Io)?;
    vt.flush_buffers(VtFlushType::Both).context(ErrorKind::Io)?;

    // Print the unlock message
    if let Some(ref message) = opt.message {
        writeln!(vt, "\n{}", message).context(ErrorKind::Io)?;
    }
    write!(vt, "\nPress enter to unlock as {}. [Press Ctrl+C to change user] ", user.bold().blue()).context(ErrorKind::Io)?;

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

    // Block all the signals with the exception of SIGINT
    let mut sigset = SigSet::all();
    sigset.remove(Signal::SIGINT);
    sigset.thread_set_mask().context(ErrorKind::Message("pthread_setmask"))?;

    // Register a handler for SIGINT which sets a flag when invoked
    let is_user_selection_requested = Arc::new(AtomicBool::new(false));
    signal_hook::flag::register(signal_hook::SIGINT, Arc::clone(&is_user_selection_requested))
        .context(ErrorKind::Message("Failed signal handler registration"))?;

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
    
    // Open console
    let console = vt::Console::open().context(ErrorKind::Message("Cannot open console device file"))?;
    
    // Lock station
    let mut lock = lock::Lock::with_options(&opt, &console)?;

    // Enable Ctrl+C on the terminal
    let vt: &mut Vt = lock.vt();
    vt.signals(VtSignals::SIGINT).context(ErrorKind::Io)?;
    
    // We clear the environment to avoid any possible interaction with PAM modules
    unsafe { nix::libc::clearenv(); }

    // The auth loop
    let mut user = &opt.users.first().unwrap()[..];
    'outer: loop {

        // Reset the flag for the user selection
        is_user_selection_requested.store(false, Ordering::Relaxed);

        // Repaint the console
        repaint_console(&opt, vt, user)?;

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
            let mut buf = [0u8];
            loop {
                match vt.read(&mut buf) {
                    Err(e) => {
                        if e.kind() == std::io::ErrorKind::Interrupted {
                            if is_user_selection_requested.load(Ordering::Relaxed) {
                                user_selection(&opt, vt, &mut user)?;
                                continue 'outer;
                            }
                        } else {
                            return Err(e.context(ErrorKind::Io).into());
                        }
                    },
                    Ok(0) => return Err(Error::from(ErrorKind::Message("Unexpected EOF")).context(ErrorKind::Io).into()),
                    Ok(1) => {
                        if buf[0] == b'\n' {
                            // We finally got an enter!
                            break;
                        }
                    },
                    Ok(_) => unreachable!()
                }
            }

            // Switch the screen back on during authentication
            if opt.dark {
                vt.blank(false).context(ErrorKind::Io)?;
            }

            // Repaint the console
            repaint_console(&opt, vt, user)?;
            writeln!(vt).context(ErrorKind::Io)?;

        } else {
            opt.quick = false;
            writeln!(vt).context(ErrorKind::Io)?;
        }

        // Try to authenticate the user
        if auth::authenticate_user(user, auth::VtConverse::new(vt))? {
            break;
        }

        // Switch the screen back on to be sure that the user knows
        // the authentication failed.
        if opt.dark {
            vt.blank(false).context(ErrorKind::Io)?;
        }

        writeln!(vt, "\nAuthentication failed.").context(ErrorKind::Io)?;
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
