/* pbmfont.h - header file for font routines in libpbm
*/

#include "pbm.h"

#ifdef __cplusplus
extern "C" {
#endif
#if 0
} /* to fake out automatic code indenters */
#endif


/* Maximum dimensions for fonts */

#define  pbm_maxfontwidth()  65535
#define  pbm_maxfontheight() 65535
    /* These limits are not in the official Adobe BDF definition, but
       should never be a problem for practical purposes, considering that
       a 65536 x 65536 glyph occupies 4G pixels.

       Note that the maximum line length allowed in a BDF file imposes
       another restriction.
    */

typedef wchar_t PM_WCHAR;
    /* Precaution to make adjustments, if necessary, for systems with
       unique wchar_t.
    */

#define PM_FONT_MAXGLYPH 255

#define PM_FONT2_MAXGLYPH 65535
    /* Upper limit of codepoint value.

       This is large enough to handle Unicode Plane 0 (Basic Multilingual
       Plane: BMP) which defines the great majority of characters used in
       modern languages.

       This can be set to a higher value at some cost to efficiency.
       As of Unicode v. 11.0.0 planes up to 16 are defined.
    */

enum pbmFontLoad { FIXED_DATA           = 0,
                   LOAD_PBMSHEET        = 1,
                   LOAD_BDFFILE         = 2,
                   CONVERTED_TYPE1_FONT = 9 };

static const char * const pbmFontOrigin[10] =
                 {"Fixed data",                                   /* 0 */
                  "Loaded from PBM sheet by libnetpbm",           /* 1 */
                  "Loaded from BDF file by libnetpbm",            /* 2 */
                  NULL, NULL, NULL, NULL, NULL, NULL,
                  "Expanded from type 1 font structure by libnetpbm"}; /* 9 */

enum pbmFontEncoding { ENCODING_UNKNOWN = 0,
                       ISO646_1991_IRV = 1,   /* ASCII */
                       ISO_8859_1 = 1000, ISO_8859_2, ISO_8859_3, ISO_8859_4,
                       ISO_8859_5,   ISO_8859_6,   ISO_8859_7,   ISO_8859_8,
                       ISO_8859_9,   ISO_8859_10,  ISO_8859_11,  ISO_8859_12,
                       ISO_8859_13,  ISO_8859_14,  ISO_8859_15,  ISO_8859_16,
                       ISO_10646 = 2000 };

/* For future use */

/* In addition to the above, the following CHARSET_REGISTRY-CHARSET_ENCODING
   values have been observed in actual BDF files:

  ADOBE-FONTSPECIFIC, DEC-DECTECH, GOST19768.74-1, IS13194-DEVANAGARI,
  JISX0201.1976-0, KOI8-C, KOI8-R, MISC-FONTSPECIFIC,
  MULEARABIC-0, MULEARABIC-1, MULEARABIC-2, MULEIPA-1, MULELAO-1,
  OMRON_UDC_ZH-0, TIS620.2529-0, TIS620.2529-1, VISCII1-1, VISCII1.1-1,
  XTIS-0
 */


struct glyph {
    /* A glyph consists of white borders and the "central glyph" which
       can be anything, but normally does not have white borders because
       it's more efficient to represent white borders explicitly.
    */
    unsigned int width;
    unsigned int height;
        /* The dimensions of the central glyph, i.e. the 'bmap' array */
    int x;
        /* Width in pixels of the white left border of this glyph.
           This can actually be negative to indicate that the central
           glyph backs up over the previous character in the line.  In
           that case, if there is no previous character in the line, it
           is as if 'x' is 0.
        */
    int y;
        /* Height in pixels of the white bottom border of this glyph.
           Can be negative.
        */
    unsigned int xadd;
        /* Width of glyph -- white left border plus central glyph
           plus white right border
        */
    const char * bmap;
        /* The raster of the central glyph.  It is an 'width' by
           'height' array in row major order, with each byte being 1
           for black; 0 for white.  E.g. if 'width' is 20 pixels and
           'height' is 40 pixels and it's a rectangle that is black on
           the top half and white on the bottom, this is an array of
           800 bytes, with the first 400 having value 0x01 and the
           last 400 having value 0x00.

           Do not share bmap objects among glyphs if using
           pbm_destroybdffont() or pbm_destroybdffont2() to free
           the font/font2 structure.
        */
};


struct font {
    /* This describes a combination of font and character set.  Given
       an code point in the range 0..255, this structure describes the
       glyph for that character.
    */
    unsigned int maxwidth, maxheight;
    int x;
        /* The minimum value of glyph.font.  The left edge of the glyph
           in the glyph set which advances furthest to the left. */
    int y;
        /* Amount of white space that should be added between lines of
           this font.  Can be negative.
        */
    struct glyph * glyph[256];
        /* glyph[i] is the glyph for code point i.
           Glyph objects must be unique for pbm_destroybdffont() to work.
        */
    const bit ** oldfont;
        /* for compatibility with old pbmtext routines */
        /* oldfont is NULL if the font is BDF derived */
    int fcols, frows;
};


struct font2 {
    /* Font structure for expanded character set.
       Code points in the range 0...maxmaxglyph are loaded.
       Loaded code point is in the range 0..maxglyph .
     */

    /* 'size' and 'len' are necessary in order to provide forward and
       backward compatibility between library functions and calling programs
       as this structure grows.  See struct pam in pam.h.
     */
    unsigned int size;
        /* The storage size of this entire structure, in bytes */

