/* pbmtog3.c - read a PBM image and produce a Group 3 FAX file

   For specifications for Group 3 (G3) fax MH coding see ITU-T T.4:
   Standardization of Group 3 facsimile terminals for document transmission
   https://www.itu.int/rec/T-REC-T.4/en

   This program generates only MH.
*/


#include <assert.h>

#include "pm_c_util.h"
#include "shhopt.h"
#include "mallocvar.h"
#include "bitreverse.h"
#include "intcode.h"
#include "g3.h"
#include "g3ttable.h"
#include "g3prefab.h"
#include "pbm.h"

enum G3eol {EOL, ALIGN8, ALIGN16, NO_EOL, NO_RTC, NO_EOLRTC};

struct OutStream;

struct OutStream {
    FILE * fp;
    struct BitString buffer;
    bool reverseBits;    /* Reverse bit order */
    enum G3eol eolAlign; /* Omit EOL and/or RTC; align EOL to 8/16 bits */
    void * data;         /* Reserved for future expansion */
};



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;
    unsigned int reversebits;
    enum G3eol   align;
    unsigned int desiredWidth;
    unsigned int verbose;
};



static void
parseCommandLine(int argc, const char ** const argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions2 on how to parse our options.  */
    optStruct3 opt;
    unsigned int option_def_index;

    unsigned int nofixedwidth;
    unsigned int align8, align16;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0,   "reversebits",      OPT_FLAG,  NULL, &cmdlineP->reversebits,
            0);
    OPTENT3(0,   "nofixedwidth",     OPT_FLAG,  NULL, &nofixedwidth,
            0);
    OPTENT3(0,   "align8",           OPT_FLAG,  NULL, &align8,
            0);
    OPTENT3(0,   "align16",          OPT_FLAG,  NULL, &align16,
            0);
    OPTENT3(0,   "verbose",          OPT_FLAG,  NULL, &cmdlineP->verbose,
            0);

    /* TODO
       Explicit fixed widths: -A4 -B4 -A3
    */

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = true;  /* We may have parms that are negative numbers */

    pm_optParseOptions4(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    free(option_def);

    if (align8) {
        if (align16)
            pm_error("You can't specify both -align8 and -align16");
        else
            cmdlineP->align = ALIGN8;
    } else if (align16)
        cmdlineP->align = ALIGN16;
    else
        cmdlineP->align = EOL;

    if (nofixedwidth)
        cmdlineP->desiredWidth = 0;
    else
        cmdlineP->desiredWidth = 1728;

    if (argc-1 == 0)
        cmdlineP->inputFileName = "-";
    else if (argc-1 != 1)
        pm_error("Program takes zero or one argument (filename).  You "
                 "specified %d", argc-1);
    else
        cmdlineP->inputFileName = argv[1];
}



static void
reversebuffer(unsigned char * const p,
              unsigned int    const n) {

    unsigned int i;

    for (i = 0; i < n; ++i)
        p[i] = bitreverse[p[i]];
}



static void
flushBuffer(struct OutStream * const outP) {
/*----------------------------------------------------------------------------
  Flush the contents of the bit buffer
-----------------------------------------------------------------------------*/
    struct BitString const buffer = outP->buffer;

    assert (buffer.bitCount <= 32);

    if (buffer.bitCount > 0) {
        unsigned int const fullBuffer = sizeof(buffer.intBuffer) * 8;
        unsigned int const bytesToWrite = (buffer.bitCount+7)/8;
        bigend32 outbytes;
        size_t rc;

        outbytes = pm_bigendFromUint32(
                   buffer.intBuffer << (fullBuffer - buffer.bitCount));
        if (outP->reverseBits)
        reversebuffer((unsigned char *)&outbytes, bytesToWrite);
        rc = fwrite((unsigned char *)&outbytes, 1, bytesToWrite, outP->fp);
        if (rc != bytesToWrite)
            pm_error("Output error");
    }
}



