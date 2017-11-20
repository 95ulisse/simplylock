#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <ctype.h>
#include <shadow.h>
#include <getopt.h>

#include "options.h"

#define SIMPLYLOCK_VERSION "0.5.0"

static char* root_username = "root";

static struct option long_options[] = {
    { "no-sysreq",               no_argument,       NULL, 's' },
    { "no-lock",                 no_argument,       NULL, 'l' },
    { "no-kernel-messages",      no_argument,       NULL, 'k' },
    { "users",                   required_argument, NULL, 'u' },
    { "allow-passwordless-root", no_argument,       NULL,  0  },
    { "message",                 required_argument, NULL, 'm' },
    { "dark",                    no_argument,       NULL, 'd' },
    { "quick",                   no_argument,       NULL, 'q' },
    { "background",              required_argument, NULL, 'b' },
    { "background-fill",         required_argument, NULL,  0  },
    { "fbdev",                   required_argument, NULL,  0  },
    { "no-detach",               no_argument,       NULL, 'D' },
    { "help",                    no_argument,       NULL, 'h' },
    { "version",                 no_argument,       NULL, 'v' },
    { 0, 0, 0, 0 }
};

static void print_usage(int argc, char** argv) {
    fprintf(
        stderr,
        "Usage: %s [-slkdqhv] [-u users] [-m message] [-b path]\n"
        "\n"
        "-s, --no-sysreq              Keep sysrequests enabled.\n"
        "-l, --no-lock                Do not lock terminal switching.\n"
        "-k, --no-kernel-messages     Do not mute kernel messages while the console is locked.\n"
        "-u, --users users            Comma separated list of users allowed to unlock.\n"
        "                             Note that the root user will always be able to unlock.\n"
        "-m, --message message        Display the given message instead of the default one.\n"
        "-d, --dark                   Dark mode: switch off the screen after locking.\n"
        "-q, --quick                  Quick mode: do not wait for enter to be pressed to unlock.\n"
        "\n"
        "-b, --background             Set background image.\n"
        "    --background-fill        Background fill mode. Available values:\n"
        "                             - center: center the image without resizing it.\n"
        "                             - stretch: stretch the image to fill all the available space.\n"
        "                             - resize: like stretch, but keeps image proportions.\n"
        "                             - resize-fill: resize the image to fill the screen but keep proportions. (default)\n"
        "    --fbdev                  Path to the framebuffer device to use to draw the background.\n"
        "\n"
        "-D, --no-detach              Dont't detach: waits for the screen to be unlocked before returning.\n"
        "\n"
        "-h, --help                   Display this help text.\n"
        "-v, --version                Display version information.\n",
        argv[0]
    );
}

static void print_version() {
    printf("simplylock v" SIMPLYLOCK_VERSION "\n");
}

static char* trim(char* str, size_t len, size_t* outLen) {
    char* begin = str;
    size_t oLen = len;

    // Moves `begin` forward skipping spaces
    while (isspace((unsigned char)*begin)) {
        begin++;
        oLen--;
    }

    // Sets to 0 all the ending spaces
    char* end = str + len - 1;
    while (end > begin && isspace((unsigned char)*end)) {
        *end = 0;
        end--;
        oLen--;
    }

    if (outLen != NULL) {
        *outLen = oLen;
    }

    return begin;
}


static int split_users(struct options* options, char* users) {

    // To know how much names we have, we count how many "," are in the string.
    // We may allocate more memory than the necessary if the string is malformed.
    // We begin with two users for sure, which are root and at least one specified on the command line.
    unsigned int num_users = 2;
    for (char* c = users; *c != 0; c++) {
        if (*c == ',') {
            num_users++;
        }
    }
    options->users = (char**)malloc(num_users * sizeof(char*));
    if (options->users == NULL) {
        return -1;
    }

    // Now we tokenize the string
    int i = 0;
    char* strtok_state;
    char* token = strtok_r(users, ",", &strtok_state);
    while (token != NULL) {
        size_t token_len;
        token = trim(token, strlen(token), &token_len);
        if (token_len > 0) {
            options->users[i] = token;
            i++;
        }
        token = strtok_r(NULL, ",", &strtok_state);
    }

    // At the end of the list we add the root user
    options->users[i] = root_username;
    i++;

    options->users_size = i;

    return 0;
}

