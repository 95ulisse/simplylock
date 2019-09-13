use std::cell::RefCell;
use std::fmt;
use std::io::{self, Read, Write, IoSlice, IoSliceMut};
use std::rc::Rc;

pub struct ReadHalf<R: Read>(Rc<RefCell<R>>);
pub struct WriteHalf<W: Write>(Rc<RefCell<W>>);

impl<R: Read> Read for ReadHalf<R> {

    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        self.0.borrow_mut().read(buf)
    }

    fn read_vectored(&mut self, bufs: &mut [IoSliceMut]) -> io::Result<usize> {
        self.0.borrow_mut().read_vectored(bufs)
    }

    fn read_to_end(&mut self, buf: &mut Vec<u8>) -> io::Result<usize> {
        self.0.borrow_mut().read_to_end(buf)
    }

    fn read_to_string(&mut self, buf: &mut String) -> io::Result<usize> {
        self.0.borrow_mut().read_to_string(buf)
    }

    fn read_exact(&mut self, buf: &mut [u8]) -> io::Result<()> {
        self.0.borrow_mut().read_exact(buf)
    }

}

impl<W: Write> Write for WriteHalf<W> {

    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        self.0.borrow_mut().write(buf)
    }

    fn flush(&mut self) -> io::Result<()> {
        self.0.borrow_mut().flush()
    }

    fn write_vectored(&mut self, bufs: &[IoSlice]) -> io::Result<usize> {
        self.0.borrow_mut().write_vectored(bufs)
    }

    fn write_all(&mut self, buf: &[u8]) -> io::Result<()> {
        self.0.borrow_mut().write_all(buf)
    }

    fn write_fmt(&mut self, fmt: fmt::Arguments) -> io::Result<()> {
        self.0.borrow_mut().write_fmt(fmt)
    }

}

/// Splits a readable and writable stream into two different objects representing
/// the reader and the writer half separately.
pub fn split_stream<S>(source: Rc<RefCell<S>>) -> (ReadHalf<S>, WriteHalf<S>)
    where S: Read + Write
{
    let source_clone = Rc::clone(&source);
    (ReadHalf(source), WriteHalf(source_clone))
}