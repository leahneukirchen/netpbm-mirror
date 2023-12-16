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
#include <string.h>

#include "pm_c_util.h"
#include "nstring.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "pbm.h"

#define NAMELENGTH 80
#define MAXROWWIDTH 3200
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
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec strings we return are stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;
    unsigned int characterSpec;
    unsigned int firstOutputArgNm;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "character",   OPT_UINT, &cmdlineP->character,
            &characterSpec, 0);
    OPTENT3(0, "x",   OPT_UINT, &cmdlineP->x,
            &cmdlineP->xSpec, 0);
    OPTENT3(0, "X",   OPT_UINT, &cmdlineP->x,
            &cmdlineP->xSpec, 0);
    OPTENT3(0, "y",   OPT_UINT, &cmdlineP->y,
            &cmdlineP->ySpec, 0);
    OPTENT3(0, "Y",   OPT_UINT, &cmdlineP->y,
            &cmdlineP->ySpec, 0);
    OPTENT3(0, "debug",   OPT_UINT, NULL,
            &cmdlineP->debug, 0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We may have parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
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

static FILE * ifP;
static unsigned int pkLoc;
static const char * fileName[MAXPKCHAR];
static int dynf ;
static eightbits inputByte ;
static eightbits bitWeight ;
static int repeatCount ;
static int flagByte ;
static int debug = 0;

#define dprintf(s,d) if (debug) printf(s,d)
#define dprintf0(s) if (debug) printf(s)

static eightbits
pkByte() {
/*----------------------------------------------------------------------------
  Get a byte from the PK file
-----------------------------------------------------------------------------*/
    ++pkLoc;

    return getc(ifP);
}



static int
get16() {
/*----------------------------------------------------------------------------
  Get a 16-bit half word from the PK file
-----------------------------------------------------------------------------*/
    int const a = pkByte() ;

    return (a<<8) + pkByte();
}



static int
get32() {
/*----------------------------------------------------------------------------
  Get a 32-bit word from the PK file
-----------------------------------------------------------------------------*/
    int a;

    a = get16();  /* initial value */

    if (a > 32767)
        a -= 65536;

    return (a << 16) + get16();
}



static int
getNybble() {
/*----------------------------------------------------------------------------
  Get a nibble from current input byte, or new byte if no current byte
-----------------------------------------------------------------------------*/
    eightbits temp;

    if (bitWeight == 0) {
        inputByte = pkByte() ;
        bitWeight = 16 ;
    }
    temp = inputByte / bitWeight ;
    inputByte -= temp * bitWeight ;
    bitWeight >>= 4 ;

    return temp;
}



static bool
getBit() {
/*----------------------------------------------------------------------------
  Get a bit from the current input byte, or a new byte if no current byte
-----------------------------------------------------------------------------*/
    bool temp ;

    bitWeight >>= 1 ;
    if (bitWeight == 0) {
        inputByte = pkByte();
        bitWeight = 128 ;
    }
    temp = (inputByte >= bitWeight);
    if (temp)
        inputByte -= bitWeight;

    return temp;
}



static int
pkPackedNum() {
/*----------------------------------------------------------------------------
  Unpack a dynamically packed number. dynf is dynamic packing threshold
-----------------------------------------------------------------------------*/
    int i, j ;

    i = getNybble() ;

    if (i == 0) {           /* large run count, >= 3 nibbles */
        do {
            j = getNybble() ;          /* count extra nibbles */
            i++ ;
        } while (j == 0) ;
        while (i > 0) {
            j = (j<<4) + getNybble() ; /* add extra nibbles */
            i-- ;
        }
        return (j - 15 +((13 - dynf)<<4) + dynf) ;
    } else if (i <= dynf) return (i) ;  /* number > 0 and <= dynf */
    else if (i < 14) return (((i - dynf - 1)<<4) + getNybble() + dynf + 1) ;
    else {
        if (i == 14)
            repeatCount = pkPackedNum() ;  /* get repeat count */
        else
            repeatCount = 1 ;      /* value 15 indicates repeat count 1 */

        return pkPackedNum();
    }
}



static void
skipSpecials() {
/*----------------------------------------------------------------------------
  Skip specials in PK files, inserted by Metafont or some other program
-----------------------------------------------------------------------------*/
    do {
        flagByte = pkByte() ;
        if (flagByte >= 240)
            switch(flagByte) {
            case 240:           /* specials of size 1-4 bytes */
            case 241:
            case 242:
            case 243: {
                int i, j;

                i = 0 ;
                for (j = 240 ; j <= flagByte ; ++j)
                    i = (i<<8) + pkByte() ;
                for (j = 1 ; j <= i ; ++j)
                    pkByte() ;  /* ignore special */
            } break ;
            case 244:           /* no-op, parameters to specials */
                get32() ;
            case 245:           /* start of postamble */
            case 246:           /* no-op */
                break ;
            case 247:           /* pre-amble in wrong place */
            case 248:
            case 249:
            case 250:
            case 251:
            case 252:
            case 253:
            case 254:
            case 255:
                pm_error("unexpected flag byte %d", flagByte) ;
            }
    } while (!(flagByte < 240 || flagByte == 245)) ;
}



static void
ignoreChar(int          const car,
           unsigned int const endOfPacket) {
/*----------------------------------------------------------------------------
   ignore character packet
-----------------------------------------------------------------------------*/
   while (pkLoc != endOfPacket)
       pkByte();

   if (car < 0 || car >= MAXPKCHAR)
      pm_message("Character %d out of range", car) ;

   skipSpecials() ;
}



static void
readHeader() {
/*----------------------------------------------------------------------------
   Read the header of the input file.

   Surprisingly, nothing in the header is useful to this program, so we're
   just reading past it and doing some validation.

   We read through the first flag byte and update the global variable
   'flagByte'.
-----------------------------------------------------------------------------*/
    unsigned int commentSz;
    unsigned int i;

    if (pkByte() != 247)
        pm_error("bad PK file (pre command missing)") ;

    if (pkByte() != 89)
        pm_error("wrong version of packed file") ;

    commentSz = pkByte() ;              /* get header comment size */

    for (i = 1 ; i <= commentSz ; ++i)
        pkByte() ;  /* ignore header comment */

    get32() ;                   /* ignore designsize */
    get32() ;                   /* ignore checksum */
    if (get32() != get32())         /* h & v pixels per point */
        pm_message("Warning: aspect ratio not 1:1") ;

    skipSpecials();
}



static void
readCharacterHeader(int *          const carP,
                    unsigned int * const endOfPacketP,
                    bool *         const mustIgnoreP,
                    int *          const cheightP,
                    int *          const cwidthP,
                    int *          const xoffsP,
                    int *          const yoffsP,
                    bool *         const turnonP) {

    int packetLength;
        /* character packet length field value */
    int cheight, cwidth;
        /* bounding box height, width field values */
    int xoffs=0, yoffs=0;
    bool turnon ;
    int x;

    dynf = (flagByte >> 4);          /* get dynamic packing value */
    flagByte &= 15;
    turnon = (flagByte >= 8) ;      /* black or white initially? */
    if (turnon)
        flagByte &= 7;     /* long or short form */

    if (flagByte == 7) {            /* long form preamble */
        packetLength  = get32();
        *carP         = get32();         /* character number */

        dprintf0("flagByte7\n");
        dprintf("car: %d\n", *carP);
        get32();               /* ignore tfmwidth */
        x=get32();             /* ignore horiz escapement */
        dprintf("horiz esc %d\n", x);
        x=get32();             /* ignore vert escapement */
        dprintf("vert esc %d\n", x);
        cwidth = get32();          /* bounding box width */
        cheight = get32();         /* bounding box height */
        dprintf("cwidth %d\n", cwidth);
        dprintf("cheight %d\n", cheight);
        if (cwidth < 0 || cheight < 0 ||
            cwidth > 65535 || cheight > 65535) {
            *mustIgnoreP = true;
        } else {
            *mustIgnoreP = false;
            xoffs = get32() ;              /* horiz offset */
            yoffs = get32() ;              /* vert offset */
            dprintf ("xoffs %d\n", xoffs);
            dprintf ("yoffs %d\n", yoffs);
        }
    } else if (flagByte > 3) {      /* extended short form */
        packetLength = ((flagByte - 4)<<16) + get16() ;

        *carP = pkByte() ;            /* char number */

        *mustIgnoreP = false;

        dprintf0("flagByte>3\n");
        dprintf("car: %d\n", *carP);
        pkByte();              /* ignore tfmwidth (3 bytes) */
        get16();               /* ignore tfmwidth (3 bytes) */
        get16();               /* ignore horiz escapement */
        cwidth = get16();          /* bounding box width */
        cheight = get16();         /* bounding box height */
        dprintf("cwidth %d\n", cwidth);
        dprintf("cheight %d\n", cheight);
        xoffs = get16();                         /* horiz offset */
        if (xoffs >= 32768)
            xoffs -= 65536;
        yoffs = get16();                         /* vert offset */
        if (yoffs >= 32768)
            yoffs -= 65536;
        dprintf("xoffs %d\n", xoffs);
        dprintf("yoffs %d\n", yoffs);
    } else {                    /* short form preamble */
        packetLength  = (flagByte << 8) + pkByte();
        *carP         = pkByte();            /* char number */

        *mustIgnoreP = false;

        dprintf0("flagByte<=3\n");
        dprintf("car: %d\n", *carP);
        pkByte();          /* ignore tfmwidth (3 bytes) */
        get16();               /* ignore tfmwidth (3 bytes) */
        x = pkByte() ;  /* ignore horiz escapement */
        dprintf("horiz esc %d\n", x);
        cwidth = pkByte();            /* bounding box width */
        cheight = pkByte() ;           /* bounding box height */
        dprintf("cwidth %d\n", cwidth);
        dprintf("cheight %d\n", cheight);
        xoffs = pkByte();               /* horiz offset */
        if (xoffs >= 128)
            xoffs -=256;
        yoffs = pkByte();               /* vert offset */
        if (yoffs >= 128)
            yoffs -= 256;
        dprintf("xoffs %d\n", xoffs);
        dprintf("yoffs %d\n", yoffs);
    }
    if (packetLength < 0)
        pm_error("Invalid character header - negative packet length");
    if (packetLength > UINT_MAX - pkLoc)
        pm_error("Invalid character header - excessive packet length");

    *endOfPacketP = packetLength + pkLoc;

    *cheightP = cheight;
    *cwidthP  = cwidth;
    *xoffsP   = xoffs;
    *yoffsP   = yoffs;
    *turnonP  = turnon;
}



static void
readOneCharacter(bool           const bmxOverrideSpec,
                 int            const bmxOverride,
                 bool           const bmyOverrideSpec,
                 int            const bmyOverride,
                 unsigned int * const carP,
                 bool *         const mustIgnoreP,
                 bit ***        const bitmapP,
                 unsigned int * const bmxP,
                 unsigned int * const bmyP) {

    int car;
    unsigned int endOfPacket;
    bool mustIgnore;
    int cheight, cwidth;
    int xoffs, yoffs;
    bool turnon;
    bit row[MAXROWWIDTH+1];

    readCharacterHeader(&car, &endOfPacket, &mustIgnore,
                        &cheight, &cwidth, &xoffs, &yoffs, &turnon);

    *carP = car;

    if (mustIgnore || !fileName[car]) {
        /* Ignore this character in the font */
        ignoreChar(car, endOfPacket);
        *mustIgnoreP = true;
    } else {
        bit ** bitmap;
        unsigned int i;

        int const bmx = bmxOverrideSpec ? bmxOverride : cwidth;
        int const bmy = bmyOverrideSpec ? bmyOverride : cheight;

        *mustIgnoreP = false;

        bitmap = pbm_allocarray(bmx, bmy);

        bitWeight = 0 ;
        for (i = 0 ; i < bmy ; ++i) {
            /* make it blank */
            unsigned int j;

            for (j = 0 ; j < bmx ; ++j)
                bitmap[i][j] = PBM_WHITE;
        }
        if (dynf == 14) {               /* bitmapped character */
            dprintf("bmy: %d\n ", bmy);
            dprintf("y: %d\n ", bmy - yoffs - 1);
            for (i = 0 ; i < cheight ; ++i) {
                unsigned int const yi = i + (bmy - yoffs - 1);
                unsigned int j;
                for (j = 0 ; j < cwidth ; ++j) {
                    unsigned int const xj = j - xoffs;
                    if (getBit() && 0 <= xj && xj < bmx && 0 <= yi && yi < bmy)
                        bitmap[yi][xj] = PBM_BLACK ;
                }
            }
        } else {                    /* dynamically packed char */
            int rowsleft = cheight ;
            int hbit = cwidth ;
            int rp = 0;
            repeatCount = 0 ;
            dprintf("bmy: %d\n ", bmy);
            dprintf("y: %d\n", cheight-rowsleft+(bmy-2*yoffs-1));
            while (rowsleft > 0) {
                int count = pkPackedNum() ; /* get current color count */
                while (count > 0) {
                    if (count < hbit) {     /* doesn't extend past row */
                        hbit -= count ;
                        while (count--)
                            row[rp++] = turnon ? PBM_BLACK : PBM_WHITE;
                    } else {                /* reaches end of row */
                        count -= hbit;
                        while (hbit--)
                            row[rp++] = turnon ? PBM_BLACK : PBM_WHITE;
                        for (i = 0; i <= repeatCount; i++) {  /* fill row */
                            unsigned int const yi = i + cheight - rowsleft - 1;
                            if (0 <= yi && yi < bmy) {
                                unsigned int j;
                                for (j = 0; j < cwidth; j++) {
                                    unsigned int const xj= j - xoffs;
                                    if (0 <= xj && xj < bmx)
                                        bitmap[yi][xj] = row[j] ;
                                }
                            }
                        }
                        rowsleft -= repeatCount + 1;
                        repeatCount = rp = 0;
                        hbit = cwidth;
                    }
                }
                turnon = !turnon;
            }
            if (rowsleft != 0 || hbit != cwidth)
                pm_error("bad pk file (more bits than required)") ;
        }
        if (endOfPacket != pkLoc)
            pm_error("bad pk file (bad packet length)") ;
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

    readHeader();

    while (flagByte != 245) {  /* not at postamble */

        unsigned int car;
        bool mustIgnore;
        bit ** bitmap;
        unsigned int cols, rows;

        readOneCharacter(!!cmdline.xSpec, cmdline.x,
                         !!cmdline.ySpec, cmdline.y,
                         &car, &mustIgnore, &bitmap, &cols, &rows);

        if (!mustIgnore) {
            generatePbmFile(fileName[car], bitmap, cols, rows);

            pbm_freearray(bitmap, rows) ;
        }

        fileName[car] = NULL;

        skipSpecials();
    }

    while (!feof(ifP))
        pkByte() ;       /* skip trailing junk */

    pm_close(ifP);

    warnMissingCodePoint();

    pm_message("%u bytes read from packed file.", pkLoc);

    return 0;
}



