
#pragma once

// required by many headers
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// color storage
typedef uint8_t color_subpixel_t;
#define COLOR_SUBPIXEL_MAX (((color_subpixel_t)(0))-((color_subpixel_t)1))

// color names
#define COLOR_NAMES \
    TRANSMOG(red) \
    TRANSMOG(green) \
    TRANSMOG(blue) \
    TRANSMOG(cyan) \
    TRANSMOG(magenta) \
    TRANSMOG(yellow) \
    TRANSMOG(white) \
    TRANSMOG(off) \
    TRANSMOG(white_4100K) \

// color names
#define TRANSMOG(X) color_name_##X,
typedef enum _color_name {
    COLOR_NAMES
} color_name;
#undef TRANSMOG

extern const char* color_name_strings[];

// brightnesses
#define COLOR_BRIGHTNESSES \
    TRANSMOG(off, 0) \
    TRANSMOG(nearly_off, 1) \
    TRANSMOG(dim, 15 * COLOR_SUBPIXEL_MAX / 100) \
    TRANSMOG(medium, 40 * COLOR_SUBPIXEL_MAX / 100) \
    TRANSMOG(bright, 60 * COLOR_SUBPIXEL_MAX / 100) \
    TRANSMOG(ouch, 80 * COLOR_SUBPIXEL_MAX / 100) \
    TRANSMOG(full_intensity, COLOR_SUBPIXEL_MAX) \

#define TRANSMOG(NAME, VAL) color_brightness_##NAME = VAL,
typedef enum _color_brightness {
    COLOR_BRIGHTNESSES
} color_brightness;
#undef TRANSMOG

extern const char* color_brightness_strings[];

typedef struct _color_rgb_t {
    color_subpixel_t r;
    color_subpixel_t g;
    color_subpixel_t b;
} color_rgb_t;

typedef struct _color_hsv_t {
    color_subpixel_t h;
    color_subpixel_t s;
    color_subpixel_t v;
} color_hsv_t;

// color space transforms
color_rgb_t color_hsv_to_rgb(color_hsv_t);
color_hsv_t color_rgb_to_hsv(color_rgb_t);
color_rgb_t color_name_and_brightness_to_rgb(color_name, color_brightness);

/**
 * Pulled from LED example main.c
 * @brief Simple helper function, converting HSV color space to RGB color space
 *
 * Wiki: https://en.wikipedia.org/wiki/HSL_and_HSV
 *
 * h = [0, 360)
 * s = [0-100]
 * v = [0-100]
 */
void led_strip_hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t *r, uint32_t *g, uint32_t *b);
