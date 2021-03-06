#define _DEFAULT_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <setjmp.h>
#include <sys/wait.h>

#include "options.h"
#include "vt.h"
#include "bg.h"
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

static int user_selection(struct options* options, struct vt* vt, void* bg, char** user) {
    int index;
    do {

        vt_flush(vt);
        vt_clear(vt);
        
        // Switch on the screen if in dark mode
        if (options->dark_mode) {
            vt_blank(vt, 0);
        }

        // Background
        if (bg != NULL) {
            bg_paint(bg);
        }

        // Users list
        fprintf(stdout, "\nThe following users are authorized to unlock:\n\n");
        for (int i = 0; i < options->users_size; i++) {
            char* format = "%d. %s\n";
            if (options->users[i] == *user) {
                format = "%d. " HIGHLIGHT "%s" RESET "\n";
            }
            fprintf(stdout, format, i + 1, options->users[i]);
        }
        fprintf(stdout, "\nInsert the number of the user that wants to unlock and press enter: ");

        // Wait for user selection
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

static void repaint_console(struct options* options, struct vt* vt, void* bg, const char* user) {
    vt_clear(vt);
    vt_flush(vt);

    if (bg != NULL) {
        bg_paint(bg);
    }

    if (options->message != NULL) {
        fprintf(stdout, "\n%s\n", options->message);
    }
    fprintf(stdout, "\nPress enter to unlock as " HIGHLIGHT "%s" RESET ". [Press Ctrl+C to change user] ", user);
}

int main(int argc, char** argv) {
    struct options* options;
    struct vt* vt;
    void* bg = NULL;
    char* user;
    int c;
    int is_console_blanked = 0;

    // Parses the options
    options = options_parse(argc, argv);
    if (options == NULL) {
        return 1;
    }
    if (options->show_help || options->show_version) {
        options_free(options);
        return 0;
    }
    user = options->users[0];

    // We need to run as root or setuid root
    if (geteuid() != 0) {
        fprintf(stderr, "Please, run simplylock as root or setuid root.\n");
        return 1;
    }

    // Now we become fully root, in case we were started as setuid from another user
    if (setregid(0, 0) < 0) {
        perror("setregid");
        return 1;
    }
    if (setreuid(0, 0) < 0) {
        perror("setreuid");
        return 1;
    }

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
    pid_t childpid;
    if ((childpid = fork()) == 0) {
        if (setsid() < 0) {
            perror("setsid");
            return 1;
        }
    } else if (childpid == -1) {
        perror("fork");
        return 1;
    } else {
        // Wait for the child process to terminate.
        if (options->dont_detach) {
            int status;
            pid_t wpid;
            while ((wpid = waitpid(childpid, &status, 0)) == -1 && errno == EINTR);
            if (wpid == -1) {
                perror("waitpid");
                return 1;
            } else if (WIFEXITED(status)) {
                return WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                return 128 + WSTOPSIG(status);
            }
        }
        return 0;
    }

    // Initialize VT library
    if (vt_init() < 0) {
        perror("vt_init");
        goto error;
    }
    is_console_blanked = options->dark_mode;

    // Load the background image if requested
    if (options->background != NULL) {
        bg = bg_init(options->background, options->background_fill, options->fbdev);
        // Don't check for errors: if there has been an error,
        // just don't paint the background.
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

    // We clear the environment to avoid any possible interaction with PAM modules
    clearenv();

    // User selection: this code will be executed only when the user presses Ctrl+C
    if (sigsetjmp(user_selection_jmp, 1) > 0) {
        user_selection(options, vt, bg, &user);
    }

    // The auth loop
    for (;;) {
        
        // Repaint the console
        repaint_console(options, vt, bg, user);

        // Wait for enter to be pressed if not in quick mode.
        // If we are in quick mode, instead, jump directly to
        // authentication, and disable quick mode, so that after
        // a failed attempt, it will be requested to press enter.
        //
        // This way, if both quick mode and dark mode are enabled,
        // the user will be able to make a first login attempt
        // with the screen switched off, and then it will be turned on later.
        if (!options->quick_mode) {
            
            // Wait for enter
            user_selection_enabled = 1;
            c = fgetc(stdin);
            while (c != EOF && c != '\n') {
                c = fgetc(stdin);
            }
            if (c == EOF) {
                perror("getchar");
                goto error;
            }
            user_selection_enabled = 0;

            // Switch the screen back on before authentication
            if (options->dark_mode) {
                vt_blank(vt, 0);
                is_console_blanked = 0;
            }

            // Repaint the whole console
            repaint_console(options, vt, bg, user);
            fprintf(stdout, "\n");

        } else {
            options->quick_mode = 0;
            fprintf(stdout, "\n");
        }

        if (auth_authenticate_user(user) == 0) {
            // The user is authenticated, so we can unlock everything
            break;
        }

        // Switch the screen back on to be sure that the user knows
        // the authentication failed.
        if (options->dark_mode) {
            vt_blank(vt, 0);

            // Repaint the whole console
            if (is_console_blanked) {
                repaint_console(options, vt, bg, user);
                fprintf(stdout, "\n");
            }

            is_console_blanked = 0;
        }

        fprintf(stdout, "\nAuthentication failed.\n");
        sleep(3);
    }

    
    if (bg != NULL) {
        bg_free(bg);
    }

    vt_clear(vt);
    unlock(options);

    // Cleanup
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    options_free(options);
    vt_end();
    return 0;

error:
    if (bg != NULL) {
        bg_free(bg);
    }
    unlock(options);
    fclose(stdin);
    fclose(stdout);
    fclose(stderr);
    options_free(options);
    vt_end();
    return 1;

}
