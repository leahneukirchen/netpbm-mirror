/* macptopbm.c - read a MacPaint file and produce a portable bitmap
**
** Copyright (C) 1988 by Jef Poskanzer.
** Some code of ReadMacPaintFile() is based on the work of
** Patrick J. Naughton.  (C) 1987, All Rights Reserved.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.


** Apr 2015 afu
** Changed code style (ANSI-style function definitions, etc.)
** Added automatic detection of MacBinary header.
** Added diagnostics for corruptions.
** Replaced byte-wise operations with bit-wise ones.
*/

#include "pbm.h"
#include "pm_c_util.h"
#include "macp.h"



static bool
validateMacPaintVersion( const unsigned char * const rBuff,
                         const int offset ) {
/*---------------------------------------------------------------------------
  Macpaint (or PNTG) files have two headers.
  The 512 byte MacPaint header is mandatory.
  The newer 128 byte MacBinary header is optional.  If it exists, it comes
  before the MacPaint header.

  Here we examine the first four bytes of the MacPaint header to get
  the version number.

  Valid version numbers are 0, 2, 3.
  We also allow 1.
-----------------------------------------------------------------------------*/

    bool retval;
    const unsigned char * const vNum = rBuff + offset;

   if ( ( ( vNum[0] | vNum[1] | vNum[2] ) != 0x00 ) || vNum[3] > 3 )
        retval = FALSE;
    else
        retval = TRUE;

    pm_message("MacPaint version (at offset %u): %02x %02x %02x %02x (%s)",
               offset, vNum[0], vNum[1], vNum[2], vNum[3],
               retval == TRUE ? "valid" : "not valid" );

    return( retval );
}



static bool
scanMacBinaryHeader( const unsigned char * rBuff ) {
/*----------------------------------------------------------------------------
  We check byte 0 and 1, and then the MacPaint header version assuming it
  starts at offset 128.

  Byte 0: must be 0x00.
  Byte 1: (filename length) must be 1-63.

  Other fields that may be of interest:

  Bytes 2 through 63: (Internal Filename)
    See Apple Charmap for valid characters.
    Unlike US-Ascii, 8-bit characters (range 0x80 - 0xFF) are valid.
    0x00-0x1F and 0x7F are control characters.  0x00 appears in some files.
    Colon ':' (0x3a) should be avoided in Mac environments but in practice
    does appear.

  Bytes 65 through 68: (File Type)
    Four Ascii characters.  Should be "PNTG".

  Bytes 82 to 85: (SizeOfDataFork)
    uint32 value.  It seems this is file size (in bytes) / 256 + N, N <= 4.

  Bytes 100 through 124:
    Should be all zero if the header is MacBinary I.
    Defined and used in MacBinary II.

  Bytes 124,125: CRC
    (MacBinary II only) CRC value of bytes 0 through 123.

  All multi-byte values are big-endian.

  Reference:
  http://www.fileformat.info/format/macpaint/egff.htm
  Fully describes the fields.  However, the detection method described
  does not work very well.

  Also see:
  http://fileformats.archiveteam.org/wiki/MacPaint
-----------------------------------------------------------------------------*/
    bool          foundMacBinaryHeader;

    /* Examine byte 0.  It should be 0x00.  Note that the first
       byte of a valid MacPaint header should also be 0x00.
    */
    if ( rBuff[0] != 0x00 ) {
        foundMacBinaryHeader = FALSE;
    }

    /* Examine byte 1, the length of the filename.
       It should be in the range 1 - 63.
    */
    else if( rBuff[1] == 0 || rBuff[1] > 63 ) {
        foundMacBinaryHeader = FALSE;
    }

    /* Check the MacPaint header version starting at offset 128. */
    else if ( validateMacPaintVersion ( rBuff, MACBIN_HEAD_LEN ) == FALSE) {
        foundMacBinaryHeader = FALSE;
    }
    else
        foundMacBinaryHeader = TRUE;

    if( foundMacBinaryHeader == TRUE)
      pm_message("Input file contains a MacBinary header "
                   "followed by a MacPaint header.");
    else
      pm_message("Input file does not start with a MacBinary header.");

    return ( foundMacBinaryHeader );
}




