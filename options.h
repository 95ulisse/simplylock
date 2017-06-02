#ifndef __OPTIONS_H__
#define __OPTIONS_H__

/**
 *    Structure containing all the SimplyLock options.
 */
struct options {
    unsigned int block_sysrequests;
    unsigned int block_vt_switch;
    unsigned int block_kernel_messages;
    unsigned int quick_unlock;
    unsigned int backlight_off;
    char** users;
    unsigned int users_size;
    char* message;
    unsigned int show_help;
    unsigned int show_version;
};

/**
 *    Parses the given raw arguments into a more usable `struct options`.
 *
 *    @param  argc Number of arguments.
 *    @param  argv Array of `char*` containing the arguments.
 *    @return      `struct options` containing the parsed options,
 *                 or `NULL` in case of an error, and sets `errno`.
 */
struct options* options_parse(int argc, char** argv, uid_t uid);

/**
 *    Releases all the resources allocated for a `struct options`.
 *
 *    @param options `struct options` to deallocate.
 */
void options_free(struct options* options);

#endif