    unsigned int len;
        /* The length, in bytes, of the information in this structure.
           The information starts in the first byte and is contiguous.
           This cannot be greater than 'size'
        */

    int maxwidth, maxheight;

    int x;
         /* The minimum value of glyph.font.  The left edge of the glyph in
            the glyph set which advances furthest to the left.
         */
    int y;
        /* Amount of white space that should be added between lines of this
           font.  Can be negative.
        */

    struct glyph ** glyph;
        /* glyph[i] is the glyph for code point i

           Glyph objects must be unique for pbm_destroybdffont2() to work.
           For example space and non-break-space are often identical at the
           image data level; they must be loaded into separate memory
           locations if using pbm_destroybdffont2().
         */

    PM_WCHAR maxglyph;
        /* max code point for glyphs, including vacant slots max value of
           above i
        */

    void * selector;
        /* Reserved

           Bit array or structure indicating which code points to load.

           When NULL, all available code points up to maxmaxglyph, inclusive
           are loaded.
       */

    PM_WCHAR maxmaxglyph;
        /* Code points above this value are not loaded, even if they occur
           in the BDF font file
        */

    const bit ** oldfont;
        /* For compatibility with old pbmtext routines.
           Valid only when data is in the form of a PBM sheet
        */

    unsigned int fcols, frows;
        /* For compatibility with old pbmtext routines.
           Valid only when oldfont is non-NULL
        */

    unsigned int bit_format;
        /* PBM_FORMAT:   glyph data: 1 byte per pixel (like P1, but not ASCII)
           RPBM_FORMAT:  glyph data: 1 bit per pixel
           Currently only PBM_FORMAT is possible
        */

    unsigned int total_chars;
        /* Number of glyphs defined in font file, as stated in the CHARS line
           of the BDF file PBM sheet font.  Always 96
        */

    unsigned int chars;
        /* Number of glyphs actually loaded into structure

           Maximum: total_chars

           Less than total_chars when a subset of the file is loaded
           PBM sheet font: always 96 */

    enum pbmFontLoad load_fn;
        /* Description of the function that created the structure and loaded
           the glyph data

           Used to choose a string to show in verbose messages.

           FIXED_DATA (==0) means memory for this structure was not
           dynamically allocated by a function; all data is hardcoded in
           source code and resides in static data.  See file pbmfontdata1.c
        */

    PM_WCHAR default_char;
        /* Code index of what to show when there is no glyph for a requested
           code Available in many BDF fonts between STARPROPERTIES -
           ENDPROPERTIES.

           Set to value read from BDF font file.

           Common values are 0, 32, 8481, 32382, 33, 159, 255.
        */

    unsigned int default_char_defined;
        /* boolean
           TRUE: above field is valid; DEFAULT_CHAR is defined in font file.
           FALSE: font file has no DEFAULT_CHAR field.
        */

    char * name;
        /* Name of the font.  Available in BDF fonts.
           NULL means no name.
        */

    enum pbmFontEncoding charset;
        /* Reserved for future use.
           Set by analyzing following charset_string.
        */

    char * charset_string;
        /* Charset registry and encoding.
           Available in most BDF fonts between STARPROPERTIES - ENDPROPERTIES.
           NULL means no name.
        */
};


/* PBM_FONT2_STRUCT_SIZE(x) tells you how big a struct font2 is up
   through the member named x.  This is useful in conjunction with the
   'len' value to determine which fields are present in the structure.
*/

/* Some compilers are really vigilant and recognize it as an error
   to cast a 64 bit address to a 32 bit type.  Hence the roundabout
   casting.  See PAM_MEMBER_OFFSET in pam.h .
*/


#define PBM_FONT2_MEMBER_OFFSET(mbrname) \
  ((size_t)(unsigned long)(char*)&((struct font2 *)0)->mbrname)
#define PBM_FONT2_MEMBER_SIZE(mbrname) \
  sizeof(((struct font2 *)0)->mbrname)
#define PBM_FONT2_STRUCT_SIZE(mbrname) \
  (PBM_FONT2_MEMBER_OFFSET(mbrname) + PBM_FONT2_MEMBER_SIZE(mbrname))


struct font *
pbm_defaultfont(const char* const which);

struct font2 *
pbm_defaultfont2(const char* const which);

struct font *
pbm_dissectfont(const bit ** const font,
                unsigned int const frows,
                unsigned int const fcols);

struct font *
pbm_loadfont(const char * const filename);

struct font2 *
pbm_loadfont2(const    char * const filename,
              PM_WCHAR        const maxmaxglyph);

struct font *
pbm_loadpbmfont(const char * const filename);

struct font2 *
pbm_loadpbmfont2(const char * const filename);

struct font *
pbm_loadbdffont(const char * const filename);

struct font2 *
pbm_loadbdffont2(const char * const filename,
                 PM_WCHAR     const maxmaxglyph);

struct font2 *
pbm_loadbdffont2_select(const char * const filename,
                        PM_WCHAR     const maxmaxglyph,
                        const void * const selector);

void
pbm_createbdffont2_base(struct font2 ** const font2P,
                        PM_WCHAR        const maxmaxglyph);

void
pbm_destroybdffont(struct font * const fontP);

void
pbm_destroybdffont2_base(struct font2 * const font2P);

void
pbm_destroybdffont2(struct font2 * const font2P);

struct font2 *
pbm_expandbdffont(const struct font * const font);

void
pbm_dumpfont(struct font * const fontP,
             FILE *        const ofP);

#ifdef __cplusplus
}
#endif
