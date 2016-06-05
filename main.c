#include <stdio.h>
#include <assert.h>
#include <unistd.h>

#include "vt.h"

int main() {

    if (vt_init() < 0)
        perror("vt_init");

    struct vt* curr = vt_getcurrent();

    struct vt* vt = vt_createnew();
    if (vt == NULL)
        perror("vt_createnew");

    if (vt_switch(vt) < 0)
        perror("vt_switch");

    vt_lockswitch(1);
    vt_setecho(vt, 1);

    fwrite("Switching locked\n", 4, 1, vt->stream);

    for (int i = 10; i >= 0; i--) {
        vt_clear(vt);
        dprintf(vt->fd, "[ %d ] %d\n", vt->number, i);
        sleep(1);
    }

    vt_lockswitch(0);

    vt_switch(curr);

    vt_free(vt);
    vt_free(curr);

    vt_end();

    return 0;
}
