/*****************************************************************************
                                  pamundice
******************************************************************************
  Assemble a grid of images into one.

  By Bryan Henderson, San Jose CA 2001.01.31

  Contributed to the public domain.

******************************************************************************/

#include <assert.h>
#include <string.h>

#include "pm_c_util.h"
#include "pam.h"
#include "shhopt.h"
#include "nstring.h"
#include "mallocvar.h"

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFilePattern;
    unsigned int across;
    unsigned int down;
    unsigned int hoverlap;
    unsigned int voverlap;
    const char * listfile;
    unsigned int listfileSpec;
    unsigned int verbose;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP ) {
/*----------------------------------------------------------------------------
   parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.

   If command line is internally inconsistent (invalid options, etc.),
   issue error message to stderr and abort program.

   Note that the strings we return are stored in the storage that
   was passed to us as the argv array.  We also trash *argv.
-----------------------------------------------------------------------------*/
    optEntry *option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int acrossSpec, downSpec;
    unsigned int hoverlapSpec, voverlapSpec;
    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "across",      OPT_UINT,    &cmdlineP->across,
            &acrossSpec,                      0);
    OPTENT3(0, "down",        OPT_UINT,    &cmdlineP->down,
            &downSpec,                        0);
    OPTENT3(0, "hoverlap",    OPT_UINT,    &cmdlineP->hoverlap,
            &hoverlapSpec,                    0);
    OPTENT3(0, "voverlap",    OPT_UINT,    &cmdlineP->voverlap,
            &voverlapSpec,                    0);
    OPTENT3(0, "listfile",    OPT_STRING,  &cmdlineP->listfile,
            &cmdlineP->listfileSpec,          0);
    OPTENT3(0, "verbose",     OPT_FLAG,    NULL,
            &cmdlineP->verbose,               0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3( &argc, (char **)argv, opt, sizeof(opt), 0 );
        /* Uses and sets argc, argv, and some of *cmdline_p and others. */

    if (acrossSpec) {
        if (cmdlineP->across == 0)
            pm_error ("-across value must be positive");
    } else
        cmdlineP->across = 1;

    if (downSpec) {
        if (cmdlineP->down == 0)
            pm_error ("-down value must be positive");
    } else
        cmdlineP->down = 1;

    if (!hoverlapSpec)
        cmdlineP->hoverlap = 0;

    if (!voverlapSpec)
        cmdlineP->voverlap = 0;

    if (cmdlineP->listfileSpec) {
        if (argc-1 > 0)
            pm_error("Program takes no parameters when -listfile is "
                     "specified.  You specified %u", argc-1);
        else
            cmdlineP->inputFilePattern = NULL;
    } else {
        if (argc-1 < 1)
            pm_error("You must specify one argument, the input file name "
                     "pattern (e.g. 'myimage%%2a%%2d.pnm'), or -listfile");
        else if (argc-1 > 1)
            pm_error("Program takes at most one parameter: "
                     "the input file name pattern.  You specified %u", argc-1);
        else
            cmdlineP->inputFilePattern = argv[1];
    }
    free(option_def);
}



/*------------------ string buffer -----------------------------------*/
struct buffer {
    char * string;
    unsigned int allocSize;
    unsigned int length;
};


static void
buffer_init(struct buffer * const bufferP) {

    bufferP->length = 0;
    bufferP->allocSize = 1024;
    MALLOCARRAY(bufferP->string, bufferP->allocSize);

    if (bufferP->string == NULL)
        pm_error("Out of memory allocating buffer to compute file name");
}



static void
buffer_term(struct buffer * const bufferP) {

    free(bufferP->string);
}



static void
buffer_addChar(struct buffer * const bufferP,
               char            const newChar) {

    if (bufferP->length + 1 + 1 > bufferP->allocSize)
        pm_error("Ridiculously long input file name.");
    else {
        bufferP->string[bufferP->length++] = newChar;
        bufferP->string[bufferP->length] = '\0';
    }
}



