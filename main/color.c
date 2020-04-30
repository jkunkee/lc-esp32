
#include "color.h"

#include "esp_log.h"
#define TAG "color.c"

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

// CCT

#define TRANSMOG(name, val) val,
const uint16_t color_cct_temp_values[] = { COLOR_CCT_TEMPERATURES };
#undef TRANSMOG
#define TRANSMOG(name, val) #name,
const char* color_cct_temp_names[] = { COLOR_CCT_TEMPERATURES };
#undef TRANSMOG

#define TRANSMOG(name, val) val,
const color_component_t color_cct_luminosity_values[] = { COLOR_CCT_LUMINOSITIES };
#undef TRANSMOG
#define TRANSMOG(name, val) #name,
const char* color_cct_luminosity_names[] = { COLOR_CCT_LUMINOSITIES };
#undef TRANSMOG

// HSV

#define TRANSMOG(name, val) val,
const uint16_t color_hsv_hue_values[] = { COLOR_HSV_HUES };
#undef TRANSMOG
#define TRANSMOG(name, val) #name,
const char* color_hsv_hue_names[] = { COLOR_HSV_HUES };
#undef TRANSMOG

#define TRANSMOG(name, val) val,
const color_component_t color_hsv_sat_values[] = { COLOR_HSV_SATURATIONS };
#undef TRANSMOG
#define TRANSMOG(name, val) #name,
const char* color_hsv_sat_names[] = { COLOR_HSV_SATURATIONS };
#undef TRANSMOG

#define TRANSMOG(name, val) val,
const color_component_t color_hsv_val_values[] = { COLOR_HSV_VALUES };
#undef TRANSMOG
#define TRANSMOG(name, val) #name,
const char* color_hsv_val_names[] = { COLOR_HSV_VALUES };
#undef TRANSMOG

// RGB

#define TRANSMOG(name, val) val,
const color_rgb_t color_rgb_color_values[] = { COLOR_RGB_TO_STRUCT(COLOR_RGB_BASE, 0, 0), };
#undef TRANSMOG
#define TRANSMOG(name, val) #name,
const char* color_rgb_color_names[] = { COLOR_RGB_COLORS };
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

// downconverters

color_rgb_t color_cie_to_rgb(color_cie_t input)
{
    return COLOR_RGB_TO_STRUCT(0, 0, 0);
}

color_rgb_t color_cct_to_rgb(color_cct_t input)
{
    return COLOR_RGB_TO_STRUCT(0, 0, 0);
}

color_rgb_t color_hsv_to_rgb(color_hsv_t input)
{
    uint32_t r, g, b;
    led_strip_hsv2rgb(COLOR_HSV_FROM_STRUCT(input), &r, &g, &b);
    return COLOR_RGB_TO_STRUCT(r, g, b);
}

// to facilitate smooth transition effects
color_hsv_t color_rgb_to_hsv(color_rgb_t input)
{
    return COLOR_HSV_TO_STRUCT(0, 0, 0);
}

// aggregate transforms

// make precarious assumption about enum types being interchangeable and int-sized
color_rgb_t color_enum_to_rgb(color_space space, int a, int b, int c)
{
    color_rgb_t color = { 0 };
    switch (space)
    {
    case color_space_cie_1931_xyY:
        {
            // TODO: parameter validation
            if (color_cie_chroma_enum_max <= (unsigned)a) { }
            color_cie_t cie = color_cie_chroma_values[a];
            cie.CCY = color_cie_luminosity_values[b];
            color = color_cie_to_rgb(cie);
        }
        break;
    case color_space_cct:
        {
            uint16_t temp = color_cct_temp_values[a];
            color_component_t luminosity = color_cct_luminosity_values[b];
            color = color_cct_to_rgb(COLOR_CCT_TO_STRUCT(temp, luminosity));
        }
        break;
    case color_space_hsv:
        {
            uint16_t h = color_hsv_hue_values[a];
            color_component_t s = color_hsv_sat_values[b];
            color_component_t v = color_hsv_val_values[c];
            color = color_hsv_to_rgb(COLOR_HSV_TO_STRUCT(h, s, v));
        }
        break;
    case color_space_rgb:
        {
            color = color_rgb_color_values[a];
        }
        break;
    default:
        color.g = 88;
        ESP_LOGE(TAG, "%s: unknown color space %d", __FUNCTION__, space);
    }
    return color;
}











