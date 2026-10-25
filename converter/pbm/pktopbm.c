/*
  pktopbm, adapted from "pktopx in C by Tomas Rokicki" by AJCD 1/8/90
  1998-09-22: jcn <janneke@gnu.org>
     - lots of bugfixes:
     * always read x/y offset bytes (3x)
     * reset bmx, bmy to defaults for each char
     * fix bitmap y placement of dynamically packed char
     * skip char early if no output file allocated
     - added debug output

  compile with: cc -lpbm -o pktopbm pktopbm.c
  */

#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>

#include "pm_c_util.h"
#include "nstring.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "pbm.h"

#define NAMELENGTH 80
#define MAXPKCHAR 256

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileNm;
    unsigned int outputFileCt;
        /* The number of output files */
    const char * outputFileNm[MAXPKCHAR];
        /* The output file name, in order */
    unsigned int character;
    unsigned int xSpec;
    unsigned int x;
    unsigned int ySpec;
    unsigned int y;
    unsigned int debug;
    unsigned int verbose;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec strings we return are stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
    optStruct3 opt;

    unsigned int option_def_index;
    unsigned int characterSpec;
    unsigned int firstOutputArgNm;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "character", OPT_UINT, &cmdlineP->character,
            &characterSpec, 0);
    OPTENT3(0, "x",         OPT_UINT, &cmdlineP->x,
            &cmdlineP->xSpec, 0);
    OPTENT3(0, "X",         OPT_UINT, &cmdlineP->x,
            &cmdlineP->xSpec, 0);
    OPTENT3(0, "y",         OPT_UINT, &cmdlineP->y,
            &cmdlineP->ySpec, 0);
    OPTENT3(0, "Y",         OPT_UINT, &cmdlineP->y,
            &cmdlineP->ySpec, 0);
    OPTENT3(0, "verbose",   OPT_FLAG, NULL,
            &cmdlineP->verbose, 0);
    OPTENT3(0, "debug",     OPT_FLAG, NULL,
            &cmdlineP->debug, 0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We may have parms that are negative numbers */

    pm_optParseOptions4(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (characterSpec) {
        if (cmdlineP->character >= MAXPKCHAR)
            pm_error("Character number (-character) must be in range 0 to %u",
                     MAXPKCHAR-1);
    } else
        cmdlineP->character = 0;

    if (argc-1 < 1) {
        cmdlineP->inputFileNm = "-";
        firstOutputArgNm = 1;
    } else {
        cmdlineP->inputFileNm = argv[1];
        firstOutputArgNm = 2;
    }

    cmdlineP->outputFileCt = 0;  /* initial value */
    {
        unsigned int argn;
        bool stdoutUsed;
        for (argn = firstOutputArgNm, stdoutUsed = false;
             argn < argc;
             ++argn) {
            if (cmdlineP->outputFileCt >= MAXPKCHAR)
                pm_error("You may not specify more than %u output files.",
                         MAXPKCHAR);
            cmdlineP->outputFileNm[cmdlineP->outputFileCt++] = argv[argn];
            if (streq(argv[argn], "-")) {
                if (stdoutUsed)
                    pm_error("You cannot specify Standard Output ('-') "
                             "for more than one output file");
                stdoutUsed = true;
            }
        }
    }
    if (cmdlineP->outputFileCt < 1)
        cmdlineP->outputFileNm[cmdlineP->outputFileCt++] = "-";

    if (cmdlineP->character + cmdlineP->outputFileCt >= MAXPKCHAR)
        pm_error("Number of output files (%u) "
                 "plus -character value (%u) exceeds "
                 "the maximum number of characters is a PK font file (%u)",
                 cmdlineP->character, cmdlineP->outputFileCt, MAXPKCHAR);
}



typedef unsigned char eightbits ;

static unsigned int pkLoc;
    /* Number of bytes of input that have been read so far from input file */
static const char * fileName[MAXPKCHAR];
static unsigned int dynf;  /* dynamic packing value */
static eightbits inputByte;
static eightbits bitWeight;
static int repeatCount;
static int flagByte;
static int debug = 0;

#define dprintf(s,d) if (debug) printf(s,d)
#define dprintf0(s) if (debug) printf(s)

static eightbits
pkByte(FILE * const ifP) {
/*----------------------------------------------------------------------------
  Get a byte from the PK file
-----------------------------------------------------------------------------*/
    int c;

    ++pkLoc;

    c = getc(ifP);

    if (c == EOF)
        pm_error("Input file ends prematurely or error reading file");

    return c;
}



static int
get16(FILE * const ifP) {
/*----------------------------------------------------------------------------
  Get a 16-bit half word from the PK file
-----------------------------------------------------------------------------*/
    int const a = pkByte(ifP) ;

    return (a<<8) + pkByte(ifP);
}



static int
get32(FILE * const ifP) {
/*----------------------------------------------------------------------------
  Get a 32-bit word from the PK file
-----------------------------------------------------------------------------*/
    int a;

    a = get16(ifP);  /* initial value */

    if (a > 32767)
        a -= 65536;

    return (a << 16) + get16(ifP);
}



static int
getNybble(FILE * const ifP) {
/*----------------------------------------------------------------------------
  Get a nibble from current input byte, or new byte if no current byte
-----------------------------------------------------------------------------*/
    eightbits retval;

    if (bitWeight == 0) {
        inputByte = pkByte(ifP);
        bitWeight = 16;
    }
    retval = inputByte / bitWeight;
    inputByte -= retval * bitWeight;
    bitWeight >>= 4;

    return retval;
}



static bool
getBit(FILE * const ifP) {
/*----------------------------------------------------------------------------
  Get a bit from the current input byte, or a new byte if no current byte
-----------------------------------------------------------------------------*/
    bool retval;

    bitWeight >>= 1 ;

    if (bitWeight == 0) {
        inputByte = pkByte(ifP);
        bitWeight = 128 ;
    }
    if (inputByte >= bitWeight) {
        retval = true;
        inputByte -= bitWeight;
    } else
        retval = false;
    return retval;
}



static int
pkPackedNum(FILE * const ifP) {
/*----------------------------------------------------------------------------
  Unpack a dynamically packed number. dynf is dynamic packing threshold

  Read from file *ifP.

  Update global variable 'repeatCount'.
-----------------------------------------------------------------------------*/
    int retval;
    int i;

    i = getNybble(ifP);

    if (i == 0) {           /* large run count, >= 3 nibbles */
        int j;
        do {
            j = getNybble(ifP);          /* count extra nibbles */
            ++i;
        } while (j == 0);
        while (i > 0) {
            j = (j<<4) + getNybble(ifP); /* add extra nibbles */
            --i;
        }
        retval = j - 15 +((13 - dynf)<<4) + dynf;
    } else if (i <= dynf)
        retval = i;  /* number > 0 and <= dynf */
    else if (i < 14)
        retval = (((i - dynf - 1)<<4) + getNybble(ifP) + dynf + 1);
    else {
        if (i == 14) {
            repeatCount = pkPackedNum(ifP);  /* get repeat count */
        } else
            repeatCount = 1;      /* value 15 indicates repeat count 1 */

        retval = pkPackedNum(ifP);
    }
    return retval;
}



static void
skipSpecials(FILE * const ifP) {
/*----------------------------------------------------------------------------
  Skip specials in PK files, inserted by Metafont or some other program
-----------------------------------------------------------------------------*/
    do {
        flagByte = pkByte(ifP);
        if (flagByte >= 240)
            switch(flagByte) {
            case 240:           /* specials of size 1-4 bytes */
            case 241:
            case 242:
            case 243: {
                unsigned int i, j;

                for (j = 240, i = 0; j <= flagByte ; ++j)
                    i = (i<<8) + pkByte(ifP);
                for (j = 1 ; j <= i ; ++j)
                    pkByte(ifP) ;  /* ignore special */
            } break ;
            case 244:           /* no-op, parameters to specials */
                get32(ifP);
            case 245:           /* start of postamble */
            case 246:           /* no-op */
                break;
            case 247:           /* pre-amble in wrong place */
            case 248:
            case 249:
            case 250:
            case 251:
            case 252:
            case 253:
            case 254:
            case 255:
                pm_error("unexpected flag byte %d", flagByte);
            }
    } while (!(flagByte < 240 || flagByte == 245)) ;
}



static void
ignoreChar(FILE *       const ifP,
           int          const car,
           unsigned int const endOfPacket) {
/*----------------------------------------------------------------------------
   Ignore character packet.

   'endOfPacket' is the input file offset of the byte after the packet to
   ignore.

   'car' is the code point for the character we're ignoring.
-----------------------------------------------------------------------------*/
   while (pkLoc != endOfPacket)
       pkByte(ifP);

   if (car < 0 || car >= MAXPKCHAR)
      pm_message("Skipping character %d out of range", car);

   skipSpecials(ifP) ;
}



static void
readHeader(FILE * const ifP) {
/*----------------------------------------------------------------------------
   Read the header of the input file.

   Surprisingly, nothing in the header is useful to this program, so we're
   just reading past it and doing some validation.

   We read through the first flag byte and update the global variable
   'flagByte'.
-----------------------------------------------------------------------------*/
    unsigned int commentSz;
        /* header comment size */
    unsigned int i;

    if (pkByte(ifP) != 247)
        pm_error("bad PK file (pre command missing)");

    if (pkByte(ifP) != 89)
        pm_error("wrong version of packed file");

    commentSz = pkByte(ifP);

    for (i = 1 ; i <= commentSz ; ++i)
        pkByte(ifP);  /* ignore header comment */

    get32(ifP);                   /* ignore designsize */
    get32(ifP);                   /* ignore checksum */
    if (get32(ifP) != get32(ifP))         /* h & v pixels per point */
        pm_message("Warning: aspect ratio not 1:1");

    skipSpecials(ifP);
}



static void
readCharacterHeaderLong(FILE *         const ifP,
                        int *          const carP,
                        unsigned int * const packetLenP,
                        bool *         const mustIgnoreP,
                        int *          const cheightP,
                        int *          const cwidthP,
                        int *          const xoffsP,
                        int *          const yoffsP) {
/*----------------------------------------------------------------------------
  Read the rest of the character header, after the flag byte, in long format.
-----------------------------------------------------------------------------*/
    unsigned int packetLengthFld;
    int x;

    packetLengthFld  = get32(ifP);
    *carP            = get32(ifP);         /* character number */

    *packetLenP = 1 + 4 + 4 + packetLengthFld;

    dprintf0("flagByte7\n");
    dprintf("car: %d\n", *carP);
    get32(ifP);               /* ignore tfmwidth */
    x=get32(ifP);             /* ignore horiz escapement */
    dprintf("horiz esc %d\n", x);
    x=get32(ifP);             /* ignore vert escapement */
    dprintf("vert esc %d\n", x);
    *cwidthP = get32(ifP);          /* bounding box width */
    *cheightP = get32(ifP);         /* bounding box height */
    dprintf("cwidth %d\n", *cwidthP);
    dprintf("cheight %d\n", *cheightP);
    if (*cwidthP < 0 || *cheightP < 0 ||
        *cwidthP > 65535 || *cheightP > 65535) {
        *mustIgnoreP = true;
    } else {
        *mustIgnoreP = false;
        *xoffsP = get32(ifP);              /* horiz offset */
        *yoffsP = get32(ifP);              /* vert offset */
        dprintf ("xoffs %d\n", *xoffsP);
        dprintf ("yoffs %d\n", *yoffsP);
    }
}



static void
readCharacterHeaderExtendedShort(FILE *         const ifP,
                                 unsigned int   const upperLenBits,
                                 int *          const carP,
                                 unsigned int * const packetLenP,
                                 bool *         const mustIgnoreP,
                                 int *          const cheightP,
                                 int *          const cwidthP,
                                 int *          const xoffsP,
                                 int *          const yoffsP) {
/*----------------------------------------------------------------------------
  Read the rest of the character header, after the flag byte, in
  extended short format.

  'upperLenBits' is the two upper bits of the 18 bit packet length, from the
  flag byte.  The lower 16 bits are the next thing in the file.
-----------------------------------------------------------------------------*/
    unsigned int packetLengthFld;

    packetLengthFld = (upperLenBits << 16) + get16(ifP);
    *carP = pkByte(ifP);            /* char number */

    *packetLenP = 1 + 2 + 1 + packetLengthFld;

    *mustIgnoreP = false;

    dprintf0("flagByte>3\n");
    dprintf("car: %d\n", *carP);
    pkByte(ifP);              /* ignore tfmwidth */
    get16(ifP);               /*   (3 bytes) */
    get16(ifP);               /* ignore horiz escapement */
    *cwidthP = get16(ifP);          /* bounding box width */
    *cheightP = get16(ifP);         /* bounding box height */
    dprintf("cwidth %d\n", *cwidthP);
    dprintf("cheight %d\n", *cheightP);
    *xoffsP = get16(ifP);                         /* horiz offset */
    if (*xoffsP >= 32768)
        *xoffsP -= 65536;
    *yoffsP = get16(ifP);                         /* vert offset */
    if (*yoffsP >= 32768)
        *yoffsP -= 65536;
    dprintf("xoffs %d\n", *xoffsP);
    dprintf("yoffs %d\n", *yoffsP);
}



static void
readCharacterHeaderShort(FILE *         const ifP,
                         unsigned int   const upperLenBits,
                         int *          const carP,
                         unsigned int * const packetLenP,
                         bool *         const mustIgnoreP,
                         int *          const cheightP,
                         int *          const cwidthP,
                         int *          const xoffsP,
                         int *          const yoffsP) {
/*----------------------------------------------------------------------------
  Read the rest of the character header, after the flag byte, in
  short format.

  'upperLenBits' is the two upper bits of the 10 bit packet length, from the
  flag byte.  The lower 8 bits are the next thing in the file.
-----------------------------------------------------------------------------*/
    unsigned int packetLengthFld;
    int x;

    packetLengthFld  = (upperLenBits << 8) + pkByte(ifP);
    *carP            = pkByte(ifP);            /* char number */

    *packetLenP = 1 + 1 + 1 + packetLengthFld;

    *mustIgnoreP = false;

    dprintf0("flagByte<=3\n");
    dprintf("car: %d\n", *carP);
    pkByte(ifP);          /* ignore tfmwidth  */
    get16(ifP);               /*  (3 bytes) */
    x = pkByte(ifP);  /* ignore horiz escapement */
    dprintf("horiz esc %d\n", x);
    *cwidthP = pkByte(ifP);            /* bounding box width */
    *cheightP = pkByte(ifP);           /* bounding box height */
    dprintf("cwidth %d\n", *cwidthP);
    dprintf("cheight %d\n", *cheightP);
    *xoffsP = pkByte(ifP);               /* horiz offset */
    if (*xoffsP >= 128)
        *xoffsP -=256;
    *yoffsP = pkByte(ifP);               /* vert offset */
    if (*yoffsP >= 128)
        *yoffsP -= 256;
    dprintf("xoffs %d\n", *xoffsP);
    dprintf("yoffs %d\n", *yoffsP);
}



static void
readCharacterHeader(FILE *         const ifP,
                    bool           const verbose,
                    int *          const carP,
                    unsigned int * const packetLenP,
                    bool *         const mustIgnoreP,
                    int *          const cheightP,
                    int *          const cwidthP,
                    int *          const xoffsP,
                    int *          const yoffsP,
                    bool *         const initialTurnonP,
                    unsigned int * const dynfP) {
/*----------------------------------------------------------------------------
   Read the header for a character, after the flag byte.

   Return as *packetLenP the length of the whole character packet, including
   the header.  N.B. this is not the packet length value from the header,
   which for some odd reason does not include the first 3 fields: flags,
   packet length, and character number.

   Return as *cwidthP and *cheightP the bounding box width and height field
   values from the header.

   Return as *turnonP true iff the color should be black instead of white
   initially.
-----------------------------------------------------------------------------*/
    unsigned int const formatCode = flagByte & 0x7;

    *dynfP = (flagByte >> 4);

    *initialTurnonP = (flagByte & 0x08) != 0;

    if (formatCode == 0x7) {
        if (verbose)
            pm_message("Long-form preamble");
        readCharacterHeaderLong(
            ifP, carP, packetLenP, mustIgnoreP,
            cheightP, cwidthP, xoffsP, yoffsP);
    } else if (formatCode & 0x4) {
        if (verbose)
            pm_message("Extended short-form preamble");
        readCharacterHeaderExtendedShort(
            ifP, formatCode & 0x3, carP, packetLenP, mustIgnoreP,
            cheightP, cwidthP, xoffsP, yoffsP);
    } else {
        if (verbose)
            pm_message("Short-form preamble");
        readCharacterHeaderShort(
            ifP, formatCode & 0x3, carP, packetLenP, mustIgnoreP,
            cheightP, cwidthP, xoffsP, yoffsP);
    }
    if (verbose) {
        pm_message(
            "Character number %u: packet length %u, width %u, height %u, "
            "X offset %u, Y offset %u, dynf %u, initial color %s",
            *carP, *packetLenP, *cwidthP, *cheightP, *xoffsP, *yoffsP, dynf,
            *initialTurnonP ? "BLACK" : "WHITE");
    }
}



static void
makeWhite(unsigned int const width,
          unsigned int const height,
          bit **       const bitmap) {

    unsigned int i;

    for (i = 0 ; i < height; ++i) {
        unsigned int j;

        for (j = 0 ; j < width; ++j)
            bitmap[i][j] = PBM_WHITE;
    }
}



static void
doBitmappedCharacter(FILE *       const ifP,
                     unsigned int const cwidth,
                     unsigned int const cheight,
                     unsigned int const bmx,
                     unsigned int const bmy,
                     int          const xoffs,
                     int          const yoffs,
                     bool         const verbose,
                     bit **       const bitmap) {

    unsigned int row;
    unsigned int bitCount = 0;

    dprintf("bmy: %d\n ", bmy);
    dprintf("y: %d\n ", bmy - yoffs - 1);

    for (row = 0 ; row < cheight; ++row) {
        unsigned int const yi = (bmy - yoffs - 1) + row;

        unsigned int col;

        for (col = 0 ; col < cwidth ; ++col) {
            unsigned int const xj = col - xoffs;
            bool const isBlack = getBit(ifP);

            ++bitCount;
            if (isBlack && 0 <= xj && xj < bmx && 0 <= yi && yi < bmy)
                bitmap[yi][xj] = PBM_BLACK ;
        }
    }
}



static void
fillRow(bit *        const bitrow,
        unsigned int const repeatCount,
        unsigned int const cwidth,
        unsigned int const firstRow,
        unsigned int const bmx,
        unsigned int const bmy,
        int          const xoffs,
        bit **       const bitmap) {

    unsigned int i;

    for (i = 0; i <= repeatCount; ++i) {
        unsigned int const yi = firstRow + i;

        if (0 <= yi && yi < bmy) {
            unsigned int j;
            for (j = 0; j < cwidth; ++j) {
                unsigned int const xj= j - xoffs;
                if (0 <= xj && xj < bmx)
                    bitmap[yi][xj] = bitrow[j];
            }
        }
    }
}



static void
doDynamicallyPackedChar(FILE *       const ifP,
                        unsigned int const cwidth,
                        unsigned int const cheight,
                        unsigned int const bmx,
                        unsigned int const bmy,
                        int          const xoffs,
                        int          const yoffs,
                        bool         const initTurnon,
                        bool         const verbose,
                        bit **       const bitmap) {

    bit *        row;  /* malloced */
        /* Buffer in which we accumulate a full row of the character from
           the file.
        */
    unsigned int rowsLeft;
    unsigned int colsLeft;
        /* Number of pixels left (not yet set) in the current row */
    unsigned int rp;
    bool         turnon;

    MALLOCARRAY(row, cwidth);
    if (!row) {
        pm_error("Failed to allocate a row buffer for %u columns of one "
                 "character", cwidth);
    }
    repeatCount = 0;

    dprintf("bmy: %d\n ", bmy);

    for (rowsLeft = cheight, colsLeft = cwidth, rp = 0, turnon = initTurnon;
         rowsLeft > 0; ) {

        unsigned int count;
            /* Number of pixels in the current run */

        for (count = pkPackedNum(ifP); count > 0; ) {
            if (count < colsLeft) {
                /* This run does not fill the row, so just set the pixels
                   in the row buffer.
                */
                for (; count > 0; --count) {
                    assert(rp < cwidth);
                    row[rp++] = turnon ? PBM_BLACK : PBM_WHITE;
                    --colsLeft;
                }
            } else {
                /* This run goes at least through the end of the row, so fill
                   out the row buffer, move the row (along with any
                   repetitions of it) to 'bitmap[][]', and set up to start the
                   next row and finish the run there.
                */
                for (; colsLeft > 0; --colsLeft) {
                    assert(rp < cwidth);
                    row[rp++] = turnon ? PBM_BLACK : PBM_WHITE;
                    --count;
                }

                fillRow(row, repeatCount, cwidth,
                        cheight - rowsLeft - 1, bmx, bmy, xoffs,
                        bitmap);

                rowsLeft -= repeatCount + 1;
                repeatCount = rp = 0;
                colsLeft = cwidth;
            }
        }
        turnon = !turnon;
    }
    if (rowsLeft != 0 || colsLeft != cwidth)
        pm_error("bad pk file (more bits than required)") ;

    free(row);
}



static void
readOneCharacter(FILE *         const ifP,
                 bool           const bmxOverrideSpec,
                 int            const bmxOverride,
                 bool           const bmyOverrideSpec,
                 int            const bmyOverride,
                 bool           const verbose,
                 unsigned int * const carP,
                 bool *         const mustIgnoreP,
                 bit ***        const bitmapP,
                 unsigned int * const bmxP,
                 unsigned int * const bmyP) {
/*----------------------------------------------------------------------------
   Read one character from *ifP, starting with the second byte (i.e. after
   the flag byte).
-----------------------------------------------------------------------------*/
    int car;
    unsigned int thisCharacterLocInFile;
    unsigned int packetLength;
    unsigned int endOfPacket;
    bool mustIgnore;
    int cheight, cwidth;
    int xoffs, yoffs;
    bool initTurnon;

    thisCharacterLocInFile = pkLoc - 1;  /* Back up to flag byte */

    readCharacterHeader(ifP, verbose, &car, &packetLength, &mustIgnore,
                        &cheight, &cwidth, &xoffs, &yoffs, &initTurnon,
                        &dynf);

    if (packetLength > UINT_MAX - thisCharacterLocInFile) {
        pm_error("Packet length in character header is too large "
                 "for computation");
    }

    endOfPacket = thisCharacterLocInFile + packetLength;

    *carP = car;

    if (mustIgnore || !fileName[car]) {
        /* Ignore this character in the font */
        ignoreChar(ifP, car, endOfPacket);
        *mustIgnoreP = true;
    } else {
        int const bmx = bmxOverrideSpec ? bmxOverride : cwidth;
        int const bmy = bmyOverrideSpec ? bmyOverride : cheight;

        bit ** bitmap;

        *mustIgnoreP = false;

        bitmap = pbm_allocarray(bmx, bmy);

        bitWeight = 0 ;

        makeWhite(bmx, bmy, bitmap);  /* Initial value */

        if (dynf == 14) {
            doBitmappedCharacter(ifP, cwidth, cheight, bmx, bmy, xoffs, yoffs,
                                 verbose, bitmap);
        } else {
            doDynamicallyPackedChar(ifP, cwidth, cheight, bmx, bmy,
                                    xoffs, yoffs, initTurnon, verbose, bitmap);
        }
        if (pkLoc != endOfPacket) {
            pm_error("Invalid PK input: "
                     "Character header says packet length %u bytes, "
                     "but the character raster ends %u bytes short of that",
                     packetLength, endOfPacket - pkLoc);
        }
        *bitmapP = bitmap;
        *bmxP    = bmx;
        *bmyP    = bmy;
    }
}



static void
generatePbmFile(const char * const fileNm,
                bit **       const bitmap,
                unsigned int const cols,
                unsigned int const rows) {

    FILE * ofP;

    ofP = pm_openw(fileNm);

    pbm_writepbm(ofP, bitmap, cols, rows, 0);

    pm_close(ofP);
}



static void
warnMissingCodePoint() {

    unsigned int car;

    for (car = 0; car < MAXPKCHAR; ++car) {
        if (fileName[car])
            pm_message("Warning: No character in position %d (file %s).",
                       car, fileName[car]) ;
    }
}



int
main(int argc, const char ** argv) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    unsigned int i;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    debug = cmdline.debug;

    for (i = 0; i < cmdline.character; ++i)
        fileName[i] = NULL;
    for (i = 0; i < cmdline.outputFileCt; ++i)
        fileName[cmdline.character + i] = cmdline.outputFileNm[i];
    for (i = cmdline.character + cmdline.outputFileCt;
         i < MAXPKCHAR;
         ++i)
        fileName[i] = NULL;

    ifP = pm_openr(cmdline.inputFileNm);

    pkLoc = 0;

    readHeader(ifP);

    while (flagByte != 245) {  /* not at postamble */

        unsigned int car;
        bool mustIgnore;
        bit ** bitmap;
        unsigned int cols, rows;

        readOneCharacter(ifP,
                         !!cmdline.xSpec, cmdline.x,
                         !!cmdline.ySpec, cmdline.y,
                         !!cmdline.verbose,
                         &car, &mustIgnore, &bitmap, &cols, &rows);

        if (!mustIgnore) {
            generatePbmFile(fileName[car], bitmap, cols, rows);

            pbm_freearray(bitmap, rows);
        }

        fileName[car] = NULL;

        skipSpecials(ifP);
    }

    while (!feof(ifP))
        pkByte(ifP);       /* skip trailing junk */

    pm_close(ifP);

    warnMissingCodePoint();

    pm_message("%u bytes read from packed file.", pkLoc);

    return 0;
}