static void
buffer_addString(struct buffer * const bufferP,
                 const char *    const newString) {

    if (bufferP->length + 1 + strlen(newString) > bufferP->allocSize)
        pm_error("Ridiculously long input file name.");
    else {
        strcat(&bufferP->string[bufferP->length], newString);
        bufferP->length += strlen(newString);
    }
}
/*------------------ end of string buffer ----------------------------*/



/*------------------ computeInputFileName ----------------------------*/
static unsigned int
digitValue(char const digitChar) {

    return digitChar - '0';
}



static void
getPrecision(const char *   const pattern,
             unsigned int   const startInCursor,
             unsigned int * const precisionP,
             unsigned int * const newInCursorP) {

    unsigned int precision;
    unsigned int inCursor;

    inCursor = startInCursor;  /* Start right after the '%' */

    precision = 0;

    while (isdigit(pattern[inCursor])) {
        precision = 10 * precision + digitValue(pattern[inCursor]);
        ++inCursor;
    }

    if (precision == 0)
        pm_error("Zero (or no) precision in substitution "
                 "specification in file name pattern '%s'.  "
                 "A proper substitution specification is like "
                 "'%%3a'.", pattern);

    *precisionP = precision;
    *newInCursorP = inCursor;
}



typedef struct {
    /* Context of % substitutions as we progress through a file name pattern */
    bool downSub;
        /* There has been a %d (down) substitution */
    bool acrossSub;
        /* There has been a %a (across) substitution */
} SubstContext;



static void
doSubstitution(const char *    const pattern,
               unsigned int    const startInCursor,
               unsigned int    const rank,
               unsigned int    const file,
               struct buffer * const bufferP,
               unsigned int *  const newInCursorP,
               SubstContext *  const substContextP) {

    unsigned int inCursor;

    inCursor = startInCursor;  /* Start right after the '%' */

    if (pattern[inCursor] == '%') {
        buffer_addChar(bufferP, '%');
        ++inCursor;
    } else {
        unsigned int precision;

        getPrecision(pattern, inCursor, &precision, &inCursor);

        if (pattern[inCursor] == '\0')
            pm_error("No format character follows '%%' in input "
                     "file name pattern '%s'.  A proper substitution "
                     "specification is like '%%3a'", pattern);
        else {
            const char * substString;
            const char * desc;

            switch (pattern[inCursor]) {
            case 'a':
                if (substContextP->acrossSub)
                    pm_error("Format specifier 'a' appears more than "
                             "once in input file pattern '%s'", pattern);
                else {
                    pm_asprintf(&substString, "%0*u", precision, file);
                    pm_asprintf(&desc, "file (across)");
                    substContextP->acrossSub = true;
                }
                break;
            case 'd':
                if (substContextP->downSub)
                    pm_error("Format specifier 'd' appears more than "
                             "once in input file pattern '%s'", pattern);
                else {
                    pm_asprintf(&substString, "%0*u", precision, rank);
                    pm_asprintf(&desc, "rank (down)");
                    substContextP->downSub = true;
                }
                break;
            default:
                pm_error("Unknown format specifier '%c' in input file "
                         "pattern '%s'.  Recognized format specifiers are "
                         "'%%a' (across) and '%%d (down)'",
                         pattern[inCursor], pattern);
            }
            if (strlen(substString) > precision)
                pm_error("%s number %u is wider than "
                         "the %u characters specified in the "
                         "input file pattern",
                         desc, (unsigned)strlen(substString), precision);
            else
                buffer_addString(bufferP, substString);

            pm_strfree(desc);
            pm_strfree(substString);

            ++inCursor;
        }
    }
    *newInCursorP = inCursor;
}



