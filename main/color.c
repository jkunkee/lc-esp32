
#include "color.h"

#include "esp_log.h"
#define TAG "color.c"

#include <math.h>

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
const color_rgb_t color_rgb_color_values[] = { COLOR_RGB_COLORS };
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
    // Argument validation
    if (input.CCx < 0.0f)
    {
        ESP_LOGE(TAG, "%s: CCx %f less than lower bound, truncating", __FUNCTION__, input.CCx);
        input.CCx = 0.0f;
    }
    if (input.CCx > 1.0f)
    {
        ESP_LOGE(TAG, "%s: CCx %f greater than upper bound, truncating", __FUNCTION__, input.CCx);
        input.CCx = 1.0f;
    }
    if (input.CCy < 0.0f)
    {
        ESP_LOGE(TAG, "%s: CCx %f less than lower bound, truncating", __FUNCTION__, input.CCy);
        input.CCy = 0.0f;
    }
    if (input.CCy > 1.0f)
    {
        ESP_LOGE(TAG, "%s: CCx %f greater than upper bound, truncating", __FUNCTION__, input.CCy);
        input.CCy = 1.0f;
    }
    // all values of Y map into the interval [0,1]

    // Convert xxY to XYZ
    // http://www.brucelindbloom.com/index.html?Eqn_xyY_to_XYZ.html
    float X, Y, Z;
    // Renormalize input.Y to a float to match x and y
    Y = ((float)(input.CCY)) / ((float)(color_cie_luminosity_values[color_cie_lm_max]));
    // Convert the other two components
    X = input.CCx * Y / input.CCy;
    Z = (1.0f - input.CCx - input.CCy) * Y / input.CCy;

    // Project XYZ into RGB
    // http://www.brucelindbloom.com/index.html?Eqn_xyY_to_XYZ.htmlj

    // XYZ to linear RGB
    // M^-1 * X,Y,Z
    // CIE RGB M^-1 from
    // http://www.brucelindbloom.com/index.html?Eqn_xyY_to_XYZ.html
    //  2.3706743 -0.9000405 -0.4706338
    // -0.5138850  1.4253036  0.0885814
    //  0.0052982 -0.0146949  1.0093968
    float R, G, B;
    R = X *  2.3706743f + Y * -0.9000405f + Z * -0.4706338f;
    G = X * -0.5138850f + Y *  1.4253036f + Z *  0.0885814f;
    B = X *  0.0052982f + Y * -0.0146949f + Z *  1.0093968f;

    // Gamma is the companding method. It is applied in the LED driver.

    // Reduce to integer type
    color_rgb_t rgb;
    rgb.r = R * COLOR_COMPONENT_MAX;
    rgb.g = G * COLOR_COMPONENT_MAX;
    rgb.b = B * COLOR_COMPONENT_MAX;

    return rgb;
}

// take a float, pretend it's on [0,255], scale it onto [0,COLOR_COMPONENT_MAX],
// clamp it, and convert it to the integer type.
//color_component_t clamp_and_scale_float_to_component_t(float val)
//{
//    float scaled = val * ((float)COLOR_COMPONENT_MAX) / 255.0f;
//    float clamped = scaled;
//    if (scaled < 0.0f)
//    {
//        clamped = 0.0f;
//    }
//    if (scaled > (float)COLOR_COMPONENT_MAX)
//    {
//        clamped = (float)COLOR_COMPONENT_MAX;
//    }
//    return clamped;
//}

