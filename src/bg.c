#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <fcntl.h>
#include <stropts.h>
#include <unistd.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <wand/MagickWand.h>

#include "bg.h"

struct bg {
    
    // Framebuffer fd and mmapped memory address
    int fbfd;
    char* fbmem;
    size_t fbmem_len;

    // Screen info
    int width;
    int height;
    int original_bpp;

    // Wands
    MagickWand* m_wand;
    PixelWand* p_wand;

};

static bool fill_image(struct bg* bg, enum background_fill_t fill) {
    
    // Extract width and height of the image
    int img_w = MagickGetImageWidth(bg->m_wand);
    int img_h = MagickGetImageHeight(bg->m_wand);

    // Screen size
    int screen_w = bg->width;
    int screen_h = bg->height;

    switch (fill) {
        
        case CENTER:
            // This centres the original image on a new canvas.
            if (MagickExtentImage(bg->m_wand, screen_w, screen_h, -(screen_w - img_w) / 2, -(screen_h - img_h) / 2) == MagickFalse) {
                fprintf(stderr, "Error manipulating image.\n");
                return false;
            }
            break;
        
        case STRETCH:
            // Resize the image to match the screen size
            if (MagickResizeImage(bg->m_wand, screen_w, screen_h, LanczosFilter, 1) == MagickFalse) {
                fprintf(stderr, "Error manipulating image.\n");
                return false;
            }
            break;
        
        case RESIZE: {
        
            // Take the smaller ratio
            float ratio_w = (float)screen_w / (float)img_w;
            float ratio_h = (float)screen_h / (float)img_h;
            float ratio = ratio_w < ratio_h ? ratio_w : ratio_h;

            // Compute the new dimensions
            int new_w = (int)(ratio * img_w);
            int new_h = (int)(ratio * img_h);

            // Resize the image
            if (MagickResizeImage(bg->m_wand, new_w, new_h, LanczosFilter, 1) == MagickFalse) {
                fprintf(stderr, "Error manipulating image.\n");
                return false;
            }

            // Center the image
            if (MagickExtentImage(bg->m_wand, screen_w, screen_h, -(screen_w - new_w) / 2, -(screen_h - new_h) / 2) == MagickFalse) {
                fprintf(stderr, "Error manipulating image.\n");
                return false;
            }

            break;
        }
        
        default:
            fprintf(stderr, "Unexpected background fill value.\n");
            abort();
            break;

    }

    return true;

}

void* bg_init(const char* path, enum background_fill_t fill, const char* fbdev) {
    
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;

    // Allocates the memory for the struct
    struct bg* bg = calloc(1, sizeof(struct bg));
    if (bg == NULL) {
        perror("Cannot allocate memory for background image.");
        return NULL;
    }
    bg->fbfd = -1;

    // Opens the framebuffer device
    int fbfd = open(fbdev, O_RDWR);
    if (fbfd < 0) {
        perror("Cannot open framebuffer device.");
        goto error;
    }
    bg->fbfd = fbfd;

    // Gets variable screen information
    if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
        perror("Error reading variable information from framebuffer.");
        goto error;
    }
    bg->width = vinfo.xres;
    bg->height = vinfo.yres;
    bg->original_bpp = vinfo.bits_per_pixel;

    // Sets 32 bits per pixel
    vinfo.bits_per_pixel = 32;
    if (ioctl(fbfd, FBIOPUT_VSCREENINFO, &vinfo)) {
        perror("Error setting bits per pixel.");
        goto error;
    }

    // Get fixed screen information
    if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo)) {
        perror("Error reading fixed information.");
        goto error;
    }

    // Mmap framebuffer memory
    bg->fbmem = (char*) mmap(0, finfo.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if ((intptr_t)bg->fbmem == -1) {
        perror("Unable to mmap framebuffer.");
        goto error;
    }
    bg->fbmem_len = finfo.smem_len;

    // Initialize MagickWand if not done yet
    if (IsMagickWandInstantiated() == MagickFalse) {
        MagickWandGenesis();
    }

    // Create the needed wands
    bg->m_wand = NewMagickWand();
    if (bg->m_wand == NULL) {
        fprintf(stderr, "Cannot allocate magick wand.\n");
        goto error;
    }
    bg->p_wand = NewPixelWand();
    if (bg->p_wand == NULL) {
        fprintf(stderr, "Cannot allocate pixel wand.\n");
        goto error;
    }
    PixelSetColor(bg->p_wand, "black");
	MagickSetImageBackgroundColor(bg->m_wand, bg->p_wand);

    // Load the image
    if (MagickReadImage(bg->m_wand, path) == MagickFalse) {
        fprintf(stderr, "Unable to load background image %s.\n", path);
        goto error;
    }

    // Prepares the image so that it matches the screen size
    if (!fill_image(bg, fill)) {
        goto error;
    }

    return bg;

error:
    bg_free(bg);
    return NULL;

}

void bg_paint(void* background) {
    struct bg* bg = (struct bg*)background;

    // Just copy the pixels from the image to the framebuffer
    MagickExportImagePixels(bg->m_wand, 0, 0, bg->width, bg->height, "BGRA", CharPixel, bg->fbmem);

}

void bg_free(void* background) {
    if (background != NULL) {
        struct bg* bg = (struct bg*)background;
        
        // Free MagickWand structures
        if (bg->m_wand != NULL) {
            DestroyMagickWand(bg->m_wand);
        }
        if (bg->p_wand != NULL) {
            DestroyPixelWand(bg->p_wand);
        }

        // Unmap framebuffer memory
        if (bg->fbmem != NULL && (intptr_t)bg->fbmem != -1) {
            munmap(bg->fbmem, bg->fbmem_len);
        }

        // Close framebuffer fd
        if (bg->fbfd != -1) {

            // Restore original bpp
            struct fb_var_screeninfo vinfo;
            if (ioctl(bg->fbfd, FBIOGET_VSCREENINFO, &vinfo) == 0) {
                vinfo.bits_per_pixel = bg->original_bpp;
                ioctl(bg->fbfd, FBIOPUT_VSCREENINFO, &vinfo);
            }

            close(bg->fbfd);
        }

        free(bg);

    }
}