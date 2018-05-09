/*============================================================================
                                  libpamcolor.c
==============================================================================
  These are the library functions, which belong in the libnetpbm library,
  that deal with colors in the PAM image format.

  This file was originally written by Bryan Henderson and is contributed
  to the public domain by him and subsequent authors.
=============================================================================*/

/* See pmfileio.c for the complicated explanation of this 32/64 bit file
   offset stuff.
*/
#define _FILE_OFFSET_BITS 64
#define _LARGE_FILES

#define _DEFAULT_SOURCE 1  /* New name for SVID & BSD source defines */
#define _BSD_SOURCE 1      /* Make sure strdup() is in string.h */
#define _XOPEN_SOURCE 500  /* Make sure strdup() is in string.h */

#include <string.h>
#include <limits.h>
#include <math.h>

#include "netpbm/pm_c_util.h"
#include "netpbm/mallocvar.h"
#include "netpbm/nstring.h"
#include "netpbm/colorname.h"

#include "netpbm/pam.h"
#include "netpbm/ppm.h"



static unsigned int
hexDigitValue(char const digit) {

    switch (digit) {
    case '0': return 0;
    case '1': return 1;
    case '2': return 2;
    case '3': return 3;
    case '4': return 4;
    case '5': return 5;
    case '6': return 6;
    case '7': return 7;
    case '8': return 8;
    case '9': return 9;
    case 'a': case 'A': return 10;
    case 'b': case 'B': return 11;
    case 'c': case 'C': return 12;
    case 'd': case 'D': return 13;
    case 'e': case 'E': return 14;
    case 'f': case 'F': return 15;
    default:
        pm_error("Invalid hex digit '%c'", digit);
        return 0;  /* Defeat compiler warning */
    }
}



static void
parseHexDigits(const char *   const string,
               char           const delim,
               samplen *      const nP,
               unsigned int * const digitCtP) {

    unsigned int digitCt;
    unsigned long n;
    unsigned long range;
        /* 16 for one hex digit, 256 for two hex digits, etc. */

    for (digitCt = 0, n = 0, range = 1; string[digitCt] != delim; ) {
        char const digit = string[digitCt];
        if (digit == '\0')
            pm_error("rgb: color spec '%s' ends prematurely", string);
        else {
            n = n * 16 + hexDigitValue(digit);
            range *= 16;
            ++digitCt;
        }
    }
    if (range <= 1)
        pm_error("No digits where hexadecimal number expected in rgb: "
                 "color spec '%s'", string);

    *nP = (samplen) n / (range-1);
    *digitCtP = digitCt;
}



static void
parseNewHexX11(char   const colorname[],
               tuplen const color) {
/*----------------------------------------------------------------------------
   Determine what color colorname[] specifies in the new style hex
   color specification format (e.g. rgb:55/40/55).

   Return that color as *colorP.

   Assume colorname[] starts with "rgb:", but otherwise it might be
   gibberish.
-----------------------------------------------------------------------------*/
    const char * cp;
    unsigned int digitCt;

    cp = &colorname[4];

    parseHexDigits(cp, '/', &color[PAM_RED_PLANE], &digitCt);

    cp += digitCt;
    ++cp;  /* Skip the slash */

    parseHexDigits(cp, '/', &color[PAM_GRN_PLANE], &digitCt);

    cp += digitCt;
    ++cp;  /* Skip the slash */

    parseHexDigits(cp, '\0', &color[PAM_BLU_PLANE], &digitCt);
}



static bool
isNormal(samplen const arg) {

    return arg >= 0.0 && arg <= 1.0;
}



static void
parseNewDecX11(const char * const colorname,
               tuplen       const color) {

    int rc;

    rc = sscanf(colorname, "rgbi:%f/%f/%f",
                &color[PAM_RED_PLANE],
                &color[PAM_GRN_PLANE],
                &color[PAM_BLU_PLANE]);

    if (rc != 3)
        pm_error("invalid color specifier '%s'", colorname);

    if (!(isNormal(color[PAM_RED_PLANE]) &&
          isNormal(color[PAM_GRN_PLANE]) &&
          isNormal(color[PAM_BLU_PLANE]))) {
        pm_error("invalid color specifier '%s' - "
                 "values must be between 0.0 and 1.0", colorname);
    }
}



