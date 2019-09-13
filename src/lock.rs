use std::cell::RefCell;
use std::fs::{File, OpenOptions};
use std::io::{Read, Write};
use std::path::Path;
use std::rc::Rc;
use atoi::atoi;
use vt::{Console, Vt, VtNumber};
use crate::options::Opt;
use crate::error::*;

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

/// Whole-station lock.
/// 
/// For as long as a `Lock` structure is in scope, the station is locked.
/// When this structure is dropped, the station is unlocked.
/// 
/// Lock the station by calling [`Lock::with_options`](crate::lock::Lock::with_options).
pub struct Lock<'a> {
    console: &'a Console,
    original_vt: VtNumber,
    original_sysrq: Option<u32>,
    original_printk: Option<u32>,
    lock_vt: Rc<RefCell<Vt<'a>>>,
    vt_switch_locked: bool,
    vt_blanked: bool
}

impl<'a> Lock<'a> {

    /// Lock the current station with the given options.
    /// 
    /// This function switches to a new virtual terminal and inhibits sysrequests
    /// and kernel messages (unless disabled by the options).
    pub fn with_options(opt: &Opt, console: &'a Console, lock_vt: Rc<RefCell<Vt<'a>>>) -> Result<Lock<'a>> {

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
            console,
            original_vt,
            original_sysrq,
            original_printk,
            lock_vt,
            vt_switch_locked: !opt.no_lock,
            vt_blanked: opt.dark
        })

    }
    
}

impl<'a> Drop for Lock<'a> {

    /// Unlocks the station.
    fn drop(&mut self) {

        // Unblank the terminal
        if self.vt_blanked {
            let _ = self.lock_vt.borrow_mut().blank(false);
        }

        // Re-enable vt switching
        if self.vt_switch_locked {
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