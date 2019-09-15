mod error;
mod options;
mod lock;
mod auth;
mod util;

use std::fs::File;
use std::io::{Write, Read};
use std::os::unix::io::FromRawFd;
use failure::Fail;
use nix::sys::wait::{WaitStatus, waitpid};
use nix::unistd::{Uid, Gid, ForkResult, geteuid, setresuid, setresgid, setsid, fork, close};
use crate::error::*;

/// This is because a lower-numbered vt might be actually free, but systemd-logind is managing it,
/// and we don't want to step on systemd, otherwise bad things will happen.
/// We chose 13 as the lower limit because the user can manually switch up to vt number 12.
/// On most systems, the maximum number of vts is 16 or 64, so this should not be a problem.
const MIN_VT_NUMBER: i32 = 13;

fn run() -> Result<i32> {

    // Parse the options
    let opt = options::parse();

    // We need to run as root or setuid root
    if !geteuid().is_root() {
        return Err(ErrorKind::Message("Please, run simplylock as root or setuid root").into());
    }

    // Now we become fully root, in case we were started as setuid from another user
    setresgid(Gid::from_raw(0), Gid::from_raw(0), Gid::from_raw(0))
        .and_then(|_| setresuid(Uid::from_raw(0), Uid::from_raw(0), Uid::from_raw(0)))
        .context(ErrorKind::Message("Cannot setresuid/setresgid root"))?;

    // Create a pipe. This pipe will be used by the forked process to signal to the parent
    // that the station has been locked and it can safely detach
    let pipe = nix::unistd::pipe().context(ErrorKind::Message("Cannot create anonymous pipe"))?;

    // Now we fork and move to a new session so that we can be the
    // foreground process for the new terminal to be created
    match fork() {
        Ok(ForkResult::Parent { child, .. }) => {
            
            // Close the parent clone of the write end of the pipe
            let _ = close(pipe.1);

            // Wait for the station to be completely locked, or wait until it is unlocked if `-D` is passed.
            if opt.no_detach {
                match waitpid(Some(child), None).context(ErrorKind::Message("waitpid"))? {
                    WaitStatus::Exited(_, code) => return Ok(code),
                    WaitStatus::Signaled(_, sig, _) => return Ok(128 + (sig as i32)),
                    _ => return Ok(1)
                }
            } else {
                let mut pipe_r = unsafe { File::from_raw_fd(pipe.0) };
                let mut buf = [ 0u8 ];
                while buf[0] != 0x42 {
                    pipe_r.read_exact(&mut buf).context(ErrorKind::Io)?;
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

    // Close the reader end of the pipe
    let _ = close(pipe.0);

    // Become session leader
    setsid().context(ErrorKind::Message("setsid"))?;
    
    // We clear the environment to avoid any possible interaction with PAM modules
    unsafe { nix::libc::clearenv(); }
    
    // Open console
    let console = vt::Console::open().context(ErrorKind::Message("Cannot open console device file"))?;
    
    // Lock station
    let vt = console.new_vt_with_minimum_number(MIN_VT_NUMBER).context(ErrorKind::Message("Cannot allocate new terminal"))?;
    let mut lock = lock::Lock::new(opt, &console, vt)?;

    // Signal to the parent process that the station has been fully locked
    {
        let mut pipe_w = unsafe { File::from_raw_fd(pipe.1) };
        pipe_w.write_all(&[ 0x42u8 ]).context(ErrorKind::Io)?;
    }

    // The auth loop
    lock.run_loop()?;

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

                match note {
                    Some(n) => notes.push(n),
                    None => {
                        if is_first {
                            eprintln!("Error: {}", f);
                        } else {
                            eprintln!("  => {}", f)
                        }
                        if let Some(bt) = f.backtrace() {
                            eprintln!("{}", bt);
                        }
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