static void
computeInputFileName(const char *  const pattern,
                     unsigned int  const rank,
                     unsigned int  const file,
                     const char ** const fileNameP,
                     bool *        const rankFileIndependentP) {

    struct buffer buffer;
    unsigned int inCursor;
    SubstContext substContext;

    buffer_init(&buffer);

    inCursor = 0;
    substContext.downSub   = 0;
    substContext.acrossSub = 0;

    while (pattern[inCursor] != '\0') {
        if (pattern[inCursor] == '%') {
            ++inCursor;

            doSubstitution(pattern, inCursor, rank, file, &buffer, &inCursor,
                           &substContext);

        } else
            buffer_addChar(&buffer, pattern[inCursor++]);
    }

    *rankFileIndependentP = !substContext.downSub && !substContext.acrossSub;

    pm_asprintf(fileNameP, "%s", buffer.string);

    buffer_term(&buffer);
}
/*------------------ end of computeInputFileName ------------------------*/




static void
createInFileListFmFile(const char  *  const listFile,
                       unsigned int   const nRank,
                       unsigned int   const nFile,
                       const char *** const inputFileListP) {

    FILE * const lfP = pm_openr(listFile);
    unsigned int const fileCt = nRank * nFile;

    const char ** inputFileList;
    unsigned int fileSeq;

    MALLOCARRAY_NOFAIL(inputFileList, nRank * nFile);

    for (fileSeq = 0; fileSeq < fileCt; ) {
        int eof;
        size_t lineLen;
        char * buf = NULL;   /* initial value */
        size_t bufferSz = 0; /* initial value */

        pm_getline(lfP, &buf, &bufferSz, &eof, &lineLen);

        if (eof)
            pm_error("Premature EOF reading list file.  "
                     "Read %u files.  Should be %u.", fileSeq, fileCt);
        else if (lineLen > 0) {
            inputFileList[fileSeq] = buf;
            ++fileSeq;
        }
    }
    pm_close(lfP);

    *inputFileListP = inputFileList;

}



static void
createInFileListFmPattern(const char  *  const pattern,
                          unsigned int   const nRank,
                          unsigned int   const nFile,
                          const char *** const inputFileListP) {

    const char ** inputFileList;
    unsigned int rank, file;
    bool warnedSingleFile;

    MALLOCARRAY_NOFAIL(inputFileList, nRank * nFile);

    for (rank = 0, warnedSingleFile = false; rank < nRank ; ++rank) {
         for (file = 0; file < nFile ; ++file) {
             const unsigned int idx = rank * nFile + file;

             bool fileNmIsRankFileIndependent;

             computeInputFileName(pattern, rank, file, &inputFileList[idx],
                                  &fileNmIsRankFileIndependent);

             if (fileNmIsRankFileIndependent && !warnedSingleFile) {
                 pm_message("Warning: No grid location (%%a/%%d) specified "
                            "in input file pattern '%s'.  "
                            "Input is single file", pattern);
                 warnedSingleFile = true;
             }
         }
    }
    *inputFileListP = inputFileList;
}



static void
destroyInFileList(const char ** const inputFileList,
                  unsigned int  const nRank,
                  unsigned int  const nFile) {

    unsigned int const fileCt = nRank * nFile;

    unsigned int fileSeq;

    for (fileSeq = 0; fileSeq < fileCt; ++fileSeq)
        pm_strfree(inputFileList[fileSeq]);

    free(inputFileList);
}



typedef struct {
    unsigned int nRank;  /* Number of images in the vertical direction */
    unsigned int nFile;  /* Number of images in the horizontal direction */
    unsigned int hoverlap;    /* horizontal overlap */
    unsigned int voverlap;    /* vertical overlap */
    const char ** list;  /* List (1-dimensional array) of filenames */
                         /* Row-major, top to bottom, left to right */
} InputFiles;



static const char *
inputFileName(InputFiles     const inputFiles,
              unsigned int   const rank,
              unsigned int   const file) {
/*----------------------------------------------------------------------------
    A selected entry from "inputFiles.list" based on "rank" and "file".

    Currently we assume that the list is a one-dimensional represetation
    of an array, row-major, top to bottom and left to right in each row.
----------------------------------------------------------------------------*/
    assert(rank < inputFiles.nRank);
    assert(file < inputFiles.nFile);

    return inputFiles.list[rank * inputFiles.nFile + file];
}



