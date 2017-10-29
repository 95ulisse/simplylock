#ifndef __BG_H__
#define __BG_H__

/**
 * All possible supported background fill modes. 
 */
enum background_fill_t {
    CENTER,     // No resize
    STRETCH,    // Stretch to fill
    RESIZE,     // Resize but keep proportions
    RESIZE_FILL // Resize the image to fill the whole screen, but keep proportions
};

/**
 * Initializes a new structure to draw a background image on a vty using the framebuffer.
 * 
 * @param path Path of the image to draw.
 * @param fill Fill mode.
 * @param fbdev Path to the framebuffer device.
 * @return `NULL` in case of error, a pointer to an opaque structure otherwise.
 */
void* bg_init(const char* path, enum background_fill_t fill, const char* fbdev);

/**
 * Redraws the image stored in `bg` to the framebuffer.
 * 
 * @param bg Opaque pointer returned by bg_init.
 */
void bg_paint(void* bg);

/**
 * Releases all the resources held by the given `bg`.
 */
void bg_free(void* bg);

#endif