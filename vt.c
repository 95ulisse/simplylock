#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/vt.h>
#include <errno.h>

#include "vt.h"

static int console_fd = -1;

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

    // First we find an available vt
    int ret;
    while ((ret = ioctl(console_fd, VT_OPENQRY, &vt->number)) == -1 && errno == EINTR);
	if (ret < 0) {
        goto error;
    }

    // Then we open the corresponding device file
    char path[1024];
    snprintf(path, sizeof(path), VT_TTY_FORMAT, vt->number);
	while ((vt->stream = fopen(path, "r+")) == NULL && errno == EINTR);
	if (vt->stream == NULL) {
		goto error;
    }
	vt->fd = fileno(vt->stream);

    // And get terminal attributes
    while ((ret = tcgetattr(vt->fd, &vt->term)) == -1 && errno == EINTR);
    if (ret < 0) {
        goto error;
    }

    // By default we turn off echo and signal generation
    vt->term.c_lflag &= ~(ECHO | ISIG);
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
