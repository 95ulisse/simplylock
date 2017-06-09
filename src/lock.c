#include <stdio.h>
#include <ctype.h>

#include "lock.h"

#define SYSRQ_PATH "/proc/sys/kernel/sysrq"
#define PRINTK_PATH "/proc/sys/kernel/printk"

// This is where we save the state of the system before we try to do anything
static char old_sysrq[100];
static char old_printk[100];
static FILE* sysrq_file = NULL;
static FILE* printk_file = NULL;
static int sysrq_blocked = 0;
static int printk_blocked = 0;
static struct vt* old_vt = NULL;
static struct vt* vt = NULL;

static int read_int(FILE* stream, char* val, size_t n) {
    for (int i = 0; i < n; i++) {
        int c = fgetc(stream);
        if (c == EOF) {
            val[i] = 0;
            return i > 0 ? 0 : -1;
        }
        if (isdigit(c)) {
            val[i] = c;
        } else {
            val[i] = 0;
            return i > 0 ? 0 : -1;
        }
    }
    return -1; // We tried to read more characted than we are allowed to
}

struct vt* lock(struct options* options) {

    // Saves sysrq state, so that later can be restored
    if (options->block_sysrequests) {
        sysrq_file = fopen(SYSRQ_PATH, "r+");
        if (sysrq_file == NULL) {
            perror("Open " SYSRQ_PATH);
            fprintf(stderr, "Please, consider running with -s to keep sysrequests enabled.\n");
            return NULL;
        }
        if (read_int(sysrq_file, old_sysrq, 100) < 0) {
            perror("read_int " SYSRQ_PATH);
            fprintf(stderr, "Please, consider running with -s to keep sysrequests enabled.\n");
            return NULL;
        }
    }

    // Saves the state of the printk, so that later can be restored
    if (options->block_kernel_messages) {
        printk_file = fopen(PRINTK_PATH, "r+");
        if (printk_file == NULL) {
            perror("Open " PRINTK_PATH);
            fprintf(stderr, "Please, consider running with -k to keep kernel messages visible.\n");
            return NULL;
        }
        if (read_int(printk_file, old_printk, 100) < 0) {
            perror("read_int " PRINTK_PATH);
            fprintf(stderr, "Please, consider running with -k to keep kernel messages visible.\n");
            return NULL;
        }
    }

    // Save current vt
    old_vt = vt_getcurrent();
    if (old_vt == NULL) {
        perror("vt_getcurrent");
        return NULL;
    }

    // Create a new vt
    vt = vt_createnew();
    if (vt == NULL) {
        perror("vt_createnew");
        return NULL;
    }

    // Block sysrq/printk
    if (options->block_sysrequests) {
        rewind(sysrq_file);
        if (fputs("0", sysrq_file) < 0) {
            perror("fputs " SYSRQ_PATH);
            fprintf(stderr, "Please, consider running with -s to keep sysrequests enabled.\n");
            return NULL;
        }
        fclose(sysrq_file);
        sysrq_blocked = 1;
    }
    if (options->block_kernel_messages) {
        rewind(printk_file);
        if (fputs("0", printk_file) < 0) {
            perror("fputs " PRINTK_PATH);
            fprintf(stderr, "Please, consider running with -k to keep kernel messages visible.\n");
            return NULL;
        }
        fclose(printk_file);
        printk_blocked = 1;
    }

    // Activate new vt
    if (vt_switch(vt) < 0) {
        perror("vt_switch new vt");
        return NULL;
    }

    // Lock vt switching
    if (options->block_vt_switch && vt_lockswitch(1) < 0) {
        perror("vt_lockswitch");
        return NULL;
    }

    // Switch the screen off
    if (options->dark_mode) {
        vt_blank(vt, 1);
    }

    return vt;
}

void unlock(struct options* options) {

    // Switch the screen on
    if (options->dark_mode) {
        vt_blank(vt, 0);
    }

    // Re-enable vt switching
    if (options->block_vt_switch && vt_lockswitch(0) < 0) {
        perror("vt_lockswitch");
    }

    // We switch back to the old vt
    if (old_vt != NULL && vt_switch(old_vt) < 0) {
        perror("vt_switch old vt");
    }
    vt_free(old_vt);
    vt_free(vt);
    old_vt = vt = NULL;

    // And now we restore the state of sysrq/printk
    if (options->block_sysrequests && sysrq_blocked) {
        sysrq_file = fopen(SYSRQ_PATH, "r+");
        if (sysrq_file == NULL) {
            perror("Open " SYSRQ_PATH);
            fprintf(stderr, "Please, consider running with -s to keep sysrequests enabled.\n");
        } else if (fputs(old_sysrq, sysrq_file) < 0) {
            perror("fputs " SYSRQ_PATH);
            fprintf(stderr, "Please, consider running with -s to keep sysrequests enabled.\n");
        }
        sysrq_blocked = 0;
    }
    if (options->block_kernel_messages && printk_blocked) {
        printk_file = fopen(PRINTK_PATH, "r+");
        if (printk_file == NULL) {
            perror("Open " PRINTK_PATH);
            fprintf(stderr, "Please, consider running with -k to keep kernel messages visible.\n");
        } else if (fputs(old_printk, printk_file) < 0) {
            perror("fputs " PRINTK_PATH);
            fprintf(stderr, "Please, consider running with -k to keep kernel messages visible.\n");
        }
        printk_blocked = 0;
    }
    if (sysrq_file != NULL) {
        fclose(sysrq_file);
        sysrq_file = NULL;
    }
    if (printk_file != NULL) {
        fclose(printk_file);
        printk_file = NULL;
    }

}
