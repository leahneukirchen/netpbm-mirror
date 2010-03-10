/* pbmclean.c - pixel cleaning. Remove pixel if less than n connected
 *              identical neighbours, n=1 default.
 * AJCD 20/9/90
 * stern, Fri Oct 19 00:10:38 MET DST 2001
 *     add '-white/-black' flags to restrict operation to given blobs
 */

#include <stdio.h>

#include "pm_c_util.h"
#include "pbm.h"
#include "shhopt.h"

struct cmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char *inputFilespec;  /* Filespecs of input files */
    bool flipWhite;
    bool flipBlack;
    unsigned int connect;
    unsigned int verbose;
};

static void
parseCommandLine(int argc, char ** argv,
                 struct cmdlineInfo *cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optStruct3 opt;  /* set by OPTENT3 */
    optEntry *option_def = malloc(100*sizeof(optEntry));
    unsigned int option_def_index;

    unsigned int black, white;
    unsigned int minneighborsSpec;

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "verbose", OPT_FLAG, NULL, &cmdlineP->verbose, 0);
    OPTENT3(0,   "black", OPT_FLAG, NULL, &black, 0);
    OPTENT3(0,   "white", OPT_FLAG, NULL, &white, 0);
    OPTENT3(0,   "minneighbors", OPT_UINT, &cmdlineP->connect, 
            &minneighborsSpec, 0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = TRUE;  /* We sort of allow negative numbers as parms */

    optParseOptions3(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    free(option_def);

    if (!black && !white) {
        cmdlineP->flipBlack = TRUE;
        cmdlineP->flipWhite = TRUE;
    } else {
        cmdlineP->flipBlack = !!black;
        cmdlineP->flipWhite = !!white;
    }    

    if (!minneighborsSpec) {
        /* Now we do a sleazy tour through the parameters to see if
           one is -N where N is a positive integer.  That's for
           backward compatibility, since Pbmclean used to have
           unconventional syntax where a -N option was used instead of
           the current -minneighbors option.  The only reason -N didn't
           get processed by pm_optParseOptions3() is that it looked
           like a negative number parameter instead of an option.  
           If we find a -N, we make like it was a -minneighbors=N option.
        */
        int i;
        bool foundNegative;

        cmdlineP->connect = 1;  /* default */
        foundNegative = FALSE;

        for (i = 1; i < argc; ++i) {
            if (foundNegative)
                argv[i-1] = argv[i];
            else {
                if (atoi(argv[i]) < 0) {
                    cmdlineP->connect = - atoi(argv[i]);
                    foundNegative = TRUE;
                }
            }
        }
        if (foundNegative)
            --argc;
    }

    if (argc-1 < 1) 
        cmdlineP->inputFilespec = "-";
    else if (argc-1 == 1)
        cmdlineP->inputFilespec = argv[1];
    else
        pm_error("You specified too many arguments (%d).  The only "
                 "argument is the optional input file specification.",
                 argc-1);
}


static unsigned int
bitpop8(unsigned char const x) {
/*----------------------------------------------------------------------------
   Return the number of 1 bits in 'x'
-----------------------------------------------------------------------------*/
static unsigned int const p[256] = {
    0, 1, 1, 2, 1, 2, 2, 3, 1, 2, 2, 3, 2, 3, 3, 4,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    1, 2, 2, 3, 2, 3, 3, 4, 2, 3, 3, 4, 3, 4, 4, 5,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    2, 3, 3, 4, 3, 4, 4, 5, 3, 4, 4, 5, 4, 5, 5, 6,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    3, 4, 4, 5, 4, 5, 5, 6, 4, 5, 5, 6, 5, 6, 6, 7,
    4, 5, 5, 6, 5, 6, 6, 7, 5, 6, 6, 7, 6, 7, 7, 8 };

    return p[x];
}

static unsigned int
bitpop24(uint32_t const w){
/*----------------------------------------------------------------------------
   Return the number of 1 bits in the lower 24 bits of 'w'
   A GCC builtin, __builtin_popcountl(), is available, but it
   emits a call to an external function instead of inlining (GCC 4.4.3).

   This table lookup method is faster.
-----------------------------------------------------------------------------*/
  return (   bitpop8((w>>16) & 0xff) 
       + bitpop8((w>>8)  & 0xff)
           + bitpop8(w & 0xff)      );  
}

/*----------------------------------------------------------------------------
Fast algorithm for counting friendly neighbor pixels

In this program both input and output rows are in raw (packed) PBM format.

We handle input rows in groups of three, named "prevrow", "thisrow",
"nextrow" and scan from left to right.  At every byte boundary, 10 bits
are read from each of the three rows and placed into a temporary storage
we call "sample".

prevrow: ... ... _______M NNNNNNNN O_______ ...
thisrow: ... ... _______W cCCCCCCC E_______ ...
nextrow: ... ... _______R SSSSSSSS T_______ ...

sample : xxMNNNNNNNNOWcCCCCCCCERSSSSSSST

We count bits by taking the logical and of "sample" and a bit-mask called
"selection", and feeding the result to a table-based bit-population counter.

For example, the bits around the leftmost bit of the byte ("c") are selected
like this:

sample :       xxMNNNNNNNNOWcCCCCCCCERSSSSSSST
selection: & | __111_______1_1_______111______

(In the actual process, "sample" is shifted right and anded against a
 constant "selection" mask.)

The above reports one bits.  For the zero (white) bits we replace "sample"
with its inverse.

If the friendly neighbor count is below a threshold (default 1), we record
that as a one bit in "flipmask".  Bits are flipped in units of eight
and written to outrow at the byte boundary.
-----------------------------------------------------------------------------*/

static unsigned int
likeNeighbors(uint32_t const blackSample, 
              unsigned int const offset) {

    bool const thispoint = ( blackSample >> (18-offset) ) & 0x01;
    uint32_t const sample = (thispoint == PBM_BLACK ) 
                          ?   blackSample
                          : ~ blackSample ;
    uint32_t const selection = 0x701407;

    return ( bitpop24( ( sample >> (7-offset) ) & selection  ) );
 
}

static uint32_t
setSample(const bit * const prevrow,
          const bit * const thisrow,
          const bit * const nextrow,
          unsigned int const col){

  uint32_t sample;
  int col8 = col/8;

  sample = ( ( prevrow[col8 -1]        )  << 29 )
         | ( ( prevrow[col8]           )  << 21 )
         | ( ( prevrow[col8 +1] & 0x80 )  << 13 )
         | ( ( thisrow[col8 -1] & 0x01 )  << 19 )
         | ( ( thisrow[col8]           )  << 11 )
         | ( ( thisrow[col8 +1] & 0x80 )  << 3 )
         | ( ( nextrow[col8 -1] & 0x01 )  << 9 )
         | ( ( nextrow[col8]           )  << 1 )
         | ( ( nextrow[col8 +1] & 0x80 )  >> 7 );

  return (sample);

}


static unsigned char
setTestmask(unsigned char const whiteTestmask,
        bool const testWhite,
            bool const testBlack) {
/* -----------------------------------------------------------------------
  Make a byte pattern of what bits should be tested within a given "thisrow"
  (current inrow) byte.  0 means test, 1 means skip.
  -------------------------------------------------------------------------- */

  if (testWhite == testBlack) /* Both are TRUE */
    return ( 0x00 );
  else if (testWhite == TRUE) /* testBlack is FALSE */
    return whiteTestmask;
  else
    return ~ whiteTestmask;
}


static int
cleanrow(const bit * const prevrow,
         const bit * const thisrow,
         const bit * const nextrow,
         bit * const outrow,
     int const cols,
     unsigned int const threshold,
     bool const flipWhite,
     bool const flipBlack) {
  /* ----------------------------------------------------------------------
  Work through row, scanning for bits that require flipping, and write
  the results to outrow.
  
  Returns the number of bits flipped within this one row.
  ------------------------------------------------------------------------- */

  uint32_t sample;
  unsigned char testmask;
  unsigned char flipmask=0x00;
  unsigned int col;
  unsigned int nFlipped =0;

  for ( col=0 ; col < cols ; ++col) {
    int const col8 = col/8;
    unsigned int const offset = col%8;

    if(offset == 0) {

      if(flipmask != 0x00) { /* Some bits have to be flipped */
        outrow[col8 -1] = thisrow [col8 -1] ^ flipmask;
        nFlipped += bitpop8(flipmask);
        flipmask = 0x00;
      }
      else if ( col8 > 0)
          outrow[col8 -1] = thisrow [col8 -1];

      sample = setSample(prevrow, thisrow, nextrow, col);
      testmask = setTestmask(thisrow[col8], flipWhite, flipBlack);
    }

    if ( ( ( testmask << offset ) & 0x80 ) ==0 ){
       if ( likeNeighbors(sample, offset ) < threshold )
          flipmask |= (0x80 >> offset);
    }
  }

  /* Write out last byte */
  {
      unsigned int col8Last = pbm_packed_bytes(cols) -1;
      if(flipmask != 0x00) {
        outrow[col8Last] = thisrow [col8Last] ^ flipmask;
        nFlipped += bitpop8(flipmask);
      }
      else
          outrow[col8Last] = thisrow [col8Last];
  }

  return nFlipped;

}





int
main(int argc, char *argv[]) {

    struct cmdlineInfo cmdline;
    FILE *ifp;
    bit ** buffer;
    bit * prevrow;
    bit * thisrow;
    bit * nextrow;
    bit * outrow;
    bit * edgerow;
    int cols, rows, format;
    unsigned int row;

    double nFlipped = 0;  /* Number of pixels we have flipped so far. 
                             Use type double to prevent overflow. */

    pbm_init( &argc, argv );

    parseCommandLine(argc, argv, &cmdline);

    ifp = pm_openr(cmdline.inputFilespec);

    pbm_readpbminit(ifp, &cols, &rows, &format);

    /* Initialize input buffers.
       We add a margin of 8 bits each on the left and right of the rows.

       On the top and bottom of the image we place an imaginary blank row
       ("edgerow") to facilitate the process.
    */
    {
    unsigned int i;
    buffer  = pbm_allocarray_packed(cols+16, 3);
    edgerow = pbm_allocrow_packed(cols+16);

    for(i=0; i < pbm_packed_bytes(cols+16); ++i)
      edgerow[i] = 0x00;

    for(i=0; i < 3; ++i)  /* Add blank (all white) bytes beside the edges */ 
      buffer[i][0] = buffer[i][ pbm_packed_bytes( cols +16 ) -1 ] = 0x00;

    thisrow = &edgerow[1];
    nextrow = &buffer[0][1];

    /* Read the top line into nextrow and clean the right end. */

    pbm_readpbmrow_packed(ifp, nextrow, cols, format);
    if (cols%8>0){
        nextrow[pbm_packed_bytes(cols) -1 ] >>= (8 - cols%8);
            nextrow[pbm_packed_bytes(cols) -1 ] <<= (8 - cols%8);
    }
    }

    outrow = pbm_allocrow(cols);

    pbm_writepbminit(stdout, cols, rows, 0) ;

    for (row = 0; row < rows; ++row) {
        prevrow = thisrow;  /* Slide up the input row window */
        thisrow = nextrow;
        if( row < rows -1){
      nextrow = &buffer[(row+1)%3][1];
          /* We take the address directly instead of shuffling the rows
             with the help of a temporary.  This provision is for proper 
             handling of the initial edgerow. */

          pbm_readpbmrow_packed(ifp, nextrow, cols, format);
          if (cols%8>0){
        nextrow[pbm_packed_bytes(cols) -1 ] >>= (8 - cols%8);
        nextrow[pbm_packed_bytes(cols) -1 ] <<= (8 - cols%8);
      }
    }
    else  /* Bottom of image.  */
          nextrow = & edgerow[1];

        nFlipped +=
      cleanrow ( prevrow, thisrow, nextrow, outrow, cols, cmdline.connect,
                     cmdline.flipWhite, cmdline.flipBlack );

        pbm_writepbmrow_packed(stdout, outrow, cols, 0) ;
    }

    pbm_freearray(buffer,3);
    pbm_freerow(edgerow);
    pbm_freerow(outrow);
    pm_close(ifp);

    if (cmdline.verbose)
        pm_message("%f pixels flipped", nFlipped);

    return 0;
}
