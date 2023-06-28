/*===========================================================================
                            g3topbm
=============================================================================

  This program reads a Group 3 FAX file and produces a PBM image.

  Bryan Henderson wrote this on August 5, 2004 and contributed it to
  the public domain.

  This program is designed to be a drop-in replacement for the program
  of the same name that was distributed with Pbmplus and Netpbm since
  1989, written by Paul Haeberli <paul@manray.sgi.com>.

  Bryan used ideas on processing G3 data from Haeberli's code, but did
  not use any of the code.

  Others have modified the program since Bryan's initial work, each
  contributing their work to the public domain.
===========================================================================*/

#define _DEFAULT_SOURCE /* New name for SVID & BSD source defines */
#define _BSD_SOURCE   /* Make nstring.h define strcaseeq() */

#include "pm_c_util.h"
#include "pbm.h"
#include "shhopt.h"
#include "nstring.h"
#include "mallocvar.h"
#include "g3.h"
#include "g3ttable.h"
#include "bitreverse.h"
#include "bitarith.h"

#define LEFTBITS pm_byteLeftBits
#define RIGHTBITS pm_byteRightBits

#define MAXCOLS 10800
#define MAXROWS 14400   /* this allows up to two pages of image */

#define WHASHA 3510
#define WHASHB 1178

#define BHASHA 293
#define BHASHB 2695

#define HASHSIZE 1021

#define MAXFILLBITS (5 * 9600)

/*
Fill bits are for flow control.  This was important when DRAM was
expensive and fax machines came with small buffers.

If data arrives too quickly it may overflow the buffer of the
receiving device.  On sending devices transmission time of compressed
data representing a single row can be shorter than the time required
to scan and encode.  The CCITT standard allows sending devices to
insert fill bits to put communication on hold in these cases.

By the CCITT standard, the maximum transmission time for one row is:

100 - 400 pixels/inch 13 seconds
(standard mode: 200 pixels/inch)
600 pixels/inch 19 seconds
1200 pixels/inch 37 seconds

If one row is not received within the above limits, the receiving
machine must disconnect the line.

The receiver may be less patient.  It may opt to disconnect if one row
is not received within 5 seconds.
*/

static G3TableEntry * whash[HASHSIZE];
static G3TableEntry * bhash[HASHSIZE];


struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFilespec;  /* Filespec of input file */
    unsigned int reversebits;
    unsigned int kludge;
    unsigned int stretch;
    unsigned int stop_error;
    unsigned int expectedLineSize;
};



