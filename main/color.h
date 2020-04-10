
// color definitions
// pixel fraction definitions
#define PX_OFF  0
#define PX_NOFF 1
#define PX_SOFT 40
#define PX_HARD 100
// templates for common colors
#define PXS_RED(intensity)       intensity, PX_OFF, PX_OFF
#define PXS_GREEN(intensity)     PX_OFF, intensity, PX_OFF
#define PXS_BLUE(intensity)      PX_OFF, PX_OFF, intensity
#define PXS_CYAN(intensity)      PX_OFF, intensity, intensity
#define PXS_MAGENTA(intensity)   intensity, PX_OFF, intensity
#define PXS_YELLOW(intensity)    intensity, intensity, PX_OFF
#define PXS_GREYSCALE(intensity) intensity, intensity, intensity
// colors based on templates
#define PXS_ON PXS_GREYSCALE(PX_HARD)
#define PXS_NOFF PXS_GREYSCALE(PX_NOFF)
#define PXS_OFF PXS_GREYSCALE(PX_OFF)
// full pixel definitions
#define PXS_UNUSED      PXS_NOFF
#define PXS_UNDERSCORE  PXS_YELLOW(PX_SOFT)
#define PXS_DASH        PXS_BLUE(PX_SOFT)
#define PXS_COLON       PX_NOFF, PX_SOFT, PX_SOFT
#define PXS_SLASH       PX_SOFT, PX_NOFF, PX_SOFT
#define PXS_TIME_BIT    PX_NOFF, PX_HARD, PX_NOFF
#define PXS_DATE_BIT    PX_HARD, PX_NOFF, PX_NOFF

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
