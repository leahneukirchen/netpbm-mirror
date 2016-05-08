/*\
 * $Id: bmp.h,v 1.3 1992/11/24 19:39:56 dws Exp dws $
 * 
 * bmp.h - routines to calculate sizes of parts of BMP files
 *
 * Some fields in BMP files contain offsets to other parts
 * of the file.  These routines allow us to calculate these
 * offsets, so that we can read and write BMP files without
 * the need to fseek().
 * 
 * Copyright (C) 1992 by David W. Sanderson.
 * 
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted,
 * provided that the above copyright notice appear in all copies and
 * that both that copyright notice and this permission notice appear
 * in supporting documentation.  This software is provided "as is"
 * without express or implied warranty.
 * 
 * $Log: bmp.h,v $
 * Revision 1.3  1992/11/24  19:39:56  dws
 * Added copyright.
 *
 * Revision 1.2  1992/11/17  02:13:37  dws
 * Adjusted a string's name.
 *
 * Revision 1.1  1992/11/16  19:54:44  dws
 * Initial revision
 *
\*/

/* 
   There is a specification of BMP (2003.07.24) at:

     http://msdn.microsoft.com/library/default.asp?url=/library/en-us/gdi/bitmaps_4v1h.asp

   There is a better written specification of the Windows BMP format
   in (2000.06.08) <http://www.daubnet.com/formats/BMP.html>.
   However, the "Windows BMP" format used in practice is much closer
   to the Microsoft definition.  The Microsoft spec was not known for
   Netpbm development until 2003.07.24.

   The ColorsImportant field is defined in the daubnet spec as "Number of
   important colors.  0 = all"  That is the entire definition.  The
   spec also says the number of entries in the color map is a function
   of the BitCount field alone.

   But Marc Moorcroft says (2000.07.23) that he found two BMP files
   some time ago that had a color map whose number of entries was not
   as specified and was in fact the value of ColorsImportant.

   And Bill Janssen actually produced some BMPs in January 2001 that
   appear to have the size of the colormap determined by ColorsUsed.
   They have 8 bits per pixel in the raster, but ColorsUsed is 4 and
   there are in fact 4 entries in the color map.  He got these from
   the Palm emulator for Windows, using the "Save Screen" menu 
   option.

   Bmptoppm had, for a few releases in 2000, code by Marc to use
   ColorsImportant as the color map size unless it was zero, in which
   case it determined color map size as specified.  The current
   thinking is that there are probably more BMPs that need to be
   interpreted per the spec than that need to be interpreted Marc's
   way.  

   But in light of Janssen's discovery, we have made the assumption
   since February 2001 that when ColorsUsed is zero, the colormap size
   is as specified, and when it is nonzero, the colormap size is given
   by ColorsUsed.

   But we were also assuming that if ColorsUsed is nonzero, the image
   is colormapped.  We heard from "Ron & Bes Vantreese"
   <eaglesun@aggienetwork.com> in February 2003 that his understanding
   of the format was that ColorsUsed == 2**24 is appropriate for a
   non-colormapped (24 bit) BMP, and images he created that way caused
   trouble for Bmptopnm.  So since then, we look at ColorsUsed only if
   we know because bits per pixel <= 8 that it is a colormapped image.

*/

#ifndef BMP_H_INCLUDED
#define BMP_H_INCLUDED

#include "pm.h"  /* For pm_error() */

enum bmpClass {C_WIN, C_OS2};

static __inline__ const char *
BMPClassName(enum bmpClass const class) {

    const char * name;

    switch (class) {
    case C_OS2: name = "OS/2 (v1)";    break;
    case C_WIN: name = "Windows (v1)"; break;
    }

  return name;
}



static char const er_internal[] = "%s: internal error!";

/* Values of the "compression" field of the BMP info header */
typedef enum BMPCompType {
    BMPCOMP_RGB       = 0,
    BMPCOMP_RLE8      = 1,
    BMPCOMP_RLE4      = 2,
    BMPCOMP_BITFIELDS = 3,
    BMPCOMP_JPEG      = 4,
    BMPCOMP_PNG       = 5
} BMPCompType;

static __inline__ const char *
BMPCompTypeName(BMPCompType const compression) {

    switch (compression) {
    case BMPCOMP_RGB:       return "none (RBG)";
    case BMPCOMP_RLE4:      return "4 bit run-length coding";
    case BMPCOMP_RLE8:      return "8 bit run-length coding";
    case BMPCOMP_BITFIELDS: return "none (bitfields)";
    case BMPCOMP_JPEG:      return "JPEG";
    case BMPCOMP_PNG:       return "PNG";   
    }
    return 0;  /* Default compiler warning */
}



static __inline__ unsigned int
BMPlenfileheader(void) {

    return 14;
}



