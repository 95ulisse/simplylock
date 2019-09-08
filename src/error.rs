use std::borrow::Cow;
use std::fmt::{self, Display};
use std::path::{Path, PathBuf};
use failure::{Fail, Backtrace, Context};

/// Re-export `faulre::ResultExt` so that we don't have to use
/// the `ResultExt` type explicitely when doing `use crate::error::*`.
pub use failure::ResultExt;

/// Common type alias for the `Result` type.
pub type Result<T> = std::result::Result<T, Error>;

/// An error that can occur in the application.
#[derive(Debug)]
pub struct Error {
    inner: Context<ErrorKind>
}

impl Error {
    pub fn kind(&self) -> &ErrorKind {
        self.inner.get_context()
    }
}

impl Fail for Error {
    fn cause(&self) -> Option<&dyn Fail> {
        self.inner.cause()
    }

    fn backtrace(&self) -> Option<&Backtrace> {
        self.inner.backtrace()
    }
}

impl Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        Display::fmt(&self.inner, f)
    }
}

impl From<ErrorKind> for Error {
    fn from(kind: ErrorKind) -> Error {
        Error { inner: Context::new(kind) }
    }
}

impl From<Context<ErrorKind>> for Error {
    fn from(inner: Context<ErrorKind>) -> Error {
        Error { inner }
    }
}

/// Wrapper around a `Path` to make it displayable.
#[derive(Clone, Eq, PartialEq, Debug)]
pub struct DisplayPath<'a>(Cow<'a, Path>);

impl<'a> Display for DisplayPath<'a> {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        self.0.display().fmt(f)
    }
}

impl<'a> From<&'a str> for DisplayPath<'a> {
    fn from(arg: &'a str) -> Self {
        DisplayPath(Path::new(arg).into())
    }
}

impl From<String> for DisplayPath<'static> {
    fn from(arg: String) -> Self {
        DisplayPath(PathBuf::from(arg).into())
    }
}

/// The specific kind of error that can occur in the application.
#[derive(Clone, Eq, PartialEq, Debug, Fail)]
pub enum ErrorKind {
    #[fail(display = "In path: {}", _0)]
    Path(DisplayPath<'static>),
    #[fail(display = "I/O error")]
    Io,
    #[fail(display = "PAM error: {}", _0)]
    Pam(String),
    #[fail(display = "Error parsing value")]
    Parse,
    #[fail(display = "{}", _0)]
    Message(&'static str),
    #[fail(display = "{}", _0)]
    Note(&'static str)
}