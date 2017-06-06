#ifndef __LOCK_H__
#define __LOCK_H__

#include "options.h"
#include "vt.h"

/**
 *    Creates a new virtual terminal and locks it down.
 *    Do not `vt_free` the returned vt, but use `unlock` to clean everything up.
 *
 *    @param  options SimplyLock options.
 *    @return         The new vt created.
 */
struct vt* lock(struct options* options);

/**
 *    Unlocks the previously locked terminal and restores
 *    the state of the system before the call to `lock`.
 *
 *    @param options SimplyLock options.
 */
void unlock(struct options* options);

#endif
