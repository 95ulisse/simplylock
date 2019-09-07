mod error;
mod options;
mod lock;

use std::io::Write;
use failure::Fail;
use crate::error::*;

fn run() -> Result<()> {
    let opt = options::parse();
    
    let console = vt::Console::open().context(ErrorKind::Message("Cannot open console device file"))?;
    
    let mut lock = lock::Lock::with_options(&opt, &console)?;
    
    let vt = lock.vt_mut();
    vt.clear();
    write!(vt, "{:#?}", opt);
    vt.flush();

    std::thread::sleep(std::time::Duration::from_secs(10));
    
    Ok(())
}

fn main() {
    if let Err(err) = run() {

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
    }
}
