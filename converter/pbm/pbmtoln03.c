/*
From tim@deakin.edu.au Fri May  7 00:18:57 1993
From: Tim Cook <tim@deakin.edu.au>
Date: Fri, 7 May 1993 15:18:34 -0500
X-Mailer: Mail User's Shell (7.2.4 2/2/92)
To: dyson@sunfish.Physics.UIowa.Edu
Subject: Re: DEC LN03+ printer (not postscript) under SunOS (on SS10) ?
Content-Length: 5893

In a message dated 6 May,  9:32, dyson@sunfish.Physics.UIowa.Edu
(Richard L. Dyson) wrote:
> > Just in case anyone is interested, I have a pbmtoln03 utility I wrote
> > when I was mucking about with an LN03+.  If you are interested in
> > printing your bitmaps at 300x300dpi, ask me for the source.
>
> I would be interested.  We still only have LN03+ printers on our VMS
> machines here...

Ok, here goes.  Note that you will need the source from the pbmplus
utilities, because my pbmtoln03 utility uses the library routines that
are a part of pbmplus to read a PBM file (I linked it with libpbm.a).
I have not tested this utility on VMS, but it looks like it should
work.
*/

/* pbmtoln03.c -        Converts a PBM bitmap to DEC LN03 SIXEL bitmap
 *
 * SYNOPSIS
 *      pbmtoln03 [pbm-file]
 *
 * OPTIONS
 *      -l nn   Use "nn" as value for left margin (default 0).
 *      -r nn   Use "nn" as value for right margin (default 2400).
 *      -t nn   Use "nn" as value for top margin (default 0).
 *      -b nn   Use "nn" as value for bottom margin (default 3400).
 *      -f nn   Use "nn" as value for form length (default 3400).
 *
 * Tim Cook, 26 Feb 1992
 * changed option parsing to PBM standards  - Ingo Wilken, 13 Oct 1993
 */

#include <stdio.h>

#include "mallocvar.h"
#include "pbm.h"



static void
outputSixelRecord(unsigned char * const record,
                  unsigned int    const width) {

    unsigned int i, j;
    unsigned char lastChar;
    int startRepeat;
    char repeatedStr[16];

    /* Do RLE */
    lastChar = record[0];
    j = 0;

    /* This will make the following loop complete */
    record[width] = '\0' ;

    for (i = 1, startRepeat = 0; i <= width; ++i) {
        unsigned int const repeated = i - startRepeat;

        if (record[i] != lastChar || repeated >= 32766) {

            /* Repeat has ended */

            if (repeated > 3) {
                /* Do an encoding */
                char * p;
                record[j++] = '!' ;
                sprintf(repeatedStr, "%u", i - startRepeat);
                for (p = repeatedStr; *p; ++p)
                    record[j++] = *p;
                record[j++] = lastChar;
            } else {
                unsigned int k;

                for (k = 0; k < repeated; ++k)
                    record[j++] = lastChar;
            }

            startRepeat = i ;
            lastChar = record[i];
        }
    }

    fwrite((char *) record, j, 1, stdout) ;
    putchar('-') ;      /* DECGNL (graphics new-line) */
    putchar('\n') ;
}



static void
convert(FILE *       const ifP,
        unsigned int const width,
        unsigned int const height,
        unsigned int const format) {

    unsigned char * sixel;  /* A row of sixels */
    unsigned int sixelRow;
    bit * bitrow;
    unsigned int remainingHeight;

    bitrow = pbm_allocrow(width);

    MALLOCARRAY(sixel, width + 2);
    if (!sixel)
        pm_error("Unable to allocation %u bytes for a row buffer", width + 2);

    for (remainingHeight = height, sixelRow = 0;
         remainingHeight > 0;
         --remainingHeight) {

        unsigned int i;

        pbm_readpbmrow(ifP, bitrow, width, format);

        switch (sixelRow) {
        case 0 :
            for (i = 0; i < width; ++i)
                sixel[i] = bitrow[i] ;
            break ;
        case 1 :
            for (i = 0; i < width; ++i)
                sixel[i] += bitrow[i] << 1;
            break ;
        case 2 :
            for (i = 0; i < width; ++i)
                sixel[i] += bitrow[i] << 2;
            break ;
        case 3 :
            for (i = 0; i < width; ++i)
                sixel[i] += bitrow[i] << 3;
            break ;
        case 4 :
            for (i = 0; i < width; ++i)
                sixel[i] += bitrow[i] << 4;
            break ;
        case 5 :
            for (i = 0; i < width; ++i)
                sixel[i] += (bitrow[i] << 5) + 077;
            outputSixelRecord(sixel, width);
            break ; }
        if (sixelRow == 5)
            sixelRow = 0;
        else
            ++sixelRow;
    }

    if (sixelRow > 0) {
        /* Incomplete sixel record needs to be output */
        unsigned int i;
        for (i = 0; i < width; ++i)
            sixel[i] += 077;
        outputSixelRecord(sixel, width);
    }

    pbm_freerow(bitrow);
    free(sixel);
}