static void
parseInteger(const char * const colorname,
             tuplen       const color) {

    unsigned int maxval;
    unsigned int r, g, b;
    int rc;

    rc = sscanf(colorname, "rgb-%u:%u/%u/%u", &maxval, &r, &g, &b);

    if (rc != 4)
        pm_error("invalid color specifier '%s'.  "
                 "If it starts with \"rgb-\", then it must have the format "
                 "rgb-<MAXVAL>:<RED>:<GRN>:<BLU>, "
                 "where <MAXVAL>, <RED>, <GRN>, and <BLU> are "
                 "unsigned integers",
                 colorname);

    if (maxval < 1 || maxval > PNM_OVERALLMAXVAL)
        pm_error("Maxval in color specification '%s' is %u, "
                 "which is invalid because it is not between "
                 "1 and %u, inclusive",
                 colorname, maxval, PNM_OVERALLMAXVAL);

    if (r > maxval)
        pm_error("Red value in color specification '%s' is %u, "
                 "whcih is invalid because the specified maxval is %u",
                 colorname, r, maxval);
    if (g > maxval)
        pm_error("Green value in color specification '%s' is %u, "
                 "whcih is invalid because the specified maxval is %u",
                 colorname, g, maxval);
    if (b > maxval)
        pm_error("Blue value in color specification '%s' is %u, "
                 "whcih is invalid because the specified maxval is %u",
                 colorname, b, maxval);

    color[PAM_RED_PLANE] = (float)r/maxval;
    color[PAM_GRN_PLANE] = (float)g/maxval;
    color[PAM_BLU_PLANE] = (float)b/maxval;
}



static void
parseOldX11(const char * const colorname,
            tuplen       const color) {
/*----------------------------------------------------------------------------
   Return as *colorP the color specified by the old X11 style color
   specififier colorname[] (e.g. #554055).
-----------------------------------------------------------------------------*/
    if (!pm_strishex(&colorname[1]))
        pm_error("Non-hexadecimal characters in #-type color specification");

    switch (strlen(colorname) - 1 /* (Number of hex digits) */) {
    case 3:
        color[PAM_RED_PLANE] = (samplen)hexDigitValue(colorname[1])/15;
        color[PAM_GRN_PLANE] = (samplen)hexDigitValue(colorname[2])/15;
        color[PAM_BLU_PLANE] = (samplen)hexDigitValue(colorname[3])/15;
        break;

    case 6:
        color[PAM_RED_PLANE] =
            ((samplen)(hexDigitValue(colorname[1]) << 4) +
             (samplen)(hexDigitValue(colorname[2]) << 0))
             / 255;
        color[PAM_GRN_PLANE] =
            ((samplen)(hexDigitValue(colorname[3]) << 4) +
             (samplen)(hexDigitValue(colorname[4]) << 0))
             / 255;
        color[PAM_BLU_PLANE] =
            ((samplen)(hexDigitValue(colorname[5]) << 4) +
             (samplen)(hexDigitValue(colorname[6]) << 0))
             / 255;
        break;

    case 9:
        color[PAM_RED_PLANE] =
            ((samplen)(hexDigitValue(colorname[1]) << 8) +
             (samplen)(hexDigitValue(colorname[2]) << 4) +
             (samplen)(hexDigitValue(colorname[3]) << 0))
            / 4095;
        color[PAM_GRN_PLANE] =
            ((samplen)(hexDigitValue(colorname[4]) << 8) +
             (samplen)(hexDigitValue(colorname[5]) << 4) +
             (samplen)(hexDigitValue(colorname[6]) << 0))
            / 4095;
        color[PAM_BLU_PLANE] =
            ((samplen)(hexDigitValue(colorname[7]) << 8) +
             (samplen)(hexDigitValue(colorname[8]) << 4) +
             (samplen)(hexDigitValue(colorname[9]) << 0))
            / 4095;
        break;

    case 12:
        color[PAM_RED_PLANE] =
            ((samplen)(hexDigitValue(colorname[1]) << 12) +
             (samplen)(hexDigitValue(colorname[2]) <<  8) +
             (samplen)(hexDigitValue(colorname[3]) <<  4) +
             (samplen)(hexDigitValue(colorname[4]) <<  0))
            / 65535;
        color[PAM_GRN_PLANE] =
            ((samplen)(hexDigitValue(colorname[5]) << 12) +
             (samplen)(hexDigitValue(colorname[6]) <<  8) +
             (samplen)(hexDigitValue(colorname[7]) <<  4) +
             (samplen)(hexDigitValue(colorname[8]) <<  0))
            / 65535;
        color[PAM_BLU_PLANE] =
            ((samplen)(hexDigitValue(colorname[ 9]) << 12) +
             (samplen)(hexDigitValue(colorname[10]) << 8) +
             (samplen)(hexDigitValue(colorname[11]) << 4) +
             (samplen)(hexDigitValue(colorname[12]) << 0))
            / 65535;
        break;

    default:
        pm_error("invalid color specifier '%s'", colorname);
    }
}