#if 1==0
static void
putbitsDump(struct OutStream * const outP,
            struct BitString   const newBits) {
/*----------------------------------------------------------------------------
  Print the content of the bit put request, in human readable text form
  For debugging.  Also good for studying how the coding scheme works.

  By default the compiler ignores this function.
  To turn on, remove the "#if" - "#endif" lines enclosing the function and
  edit the output function name in putbits().
-----------------------------------------------------------------------------*/
    unsigned int const bitCount = newBits.bitCount;

    unsigned int i;
    char charBuff[128];

    assert (bitCount >= 0 && bitCount < 32);
    assert (sizeof(newBits.intBuffer) + 2 < 128);

    for (i = 0; i < bitCount; ++i) {
        unsigned int const n = bitCount - i - 1;
        charBuff[i] = ((newBits.intBuffer >> n) & 0x01) + '0';
    }

    charBuff[bitCount]   = '\n';
    charBuff[bitCount+1] = '\0';
    fwrite(charBuff, 1, bitCount+1, outP->fp);
}
#endif



static void
putbitsBinary(struct OutStream * const outP,
              struct BitString   const newBits) {
/*----------------------------------------------------------------------------
   Push the bits 'newBits' onto the right end of output buffer
   out.buffer (moving the bits already in the buffer left).

   Flush the buffer to stdout as necessary to make room.

   'newBits' must be shorter than a whole word.

   N.B. the definition of struct BitString requires upper bits to be zero.
-----------------------------------------------------------------------------*/
    unsigned int const fullBuffer = sizeof(outP->buffer.intBuffer) * 8;
    unsigned int const spaceLeft = fullBuffer - outP->buffer.bitCount;
        /* Number of bits of unused space (at the high end) in buffer */

    assert(newBits.bitCount < fullBuffer);
    assert(newBits.intBuffer >> newBits.bitCount == 0);

    if (spaceLeft > newBits.bitCount) {
        /* New bits fit with bits to spare */
        outP->buffer.intBuffer =
            outP->buffer.intBuffer << newBits.bitCount | newBits.intBuffer;
        outP->buffer.bitCount += newBits.bitCount;
    } else {
        /* New bits fill buffer.  We'll have to flush the buffer to stdout
           and put the rest of the bits in the new buffer.
        */
        unsigned int const nextBufBitCount = newBits.bitCount - spaceLeft;
        unsigned int const bitMask = ((1<<nextBufBitCount) - 1);

        outP->buffer.intBuffer = ( (outP->buffer.intBuffer << spaceLeft)
                                 | (newBits.intBuffer >> nextBufBitCount));
        outP->buffer.bitCount  = fullBuffer;
        flushBuffer(outP);

        outP->buffer.intBuffer = newBits.intBuffer & bitMask;
        outP->buffer.bitCount = nextBufBitCount;
    }
}



static void
initOutStream(struct OutStream * const outP,
              bool               const reverseBits,
              enum G3eol         const eolAlign) {

    outP->buffer.intBuffer = 0;
    outP->buffer.bitCount  = 0;
    outP->reverseBits      = reverseBits;
    outP->fp               = stdout;
    outP->eolAlign         = eolAlign;
}



static struct BitString
tableEntryToBitString(G3TableEntry const tableEntry) {

    struct BitString retval;

    retval.intBuffer = tableEntry.code;
    retval.bitCount  = tableEntry.length;

    return retval;
}



static void
putbits(struct OutStream * const outP,
        struct BitString   const newBits) {

    putbitsBinary(outP, newBits);
    /* Change to putbitsDump() for human readable output */
}



static void
putcodeShort(struct OutStream * const outP,
             bit                const color,
             unsigned int       const runLength) {

    /* Note that this requires g3ttable_table to be aligned white entry, black
       entry, white, black, etc.
    */
    unsigned int index = runLength * 2 + color;
    putbits(outP, tableEntryToBitString(g3ttable_table[index]));
}



