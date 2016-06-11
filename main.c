#define _DEFAULT_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "options.h"
#include "vt.h"
#include "auth.h"
#include "lock.h"

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
    struct vt* vt = lock(options);
    if (vt == NULL) {
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