color_rgb_t color_cct_to_rgb(color_cct_t input)
{
    // http://www.brucelindbloom.com/index.html?Eqn_T_to_xy.html
    if (input.temp < 4000 || 25000 < input.temp)
    {
        ESP_LOGW(TAG, "%s: temperature %d out of algorithm range (4000-25000K)", __FUNCTION__, input.temp);
    }

    color_cie_t xxY;

    float ccx, ccy, temp;

    temp = (float)(input.temp);

    if (temp < 7000)
    {
        ccx = -4.6070e9f / powf(temp, 3.0f) + 2.9678e6f / powf(temp, 2.0f) + 0.09911e3f / temp + 0.244063f;
    }
    else
    {
        ccx = -2.0064e9f / powf(temp, 3.0f) + 1.9018e6f / powf(temp, 2.0f) + 0.24748e3f / temp + 0.237040f;
    }

    ccy = -3.000f * powf(temp, 2.0f) + 2.870f * temp - 0.275f;

    xxY.CCx = ccx;
    xxY.CCy = ccy;
    xxY.CCY = input.lm;

    return color_cie_to_rgb(xxY);

/*
    // https://tannerhelland.com/2012/09/18/convert-temperature-rgb-algorithm-code.html

    color_rgb_t result;

    float adj_temp = input.temp / 100.0f;
    float rf, gf, bf;

    // red
    if (input.temp < 6600)
    {
        rf = 255.0f;
    }
    else
    {
        rf = 329.698727446f * powf(adj_temp - 60.0f, -0.1332047592f);
    }

    // green
    if (input.temp < 6600)
    {
        gf = 99.4708025861f * logf(adj_temp) - 161.1195681661f;
    }
    else
    {
        gf = 288.1221695283f * powf(adj_temp - 60.0f, -0.0755148492f);
    }

    // blue
    if (input.temp > 6500)
    {
        bf = 255.0f;
    }
    else if (input.temp < 2000)
    {
        bf = 0.0f;
    }
    else
    {
        bf = 138.5177615561f * logf(adj_temp - 10.0f) - 305.04479227307f;
    }

    result.r = clamp_and_scale_float_to_component_t(rf);
    result.g = clamp_and_scale_float_to_component_t(gf);
    result.b = clamp_and_scale_float_to_component_t(bf);

    return result;
*/
}

color_rgb_t color_hsv_to_rgb(color_hsv_t input)
{
    uint32_t r, g, b;
    uint32_t h, s, v;

    h = input.h;
    s = input.s;
    v = input.v;

    if (h > 359 || s > 100 || v > 100)
    {
        ESP_LOGE(TAG, "%s: Value out of range! h=%d,s=%d,v=%d", __FUNCTION__, h, s, v);
    }

    led_strip_hsv2rgb(h, s, v, &r, &g, &b);
    ESP_LOGI(TAG, "%s: Converted h=%d,s=%d,v=%d to r=%d,g=%d,b=%d", __FUNCTION__, h, s, v, r, g, b);
    return COLOR_RGB_TO_STRUCT(r, g, b);
}

// to facilitate smooth transition effects
color_hsv_t color_rgb_to_hsv(color_rgb_t input)
{
    color_hsv_t result;

    // https://www.rapidtables.com/convert/color/rgb-to-hsv.html
    // https://en.wikipedia.org/wiki/HSL_and_HSV
    float rf, gf, bf;

    // Scale inputs onto [0,1]
    rf = (float)(input.r) / ((float)COLOR_COMPONENT_MAX);
    gf = (float)(input.g) / ((float)COLOR_COMPONENT_MAX);
    bf = (float)(input.b) / ((float)COLOR_COMPONENT_MAX);

    // find max
    float cmax = fmaxf(rf, fmaxf(gf, bf));
    float cmin = fminf(rf, fminf(gf, bf));

    float delta = cmax - cmin;

    // Hue [0,360)
    float hue;
    // prevent divide-by-zero errors for greys
    if (delta == 0.0f)
    {
        hue = 0;
    }
    else if (cmax == rf)
    {
        hue = fmodf((gf - bf) / delta, 6.0f);
    }
    else if (cmax == gf)
    {
        hue = (bf - rf) / delta + 2.0f;
    }
    else /* if (cmax == bf) */
    {
        hue = (rf - gf) / delta + 4.0f;
    }
    result.h = hue * 60.0f;

    // Saturation [0,100]
    float saturation;
    // prevent divide-by-zero errors
    if (cmax == 0.0f)
    {
        saturation = 0.0f;
    }
    else
    {
        saturation = delta / cmax;
    }
    result.s = saturation * 100.0f;

    // Value [0,100]
    result.v = cmax * 100.0f;

    ESP_LOGI(TAG, "%s: Converted r=%u=%f,g=%u=%f,b=%u=%f to h=%f=%u,s=%f=%u,v=%f=%u",
                    __FUNCTION__,
                    input.r, rf, input.g, gf, input.b, bf,
                    hue, result.h, saturation, result.s, cmax, result.v);

    return result;
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