static void
putcodeLong(struct OutStream * const outP,
            bit                const color,
            unsigned int       const runLength) {
/*----------------------------------------------------------------------------
   Output Make-up code and Terminating code at once.

   For run lengths which require both: length 64 and above

   The codes are combined here to avoid calculations in putbits()

   Terminating code is max 12 bits, Make-up code is max 13 bits.
   (See g3ttable_table, g3ttable_mtable entries in g3ttable.h)

   Also reduces object code size when putcode is compiled inline.
-----------------------------------------------------------------------------*/
    unsigned int const loIndex = runLength % 64 * 2 + color;
    unsigned int const hiIndex = runLength / 64 * 2 + color;
    unsigned int const loLength = g3ttable_table[loIndex].length;
    unsigned int const hiLength = g3ttable_mtable[hiIndex].length;

    struct BitString combinedCode;

    combinedCode.intBuffer = g3ttable_mtable[hiIndex].code << loLength |
                             g3ttable_table[loIndex].code;
    combinedCode.bitCount  = hiLength + loLength;

    putbits(outP, combinedCode);
}



static  void
putcodeExtra(struct OutStream * const outP,
             int                const color,
             int                const runLength) {
/*----------------------------------------------------------------------------
   Lengths over 2560.  This is rare.
   According to the standard, the mark-up code for 2560 can be issued as
   many times as necessary without terminal codes.
   --------------------------------------------------------------------------*/
    G3TableEntry const markUp2560 = g3ttable_mtable[2560/64*2];
                              /* Same code for black and white */

    unsigned int remainingLen;

    for (remainingLen = runLength; remainingLen > 2560; remainingLen -= 2560)
      putbits(outP, tableEntryToBitString(markUp2560));
    /* after the above: 0 < remainingLen <= 2560 */

    if (remainingLen >= 64)
        putcodeLong(outP, color, remainingLen);
    else
        putcodeShort(outP, color, remainingLen);
}



static void
putspan(struct OutStream * const outP,
        bit                const color,
        unsigned int       const runLength) {

    if (runLength < 64)
        putcodeShort(outP, color, runLength);
    else if (runLength < 2560)
        putcodeLong (outP, color, runLength);
    else  /* runLength > 2560 : rare */
        putcodeExtra(outP, color, runLength);
}



static void
puteol(struct OutStream * const outP) {

    switch (outP->eolAlign) {
    case EOL: {
        struct BitString const eol = { 1, 12 };

        putbits(outP, eol);
    } break;
    case ALIGN8:  case ALIGN16: {
        unsigned int const bitCount = outP->buffer.bitCount;
        unsigned int const fillbits =
            (outP->eolAlign == ALIGN8) ? (44 - bitCount) % 8
            : (52 - bitCount) % 16;

        struct BitString eol;

        eol.bitCount = 12 + fillbits;     eol.intBuffer = 1;
        putbits(outP, eol);
    } break;
    case NO_EOL: case NO_EOLRTC:
        break;
    case NO_RTC:
        pm_error("INTERNAL ERROR: no-RTC EOL treatment not implemented");
        break;
    }
}



static void
putrtc(struct OutStream * const outP) {

    switch (outP->eolAlign) {
    case NO_RTC: case NO_EOLRTC:
        break;
    default:
        puteol(outP);    puteol(outP);    puteol(outP);
        puteol(outP);    puteol(outP);    puteol(outP);
    }
}



