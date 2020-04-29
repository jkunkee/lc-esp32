
#pragma once

// required by many headers
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// There has to be a starting representation of color and brightness:
// * CIE 1931 Chromaticity Coordinate (CC) plus luminosity: CCx, CCy, lm
//   https://www.cree.com/led-components/media/documents/LED_color_mixing.pdf
// * Correlated Color Temperature plus luminosity
// * Hue, Saturation, Value: h, s, v
// * Red, Green, Blue: r, g, b
//
// The end goal is LED RGB PWM values.
// One road there is to convert everything to ideal RGB, then apply
// LED-specific brightness correction, then apply LED-specific gamma
// correction.
//
// The programmer should have access to each layer.

// Color Storage
// Set up all the types for actually storing color values.

// For simplicity (mostly because I haven't found a reason to do otherwise),
// color components will all be a consistent unsigned type.

typedef uint8_t color_component_t;

// Maximum value of color_component_t for convenience

#define COLOR_COMPONENT_MAX (((color_component_t)(0))-((color_component_t)1))

///////////////////////////////////////////////////////////////////////////////
// Each input color space and processing layer needs a consistent way to store
// color tuples.

// CIE 1931 Chromaticity Coordinates plus luminosity
typedef struct _color_cie_t {
    float CCx;
    float CCy;
    color_component_t lm;
} color_cie_t;

// avoid repetitive typecase+brace fiddling
#define COLOR_CIE_TO_STRUCT(x, y, l) ((color_cie_t){.CCx = x, .CCy = y, .lm = l})

// Correlated Color Temperature
typedef struct _color_cct_t {
    uint16_t temp;
    color_component_t lm;
} color_cct_t;

#define COLOR_CCT_TO_STRUCT(t, l) ((color_cct_t){.temp = t, .lm = l})

typedef struct _color_hsv_t {
    uint16_t h;
    color_component_t s;
    color_component_t v;
} color_hsv_t;

#define COLOR_HSV_TO_STRUCT(hm, sm, vm) ((color_hsv_t){.h = hm, .s = sm, .v = vm})
#define COLOR_HSV_FROM_STRUCT(hsv) hsv.h, hsv.s, hsv.v

typedef struct _color_rgb_t {
    color_component_t r;
    color_component_t g;
    color_component_t b;
} color_rgb_t;

typedef color_rgb_t color_rgb_pwm_t;

#define COLOR_RGB_TO_STRUCT(rm, gm, bm) ((color_rgb_t){.r = rm, .g = gm, .b = bm})
#define COLOR_RGB_FROM_STRUCT(color) color.r, color.g, color.b

///////////////////////////////////////////////////////////////////////////////
// Names
// Programming and making UIs using names instead of raw numbers is easier.

// This weird #define TRANSMOG pattern allows one array of macros to be reused
// in a number of different contexts.

// Color space names

#define COLOR_SPACES \
    TRANSMOG(cie_1931, "CIE 1931") \
    TRANSMOG(cct, "CCT") \
    TRANSMOG(hsv, "HSV") \
    TRANSMOG(rgb, "RGB") \

// Generic Color Component values

#define COLOR_COMPONENT_VALUES \
    TRANSMOG(zero, 0) \
    TRANSMOG(one, 1) \
    TRANSMOG(low, 15 * COLOR_COMPONENT_MAX / 100) \
    TRANSMOG(medium, 40 * COLOR_COMPONENT_MAX / 100) \
    TRANSMOG(high, 60 * COLOR_COMPONENT_MAX / 100) \
    TRANSMOG(ouch, 80 * COLOR_COMPONENT_MAX / 100) \
    TRANSMOG(max, COLOR_COMPONENT_MAX) \

// CIE CCx,CCy pairs