static void
getCommonInfo(InputFiles     const inputFiles,
              int *          const formatP,
              unsigned int * const depthP,
              sample *       const maxvalP,
              char *         const tupleType) {
/*----------------------------------------------------------------------------
   Get from the top left input image all the information which is common
   among all input images and the output image.  I.e. everything except
   width and height.
-----------------------------------------------------------------------------*/
    FILE * ifP;
        /* Top left input image stream */
    struct pam inpam00;
        /* Description of top left input image */

    ifP = pm_openr(inputFileName(inputFiles, 0, 0));

    pnm_readpaminit(ifP, &inpam00, PAM_STRUCT_SIZE(tuple_type));

    *formatP = inpam00.format;
    *depthP  = inpam00.depth;
    *maxvalP = inpam00.maxval;
    strcpy(tupleType, inpam00.tuple_type);

    pm_close(ifP);
}



static void
getImageInfo(InputFiles   const inputFiles,
             unsigned int const rank,
             unsigned int const file,
             struct pam * const pamP) {

    FILE * ifP;

    ifP = pm_openr(inputFileName(inputFiles, rank, file));

    pnm_readpaminit(ifP, pamP, PAM_STRUCT_SIZE(tuple_type));

    pm_close(ifP);
    pamP->file = NULL;  /* for robustness */
}



static void
getOutputWidth(InputFiles const inputFiles,
               int *      const widthP) {
/*----------------------------------------------------------------------------
   Get the output width by adding up the widths of all 'inputFiles.nFile'
   images of the top rank, and allowing for overlap of 'inputFiles.hoverlap'
   pixels.
-----------------------------------------------------------------------------*/
    double       totalWidth;
    unsigned int file;

    for (file = 0, totalWidth = 0; file < inputFiles.nFile; ++file) {
        struct pam inpam;

        getImageInfo(inputFiles, 0, file, &inpam);

        if (inpam.width < inputFiles.hoverlap)
            pm_error("Rank 0, file %u image has width %u, "
                     "which is less than the horizontal overlap of %u pixels",
                     file, inpam.width, inputFiles.hoverlap);
        else {
            totalWidth += inpam.width;

            if (file < inputFiles.nFile-1)
                totalWidth -= inputFiles.hoverlap;
        }
    }
    *widthP = (int) totalWidth;
}



static void
getOutputHeight(InputFiles const inputFiles,
                int *      const heightP) {
/*----------------------------------------------------------------------------
   Get the output height by adding up the widths of all 'inputFiles.nRank'
   images of the left file, and allowing for overlap of 'inputFiles.voverlap'
   pixels.
-----------------------------------------------------------------------------*/
    double       totalHeight;
    unsigned int rank;

    for (rank = 0, totalHeight = 0; rank < inputFiles.nRank; ++rank) {
        struct pam inpam;

        getImageInfo(inputFiles, rank, 0, &inpam);

        if (inpam.height < inputFiles.voverlap)
            pm_error("Rank %u, file 0 image has height %u, "
                     "which is less than the vertical overlap of %u pixels",
                     rank, inpam.height, inputFiles.voverlap);

        totalHeight += inpam.height;

        if (rank < inputFiles.nRank-1)
            totalHeight -= inputFiles.voverlap;
    }
    *heightP = (int) totalHeight;
}



