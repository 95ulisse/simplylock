#include <security/pam_appl.h>
#include <security/pam_misc.h>

#include "auth.h"

static struct pam_conv conv = {
    misc_conv,
    NULL
};

int auth_authenticate_user(char* user) {
    int ret = 0;

    // We start a new PAM session
    pam_handle_t* pamh;
    int pam_ret = pam_start("simplylock", user, &conv, &pamh);

    // Authentication
    if (pam_ret == PAM_SUCCESS) {
        pam_ret = pam_authenticate(pamh, 0);
    } else {
        ret = -1;
    }

    // Authorization
    if (pam_ret == PAM_SUCCESS) {
        pam_ret = pam_acct_mgmt(pamh, 0);
    } else {
        ret = -1;
    }

    // Has the user successfully authenticated?
    ret = pam_ret == PAM_SUCCESS ? 0 : -1;

    // Terminate PAM session
    if (pam_end(pamh, pam_ret) != PAM_SUCCESS) {
        ret = -1;
    }

    return ret;
}
