#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/vt.h>
#include <linux/tiocl.h>
#include <errno.h>

#include "vt.h"

#define CONSOLEBLANK_PATH "/sys/module/kernel/parameters/consoleblank"
#define MIN_VT_NUMBER 13

static int console_fd = -1;



static inline int set_console_blank_timer(struct vt* vt, int timer) {
    return dprintf(vt->fd, "\033[9;%d]", timer);
}

static int ensure_console_blank_timer_enabled(struct vt* vt) {
    int fd = open(CONSOLEBLANK_PATH, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    // Read the integer inside the file
    char buf[20];
    if (read(fd, buf, sizeof(buf)) < 0) {
        return -1;
    }

    // Parse the integer and close the file
    int ret = atoi(buf);
    close(fd);

    // If we have a 0 timer, set the timer to 1
    if (ret == 0) {
        if (set_console_blank_timer(vt, 1) < 0) {
            ret = -1;
        }
    }

    return ret;
}



int vt_init() {
    while ((console_fd = open(VT_CONSOLE_DEVICE, O_RDWR)) == -1 && errno == EINTR);
    return console_fd != -1;
}

void vt_end() {
    if (console_fd > -1) {
        close(console_fd);
        console_fd = -1;
    }
}

struct vt* vt_getcurrent() {

    // Gets the current vt number
    int ret;
    struct vt_stat vtstate;
    while ((ret = ioctl(console_fd, VT_GETSTATE, &vtstate)) == -1 && errno == EINTR);
    if (ret < 0) {
        return NULL;
    }

    // Allocates a new struct to return to the caller
    struct vt* vt = (struct vt*)calloc(1, sizeof(struct vt));
    if (vt == NULL) {
        return NULL;
    }
    vt->number = vtstate.v_active;

    return vt;

}

struct vt* vt_createnew() {

    struct vt* vt = (struct vt*)malloc(sizeof(struct vt));
    if (vt == NULL) {
        return NULL;
    }
    vt->fd = -1;

    // First we find an available vt
    int ret;
    int num;
    while ((ret = ioctl(console_fd, VT_OPENQRY, &num)) == -1 && errno == EINTR);
    if (ret < 0) {
        goto error;
    }

    // If we got a low vt number, start searching for the higher ones.
    // This is because a vt might be actually free, but systemd-logind is managing it,
    // and we don't want to step on systemd, otherwise bad things will happen.
    // We chose 13 as the lower limit because the user can manually switch up to vt number 12.
    // On most systems, the maximum number of vts is 64, so this should not be a problem.
    if (num < MIN_VT_NUMBER) {
        num = MIN_VT_NUMBER;
       
        // Fast path: the kernel provides a quick way to get the state of the first 16 vts
        // by returning a mask with 1s indicating the ones in use.
        struct vt_stat stat;
        int ret;
        while ((ret = ioctl(console_fd, VT_GETSTATE, &stat)) == -1 && errno == EINTR);
        if (ret < 0) {
            goto error;
        }

        int found = 0;
        for (unsigned short mask = 1 << num; num < 16; ++num, mask <<= 1) {
            if ((stat.v_state & mask) == 0) {
                found = 1;
                break;
            }
        }

        // Slow path: we might be unlucky, and all the first 16 vts are already occupied.
        // This should never happen in a real case, but better safe than sorry.
        //
        // Here the kernel does not help us and we have to test each single vt one by one:
        // by issuing a VT_OPENQRY ioctl we can get back the first free vt.
        // We keep opening file descriptors until the next free vt is greater than MIN_VT_NUMBER.
        //
        // I don't have words to describe how ugly and problematic this is,
        // but it's the only stable working solution I found. I seriously hope that this will never be needed.
        if (!found) {
            
            // Keep track of the fds we open
            int fds[MAX_NR_CONSOLES];
            for (int i = 0; i < MAX_NR_CONSOLES; ++i) {
                fds[i] = -1;
            }

            int first_free = 0;
            char path[1024];
            do {

                // Ask for the first free
                int ret;
                while ((ret = ioctl(console_fd, VT_OPENQRY, &first_free)) == -1 && errno == EINTR);
                if (ret < 0) {
                    goto error;
                }

                // Open the corresponding device file to mark it as busy
                snprintf(path, sizeof(path), VT_TTY_FORMAT, first_free);
                while ((ret = open(path, O_RDWR)) == -1 && errno == EINTR);
                if (ret < 0) {
                    goto error;
                }
                fds[first_free] = ret;

            } while (first_free < num);

            // We did it!
            num = first_free;
            vt->fd = fds[num];

            // Now clean up
            for (int i = 0; i < MAX_NR_CONSOLES; ++i) {
                if (i != num && fds[i] != -1) {
                    close(fds[i]);
                }
            }

        }

    }
    vt->number = num;

    // Then we open the corresponding device file
    if (vt->fd == -1) {
        char path[1024];
        snprintf(path, sizeof(path), VT_TTY_FORMAT, vt->number);
        while ((vt->stream = fopen(path, "r+")) == NULL && errno == EINTR);
        if (vt->stream == NULL) {
            goto error;
        }
        vt->fd = fileno(vt->stream);
    } else {
        // Reuse the same fd we found during the slow path of the previous check
        while ((vt->stream = fdopen(vt->fd, "r+")) == NULL && errno == EINTR);
        if (vt->stream == NULL) {
            goto error;
        }
    }

    // And get terminal attributes
    while ((ret = tcgetattr(vt->fd, &vt->term)) == -1 && errno == EINTR);
    if (ret < 0) {
        goto error;
    }

    // By default we turn off echo and signal generation.
    // We also disable Ctrl+D for EOF, since we will almost never want it.
    vt->term.c_iflag |= IGNBRK;
    vt->term.c_lflag &= ~(ECHO | ISIG);
    vt->term.c_cc[VEOF] = 0;
    while ((ret = tcsetattr(vt->fd, TCSANOW, &vt->term)) == -1 && errno == EINTR);
    if (ret < 0) {
        goto error;
    }

    return vt;

error:

    if (vt != NULL) {
        if (vt->stream != NULL) {
            fclose(vt->stream);
        }
        free(vt);
    }

    return NULL;
}

void vt_free(struct vt* vt) {
    if (vt == NULL) {
        return;
    }
    if (vt->stream != NULL) {
        fclose(vt->stream);

        int ret;
        while ((ret = ioctl(console_fd, VT_DISALLOCATE, vt->number)) == -1 && errno == EINTR);
    }
    free(vt);
}

int vt_switch(struct vt* vt) {

    if (vt == NULL) {
        errno = EINVAL;
        return -1;
    }

    // Switch vt
    int ret;
    while ((ret = ioctl(console_fd, VT_ACTIVATE, vt->number)) == -1 && errno == EINTR);
    if (ret < 0) {
        return -1;
    }

    // Wait for switch to complete
    while ((ret = ioctl(console_fd, VT_WAITACTIVE, vt->number)) == -1 && errno == EINTR);
    if (ret < 0) {
        return -1;
    }

    return 0;

}

int vt_lockswitch(int lock) {
    int ret;
    while ((ret = ioctl(console_fd, lock ? VT_LOCKSWITCH : VT_UNLOCKSWITCH, 1)) == -1 && errno == EINTR);
    return ret;
}

int vt_setecho(struct vt* vt, int echo) {
    if (echo) {
        vt->term.c_lflag |= ECHO;
    } else {
        vt->term.c_lflag &= ~ECHO;
    }

    int ret;
    while ((ret = tcsetattr(vt->fd, TCSANOW, &vt->term)) == -1 && errno == EINTR);
    return ret;
}

int vt_flush(struct vt* vt) {
    return tcflush(vt->fd, TCIFLUSH);
}

int vt_clear(struct vt* vt) {
    return write(vt->fd, "\033[H\033[J", 6) == 6 ? 0 : -1;
}

int vt_blank(struct vt* vt, int blank) {

    // If the console blanking timer is disabled, the ioctl below will fail,
    // so we need to enable it just for the time needed for the ioctl to work.
    int need_console_blank_reset = 0;
    if (blank && ensure_console_blank_timer_enabled(vt) == 0) {
        need_console_blank_reset = 1;
    }

    int arg = blank ? TIOCL_BLANKSCREEN : TIOCL_UNBLANKSCREEN;
    int ret = ioctl(vt->fd, TIOCLINUX, &arg);

    // Reset the console blanking timer if modified
    if (need_console_blank_reset) {
        set_console_blank_timer(vt, 0);
    }

    return ret;
}

int vt_signals(struct vt* vt, vt_signals_t sigs) {

    // Since we created the vt with signals disabled, we need to enable them
    vt->term.c_lflag |= ISIG;

    // Now we enable/disable the single signals
    if ((sigs & VT_SIGINT) == 0) {
        vt->term.c_cc[VINTR] = 0;
    } else {
        vt->term.c_cc[VINTR] = 3;
    }
    if ((sigs & VT_SIGQUIT) == 0) {
        vt->term.c_cc[VQUIT] = 0;
    } else {
        vt->term.c_cc[VQUIT] = 34;
    }
    if ((sigs & VT_SIGTSTP) == 0) {
        vt->term.c_cc[VSUSP] = 0;
    } else {
        vt->term.c_cc[VSUSP] = 32;
    }

    // And update the terminal
    int ret;
    while ((ret = tcsetattr(vt->fd, TCSANOW, &vt->term)) == -1 && errno == EINTR);
    return ret;
}