static void
initOutpam(InputFiles   const inputFiles,
           FILE *       const ofP,
           bool         const verbose,
           struct pam * const outpamP) {
/*----------------------------------------------------------------------------
   Figure out the attributes of the output image and return them as
   *outpamP.

   Do this by examining the top rank and left file of the input images,
   which are in 'inputFiles.list'.

   In computing dimensions, assume 'inputFiles.hoverlap' pixels of horizontal
   overlap and 'inputFiles.voverlap' pixels of vertical overlap.

   We overlook any inconsistencies among the images.  E.g. if two images
   have different depths, we just return one of them.  If two images in
   the top rank have different heights, we use just one of them.

   Therefore, Caller must check all the input images to make sure they are
   consistent with the information we return.
-----------------------------------------------------------------------------*/
    assert(inputFiles.nFile >= 1);
    assert(inputFiles.nRank >= 1);

    outpamP->size        = sizeof(*outpamP);
    outpamP->len         = PAM_STRUCT_SIZE(tuple_type);
    outpamP->file        = ofP;
    outpamP->plainformat = 0;

    getCommonInfo(inputFiles, &outpamP->format, &outpamP->depth,
                  &outpamP->maxval, outpamP->tuple_type);

    getOutputWidth(inputFiles,  &outpamP->width);

    getOutputHeight(inputFiles, &outpamP->height);

    if (verbose) {
        pm_message("Output width = %u pixels",  outpamP->width);
        pm_message("Output height = %u pixels", outpamP->height);
    }
}



static void
openInStreams(struct pam         inpam[],
              unsigned int const rank,
              InputFiles   const inputFiles) {
/*----------------------------------------------------------------------------
   Open the input files for a single horizontal slice (there's one file
   for each vertical slice) and read the Netpbm headers from them.  Return
   the pam structures to describe each as inpam[].

   Open the files for horizontal slice number 'rank', as described by
   'inputFiles'.
-----------------------------------------------------------------------------*/
    unsigned int file;

    for (file = 0; file < inputFiles.nFile; ++file) {
        FILE * const ifP = pm_openr(inputFileName(inputFiles, rank, file));

        pnm_readpaminit(ifP, &inpam[file], PAM_STRUCT_SIZE(tuple_type));
    }
}



static void
closeInFiles(struct pam         pam[],
             unsigned int const fileCt) {
/*----------------------------------------------------------------------------
   Close the 'fileCt' input file streams represented by pam[].
-----------------------------------------------------------------------------*/
    unsigned int fileSeq;

    for (fileSeq = 0; fileSeq < fileCt; ++fileSeq)
        pm_close(pam[fileSeq].file);
}



static void
assembleRow(tuple              outputRow[],
            struct pam         inpam[],
            unsigned int const fileCt,
            unsigned int const hOverlap) {
/*----------------------------------------------------------------------------
   Assemble the row outputRow[] from the 'fileCt' input files
   described out inpam[].

   'hOverlap', which is meaningful only when 'fileCt' is greater than 1,
   is the amount by which files overlap each other.  We assume every
   input image is at least that wide.

   We assume that outputRow[] is allocated wide enough to contain the
   entire assembly.
-----------------------------------------------------------------------------*/
    tuple * inputRow;
    unsigned int fileSeq;

    for (fileSeq = 0, inputRow = &outputRow[0]; fileSeq < fileCt; ++fileSeq) {

        unsigned int const overlap = fileSeq == fileCt - 1 ? 0 : hOverlap;

        assert(hOverlap <= inpam[fileSeq].width);

        pnm_readpamrow(&inpam[fileSeq], inputRow);

        inputRow += inpam[fileSeq].width - overlap;
    }
}



static void
allocInpam(unsigned int  const rankCount,
           struct pam ** const inpamArrayP) {

    struct pam * inpamArray;

    MALLOCARRAY(inpamArray, rankCount);

    if (inpamArray == NULL)
        pm_error("Unable to allocate array for %u input pam structures.",
                 rankCount);

    *inpamArrayP = inpamArray;
}