enum BMPinfoHeaderLen {
/*----------------------------------------------------------------------------
   BMPs come in various kinds, distinguished by the length of their
   info header, which is the first field in that header.

   These are those lengths.
-----------------------------------------------------------------------------*/
    BMP_HDRLEN_OS2_1x =  12,
        /* BITMAPCOREHEADER; since Windows 2.0, OS/2 1.x */
    BMP_HDRLEN_OS2_2x =  64,
        /* not documented by Microsoft; since OS/2 2.x */
    BMP_HDRLEN_WIN_V1 =  40,
        /* BITMAPINFOHEADER; since Windows NT 3, Windows 3.x */
    BMP_HDRLEN_WIN_V2 =  52,
        /* not documented by Microsoft */
    BMP_HDRLEN_WIN_V3 =  56,
        /* not documented by Microsoft */
    BMP_HDRLEN_WIN_V4 = 108,
        /* BITMAPV4HEADER; since Windows NT 4, Windows 95 */
    BMP_HDRLEN_WIN_V5 = 124
        /* BITMAPV5HEADER; since Windows 2000, Windows 98 */
};



static __inline__ unsigned int
BMPleninfoheader(enum bmpClass const class) {

    unsigned int retval;

    switch (class) {
    case C_WIN: retval = BMP_HDRLEN_WIN_V1; break;
    case C_OS2: retval = BMP_HDRLEN_OS2_1x; break;
    }
    return retval;
}



static __inline__ unsigned int
BMPlencolormap(enum bmpClass const class,
               unsigned int  const bitcount, 
               unsigned int  const cmapsize) {
/*----------------------------------------------------------------------------
   The number of bytes of the BMP stream occupied by the colormap in a
   BMP of class 'class' with 'bitcount' bits per pixel and 'cmapsize'
   entries in the palette.

   'cmapsize' == 0 means there is no palette.
-----------------------------------------------------------------------------*/
    unsigned int lenrgb;
    unsigned int lencolormap;

    if (bitcount < 1)
        pm_error(er_internal, "BMPlencolormap");
    else if (bitcount > 8) 
        lencolormap = 0;
    else {
        switch (class) {
        case C_WIN: lenrgb = 4; break;
        case C_OS2: lenrgb = 3; break;
        }

        if (!cmapsize) 
            lencolormap = (1 << bitcount) * lenrgb;
        else 
            lencolormap = cmapsize * lenrgb;
    }
    return lencolormap;
}



static __inline__ unsigned int
BMPlenline(enum bmpClass const class,
           unsigned int  const bitcount, 
           unsigned int  const x) {
/*----------------------------------------------------------------------------
  length, in bytes, of a line of the image

  Each row is padded on the right as needed to make it a multiple of 4
  bytes int.  This appears to be true of both OS/2 and Windows BMP
  files.
-----------------------------------------------------------------------------*/
    unsigned int bitsperline;
    unsigned int retval;

    bitsperline = x * bitcount;  /* tentative */

    /*
     * if bitsperline is not a multiple of 32, then round
     * bitsperline up to the next multiple of 32.
     */
    if ((bitsperline % 32) != 0)
        bitsperline += (32 - (bitsperline % 32));

    if ((bitsperline % 32) != 0) {
        pm_error(er_internal, "BMPlenline");
        retval = 0;
    } else 
        /* number of bytes per line == bitsperline/8 */
        retval = bitsperline >> 3;

    return retval;
}



static __inline__ unsigned int
BMPlenbits(enum bmpClass const class,
           unsigned int  const bitcount, 
           unsigned int  const x,
           unsigned int  const y) {
/*----------------------------------------------------------------------------
  Return the number of bytes used to store the image bits
  for an uncompressed image.
-----------------------------------------------------------------------------*/
    return y * BMPlenline(class, bitcount, x);
}



static __inline__ unsigned int
BMPoffbits(enum bmpClass const class,
           unsigned int  const bitcount, 
           unsigned int  const cmapsize) {
/*----------------------------------------------------------------------------
  return the offset to the BMP image bits.
-----------------------------------------------------------------------------*/
    return BMPlenfileheader()
        + BMPleninfoheader(class)
        + BMPlencolormap(class, bitcount, cmapsize);
}


static __inline__ unsigned int
BMPlenfileGen(enum bmpClass const class,
              unsigned int  const bitcount, 
              unsigned int  const cmapsize,
              unsigned int  const x,
              unsigned int  const y,
              unsigned int  const imageSize,
              BMPCompType   const compression) {
/*----------------------------------------------------------------------------
  Return the size of the BMP file in bytes.
-----------------------------------------------------------------------------*/
    unsigned int retval;

    switch (compression) {
    case BMPCOMP_RGB:
    case BMPCOMP_BITFIELDS:
        retval =
            BMPoffbits(class, bitcount, cmapsize) +
            BMPlenbits(class, bitcount, x, y);
        break;
    default:
        retval = BMPoffbits(class, bitcount, cmapsize) + imageSize;
    }
    return retval;
}



static __inline__ unsigned int
BMPlenfile(enum bmpClass const class,
           unsigned int  const bitcount, 
           unsigned int  const cmapsize,
           unsigned int  const x,
           unsigned int  const y) {
/*----------------------------------------------------------------------------
  return the size of the BMP file in bytes; no compression
-----------------------------------------------------------------------------*/
    return BMPlenfileGen(class, bitcount, cmapsize, x, y, 0, BMPCOMP_RGB);
}

#endif
