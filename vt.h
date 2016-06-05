#ifndef __VT_H__
#define __VT_H__

#include <stdio.h>
#include <termios.h>

#define VT_CONSOLE_DEVICE "/dev/console"
#define VT_TTY_FORMAT "/dev/tty%d"

/**
 *    Structure representing a virtual terminal.
 *
 *    A `struct vt` can be both in an open or closed state:
 *    - When open, all the fields are populated and can all be used.
 *    - When closed, only the `number` field is meaningful, and the others are all set to 0.
 *
 *    @field number Number of the terminal.
 *    @field fd File descriptor pointing to the terminal. This descriptor
 *        can be used to write and read to/from the vt.
 *    @field stream Stream pointing to the terminal. This stream
 *        can be used to write and read to/from the vt.
 *        This stream points to the same file descriptor as `fd`.
 *    @field term Structure `termios` containing informations
 *        about the attributes of the terminal.
 */
struct vt {
    unsigned int number;
    unsigned int fd;
    FILE* stream;
    struct termios term;
};

/**
 *    Initializes the vt library.
 *
 *    @return `0` in case of success, `-1` otherwise and sets `errno`.
 */
int vt_init();

/**
 *    Tears down the vt library.
 */
void vt_end();

/**
 *    Returns a closed `struct vt` representing the current terminal.
 */
struct vt* vt_getcurrent();

/**
 *    Creates and allocates a new virtual terminal.
 *
 *    @return An open `struct vt` representing the new terminal allocated.
 */
struct vt* vt_createnew();

/**
 *    Frees all the resources held by a `struct vt`.
 *
 *    @param vt Virtual terminal to destroy.
 */
void vt_free(struct vt* vt);

/**
 *    Switches to the given virtual terminal.
 *
 *    @param  to Terminal to switch to.
 *    @return `0` in case of success, `-1` otherwise and sets `errno`.
 */
int vt_switch(struct vt* to);

/**
 *    Enables or disables the terminal switching mechanism.
 *
 *    @param lock `1` to disable switching or `0` to enable.
 *    @return `0` in case of success, `-1` otherwise and sets `errno`.
 */
int vt_lockswitch(int lock);

/**
 *    Enables or disables the echo of the characters typed by the user.
 *
 *    @param  vt   Virtual terminal to operate on.
 *    @param  echo `1` to enable echo, `0` to disable.
 *    @return `0` in case of success, `-1` otherwise and sets `errno`.
 */
int vt_setecho(struct vt* vt, int echo);

/**
 *    Flushes all the data written by the user but not yet read by the application.
 *
 *    @param  vt Virtual terminal to flush.
 *    @return `0` in case of success, `-1` otherwise and sets `errno`.
 */
int vt_flush(struct vt* vt);

/**
 *    Clears the terminal.
 *
 *    @param  vt Virtual terminal to clear.
 *    @return `0` in case of success, `-1` otherwise and sets `errno`.
 */
int vt_clear(struct vt* vt);

#endif
