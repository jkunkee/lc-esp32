
#include "color.h"

// Create exactly one instance of these tables

// Color space names

#define TRANSMOG(cname, strname) strname,
const char* color_space_names[] = { COLOR_SPACES };
#undef TRANSMOG

// CIE

#define TRANSMOG(name, x, y) COLOR_CIE_TO_STRUCT(x, y, 0),
const color_cie_t color_cie_chroma_values[] = { COLOR_CIE_CHROMAS };
#undef TRANSMOG
#define TRANSMOG(name, x, y) #name,
const char* color_cie_chroma_names[] = { COLOR_CIE_CHROMAS };
#undef TRANSMOG

#define TRANSMOG(name, val) val,
const color_component_t color_cie_luminosity_values[] = { COLOR_CIE_LUMINOSITIES };
#undef TRANSMOG
#define TRANSMOG(name, val) #name,
const char* color_cie_luminosity_names[] = { COLOR_CIE_LUMINOSITIES };
#undef TRANSMOG

// TODO: Finish port
#error This is not in a functional state.
// HSV

// RGB

#define COLOR_NAMES
#define TRANSMOG(name, value) #name,
const char* color_name_strings[] = {
    COLOR_NAMES
};
#undef TRANSMOG

#define COLOR_BRIGHTNESSES
#define TRANSMOG(NAME, VAL) #NAME,
const char* color_brightness_strings[] = {
    COLOR_BRIGHTNESSES
};
#undef TRANSMOG

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
void led_strip_hsv2rgb(uint32_t h, uint32_t s, uint32_t v, uint32_t *r, uint32_t *g, uint32_t *b)
{
    h %= 360; // h -> [0,359]
    uint32_t rgb_max = v * 2.55f;
    uint32_t rgb_min = rgb_max * (100 - s) / 100.0f;

    uint32_t i = h / 60;
    uint32_t diff = h % 60;

    // RGB adjustment amount by hue
    uint32_t rgb_adj = (rgb_max - rgb_min) * diff / 60;

    switch (i) {
    case 0:
        *r = rgb_max;
        *g = rgb_min + rgb_adj;
        *b = rgb_min;
        break;
    case 1:
        *r = rgb_max - rgb_adj;
        *g = rgb_max;
        *b = rgb_min;
        break;
    case 2:
        *r = rgb_min;
        *g = rgb_max;
        *b = rgb_min + rgb_adj;
        break;
    case 3:
        *r = rgb_min;
        *g = rgb_max - rgb_adj;
        *b = rgb_max;
        break;
    case 4:
        *r = rgb_min + rgb_adj;
        *g = rgb_min;
        *b = rgb_max;
        break;
    default:
        *r = rgb_max;
        *g = rgb_min;
        *b = rgb_max - rgb_adj;
        break;
    }
}

//color_rgb_t temp_to_rgb(const uint32_t temp_in_kelvins)
//{
//    if (temp_in_kelvins < 1000 || 40000 < temp_in_kelvins)
//    {
//        return ;
//	}
//}
