#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>

#include "options.h"
#include "vt.h"

int main(int argc, char** argv) {

    // Parses the options
    struct options* options = options_parse(argc, argv);
    if (options == NULL) {
        exit(1);
    }
    if (options->show_help || options->show_version) {
        free(options);
        exit(0);
    }

    fprintf(stdout, "%d %d %s %s %d\n",
        options->block_sysrequests,
        options->block_vt_switch,
        options->user,
        options->message,
        options->show_help
    );

    free(options);

    return 0;
}