struct options* options_parse(int argc, char** argv) {

    // Allocates the sturcture
    struct options* options = (struct options*)malloc(sizeof(struct options));
    if (options == NULL) {
        return NULL;
    }

    // Defaults
    options->block_sysrequests = 1;
    options->block_vt_switch = 1;
    options->block_kernel_messages = 1;
    options->users = NULL;
    options->allow_passwordless_root = 0;
    options->message = NULL;
    options->dark_mode = 0;
    options->quick_mode = 0;
    options->background = NULL;
    options->background_fill = RESIZE_FILL;
    options->fbdev = "/dev/fb0";
    options->dont_detach = 0;
    options->show_help = 0;
    options->show_version = 0;

    // Args parsing
    int opt;
    int longopt_index;
    while ((opt = getopt_long(argc, argv, "slku:m:dqb:Dhv", long_options, &longopt_index)) != -1) {
        switch (opt) {
            case 's':
                options->block_sysrequests = 0;
                break;
            case 'l':
                options->block_vt_switch = 0;
                break;
            case 'k':
                options->block_kernel_messages = 0;
                break;
            case 'u':
                if (split_users(options, optarg) < 0) {
                    goto error;
                }
                break;
            case 'm':
                options->message = optarg;
                break;
            case 'd':
                options->dark_mode = 1;
                break;
            case 'q':
                options->quick_mode = 1;
                break;
            case 'b':
                options->background = optarg;
                break;
            case 'D':
                options->dont_detach = 1;
                break;
            case 'h':
                print_usage(argc, argv);
                options->show_help = 1;
                break;
            case 'v':
                print_version();
                options->show_version = 1;
                break;

            case 0: {
                const char* opt_name = long_options[longopt_index].name;
                if (strcmp("allow-passwordless-root", opt_name) == 0) {
                    options->allow_passwordless_root = 1;
                    break;
                } else if (strcmp("fbdev", opt_name) == 0) {
                    options->fbdev = optarg;
                    break;
                } else if (strcmp("background-fill", opt_name) == 0) {
                    if (strcmp("center", optarg) == 0) {
                        options->background_fill = CENTER;
                        break;
                    } else if (strcmp("stretch", optarg) == 0) {
                        options->background_fill = STRETCH;
                        break;
                    } else if (strcmp("resize", optarg) == 0) {
                        options->background_fill = RESIZE;
                        break;
                    } else if (strcmp("resize-fill", optarg) == 0) {
                        options->background_fill = RESIZE_FILL;
                        break;
                    }
                    // Fall to default
                }
                // Fall to default
            }

            default:
                print_usage(argc, argv);
                goto error;
        }
    }

    // If no user was manually provided, we use the user that started the application
    if (options->users == NULL) {
        uid_t uid = getuid();
        if (uid != 0) {
            struct passwd* passwd = getpwuid(uid);
            if (passwd == NULL) {
                goto error;
            }
            options->users = (char**)malloc(2 * sizeof(char*));
            options->users[0] = passwd->pw_name;
            options->users[1] = root_username;
            options->users_size = 2;
        } else {
            options->users = (char**)malloc(sizeof(char*));
            options->users[0] = root_username;
            options->users_size = 1;
        }
    }

    // Special check for the root user:
    // If only root can unlock the pc, check that it has a password.
    // Ubuntu, for example, has a passwordless root user by default.
    if (options->users_size == 1 && memcmp(options->users[0], root_username, 5) == 0) {
        struct spwd* shadow_entry = getspnam(root_username);
        if (shadow_entry == NULL || shadow_entry->sp_pwdp == NULL) {
            goto error;
        }

        // Check that a password exists for the root user and that it's not locked
        char* pwd = shadow_entry->sp_pwdp;
        if (strlen(pwd) == 0 || pwd[0] == '!' || pwd[0] == '*') {
            if (!options->allow_passwordless_root) {
                fprintf(stderr,
                    "Only root user can unlock, and it does not have a valid password. The station will not be locked.\n"
                    "To override this security measure, pass --allow-passwordless-root.\n"
                );
                goto error;
            }
        }
    }

    return options;

error:

    if (options != NULL) {
        free(options);
    }

    return NULL;

}

void options_free(struct options* options) {
    // We do not free the single user names because they are pointers
    // to static strings or the arg vector.
    free(options->users);
    free(options);
}