static void
skipHeader( FILE * const ifP ) {
/*--------------------------------------------------------------------------
  Determine whether the MacBinary header exists.
  If it does, read off the initial 640 (=128 + 512) bytes of the file.
  If it doesn't, read off 512 bytes.

  In the latter case we check the MacHeader version number, but just issue
  a warning if the value is invalid.  This is for backward comaptibility.
---------------------------------------------------------------------------*/
    unsigned int re;
    const unsigned int buffsize = MAX( MACBIN_HEAD_LEN, MACP_HEAD_LEN );
    unsigned char * const rBuff = malloc(buffsize);

    if( rBuff == NULL )
        pm_error("Out of memory.");

    /* Read 512 bytes.
       See if MacBinary header exists in the first 128 bytes and
       the next 4 bytes signal the start of a MacPaint header. */
    re = fread ( rBuff, MACP_HEAD_LEN, 1, ifP);
        if (re < 1)
        pm_error("EOF/error while reading header.");

    if ( scanMacBinaryHeader( rBuff ) == TRUE ) {
    /* MacBinary header found.  Read another 128 bytes to complete the
       MacPaint header, but don't conduct any further analysis. */
        re = fread ( rBuff, MACBIN_HEAD_LEN, 1, ifP);
            if (re < 1)
            pm_error("EOF/error while reading MacPaint header.");

    } else {
    /* MacBinary header not found.  We assume file starts with
       MacPaint header.   Check MacPaint version but dismiss error. */
        if (validateMacPaintVersion( rBuff, 0 ) == TRUE)
          pm_message("Input file starts with valid MacPaint header.");
        else
          pm_message("  - Ignoring invalid version number.");
    }
    free( rBuff );
}



static void
skipExtraBytes( FILE * const ifP,
                int    const extraskip) {
/*--------------------------------------------------------------------------
  This function exists for backward compatibility.  Its purpose is to
  manually delete the MacBinary header.

  We check the MacHeader version number, but just issue a warning if the
  value is invalid.
---------------------------------------------------------------------------*/
    unsigned int re;
    unsigned char * const rBuff = malloc(MAX (extraskip, MACP_HEAD_LEN));

    if( rBuff == NULL )
        pm_error("Out of memory.");

    re = fread ( rBuff, 1, extraskip, ifP);
        if (re < extraskip)
        pm_error("EOF/error while reading off initial %u bytes"
                     "specified by -extraskip.", extraskip);
    re = fread ( rBuff, MACP_HEAD_LEN, 1, ifP);
        if (re < 1)
        pm_error("EOF/error while reading MacPaint header.");

    /* Check the MacPaint version number.  Dismiss error. */
    if (validateMacPaintVersion( rBuff, 0 ) == TRUE)
        pm_message("Input file starts with valid MacPaint header.");
    else
        pm_message("  - Ignoring invalid version number.");

    free( rBuff );
}



static unsigned char
readChar( FILE * const ifP ) {

    int const ch = getc( ifP );

    if (ch ==EOF)
        pm_error("EOF encountered while unpacking image data.");

    /* else */
        return ((unsigned char) ch);
}