// The interval [0.0, 1.0] is represented directly.
// The author picked the nm values off of Figures 7-9 of
// https://www.cree.com/led-components/media/documents/LED_color_mixing.pdf
// by eye. These were then converted to CIE x,y using
// https://www.ledtuning.nl/en/cie-convertor
// Another good resource is http://hyperphysics.phy-astr.gsu.edu/hbase/vision/cie.html
// TRANSMOG(name, CCx, CCy)
#define COLOR_CIE_CHROMAS \
    TRANSMOG(far_red_770, 0.734701097, 0.265298903) \
    TRANSMOG(red_650, 0.725992318, 0.274007682) \
    TRANSMOG(yellow_575, 0.478774791, 0.520202307) \
    TRANSMOG(green_510, 0.013870246, 0.750186428) \
    TRANSMOG(cyan_490, 0.045390735, 0.294975965) \
    TRANSMOG(blue_477, 0.102775863, 0.102863739) \
    TRANSMOG(far_magenta_380, 0.174112257, 0.004963727) \
    TRANSMOG(achromat_E, 1.0/3.0, 1.0/3.0) \

// luminosities for use with CIE CCx,CCY pairs
#define COLOR_CIE_LUMINOSITIES COLOR_COMPONENT_VALUES

// Correlated Color Temperature
// https://www.cree.com/led-components/media/documents/LED_color_mixing.pdf
// http://www.westinghouselighting.com/color-temperature.aspx

#define COLOR_CCT_TEMPERATURES \
    TRANSMOG(red_1000, 1000) \
    TRANSMOG(warm_2500, 2500) \
    TRANSMOG(cool_3800, 3800) \
    TRANSMOG(day_5500, 5500) \
    TRANSMOG(blue_10000, 10000) \

#define COLOR_CCT_LUMINOSITIES COLOR_COMPONENT_VALUES

// Hue, Saturation, Value presets

// Hue
// Degrees around the color wheel are represented literally.
// Functions accepting them should treat them modulo 360.
#define COLOR_HSV_HUES \
    TRANSMOG(red, 0) \
    TRANSMOG(green, 120) \
    TRANSMOG(blue, 240) \
    TRANSMOG(cyan, 180) \
    TRANSMOG(magenta, 300) \
    TRANSMOG(yellow, 60) \

// Saturation
// Values here on [0-COLOR_COMPONENT_MAX] map to [0.0-1.0]
#define COLOR_HSV_SATURATIONS COLOR_COMPONENT_VALUES

// Value
// This uses the simplest definition of value: the arithmetic mean of RGB
#define COLOR_HSV_VALUES COLOR_COMPONENT_VALUES

// Red, Green, Blue presets
//
// Cryhavocolor "Dogs of War" colors courtesy of
// https://www.schlockmercenary.com/2014-12-08

#define COLOR_RGB_BASE 120
#define COLOR_RGB_COLORS \
    TRANSMOG(red,     COLOR_RGB_TO_STRUCT(COLOR_RGB_BASE, 0, 0)) \
    TRANSMOG(green,   COLOR_RGB_TO_STRUCT(0, COLOR_RGB_BASE, 0)) \
    TRANSMOG(blue,    COLOR_RGB_TO_STRUCT(0, 0, COLOR_RGB_BASE)) \
    TRANSMOG(cyan,    COLOR_RGB_TO_STRUCT(0, COLOR_RGB_BASE/2, COLOR_RGB_BASE/2)) \
    TRANSMOG(magenta, COLOR_RGB_TO_STRUCT(COLOR_RGB_BASE/2, 0, COLOR_RGB_BASE/2)) \
    TRANSMOG(yellow,  COLOR_RGB_TO_STRUCT(COLOR_RGB_BASE/2, COLOR_RGB_BASE/2, 0)) \
    TRANSMOG(white,   COLOR_RGB_TO_STRUCT(COLOR_RGB_BASE/3, COLOR_RGB_BASE/3, COLOR_RGB_BASE/3)) \
    TRANSMOG(nearly_off, COLOR_RGB_TO_STRUCT(1, 1, 1)) \
    TRANSMOG(off,     COLOR_RGB_TO_STRUCT(0, 0, 0)) \
    TRANSMOG(whyamionfirewhite,   COLOR_RGB_TO_STRUCT(COLOR_COMPONENT_MAX, COLOR_COMPONENT_MAX, COLOR_COMPONENT_MAX)) \
    TRANSMOG(auiiieeyellow,       COLOR_RGB_TO_STRUCT(COLOR_COMPONENT_MAX, COLOR_COMPONENT_MAX, 0)) \
    TRANSMOG(whosebloodisthisred, COLOR_RGB_TO_STRUCT(COLOR_COMPONENT_MAX, 0, 0)) \

