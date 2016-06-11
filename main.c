#define _DEFAULT_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>

#include "options.h"
#include "vt.h"
#include "auth.h"

#define SYSRQ_PATH "/proc/sys/kernel/sysrq"
#define PRINTK_PATH "/proc/sys/kernel/printk"

#define REDIRECT_STD_STREAM(s, f, mode) \
    do { \
        if (fclose(s) == EOF) { \
            perror("fclose " #s); \
            goto error; \
        } \
        if (dup2(vt->fd, f) < 0) { \
            perror("dup2"); \
            goto error; \
        } \
        s = fdopen(f, mode); \
        if (s == NULL) { \
            perror("fdopen" #s); \
            goto error; \
        } \
    } while (0)

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

// Main locking mechanism
static int lock(struct options* options) {

    // Saves sysrq state, so that later can be restored
    if (options->block_sysrequests) {
        sysrq_file = fopen(SYSRQ_PATH, "r+");
        if (sysrq_file == NULL) {
            perror("Open " SYSRQ_PATH);
            fprintf(stderr, "Please, consider running with -s to keep sysrequests enabled.\n");
            return -1;
        }
        if (read_int(sysrq_file, old_sysrq, 100) < 0) {
            perror("read_int " SYSRQ_PATH);
            fprintf(stderr, "Please, consider running with -s to keep sysrequests enabled.\n");
            return -1;
        }
    }

    // Saves the state of the printk, so that later can be restored
    if (options->block_kernel_messages) {
        printk_file = fopen(PRINTK_PATH, "r+");
        if (printk_file == NULL) {
            perror("Open " PRINTK_PATH);
            fprintf(stderr, "Please, consider running with -k to keep kernel messages visible.\n");
            return -1;
        }
        if (read_int(printk_file, old_printk, 100) < 0) {
            perror("read_int " PRINTK_PATH);
            fprintf(stderr, "Please, consider running with -k to keep kernel messages visible.\n");
            return -1;
        }
    }

    // Save current vt
    old_vt = vt_getcurrent();
    if (old_vt == NULL) {
        perror("vt_getcurrent");
        return -1;
    }

    // Create a new vt
    vt = vt_createnew();
    if (vt == NULL) {
        perror("vt_createnew");
        return -1;
    }

    // Block sysrq/printk
    if (options->block_sysrequests) {
        rewind(sysrq_file);
        if (fputs("0", sysrq_file) < 0) {
            perror("fputs " SYSRQ_PATH);
            fprintf(stderr, "Please, consider running with -s to keep sysrequests enabled.\n");
            return -1;
        }
        sysrq_blocked = 1;
    }
    if (options->block_kernel_messages) {
        rewind(printk_file);
        if (fputs("0", printk_file) < 0) {
            perror("fputs " PRINTK_PATH);
            fprintf(stderr, "Please, consider running with -k to keep kernel messages visible.\n");
            return -1;
        }
        printk_blocked = 1;
    }

    // Activate new vt
    if (vt_switch(vt) < 0) {
        perror("vt_switch new vt");
        return -1;
    }

    return 0;
}

// The unlock corresponding to the previous `lock`
static void unlock(struct options* options) {

    // We switch back to the old vt
    if (old_vt != NULL && vt_switch(old_vt) < 0) {
        perror("vt_switch old vt");
    }
    vt_free(old_vt);
    vt_free(vt);

    // And now we restore the state of sysrq/printk
    if (options->block_sysrequests && sysrq_blocked) {
        rewind(sysrq_file);
        if (fputs(old_sysrq, sysrq_file) < 0) {
            perror("fputs " SYSRQ_PATH);
            fprintf(stderr, "Please, consider running with -s to keep sysrequests enabled.\n");
        }
    }
    if (options->block_kernel_messages && printk_blocked) {
        rewind(printk_file);
        if (fputs(old_printk, printk_file) < 0) {
            perror("fputs " PRINTK_PATH);
            fprintf(stderr, "Please, consider running with -k to keep kernel messages visible.\n");
        }
    }
    if (sysrq_file != NULL) {
        fclose(sysrq_file);
    }
    if (printk_file != NULL) {
        fclose(printk_file);
    }

}

int main(int argc, char** argv) {

    // We need to run as root or setuid root
    if (geteuid() != 0) {
        fprintf(stderr, "Please, run simplylock as root or setuid root.\n");
        return 1;
    }

    // Parses the options
    struct options* options = options_parse(argc, argv);
    if (options == NULL) {
        return 1;
    }
    if (options->show_help || options->show_version) {
        options_free(options);
        return 0;
    }

    // Initialize VT library
    if (vt_init() < 0) {
        perror("vt_init");
        goto error;
    }

    // Here we go!
    if (lock(options) < 0) {
        goto error;
    }

    // We redirect all three standard streams to the new vt
    REDIRECT_STD_STREAM(stdin, STDIN_FILENO, "r");
    REDIRECT_STD_STREAM(stdout, STDOUT_FILENO, "w");
    REDIRECT_STD_STREAM(stderr, STDERR_FILENO, "w");

    // Disable buffering on stdout and stderr since this might cause problems with PAM stdio
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    // We clear the environment to avoid any possible interaction with PAM modules
    clearenv();

    // The auth loop
    char* user = options->users[0];
    for (;;) {
        vt_clear(vt);
        vt_flush(vt);
        fprintf(stdout, "\n%s ", options->message);
        int c = fgetc(stdin);
        while (c != EOF && c != '\n') {
            c = fgetc(stdin);
        }
        if (c == EOF) {
            perror("getchar");
            goto error;
        }
        fprintf(stdout, "\n");
        if (auth_authenticate_user(user) == 0) {
            // The user is authenticated, so we can unlock everything
            break;
        }
        fprintf(stdout, "\nAuthentication failed.\n");
        sleep(3);
    }

    unlock(options);

    // Cleanup
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    options_free(options);
    vt_end();
    return 0;

error:
    unlock(options);
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    options_free(options);
    vt_end();
    return 1;

}