int
main (int argc, char **argv) {
   int argc_copy = argc ;
   char **argv_copy = argv ;
   int argn;
   const char * const usage = "[-left <nn>] [-right <nn>] [-top <nn>] "
       "[-bottom <nn>] [-formlength <nn>] [pbmfile]";

   /* Options */
   /* These defaults are for a DEC LN03 with A4 paper (2400x3400 pixels) */
   const char *opt_left_margin = "0";
   const char *opt_top_margin = opt_left_margin;
   const char *opt_right_margin = "2400";
   const char *opt_bottom_margin = "3400";
   const char *opt_form_length = opt_bottom_margin;

   FILE * ifP;
   int width, height, format ;

   pbm_init (&argc_copy, argv_copy) ;

   argn = 1;
   while( argn < argc && argv[argn][0] == '-' && argv[argn][1] != '\0' ) {
      if( pm_keymatch(argv[argn], "-left", 2) ) {
         if( ++argn >= argc )
            pm_usage(usage);
         opt_left_margin = argv[argn];
      }
      else
      if( pm_keymatch(argv[argn], "-right", 2) ) {
         if( ++argn >= argc )
            pm_usage(usage);
         opt_right_margin = argv[argn];
      }
      else
      if( pm_keymatch(argv[argn], "-top", 2) ) {
         if( ++argn >= argc )
            pm_usage(usage);
         opt_top_margin = argv[argn];
      }
      else
      if( pm_keymatch(argv[argn], "-bottom", 2) ) {
         if( ++argn >= argc )
            pm_usage(usage);
         opt_bottom_margin = argv[argn];
      }
      else
      if( pm_keymatch(argv[argn], "-formlength", 2) ) {
         if( ++argn >= argc )
            pm_usage(usage);
         opt_form_length = argv[argn];
      }
      else
         pm_usage(usage);
      ++argn;
   }

   if( argn < argc ) {
      ifP = pm_openr( argv[argn] );
      argn++;
   }
   else
      ifP = stdin;

   if( argn != argc )
      pm_usage(usage);

   pbm_readpbminit (ifP, &width, &height, &format) ;

/*
 * In explanation of the sequence below:
 *      <ESC>[!p        DECSTR  soft terminal reset
 *      <ESC>[11h       PUM     select unit of measurement
 *      <ESC>[7 I       SSU     select pixel as size unit
 *      <ESC>[?52l      DECOPM  origin is corner of printable area
 *      <ESC>[%s;%ss    DECSLRM left and right margins
 *      <ESC>[%s;%sr    DECSTBM top and bottom margins
 *      <ESC>[%st       DECSLPP form length
 *      <ESC>P0;0;1q            select sixel graphics mode
 *      "1;1            DECGRA  aspect ratio (1:1)
 */

   /* Initialize sixel file */
   printf ("\033[!p\033[11h\033[7 I\033[?52l\033[%s;%ss\033"
           "[%s;%sr\033[%st\033P0;0;1q\"1;1",
      opt_left_margin, opt_right_margin, opt_top_margin, opt_bottom_margin,
      opt_form_length);

   /* Convert data */
   convert (ifP, width, height, format) ;

   /* Terminate sixel data */
   puts ("\033\\") ;

   /* If the program failed, it previously aborted with nonzero completion
      code, via various function calls.
   */
   return 0;
}


