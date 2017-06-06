#define _DEFAULT_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <setjmp.h>

#include "options.h"
#include "vt.h"
#include "auth.h"
#include "lock.h"

#define HIGHLIGHT "\033[1m\033[34m"
#define RESET "\033[0m"

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

static int user_selection_enabled = 0;
static sigjmp_buf user_selection_jmp;

static void on_sigint(int sig) {
    if (user_selection_enabled) {
        siglongjmp(user_selection_jmp, 1);
    }
}

static inline int register_signal(int sig, void (*handler)(int)) {
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    action.sa_handler = handler;
    return sigaction(sig, &action, NULL);
}

static int user_selection(struct options* options, struct vt* vt, char** user) {
    int index;
    do {

        vt_flush(vt);
        vt_clear(vt);

        if(options->backlight_off != 0) system("vbetool dpms on");

        fprintf(stdout, "\nThe following users are authorized to unlock:\n\n");
        for (int i = 0; i < options->users_size; i++) {
            char* format = "%d. %s\n";
            if (options->users[i] == *user) {
                format = "%d. " HIGHLIGHT "%s" RESET "\n";
            }
            fprintf(stdout, format, i + 1, options->users[i]);
        }
        fprintf(stdout, "\nInsert the number of the user that wants to unlock and press enter: ");

        char* line = NULL;
        size_t n = 0;
        vt_setecho(vt, 1);
        if (getline(&line, &n, vt->stream) < 0) {
            return -1;
        }
        vt_setecho(vt, 0);

        char* tmp;
        index = strtol(line, &tmp, 10);
        if (*tmp != '\n') {
            index = -1;
        } else {
            index--;
        }

    } while (index < 0 || index >= options->users_size);

    *user = options->users[index];

    return 0;
}

void turn_off_backlight(int *state) {
    if(*state != 0) {
        system("vbetool dpms off");
        *state = 0;
    }
}
void turn_on_backlight(int *state) {
    if(*state != 1) {
        system("vbetool dpms on");
        *state = 1;
    }
}

int main(int argc, char** argv) {
    struct options* options;
    struct vt* vt;
    char* user;
    int c;
    uid_t original_uid;
    int backlight_state;

    // We need to run as root or setuid root
    if (geteuid() != 0) {
        fprintf(stderr, "Please, run simplylock as root or setuid root.\n");
        return 1;
    }

    // Now we become fully root, in case we were started as setuid from another user
    original_uid = getuid();
    if (setregid(0, 0) < 0) {
        perror("setregid");
        return 1;
    }
    if (setreuid(0, 0) < 0) {
        perror("setreuid");
        return 1;
    }

    // Parses the options
    options = options_parse(argc, argv, original_uid);
    if (options == NULL) {
        return 1;
    }

    if (options->show_help || options->show_version) {
        options_free(options);
        return 0;
    }
    user = options->users[0];

    // Register signal handler for SIGINT
    if (register_signal(SIGINT, on_sigint) < 0) {
        perror("register_signal SIGINT");
        return 1;
    }

    // Ignore all other termination signals
    if (register_signal(SIGQUIT, SIG_IGN) < 0) {
        perror("register_signal SIGQUIT");
        return 1;
    }
    if (register_signal(SIGTERM, SIG_IGN) < 0) {
        perror("register_signal SIGTERM");
        return 1;
    }
    if (register_signal(SIGTSTP, SIG_IGN) < 0) {
        perror("register_signal SIGTSTP");
        return 1;
    }

    // Now we fork and move to a new session so that we can be the
    // foreground process for the new terminal to be created
    if (fork() == 0) {
        setsid();
    } else {
        goto clean_and_exit;
    }

    // Initialize VT library
    if (vt_init() < 0) {
        perror("vt_init");
        goto error;
    }

    // Locking of the terminal
    vt = lock(options);
    if (vt == NULL) {
        goto error;
    }

    // Enable Ctrl+C on the terminal
    if (vt_signals(vt, VT_SIGINT) < 0) {
        perror("vt_signals");
        goto error;
    }

    // We redirect all three standard streams to the new vt
    REDIRECT_STD_STREAM(stdin, STDIN_FILENO, "r");
    REDIRECT_STD_STREAM(stdout, STDOUT_FILENO, "w");
    REDIRECT_STD_STREAM(stderr, STDERR_FILENO, "w");

    // Disable buffering on std streams since this might cause problems with PAM stdio
    setbuf(stdin, NULL);
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    backlight_state = 1;
    if(options->backlight_off != 0) turn_off_backlight(&backlight_state);

    // We clear the environment to avoid any possible interaction with PAM modules
    clearenv();

    // User selection: this code will be executed only when the user presses Ctrl+C
    if (sigsetjmp(user_selection_jmp, 1) > 0) {
        user_selection(options, vt, &user);
    }

    // The auth loop
    for (;;) {
        vt_clear(vt);
        vt_flush(vt);

        if (options->message != NULL) {
            fprintf(stdout, "\n%s\n", options->message);
        }
        fprintf(stdout, "\nPress enter to unlock as " HIGHLIGHT "%s" RESET ". [Press Ctrl+C to change user] ", user);

        user_selection_enabled = 1;

        if (options->quick_unlock != 0) {
            options->quick_unlock = 0;
        } else {
            c = fgetc(stdin);
            while (c != EOF && c != '\n') {
                c = fgetc(stdin);
                if(options->backlight_off != 0) turn_on_backlight(&backlight_state);
            }
        }

        if (c == EOF) {
            perror("getchar");
            goto error;
        }
        putc('\n', stdout);
        user_selection_enabled = 0;

        if (auth_authenticate_user(user) == 0) {
            // The user is authenticated, so we can unlock everything
            break;
        }

        if(options->backlight_off != 0) turn_on_backlight(&backlight_state);
        fprintf(stdout, "\nAuthentication failed.\n");
        sleep(3);
    }

    if(options->backlight_off != 0) turn_on_backlight(&backlight_state);

    vt_clear(vt);
    unlock(options);

    // Cleanup
clean_and_exit:
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