static void
verifyRankFileAttributes(struct pam *       const inpam,
                         unsigned int       const nFile,
                         const struct pam * const outpamP,
                         unsigned int       const hoverlap,
                         unsigned int       const rank) {
/*----------------------------------------------------------------------------
   Verify that the 'nFile' images that make up a rank, which are described
   by inpam[], are consistent with the properties of the assembled image
   *outpamP.

   I.e. verify that each image has the depth, maxval, format, and tuple
   type of *outpamP and their total width is the width given by
   *outpamP.

   Also verify that every image has the same height.

   Abort the program if verification fails.
-----------------------------------------------------------------------------*/
    unsigned int file;
    unsigned int totalWidth;

    for (file = 0, totalWidth = 0; file < nFile; ++file) {
        struct pam * const inpamP = &inpam[file];

        if (inpamP->depth != outpamP->depth)
            pm_error("Rank %u, File %u image has depth %u, "
                     "which differs from others (%u)",
                     rank, file, inpamP->depth, outpamP->depth);
        else if (inpamP->maxval != outpamP->maxval)
            pm_error("Rank %u, File %u image has maxval %lu, "
                     "which differs from others (%lu)",
                     rank, file, inpamP->maxval, outpamP->maxval);
        else if (inpamP->format != outpamP->format)
            pm_error("Rank %u, File %u image has format 0x%x, "
                     "which differs from others (0x%x)",
                     rank, file, inpamP->format, outpamP->format);
        else if (!streq(inpamP->tuple_type, outpamP->tuple_type))
            pm_error("Rank %u, File %u image has tuple type '%s', "
                     "which differs from others ('%s')",
                     rank, file, inpamP->tuple_type, outpamP->tuple_type);

        else if (inpamP->height != inpam[0].height)
            pm_error("Rank %u, File %u image has height %u, "
                     "which differs from that of File 0 in the same rank (%u)",
                     rank, file, inpamP->height, inpam[0].height);
        else {
            totalWidth += inpamP->width;

            if (file < nFile-1)
                totalWidth -= hoverlap;
        }
    }

    if (totalWidth != outpamP->width)
        pm_error("Rank %u has a total width (%u) different from that of "
                 "other ranks (%u)", rank, totalWidth, outpamP->width);
}



static void
assembleTiles(struct pam * const outpamP,
              InputFiles   const inputFiles,
              struct pam         inpam[],
              tuple *      const tuplerow) {

    unsigned int rank;
        /* Number of the current rank (horizontal slice).  Ranks are numbered
           sequentially starting at 0.
        */

    unsigned int const nRank    = inputFiles.nRank;
    unsigned int const nFile    = inputFiles.nFile;
    unsigned int const hoverlap = inputFiles.hoverlap;
    unsigned int const voverlap = inputFiles.voverlap;

    for (rank = 0; rank < nRank; ++rank) {
        unsigned int row;
        unsigned int rankHeight;

        openInStreams(inpam, rank, inputFiles);

        verifyRankFileAttributes(inpam, nFile, outpamP, hoverlap, rank);

        rankHeight = inpam[0].height - (rank == nRank-1 ? 0 : voverlap);

        for (row = 0; row < rankHeight; ++row) {
            assembleRow(tuplerow, inpam, nFile, hoverlap);

            pnm_writepamrow(outpamP, tuplerow);
        }
        closeInFiles(inpam, nFile);
    }
}



int
main(int argc, const char ** argv) {

    struct CmdlineInfo cmdline;
    InputFiles inputFiles;
    struct pam outpam;
    struct pam * inpam;
        /* malloc'ed.  inpam[x] is the pam structure that controls the
           current rank of file x.
        */
    tuple * tuplerow;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    allocInpam(cmdline.across, &inpam);

    if (cmdline.listfileSpec)
        createInFileListFmFile(cmdline.listfile,
                               cmdline.down, cmdline.across,
                               &inputFiles.list);
    else
        createInFileListFmPattern(cmdline.inputFilePattern,
                                  cmdline.down, cmdline.across,
                                  &inputFiles.list);

    inputFiles.nFile    = cmdline.across;
    inputFiles.nRank    = cmdline.down;
    inputFiles.hoverlap = cmdline.hoverlap;
    inputFiles.voverlap = cmdline.voverlap;

    initOutpam(inputFiles, stdout, cmdline.verbose, &outpam);

    tuplerow = pnm_allocpamrow(&outpam);

    pnm_writepaminit(&outpam);

    assembleTiles(&outpam, inputFiles, inpam, tuplerow);

    pnm_freepamrow(tuplerow);
    destroyInFileList(inputFiles.list, inputFiles.nRank, inputFiles.nFile);
    free(inpam);

    return 0;
}
