use std::fs::File;
use std::io::Read;
use std::path::Path;
use std::str::FromStr;
use vt::{Console, Vt};
use crate::options::Opt;
use crate::error::*;

const SYSRQ_PATH: &str = "/proc/sys/kernel/sysrq";
const PRINTK_PATH: &str = "/proc/sys/kernel/printk";

/// This is because a lower-numbered vt might be actually free, but systemd-logind is managing it,
/// and we don't want to step on systemd, otherwise bad things will happen.
/// We chose 13 as the lower limit because the user can manually switch up to vt number 12.
/// On most systems, the maximum number of vts is 16 or 64, so this should not be a problem.
const MIN_VT_NUMBER: u16 = 13;

fn read_from_file<T, P>(path: P) -> Result<T>
    where T: FromStr,
          <T as FromStr>::Err: failure::Fail,
          P: AsRef<Path>
{
    let n = File::open(path)
        .context(ErrorKind::Io)
        .and_then(|mut f| {
            let mut s = String::new();
            f.read_to_string(&mut s)
                .context(ErrorKind::Io)
                .map(|_| s)
        })
        .and_then(|s| s.trim().parse::<T>().context(ErrorKind::Parse))?;
    Ok(n)
}

/// Whole-station lock.
/// 
/// For as long as a `Lock` structure is in scope, the station is locked.
/// When this structure is dropped, the station is unlocked.
/// 
/// Lock the station by calling [`Lock::with_options`](crate::lock::Lock::with_options).
pub struct Lock<'a> {
    original_vt: Vt<'a>,
    original_sysrq: Option<u32>,
    original_printk: Option<u32>,
    lock_vt: Vt<'a>
}

impl<'a> Lock<'a> {

    /// Lock the current station with the given options.
    /// 
    /// This function allocates and switches to a new virtual terminal and inhibits sysrequests
    /// and kernel messages (unless disabled by the options).
    pub fn with_options(opt: &Opt, console: &'a Console) -> Result<Lock<'a>> {

        // Save the state of sysrq and kernel messages
        let original_sysrq: Option<u32> = if opt.no_sysrq {
            None
        } else {
            Some(
                read_from_file(SYSRQ_PATH)
                    .context(ErrorKind::Path(SYSRQ_PATH.into()))
                    .context(ErrorKind::Note("Please, consider running with -s to keep sysrequests enabled."))?
            )
        };
        let original_printk: Option<u32> = if opt.no_kernel_messages {
            None
        } else {
            Some(
                read_from_file(PRINTK_PATH)
                    .context(ErrorKind::Path(PRINTK_PATH.into()))
                    .context(ErrorKind::Note("Please, consider running with -k to keep kernel messages visible."))?
            )
        };

        // Save the current vt and allocate a new one
        let original_vt = console.current_vt()
            .context(ErrorKind::Message("Cannot get current terminal"))?;
        let lock_vt = console.new_vt_with_minimum_number(MIN_VT_NUMBER)
            .context(ErrorKind::Message("Cannot allocate new terminal"))?;

        Ok(Lock {
            original_vt,
            original_sysrq,
            original_printk,
            lock_vt
        })

    }

    /// Returns a reference to the [`Vt`](vt::Vt) used to lock the station.
    pub fn vt(&self) -> &Vt<'a> {
        &self.lock_vt
    }

    /// Returns a mutable reference to the [`Vt`](vt::Vt) used to lock the station.
    pub fn vt_mut(&mut self) -> &mut Vt<'a> {
        &mut self.lock_vt
    }

}

impl<'a> Drop for Lock<'a> {

    /// Unlocks the station.
    fn drop(&mut self) {

    }

}