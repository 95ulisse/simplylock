#ifndef __AUTH_H__
#define __AUTH_H__

/**
 *    Uses PAM to authenticate the given user.
 *
 *    @param  user User to authenticate.
 *    @return      `0` if the user successfully authenticated, `-1` otherwise.
 */
int auth_authenticate_user(char* user);

#endif