static void
parseCommandLine(int argc, const char ** const argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;  /* malloc'ed */
        /* Instructions to OptParseOptions3 on how to parse our options.  */
    optStruct3 opt;

    unsigned int option_def_index;

    unsigned int widthSpec, paper_sizeSpec;
    const char * paperSize;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "reversebits",      OPT_FLAG,  NULL, &cmdlineP->reversebits,
            0);
    OPTENT3(0, "kludge",           OPT_FLAG,  NULL, &cmdlineP->kludge,
            0);
    OPTENT3(0, "stretch",          OPT_FLAG,  NULL, &cmdlineP->stretch,
            0);
    OPTENT3(0, "stop_error",       OPT_FLAG,  NULL, &cmdlineP->stop_error,
            0);
    OPTENT3(0, "width",            OPT_UINT,  &cmdlineP->expectedLineSize,
            &widthSpec,                0);
    OPTENT3(0, "paper_size",       OPT_STRING, &paperSize,
            &paper_sizeSpec,           0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We may have parms that are negative numbers */

    pm_optParseOptions3(&argc, (char**)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (widthSpec && paper_sizeSpec)
        pm_error("You can't specify both -width and -paper_size");

    if (widthSpec) {
        if (cmdlineP->expectedLineSize < 1)
            pm_error("-width must be at least 1");
    } else if (paper_sizeSpec) {
        if (strcaseeq(paperSize, "A6"))
            cmdlineP->expectedLineSize = 864;
        else if (strcaseeq(paperSize, "A5"))
            cmdlineP->expectedLineSize = 1216;
        else if (strcaseeq(paperSize, "A4"))
            cmdlineP->expectedLineSize = 1728;
        else if (strcaseeq(paperSize, "B4"))
            cmdlineP->expectedLineSize = 2048;
        else if (strcaseeq(paperSize, "A3"))
            cmdlineP->expectedLineSize = 2432;
        else
            pm_error("Unrecognized value for -paper_size '%s'.  "
                     "We recognize only A3, A4, A5, A6, and B4.",
                     paperSize);
    } else
        cmdlineP->expectedLineSize = 0;

    if (argc-1 == 0)
        cmdlineP->inputFilespec = "-";
    else if (argc-1 != 1)
        pm_error("Program takes zero or one argument (filename).  You "
                 "specified %d", argc-1);
    else
        cmdlineP->inputFilespec = argv[1];
}



struct BitStream {

    FILE * fileP;
    bool reversebits;
    int shdata;
        /* 8-bit buffer for rawgetbit(). */
    unsigned int shbit;
        /* single bit mask for the bit of 'shdata' that is next in the stream.
           zero when 'shdata' is empty.
        */
    unsigned int zeroBitCount;
        /* Number of consecutive zero bits the stream has seen.  Note that
           because an EOL mark ends in a one bit, this starts over for each
           line.
        */
};



static void
readBit(struct BitStream * const bitStreamP,
        unsigned int *     const bitP,
        const char **      const errorP) {
/*----------------------------------------------------------------------------
   Return the next raw bit from the G3 input stream.

   Do not call this outside of the bit stream object; Caller is responsible
   for maintaining object state.
-----------------------------------------------------------------------------*/
    *errorP = NULL;  /* initial assumption */

    if ((bitStreamP->shbit & 0xff) == 0) {
        bitStreamP->shdata = getc(bitStreamP->fileP);
        if (bitStreamP->shdata == EOF)
            pm_asprintf(errorP, "EOF or error reading file");
        else {
            bitStreamP->shbit = 0x80;
            if ( bitStreamP->reversebits )
                bitStreamP->shdata = bitreverse[ bitStreamP->shdata ];
            }
    }

    if (bitStreamP->shdata & bitStreamP->shbit)
        *bitP = 1;
    else
        *bitP = 0;

    bitStreamP->shbit >>= 1;
}



static void
readBitAndDetectEol(struct BitStream * const bitStreamP,
                    unsigned int *     const bitP,
                    bool *             const eolP,
                    const char **      const errorP) {
/*----------------------------------------------------------------------------
   Same as readBit(), but iff the bit read is the final bit of an EOL
   mark, return *eolP == TRUE.

   An EOL mark is 11 zero bits followed by a one.
-----------------------------------------------------------------------------*/
    readBit(bitStreamP, bitP, errorP);
    if (!*errorP) {
        bool eol;

        eol = FALSE;  /* initial assumption */
        if (*bitP == 0)
            ++bitStreamP->zeroBitCount;
        else {
            if (bitStreamP->zeroBitCount >= 11)
                eol = TRUE;
            bitStreamP->zeroBitCount = 0;
        }
        *eolP = eol;
    }
}


static void
initBitStream(struct BitStream * const bitStreamP,
              FILE *             const fileP,
              bool               const reversebits) {

    bitStreamP->fileP        = fileP;
    bitStreamP->reversebits  = reversebits;
    bitStreamP->shbit        = 0x00;
    bitStreamP->zeroBitCount = 0;
}



static void
skipToNextLine(struct BitStream * const bitStreamP) {

    bool eol;
    const char * error;

    eol = FALSE;
    error = NULL;

    while (!eol && !error) {
        unsigned int bit;

        readBitAndDetectEol(bitStreamP, &bit, &eol, &error);
    }
}



static void
addtohash(G3TableEntry *     hash[],
          G3TableEntry       table[],
          unsigned int const n,
          int          const a,
          int          const b) {

    unsigned int i;

    for (i = 0; i < n; ++i) {
        G3TableEntry * const teP = &table[i*2];
        unsigned int const pos =
            ((teP->length + a) * (teP->code + b)) % HASHSIZE;
        if (hash[pos])
            pm_error("internal error: addtohash fatal hash collision");
        hash[pos] = teP;
    }
}



static G3TableEntry *
hashfind(G3TableEntry *       hash[],
         int            const length,
         int            const code,
         int            const a,
         int            const b) {

    unsigned int pos;
    G3TableEntry * te;

    pos = ((length + a) * (code + b)) % HASHSIZE;
    te = hash[pos];
    return ((te && te->length == length && te->code == code) ? te : NULL);
}



static void
buildHashes(G3TableEntry * (*whashP)[HASHSIZE],
            G3TableEntry * (*bhashP)[HASHSIZE]) {

    unsigned int i;

    for (i = 0; i < HASHSIZE; ++i)
        (*whashP)[i] = (*bhashP)[i] = NULL;

    addtohash(*whashP, &g3ttable_table[0], 64, WHASHA, WHASHB);
    addtohash(*whashP, &g3ttable_mtable[2], 40, WHASHA, WHASHB);

    addtohash(*bhashP, &g3ttable_table[1], 64, BHASHA, BHASHB);
    addtohash(*bhashP, &g3ttable_mtable[3], 40, BHASHA, BHASHB);

}



static void
makeRowWhite(unsigned char * const packedBitrow,
             unsigned int    const cols) {

    unsigned int colByte;
    for (colByte = 0; colByte < pbm_packed_bytes(cols); ++colByte)
        packedBitrow[colByte] = PBM_WHITE * 0xff;
}



static G3TableEntry *
g3code(unsigned int const curcode,
       unsigned int const curlen,
       bit          const color) {
/*----------------------------------------------------------------------------
   Return the position in the code tables mtable and ttable of the
   G3 code which is the 'curlen' bits long with value 'curcode'.

   Note that it is the _position_ in the table that determines the meaning
   of the code.  The contents of the table entry do not.
-----------------------------------------------------------------------------*/
    G3TableEntry * retval;

    switch (color) {
    case PBM_WHITE:
        if (curlen < 4)
            retval = NULL;
        else
            retval = hashfind(whash, curlen, curcode, WHASHA, WHASHB);
        break;
    case PBM_BLACK:
        if (curlen < 2)
            retval = NULL;
        else
            retval = hashfind(bhash, curlen, curcode, BHASHA, BHASHB);
        break;
    default:
        pm_error("INTERNAL ERROR: color is not black or white");
    }
    return retval;
}



static void
writeBlackBitSpan(unsigned char * const packedBitrow,
                  int             const cols,
                  int             const offset) {
/*----------------------------------------------------------------------------
   Write black (="1") bits into packedBitrow[], starting at 'offset',
   length 'cols'.
-----------------------------------------------------------------------------*/
    unsigned char * const dest = & packedBitrow[offset/8];
    unsigned int const rs  = offset % 8;
    unsigned int const trs = (cols + rs) % 8;
    unsigned int const colBytes = pbm_packed_bytes(cols + rs);
    unsigned int const last = colBytes - 1;

    unsigned char const origHead = dest[0];
    unsigned char const origEnd =  0x00;

    unsigned int i;

    for( i = 0; i < colBytes; ++i)
        dest[i] = PBM_BLACK * 0xff;

    if (rs > 0)
        dest[0] = LEFTBITS(origHead, rs) | RIGHTBITS(dest[0], 8-rs);

    if (trs > 0)
        dest[last] = LEFTBITS(dest[last], trs) | RIGHTBITS(origEnd, 8-trs);
}



enum g3tableId {TERMWHITE, TERMBLACK, MKUPWHITE, MKUPBLACK};

static void
processG3Code(const G3TableEntry * const teP,
              unsigned char *      const packedBitrow,
              unsigned int *       const colP,
              bit *                const colorP,
              unsigned int *       const countP) {
/*----------------------------------------------------------------------------
   'teP' is a pointer into the mtable/ttable.  Note that the thing it points
   to is irrelevant to us; it is only the position in the table that
   matters.
-----------------------------------------------------------------------------*/
    enum g3tableId const teId =
        (teP > g3ttable_mtable ? 2 : 0) + (teP - g3ttable_table) % 2;

    unsigned int teCount;

    switch(teId) {
    case TERMWHITE: teCount = (teP - g3ttable_table    ) / 2;      break;
    case TERMBLACK: teCount = (teP - g3ttable_table - 1) / 2;      break;
    case MKUPWHITE: teCount = (teP - g3ttable_mtable    ) / 2 * 64; break;
    case MKUPBLACK: teCount = (teP - g3ttable_mtable - 1) / 2 * 64; break;
    }

    switch (teId) {
    case TERMWHITE:
    case TERMBLACK: {
        unsigned int totalRunLength;
        unsigned int col;

        col = *colP;
        totalRunLength = MIN(*countP + teCount, MAXCOLS - col);

        if (totalRunLength > 0) {
            if (*colorP == PBM_BLACK)
                writeBlackBitSpan(packedBitrow, totalRunLength, col);
            /* else : Row was initialized to white, so we just skip */
            col += totalRunLength;
        }
        *colorP = !*colorP;
        *countP = 0;
        *colP   = col;
    } break;
    case MKUPWHITE:
    case MKUPBLACK:
        *countP += teCount;
        break;
    default:
        pm_error("Can't happen");
    }
}



static void
formatBadCodeException(const char ** const exceptionP,
                       unsigned int  const col,
                       unsigned int  const curlen,
                       unsigned int  const curcode) {

    pm_asprintf(exceptionP,
                "bad code word at Column %u.  "
                "No prefix of the %u bits 0x%x matches any recognized "
                "code word and no code words longer than 13 bits are "
                "defined.  ",
                col, curlen, curcode);
}



static void
readFaxRow(struct BitStream * const bitStreamP,
           unsigned char *    const packedBitrow,
           unsigned int *     const lineLengthP,
           const char **      const exceptionP,
           const char **      const errorP) {
/*----------------------------------------------------------------------------
  Read one line of G3 fax from the bit stream *bitStreamP into
  packedBitrow[].  Return the length of the line, in pixels, as *lineLengthP.

  If there's a problem with the line, return as much of it as we can,
  advance the input stream past the next EOL mark, and put a text
  description of the problem in newly malloc'ed storage at
  *exceptionP.  If there's no problem, return *exceptionP = NULL.

  We guarantee that we make progress through the input stream.

  Iff there is an error, return a text description of it in newly
  malloc'ed storage at *errorP and all other specified behavior
  (including return values) is unspecified.
-----------------------------------------------------------------------------*/
    unsigned int col;
    unsigned int curlen;
        /* Number of bits we've read so far for the code we're currently
           reading
        */
    unsigned int fillbits;
        /* Number of consecutive 0 bits.  Can precede EOL codes */
    unsigned int curcode;
        /* What we've assembled so far of the code we're currently reading */
    unsigned int count;
        /* Number of consecutive pixels of the same color */
    bit currentColor;
        /* The color of the current run of pixels */
    bool done;

    makeRowWhite(packedBitrow, MAXCOLS);  /* initialize row */

    col = 0;
    curlen = 0;
    curcode = 0;
    fillbits = 0;
    currentColor = PBM_WHITE;
    count = 0;
    *exceptionP = NULL;
    *errorP = NULL;
    done = FALSE;

    while (!done) {
        if (col >= MAXCOLS) {
            pm_asprintf(exceptionP, "Line is too long for this program to "
                        "handle -- longer than %u columns", MAXCOLS);
            done = TRUE;
        } else {
            unsigned int bit;
            bool eol;
            const char * error;

            readBitAndDetectEol(bitStreamP, &bit, &eol, &error);
            if (error) {
                if (col > 0)
                    /* We got at least some of the row, so it's only an
                       exception, not a fatal error.
                    */
                    *exceptionP = error;
                else
                    *errorP = error;
                done = TRUE;
            } else if (eol)
                done = TRUE;
            else {
                curcode = (curcode << 1) | bit;
                ++curlen;

		if (curlen > 11 && curcode == 0x00) {
		    if (++fillbits > MAXFILLBITS)
                pm_error("Encountered %u consecutive fill bits.  "
                       "Aborting", fillbits);
		}
		else if (curlen - fillbits > 13) {
                    formatBadCodeException(exceptionP, col, curlen, curcode);
                    done = TRUE;
                } else if (curcode != 0) {
                    const G3TableEntry * const teP =
                        g3code(curcode, curlen, currentColor);
                        /* Address of structure that describes the
                           current G3 code.  Null means 'curcode' isn't
                           a G3 code yet (probably just the beginning of one)
                        */
                    if (teP) {
                        processG3Code(teP, packedBitrow,
                                      &col, &currentColor, &count);

                        curcode = 0;
                        curlen = 0;
                    }
                }
            }
        }
    }
    if (*exceptionP)
        skipToNextLine(bitStreamP);

    *lineLengthP = col;
}



static void
freeBits(unsigned char ** const packedBits,
         unsigned int     const rows,
         bool             const stretched) {

    unsigned int row;

    for (row = 0; row < rows; ++row) {
        if (stretched && row % 2 == 1) {
            /* This is just a pointer to the previous row; don't want to
               free it twice.
            */
        } else
            pbm_freerow_packed(packedBits[row]);
    }
    free(packedBits);
}



static void
handleRowException(const char * const exception,
                   const char * const error,
                   unsigned int const row,
                   bool         const tolerateErrors) {


    if (exception) {
        if (tolerateErrors)
            pm_message("Problem reading Row %u.  Skipping rest of row.  %s",
                       row, exception);
        else
            pm_error("Problem reading Row %u.  Aborting.  %s", row, exception);
        pm_strfree(exception);
    }

    if (error) {
        if (tolerateErrors)
            pm_message("Unable to read Row %u.  Skipping rest of image.  %s",
                       row, error);
        else
            pm_error("Unable to read Row %u.  Aborting.  %s", row, error);
        pm_strfree(error);
    }
}



typedef struct {
    unsigned int expectedLineSize;
        /* The size that lines are supposed to be.  Zero means we're happy
           with any size.
        */
    unsigned int maxLineSize;
        /* The maximum line size we have seen so far, or zero if we have
           not seen any lines yet.
        */
    bool warned;
        /* We have warned the user that he has a line length problem */
    bool tolerateErrors;
        /* Try to continue when we detect a line size error, as opposed to
           aborting the program.
        */
} lineSizeAnalyzer;



static void
initializeLineSizeAnalyzer(lineSizeAnalyzer * const analyzerP,
                           unsigned int       const expectedLineSize,
                           bool               const tolerateErrors) {

    analyzerP->expectedLineSize = expectedLineSize;
    analyzerP->tolerateErrors   = tolerateErrors;

    analyzerP->maxLineSize = 0;
    analyzerP->warned      = FALSE;
}



static void
analyzeLineSize(lineSizeAnalyzer * const analyzerP,
                unsigned int       const thisLineSize) {

    const char * error;

    if (analyzerP->expectedLineSize &&
        thisLineSize != analyzerP->expectedLineSize)
        pm_asprintf(&error, "Image contains a line of %u pixels.  "
                    "You specified lines should be %u pixels.",
                    thisLineSize, analyzerP->expectedLineSize);
    else {
        if (analyzerP->maxLineSize && thisLineSize != analyzerP->maxLineSize)
            pm_asprintf(&error, "There are at least two different "
                        "line lengths in this image, "
                        "%u pixels and %u pixels.  "
                        "This is a violation of the G3 standard.  ",
                        thisLineSize, analyzerP->maxLineSize);
        else
            error = NULL;
    }

    if (error) {
        if (analyzerP->tolerateErrors) {
            if (!analyzerP->warned) {
                pm_message("Warning: %s.", error);
                analyzerP->warned = TRUE;
            }
        } else
            pm_error("%s", error);

        pm_strfree(error);
    }
    analyzerP->maxLineSize = MAX(thisLineSize, analyzerP->maxLineSize);
}



/* An empty line means EOF.  An ancient comment in the code said there
   is supposed to be 6 EOL marks in a row to indicate EOF, but the code
   checked for 3 and considered 2 in a row just to mean a zero length
   line.  Starting in Netpbm 10.24 (August 2004), we assume there is
   no valid reason to have an empty line and recognize EOF as any
   empty line.  Alternatively, we could read off and ignore two empty
   lines without a 3rd.
*/

static void
readFax(struct BitStream * const bitStreamP,
        bool               const stretch,
        unsigned int       const expectedLineSize,
        bool               const tolerateErrors,
        unsigned char ***  const packedBitsP,
        unsigned int *     const colsP,
        unsigned int *     const rowsP) {

    lineSizeAnalyzer lineSizeAnalyzer;
    unsigned char ** packedBits;
    const char * error;
    bool eof;
    unsigned int row;

    MALLOCARRAY_NOFAIL(packedBits, MAXROWS);

    initializeLineSizeAnalyzer(&lineSizeAnalyzer,
                               expectedLineSize, tolerateErrors);

    eof = FALSE;
    error = NULL;
    row = 0;

    while (!eof && !error) {
        unsigned int lineSize;

        if (row >= MAXROWS)
            pm_asprintf(&error, "Image is too tall.  This program can "
                        "handle at most %u rows", MAXROWS);
        else {
            const char * exception;

            packedBits[row] = pbm_allocrow_packed(MAXCOLS);
            readFaxRow(bitStreamP, packedBits[row],
                       &lineSize, &exception, &error);

            handleRowException(exception, error, row, tolerateErrors);

            if (!error) {
                if (lineSize == 0) {
                    /* EOF.  See explanation above */
                    eof = TRUE;
                } else {
                    analyzeLineSize(&lineSizeAnalyzer, lineSize);

                    if (stretch) {
                        ++row;
                        if (row >= MAXROWS)
                            pm_asprintf(&error, "Image is too tall.  This "
                                        "program can handle at most %u rows "
                                        "after stretching", MAXROWS);
                        else
                            packedBits[row] = packedBits[row-1];
                    }
                    ++row;
                }
            }
        }
    }
    *rowsP        = row;
    *colsP        = lineSizeAnalyzer.maxLineSize;
    *packedBitsP  = packedBits;
}



int
main(int argc, const char * argv[]) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    struct BitStream bitStream;
    unsigned int rows, cols;
    unsigned char ** packedBits;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFilespec);

    initBitStream(&bitStream, ifP, cmdline.reversebits);

    if (cmdline.kludge) {
        /* Skip extra lines to get in sync. */
        skipToNextLine(&bitStream);
        skipToNextLine(&bitStream);
        skipToNextLine(&bitStream);
    }
    skipToNextLine(&bitStream);

    buildHashes(&whash, &bhash);

    readFax(&bitStream, cmdline.stretch, cmdline.expectedLineSize,
            !cmdline.stop_error,
            &packedBits, &cols, &rows);

    pm_close(ifP);

    if (cols > 0 && rows > 0) {
        unsigned int row;
        pbm_writepbminit(stdout, cols, rows, 0);
        for (row = 0; row < rows; ++row)
            pbm_writepbmrow_packed(stdout, packedBits[row], cols, 0);
    } else
        pm_error("No image data in input");

    pm_close(stdout);

    freeBits(packedBits, rows, cmdline.stretch);

    return 0;
}