static void
parseOldX11Dec(const char* const colorname,
               tuplen      const color) {

    int rc;

    rc = sscanf(colorname, "%f,%f,%f",
                &color[PAM_RED_PLANE],
                &color[PAM_GRN_PLANE],
                &color[PAM_BLU_PLANE]);

    if (rc != 3)
        pm_error("invalid color specifier '%s'", colorname);

    if (!(isNormal(color[PAM_RED_PLANE]) &&
          isNormal(color[PAM_GRN_PLANE]) &&
          isNormal(color[PAM_BLU_PLANE]))) {
        pm_error("invalid color specifier '%s' - "
                 "values must be between 0.0 and 1.0", colorname);
    }
}



tuplen
pnm_parsecolorn(const char * const colorname) {

    tuplen retval;

    MALLOCARRAY_NOFAIL(retval, 3);

    if (strneq(colorname, "rgb:", 4))
        /* It's a new-X11-style hexadecimal rgb specifier. */
        parseNewHexX11(colorname, retval);
    else if (strneq(colorname, "rgbi:", 5))
        /* It's a new-X11-style decimal/float rgb specifier. */
        parseNewDecX11(colorname, retval);
    else if (strneq(colorname, "rgb-", 4))
        /* It's a Netpbm-native decimal integer rgb specifier */
        parseInteger(colorname, retval);
    else if (colorname[0] == '#')
        /* It's an old-X11-style hexadecimal rgb specifier. */
        parseOldX11(colorname, retval);
    else if ((colorname[0] >= '0' && colorname[0] <= '9') ||
             colorname[0] == '.')
        /* It's an old-style decimal/float rgb specifier. */
        parseOldX11Dec(colorname, retval);
    else
        /* Must be a name from the X-style rgb file. */
        pm_parse_dictionary_namen(colorname, retval);

    return retval;
}



static void
warnIfNotExact(const char * const colorname,
               tuple        const rounded,
               tuplen       const exact,
               sample       const maxval,
               unsigned int const plane) {

    float const epsilon = 1.0/65536.0;

    if (fabs((float)(rounded[plane] / maxval) - exact[plane]) > epsilon) {
        pm_message("WARNING: Component %u of color '%s' is %f, "
                   "which cannot be represented precisely with maxval %lu.  "
                   "Approximating as %lu.",
                   plane, colorname, exact[plane], maxval, rounded[plane]);
    }
}