static void
ReadMacPaintFile( FILE *  const ifP,
                  int  * outOfSyncP,
                  int  * pixelCntP ) {
/*---------------------------------------------------------------------------
  Unpack image data.  Compression method is called "Packbits".
  This run-length encoding scheme has also been adopted by
  Postscript and TIFF.  See source: converter/other/pnmtops.c

  Unpacked raster array is raw PBM.  No conversion is required.

  One source says flag byte should not be 0xFF (255), but we don't reject
  the value, for in practice, it is widely used.

  Sequences should never cross row borders.
  Violations of this rule are recorded in outOfSync.

  Note that pixelCnt counts bytes, not bits, so it is the number of pixels
  multiplied by 8.  This counter exists to detect corruptions.
---------------------------------------------------------------------------*/
    int           pixelCnt   = 0;   /* Initial value */
    int           outOfSync  = 0;   /* Initial value */
    unsigned int  flag;             /* Read from input */
    unsigned int  i;
    unsigned char * const bitrow = pbm_allocrow_packed(MACP_COLS);

    while ( pixelCnt < MACP_BYTES ) {
        flag = (unsigned int) readChar( ifP );    /* Flag (count) byte */
        if ( flag < 0x80 ) {
            /* Unpack next (flag + 1) chars as is */
            for ( i = 0; i <= flag; i++ )
                if( pixelCnt < MACP_BYTES) {
                  int const colChar = pixelCnt % MACP_COLCHARS;
                  pixelCnt++;
                  bitrow[colChar] = readChar( ifP );
                  if (colChar == MACP_COLCHARS-1)
                      pbm_writepbmrow_packed( stdout, bitrow, MACP_COLS, 0 );
                  if (colChar == 0 && i > 0 )
                      outOfSync++;
                }
        }
        else {
          /* Repeat next char (2's complement of flagCnt) times */
            unsigned int  const flagCnt = 256 - flag;
            unsigned char const ch = readChar( ifP );
            for ( i = 0; i <= flagCnt; i++ )
                if( pixelCnt < MACP_BYTES) {
                  int const colChar = pixelCnt % MACP_COLCHARS;
                  pixelCnt++;
                  bitrow[colChar] = ch;
                  if (colChar == MACP_COLCHARS-1)
                      pbm_writepbmrow_packed( stdout, bitrow, MACP_COLS, 0 );
                  if (colChar == 0 && i > 0 )
                      outOfSync++;
                }
        }
    }
    pbm_freerow_packed ( bitrow );
    *outOfSyncP  = outOfSync;
    *pixelCntP   = pixelCnt;
}


int
main( int argc, char * argv[])  {

    FILE * ifp;
    int argn, extraskip;
    const char * const usage = "[-extraskip N] [macpfile]";
    int outOfSync;
    int pixelCnt;

    pbm_init( &argc, argv );

    argn = 1;      /* initial value */
    extraskip = 0; /* initial value */

    /* Check for flags. */
    if ( argn < argc && argv[argn][0] == '-' && argv[argn][1] != '\0' ) {
        if ( pm_keymatch( argv[argn], "-extraskip", 2 ) ) {
            argn++;
            if ( argn == argc || sscanf( argv[argn], "%d", &extraskip ) != 1 )
                pm_usage( usage );
        }
        else
            pm_usage( usage );
        argn++;
    }

    if ( argn < argc ) {
        ifp = pm_openr( argv[argn] );
        argn++;
        }
    else
        ifp = stdin;

    if ( argn != argc )
        pm_usage( usage );

    if ( extraskip > 256 * 1024 )
        pm_error("-extraskip value too large");
    else if ( extraskip > 0 )
        skipExtraBytes( ifp, extraskip);
    else
        skipHeader( ifp );

    pbm_writepbminit( stdout, MACP_COLS, MACP_ROWS, 0 );

    ReadMacPaintFile( ifp, &outOfSync, &pixelCnt );
    /* We may not be at EOF.
       Macpaint files often have extra bytes after image data. */
    pm_close( ifp );

    if ( pixelCnt == 0 )
        pm_error("No image data.");

    else if ( pixelCnt < MACP_BYTES )
        pm_error("Compressed image data terminated prematurely.");

    else if ( outOfSync > 0 )
        pm_message("Warning: Corrupt image data.  %d rows misaligned.",
                   outOfSync);

    pm_close( stdout );
    exit( 0 );
}