///////////////////////////////////////////////////////////////////////////////
// Useful Values
// Enums, enum-to-value, and enum-to-name translation tables.

#define MAKE_ENUM(prefix, list) \
typedef enum _##prefix \
{ \
    list \
    prefix##_enum_max \
} prefix;

// Color space names
#define TRANSMOG(cname, strname) color_space_##cname,
MAKE_ENUM(color_space, COLOR_SPACES)
#undef TRANSMOG

extern const char* color_space_names[];

// CIE

#define TRANSMOG(name, x, y) color_cie_chroma_##name,
typedef enum _color_cie_chroma
{
    COLOR_CIE_CHROMAS
    color_cie_chroma_enum_max,
} color_cie_chroma;
#undef TRANSMOG

extern const color_cie_t color_cie_chroma_values[];
extern const char* color_cie_chroma_names[];

#define TRANSMOG(name, value) color_cie_lm_##name,
typedef enum _color_cie_luminosity
{
    COLOR_CIE_LUMINOSITIES
    color_cie_lm_enum_max,
} color_cie_luminosity;
#undef TRANSMOG

extern const color_component_t color_cie_luminosity_values[];
extern const char* color_cie_luminosity_names[];

// CCT

#define TRANSMOG(name, value) color_cct_temp_##name,
typedef enum _color_cct_temp
{
    COLOR_CCT_TEMPERATURES
    color_cct_temp_enum_max,
} color_cct_temp;
#undef TRANSMOG

extern const uint16_t color_cct_temp_values[];
extern const char* color_cct_temp_names[];

#define TRANSMOG(name, value) color_cct_lm_##name,
typedef enum _color_cct_luminosity
{
    COLOR_CCT_LUMINOSITIES
    color_cct_lm_enum_max,
} color_cct_luminosity;
#undef TRANSMOG

extern const color_component_t color_cct_luminosity_values[];
extern const char* color_cct_luminosity_names[];

// HSV

#define TRANSMOG(name, value) color_hsv_hue_##name,
typedef enum _color_hsv_hue
{
    COLOR_HSV_HUES
    color_hsv_hue_enum_max,
} color_hsv_hue;
#undef TRANSMOG

extern const uint16_t color_hsv_hue_values[];
extern const char* color_hsv_hue_names[];

#define TRANSMOG(name, value) color_hsv_sat_##name,
typedef enum _color_hsv_sat
{
    COLOR_HSV_SATURATIONS
    color_hsv_sat_enum_max,
} color_hsv_sat;
#undef TRANSMOG

extern const color_component_t color_hsv_sat_values[];
extern const char* color_hsv_sat_names[];

#define TRANSMOG(name, value) color_hsv_val_##name,
typedef enum _color_hsv_val
{
    COLOR_HSV_VALUES
    color_hsv_val_enum_max,
} color_hsv_val;
#undef TRANSMOG

extern const color_component_t color_hsv_val_values[];
extern const char* color_hsv_val_names[];

// RGB

#define TRANSMOG(name, value) color_rgb_color_##name,
typedef enum _color_rgb_color
{
    COLOR_RGB_COLORS
    color_rgb_color_enum_max,
} color_rgb_color;
#undef TRANSMOG

extern const color_rgb_t color_rgb_color_values[];
extern const char* color_rgb_color_names[];

///////////////////////////////////////////////////////////////////////////////
// Color space transforms

// downconverters
color_rgb_t color_cie_to_rgb(color_cie_t);
color_rgb_t color_cct_to_rgb(color_cct_t);
color_rgb_t color_hsv_to_rgb(color_hsv_t);

// to facilitate smooth transition effects
color_hsv_t color_rgb_to_hsv(color_rgb_t);

// make precarious assumption about enum types being interchangeable
color_rgb_t color_enum_to_rgb(color_space, int chroma_temp_hue_color, int luminosity_saturation, int value);