tuple
pnm_parsecolor2(const char * const colorname,
                sample       const maxval,
                int          const closeOk) {

    tuple retval;
    tuplen color;
    struct pam pam;

    pam.len = PAM_STRUCT_SIZE(bytes_per_sample);
    pam.depth = 3;
    pam.maxval = maxval;
    pam.bytes_per_sample = pnm_bytespersample(maxval);

    retval = pnm_allocpamtuple(&pam);

    color = pnm_parsecolorn(colorname);

    retval[PAM_RED_PLANE] = ROUNDU(color[PAM_RED_PLANE] * maxval);
    retval[PAM_GRN_PLANE] = ROUNDU(color[PAM_GRN_PLANE] * maxval);
    retval[PAM_BLU_PLANE] = ROUNDU(color[PAM_BLU_PLANE] * maxval);

    if (!closeOk) {
        warnIfNotExact(colorname, retval, color, maxval, PAM_RED_PLANE);
        warnIfNotExact(colorname, retval, color, maxval, PAM_GRN_PLANE);
        warnIfNotExact(colorname, retval, color, maxval, PAM_BLU_PLANE);
    }

    free(color);

    return retval;
}



tuple
pnm_parsecolor(const char * const colorname,
               sample       const maxval) {

    return pnm_parsecolor2(colorname, maxval, true);
}



const char *
pnm_colorname(struct pam * const pamP,
              tuple        const color,
              int          const hexok) {

    const char * retval;
    pixel colorp;
    char * colorname;

    if (pamP->depth < 3)
        PPM_ASSIGN(colorp, color[0], color[0], color[0]);
    else
        PPM_ASSIGN(colorp,
                   color[PAM_RED_PLANE],
                   color[PAM_GRN_PLANE],
                   color[PAM_BLU_PLANE]);

    colorname = ppm_colorname(&colorp, pamP->maxval, hexok);

    retval = strdup(colorname);
    if (retval == NULL)
        pm_error("Couldn't get memory for color name string");

    return retval;
}



double pnm_lumin_factor[3] = {PPM_LUMINR, PPM_LUMING, PPM_LUMINB};

void
pnm_YCbCrtuple(tuple    const tuple,
               double * const YP,
               double * const CbP,
               double * const CrP) {
/*----------------------------------------------------------------------------
   Assuming that the tuple 'tuple' is of tupletype RGB, return the
   Y/Cb/Cr representation of the color represented by the tuple.
-----------------------------------------------------------------------------*/
    int const red = (int)tuple[PAM_RED_PLANE];
    int const grn = (int)tuple[PAM_GRN_PLANE];
    int const blu = (int)tuple[PAM_BLU_PLANE];

    *YP  = (+ PPM_LUMINR * red + PPM_LUMING * grn + PPM_LUMINB * blu);
    *CbP = (- 0.16874 * red - 0.33126 * grn + 0.50000 * blu);
    *CrP = (+ 0.50000 * red - 0.41869 * grn - 0.08131 * blu);
}



void
pnm_YCbCr_to_rgbtuple(const struct pam * const pamP,
                      tuple              const tuple,
                      double             const Y,
                      double             const Cb,
                      double             const Cr,
                      int *              const overflowP) {

    double rgb[3];
    unsigned int plane;
    bool overflow;

    rgb[PAM_RED_PLANE] = Y + 1.4022 * Cr + .5;
    rgb[PAM_GRN_PLANE] = Y - 0.7145 * Cr - 0.3456 * Cb + .5;
    rgb[PAM_BLU_PLANE] = Y + 1.7710 * Cb + .5;

    overflow = FALSE;  /* initial assumption */

    for (plane = 0; plane < 3; ++plane) {
        if (rgb[plane] > pamP->maxval) {
            overflow = TRUE;
            tuple[plane] = pamP->maxval;
        } else if (rgb[plane] < 0.0) {
            overflow = TRUE;
            tuple[plane] = 0;
        } else
            tuple[plane] = (sample)rgb[plane];
    }
    if (overflowP)
        *overflowP = overflow;
}