static void
readOffSideMargins(unsigned char * const bitrow,
                   unsigned int    const colChars,
                   unsigned int  * const firstNonWhiteCharP,
                   unsigned int  * const lastNonWhiteCharP,
                   bool          * const blankRowP) {
/*----------------------------------------------------------------------------
  Determine the white margins on the left and right side of a row.
  This is an enhancement: convertRowToG3() works without this.
-----------------------------------------------------------------------------*/
    unsigned int charCnt;
    unsigned int firstChar;
    unsigned int lastChar;
    bool         blankRow;

    assert(colChars > 0);

    for (charCnt = 0; charCnt < colChars && bitrow[charCnt] == 0; ++charCnt);

    if (charCnt >= colChars) {
        /* Reached end of bitrow with no black pixels encountered */
        firstChar = lastChar = 0;
        blankRow  = true;
    } else {
        /* There is at least one black pixel in the row */
        firstChar = charCnt;
        blankRow = false;

        charCnt = colChars - 1;

        while (bitrow[charCnt--] == 0x00)
            ;
        lastChar = charCnt + 1;
    }

    *firstNonWhiteCharP = firstChar;
    *lastNonWhiteCharP  = lastChar;
    *blankRowP          = blankRow;
}



static void
setBlockBitsInFinalChar(unsigned char * const finalByteP,
                        unsigned int    const cols) {
/*----------------------------------------------------------------------------
   If the char in the row is fractional, set it up so that the don't care
   bits are the opposite color of the last valid pixel.
----------------------------------------------------------------------------*/
    unsigned char const finalByte  = *finalByteP;
    unsigned int const silentBitCnt = 8 - cols % 8;
    bit const rowEndColor = (finalByte >> silentBitCnt) & 0x01;

    if (rowEndColor == PBM_WHITE) {
        unsigned char const blackMask = (0x01 << silentBitCnt) - 1;

        *finalByteP = finalByte | blackMask;
    }
    /* No adjustment required if the row ends with a black pixel.
       pbm_cleanrowend_packed() takes care of this.
    */
}



static void
trimFinalChar(struct OutStream * const outP,
              bit                const color,
              int                const carryLength,
              int                const existingCols,
              int                const desiredWidth) {
/*---------------------------------------------------------------------------
   If the carry value from the last char in the row represents a valid
   sequence, output it.

   (1) If input row width is not a whole multiple of 8 and -nofixwidth
       was specified, the final carry value represents inactive bits
       at the row end.  Emit no code.  See setBlockBitsInFinalChar().

   (2) If there is white margin on the right side, the final carry value
       is valid.  We add to it the margin width.  Right-side margin may
       be added in main() to a narrow input image, detected in the
       input row by readOffSideMargins() or both.  The same treatment
       applies regardless of the nature of the right-side margin.
----------------------------------------------------------------------------*/
    if (existingCols == desiredWidth) {
        if (existingCols % 8 == 0)
            putspan(outP, color, carryLength);  /* Code up to byte boundary */
        /* Emit nothing if existingCols is not a whole multiple of 8 */
    } else if (existingCols < desiredWidth) {
        if (color == 0) {       /* Last bit sequence in final char: white */
            unsigned int const totalLength =
                carryLength + (desiredWidth - existingCols);
            putspan(outP, 0, totalLength);
        } else {                 /* Black */
            unsigned int const padLength = desiredWidth - existingCols;
            putspan(outP, 1, carryLength);
            putspan(outP, 0, padLength);
        }
    }
}



