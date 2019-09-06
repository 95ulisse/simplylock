use std::path::PathBuf;
use std::str::FromStr;
use pwd::Passwd;
use structopt::StructOpt;
use users::{get_user_by_uid, get_current_uid};

#[derive(Debug)]
pub enum BackgroundFill {
    Center,
    Stretch,
    Resize,
    ResizeFill
}

impl FromStr for BackgroundFill {
    type Err = &'static str;
    fn from_str(s: &str) -> Result<Self, Self::Err> {
        match &*(s.to_ascii_lowercase()) {
            "center" => Ok(BackgroundFill::Center),
            "stretch" => Ok(BackgroundFill::Stretch),
            "resize" => Ok(BackgroundFill::Resize),
            "resize-fill" => Ok(BackgroundFill::ResizeFill),
            _ => Err("Invalid background-fill value")
        }
    }
}

#[derive(Debug, StructOpt)]
#[structopt()]
pub struct Opt {

    /// Keep sysrequests enabled
    #[structopt(short = "s", long)]
    pub no_sysreq: bool,

    /// Do not lock terminal switching
    #[structopt(short = "l", long)]
    pub no_lock: bool,

    /// Do not mute kernel messages while the console is locked
    #[structopt(short = "k", long)]
    pub no_kernel_messages: bool,

    /// Comma separated list of users allowed to unlock
    /// 
    /// Note that the root user will always be albe to unlock
    #[structopt(short, long, use_delimiter = true)]
    pub users: Vec<String>,

    /// Allow only the root user to unlock even if it has no password
    /// 
    /// This is a security check to avoid locking the station without being able to unlock it.
    #[structopt(long, hidden = true)]
    pub allow_passwordless_root: bool,

    /// Display the given message before the prompt
    #[structopt(short, long)]
    pub message: Option<String>,

    /// Dark mode: switch off the screen after locking
    #[structopt(short, long)]
    pub dark: bool,

    /// Quick mode: do not wait for enter to be pressed to unlock
    #[structopt(short, long)]
    pub quick: bool,

    /// Set background image
    #[structopt(short, long)]
    pub background: Option<PathBuf>,

    /// Background fill mode
    #[structopt(
        long,
        long_help = "Background fill mode. Available values:
- center: center the image without resizing it.
- stretch: stretch the image to fill all the available space.
- resize: like stretch, but keeps image proportions.
- resize-fill: resize the image to fill the screen but keep proportions.\n

"
    )]
    pub background_fill: Option<BackgroundFill>,

    /// Path to the framebuffer device to use to draw the background
    #[structopt(long)]
    pub fbdev: Option<PathBuf>,

    /// Dont't detach: waits for the screen to be unlocked before returning
    #[structopt(short = "D", long)]
    pub no_detach: bool

}

/// Parses the command line options.
pub fn parse() -> Opt {
    let mut opt = Opt::from_args();

    // If no user was manually provided, use the user that started the application
    if opt.users.is_empty() {
        let user = get_user_by_uid(get_current_uid()).unwrap();
        opt.users.push(user.name().to_string_lossy().to_string());
    }

    // Add the root user to the list of users, since it will always be able to unlock the station
    if !opt.users.iter().any(|x| x == "root") {
        opt.users.push("root".to_string());
    }

    // Special check for the root user:
    // If only root can unlock the pc, check that it has a password.
    // Ubuntu, for example, has a passwordless root user by default.
    if opt.users == [ "root" ] {
        Passwd::from_uid(0)
            .and_then(|passwd| passwd.passwd)
            .and_then(|p| {
                if p.is_empty() || p.bytes().next().unwrap() == b'!' || p.bytes().next().unwrap() == b'*' {
                    None
                } else {
                    Some(())
                }
            })
            .unwrap_or_else(||
                if !opt.allow_passwordless_root {
                    eprintln!("Only root user can unlock, and it does not have a valid password. The station will not be locked.");
                    eprintln!("To override this security measure, pass --allow-passwordless-root.");
                    std::process::exit(1);
                }
            );
    }

    opt
}