static void
convertRowToG3(struct OutStream * const outP,
               unsigned char    * const bitrow,
               unsigned int       const existingCols,
               unsigned int       const desiredWidth) {
/*----------------------------------------------------------------------------
   Table based Huffman coding

   Normally Huffman code encoders count sequences of ones and zeros
   and convert them to binary codes as they terminate.  This program
   recognizes chains of pixels and converts them directly, reading
   prefabricated code chains from an indexed table.

   For example the 8-bit sequence 01100110 translates to
   Huffman code: 000111 11 0111 11 000111.

   In reality things are more complicated.  The leftmost 0 (MSB) may be
   part of a longer sequence starting in the adjacent byte or perhaps
   spanning several bytes.  Likewise for the rightmost 0.

   So we first remove the sequence on the left side and compare its
   color with the leftmost pixel of the adjacent byte and emit either
   one code for a single sequence if they agree or two if they disagree.
   Next the composite code for the central part (in the above example
   110011 -> 11 0111 11) is emitted.  Finally we save the length and
   color of the sequence on the right end as carry-over for the next
   byte cycle.  Some 8-bit input sequences (00000000, 01111111,
   00111111, etc.) have no central part: these are special cases.
---------------------------------------------------------------------------*/
    unsigned int const colChars = pbm_packed_bytes(existingCols);

    unsigned int charCnt;
    unsigned int firstActiveChar;
    unsigned int lastActiveChar;
    bool         blankRow;
    bit          borderColor;

    borderColor = PBM_WHITE; /* initial value */

    if (existingCols == desiredWidth && (desiredWidth % 8) > 0)
        setBlockBitsInFinalChar(&bitrow[colChars-1], desiredWidth);

    readOffSideMargins(bitrow, colChars,
                       &firstActiveChar, &lastActiveChar, &blankRow);

    if (blankRow)
        putspan(outP, PBM_WHITE, desiredWidth);
    else {
        unsigned int carryLength;

        for (charCnt = firstActiveChar, carryLength = firstActiveChar * 8;
             charCnt <=lastActiveChar;
             ++charCnt) {

            unsigned char const byte = bitrow[charCnt];
            bit const rColor = !borderColor;

            if (byte == borderColor * 0xFF) {
                carryLength += 8;
            } else if (byte == (unsigned char) ~(borderColor * 0xFF)) {
                putspan(outP, borderColor, carryLength);
                carryLength = 8;
                borderColor = rColor;
            } else {
                struct PrefabCode const code = g3prefab_code[byte];
                unsigned int const activeLength =
                    8 - code.leadBits - code.trailBits;

                if (borderColor == (byte >> 7)) {
                    putspan(outP, borderColor, carryLength + code.leadBits);
                } else {
                    putspan(outP, borderColor, carryLength);
                    putcodeShort(outP, rColor, code.leadBits);
                }
                if (activeLength > 0)
                    putbits(outP, code.activeBits);

                borderColor = byte & 0x01;
                carryLength = code.trailBits;
            }
        }
        trimFinalChar(outP, borderColor, carryLength,
                      (lastActiveChar + 1) * 8, desiredWidth);
    }
    puteol(outP);
}



int
main(int          argc,
     const char * argv[]) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    unsigned char * bitrow;
       /* This is the bits of the current row, as read from the input and
          modified various ways at various points in the program.  It has
          a word of zero padding on the high (right) end for the convenience
          of code that accesses this buffer in word-size bites.
        */

    int rows;
    int cols;
    int format;
    unsigned int existingCols;
    unsigned int desiredWidth;
    unsigned int row;
    struct OutStream out;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    pbm_readpbminit(ifP, &cols, &rows, &format);

    if (cmdline.desiredWidth == 0)
        desiredWidth = existingCols = cols;
    else {
        if (cmdline.desiredWidth < cols)
            existingCols = desiredWidth = cmdline.desiredWidth;
        else {
            existingCols = pbm_packed_bytes(cols) * 8;
            desiredWidth = cmdline.desiredWidth;
        }
    }

    MALLOCARRAY(bitrow, pbm_packed_bytes(cols) + sizeof(uint32_t));

    if (!bitrow)
        pm_error("Failed to allocate a row buffer for %u columns", cols);

    initOutStream(&out, cmdline.reversebits, cmdline.align);

    puteol(&out);

    for (row = 0; row < rows; ++row) {
        pbm_readpbmrow_packed(ifP, bitrow, cols, format);
        pbm_cleanrowend_packed(bitrow, cols);
        convertRowToG3(&out, bitrow, existingCols, desiredWidth);
    }

    pbm_freerow_packed(bitrow);
    putrtc(&out);
    flushBuffer(&out);
    pm_close(ifP);

    return 0;
}



