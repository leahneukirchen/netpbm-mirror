/*============================================================================
                                  pamundice
==============================================================================
  Assemble a grid of images into one.

  By Bryan Henderson, San Jose CA 2001.01.31

  Contributed to the public domain.
===========================================================================*/

#include <assert.h>
#include <string.h>
#include <stdbool.h>

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
    const char * indexfile;
    unsigned int indexfileSpec;
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
    OPTENT3(0, "indexfile",   OPT_STRING,  &cmdlineP->indexfile,
            &cmdlineP->indexfileSpec,         0);
    OPTENT3(0, "verbose",     OPT_FLAG,    NULL,
            &cmdlineP->verbose,               0);

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = false;  /* We have no parms that are negative numbers */

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


    if (cmdlineP->indexfileSpec) {
        if (acrossSpec || downSpec)
            pm_error ("You cannot specify -indexfile with -down or -across");
        else if (cmdlineP->listfileSpec)
            pm_error("You cannot specify both -listfile and -indexfile");

    }

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
                       unsigned int   const rankCt,
                       unsigned int   const fileCt,
                       const char *** const inputFileListP) {

    unsigned int const inFileCt = rankCt * fileCt;
    FILE * const lfP = pm_openr(listFile);

    const char ** inputFileList;
    unsigned int fileSeq;

    MALLOCARRAY_NOFAIL(inputFileList, rankCt * fileCt);

    for (fileSeq = 0; fileSeq < inFileCt; ) {
        int eof;
        size_t lineLen;
        char * buf = NULL;   /* initial value */
        size_t bufferSz = 0; /* initial value */

        pm_getline(lfP, &buf, &bufferSz, &eof, &lineLen);

        if (eof)
            pm_error("Premature EOF reading list file.  "
                     "Read %u files.  Should be %u.", fileSeq, inFileCt);
        else if (lineLen > 0)
            inputFileList[fileSeq++] = buf;
    }
    pm_close(lfP);

    *inputFileListP = inputFileList;

}



static void
createInFileListFmIdxFile(const char *   const indexFileNm,
                          const char   * const pattern,
                          unsigned int * const rankCtP,
                          unsigned int * const fileCtP,
                          const char *** const inputFileListP) {

    const char ** inputFileList;
    unsigned int rankCt, fileCt;
    unsigned int rank;
    bool warnedSingleFile;

    FILE * ifP;
    struct pam indexPam;
    tuple * indexRow;   /* Index row buffer */

    ifP = pm_openr(indexFileNm);

    pnm_readpaminit(ifP, &indexPam, PAM_STRUCT_SIZE(tuple_type));

    rankCt = indexPam.height;
    fileCt = indexPam.width;

    indexRow = pnm_allocpamrow(&indexPam);

    if (indexPam.depth < 2)
        pm_error("Insufficient number of planes (%u) in index file.  "
                 "Need %u.", indexPam.depth, 2);

    MALLOCARRAY_NOFAIL(inputFileList, rankCt * fileCt);

    for (rank = 0, warnedSingleFile = false; rank < rankCt; ++rank) {
        unsigned int file;

         pnm_readpamrow(&indexPam, indexRow);

         for (file = 0; file < fileCt; ++file) {
             unsigned int const idx = rank * fileCt + file;

             bool fileNmIsRankFileIndependent;

             computeInputFileName(pattern,
                                  indexRow[file][0],
                                  indexRow[file][1],
                                  &inputFileList[idx],
                                  &fileNmIsRankFileIndependent);

             if (fileNmIsRankFileIndependent && !warnedSingleFile) {
                 pm_message("Warning: No grid location (%%a/%%d) specified "
                            "in input file pattern '%s'.  "
                            "Input is single file.  ",
                            pattern);
                 warnedSingleFile = true;
             }
         }
    }

    pnm_freepamrow(indexRow);
    pm_close(ifP);

    *inputFileListP = inputFileList;
    *rankCtP        = rankCt;
    *fileCtP        = fileCt;
}



static void
createInFileListFmPattern(const char  *  const pattern,
                          unsigned int   const rankCt,
                          unsigned int   const fileCt,
                          const char *** const inputFileListP) {

    const char ** inputFileList;
    unsigned int rank, file;
    bool warnedSingleFile;

    MALLOCARRAY_NOFAIL(inputFileList, rankCt * fileCt);

    for (rank = 0, warnedSingleFile = false; rank < rankCt ; ++rank) {
         for (file = 0; file < fileCt ; ++file) {
             const unsigned int idx = rank * fileCt + file;

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
                  unsigned int  const rankCt,
                  unsigned int  const fileCt) {

    unsigned int const inFileCt = rankCt * fileCt;

    unsigned int fileSeq;

    for (fileSeq = 0; fileSeq < inFileCt; ++fileSeq)
        pm_strfree(inputFileList[fileSeq]);

    free(inputFileList);
}



typedef struct {
    unsigned int  rankCt;    /* Number of images in the vertical direction */
    unsigned int  fileCt;    /* Number of images in the horizontal direction */
    unsigned int  hoverlap;  /* horizontal overlap */
    unsigned int  voverlap;  /* vertical overlap */
    const char ** list;
        /* List (1-dimensional array) of filenames
           Row-major, top to bottom, left to right
        */
} InputFiles;



static const char *
inputFileName(InputFiles   const inputFiles,
              unsigned int const rank,
              unsigned int const file) {
/*----------------------------------------------------------------------------
    A selected entry from "inputFiles.list" based on "rank" and "file".

    Currently we assume that the list is a one-dimensional represetation
    of an array, row-major, top to bottom and left to right in each row.
----------------------------------------------------------------------------*/
    assert(rank < inputFiles.rankCt);
    assert(file < inputFiles.fileCt);

    return inputFiles.list[rank * inputFiles.fileCt + file];
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
   Get the output width by adding up the widths of all 'inputFiles.fileCt'
   images of the top rank, and allowing for overlap of 'inputFiles.hoverlap'
   pixels.
-----------------------------------------------------------------------------*/
    double       totalWidth;
    unsigned int file;

    for (file = 0, totalWidth = 0; file < inputFiles.fileCt; ++file) {
        struct pam inpam;

        getImageInfo(inputFiles, 0, file, &inpam);

        if (inpam.width < inputFiles.hoverlap)
            pm_error("Rank 0, file %u image has width %u, "
                     "which is less than the horizontal overlap of %u pixels",
                     file, inpam.width, inputFiles.hoverlap);
        else {
            totalWidth += inpam.width;

            if (file < inputFiles.fileCt-1)
                totalWidth -= inputFiles.hoverlap;
        }
    }
    *widthP = (int) totalWidth;
}



static void
getOutputHeight(InputFiles const inputFiles,
                int *      const heightP) {
/*----------------------------------------------------------------------------
   Get the output height by adding up the widths of all 'inputFiles.rankCt'
   images of the left file, and allowing for overlap of 'inputFiles.voverlap'
   pixels.
-----------------------------------------------------------------------------*/
    double       totalHeight;
    unsigned int rank;

    for (rank = 0, totalHeight = 0; rank < inputFiles.rankCt; ++rank) {
        struct pam inpam;

        getImageInfo(inputFiles, rank, 0, &inpam);

        if (inpam.height < inputFiles.voverlap)
            pm_error("Rank %u, file 0 image has height %u, "
                     "which is less than the vertical overlap of %u pixels",
                     rank, inpam.height, inputFiles.voverlap);

        totalHeight += inpam.height;

        if (rank < inputFiles.rankCt-1)
            totalHeight -= inputFiles.voverlap;
    }
    *heightP = (int) totalHeight;
}



static InputFiles
inputFileList(const char * const indexFileNm,
              const char * const listFileNm,
              const char * const inputFilePattern,
              unsigned int const across,
              unsigned int const down,
              unsigned int const hoverlap,
              unsigned int const voverlap) {

    InputFiles inputFiles;

    if (indexFileNm) {
        createInFileListFmIdxFile(indexFileNm, inputFilePattern,
                                  &inputFiles.rankCt,
                                  &inputFiles.fileCt,
                                  &inputFiles.list);
    } else {
        inputFiles.fileCt = across;
        inputFiles.rankCt = down;

        if (listFileNm)
            createInFileListFmFile(listFileNm, down, across,
                                   &inputFiles.list);
        else
            createInFileListFmPattern(inputFilePattern, down, across,
                                      &inputFiles.list);
    }

    inputFiles.hoverlap = hoverlap;
    inputFiles.voverlap = voverlap;

    return inputFiles;
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
    assert(inputFiles.fileCt >= 1);
    assert(inputFiles.rankCt >= 1);

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
openInStreams(struct pam * const inpam,  /* array */
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

    for (file = 0; file < inputFiles.fileCt; ++file) {
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
allocInpam(InputFiles    const inputFiles,
           struct pam ** const inpamArrayP) {

    struct pam * inpamArray;

    MALLOCARRAY(inpamArray, inputFiles.fileCt);

    if (inpamArray == NULL)
        pm_error("Unable to allocate array for %u input pam structures.",
                 inputFiles.fileCt);

    *inpamArrayP = inpamArray;
}




static void
verifyRankFileAttributes(struct pam *       const inpam,
                         unsigned int       const fileCt,
                         const struct pam * const outpamP,
                         unsigned int       const hoverlap,
                         unsigned int       const rank) {
/*----------------------------------------------------------------------------
   Verify that the 'fileCt' images that make up a rank, which are described
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

    for (file = 0, totalWidth = 0; file < fileCt; ++file) {
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

            if (file < fileCt-1)
                totalWidth -= hoverlap;
        }
    }

    if (totalWidth != outpamP->width)
        pm_error("Rank %u has a total width (%u) different from that of "
                 "other ranks (%u)", rank, totalWidth, outpamP->width);
}



static void
assembleRowPbm(unsigned char *    const bitrow,
               const struct pam * const inpam,  /* array */
               unsigned int       const fileCt,
               unsigned int       const hOverlap) {
/*----------------------------------------------------------------------------
   Assemble the row outputRow[] from the 'fileCt' input files described out
   inpam[].

   'hOverlap', which is meaningful only when 'fileCt' is greater than 1, is
   the amount by which files overlap each other.  We assume every input image
   is at least that wide.

   Use 'bitrow' as a scratch buffer to assemble the row.  It is large enough
   to contain the entire assembly.
-----------------------------------------------------------------------------*/
    unsigned int fileSeq;
    unsigned int offset;

    for (fileSeq = offset = 0; fileSeq < fileCt; ++fileSeq) {

        assert(fileCt > 0);

        unsigned int const overlap = fileSeq == fileCt - 1 ? 0 : hOverlap;

        assert(hOverlap <= inpam[fileSeq].width);

        pbm_readpbmrow_bitoffset(inpam[fileSeq].file, bitrow,
                                 inpam[fileSeq].width,
                                 inpam[fileSeq].format, offset);

        offset += inpam[fileSeq].width - overlap;
    }
}



static void
assembleTilesPbm(const struct pam * const outpamP,
                 InputFiles         const inputFiles,
                 struct pam *       const inpam,  /* array */
                 unsigned char *    const bitrow) {

    unsigned int rank;
        /* Number of the current rank (horizontal slice).  Ranks are numbered
           sequentially starting at 0.
        */

    unsigned int const rankCt   = inputFiles.rankCt;
    unsigned int const fileCt   = inputFiles.fileCt;
    unsigned int const hoverlap = inputFiles.hoverlap;
    unsigned int const voverlap = inputFiles.voverlap;

    for (rank = 0; rank < rankCt; ++rank) {
        unsigned int row;
        unsigned int rankHeight;

        openInStreams(inpam, rank, inputFiles);

        verifyRankFileAttributes(inpam, fileCt, outpamP, hoverlap, rank);

        rankHeight = inpam[0].height - (rank == rankCt-1 ? 0 : voverlap);

        for (row = 0; row < rankHeight; ++row) {
            assembleRowPbm(bitrow, inpam, fileCt, hoverlap);
            pbm_writepbmrow_packed(stdout, bitrow, outpamP->width, 0);
        }
        closeInFiles(inpam, fileCt);
    }
}



static void
assembleRow(tuple              outputRow[],
            struct pam         inpam[],
            unsigned int const fileCt,
            unsigned int const hOverlap) {
/*----------------------------------------------------------------------------
   Assemble the row outputRow[] from the 'fileCt' input files described out
   inpam[].

   'hOverlap', which is meaningful only when 'fileCt' is greater than 1, is
   the amount by which files overlap each other.  We assume every input image
   is at least that wide.

   We assume that outputRow[] is allocated wide enough to contain the entire
   assembly.
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
assembleTiles(struct pam * const outpamP,
              InputFiles   const inputFiles,
              struct pam * const inpam,  /* array */
              tuple *      const tuplerow) {

    unsigned int rank;
        /* Number of the current rank (horizontal slice).  Ranks are numbered
           sequentially starting at 0.
        */

    unsigned int const rankCt   = inputFiles.rankCt;
    unsigned int const fileCt   = inputFiles.fileCt;
    unsigned int const hoverlap = inputFiles.hoverlap;
    unsigned int const voverlap = inputFiles.voverlap;

    for (rank = 0; rank < rankCt; ++rank) {
        unsigned int row;
        unsigned int rankHeight;

        openInStreams(inpam, rank, inputFiles);

        verifyRankFileAttributes(inpam, fileCt, outpamP, hoverlap, rank);

        rankHeight = inpam[0].height - (rank == rankCt-1 ? 0 : voverlap);

        for (row = 0; row < rankHeight; ++row) {
            assembleRow(tuplerow, inpam, fileCt, hoverlap);

            pnm_writepamrow(outpamP, tuplerow);
        }
        closeInFiles(inpam, fileCt);
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

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    inputFiles =
        inputFileList(cmdline.indexfileSpec ? cmdline.indexfile : NULL,
                      cmdline.listfileSpec ? cmdline.listfile : NULL,
                      cmdline.inputFilePattern,
                      cmdline.across, cmdline.down,
                      cmdline.hoverlap, cmdline.voverlap);

    allocInpam(inputFiles, &inpam);

    initOutpam(inputFiles, stdout, cmdline.verbose, &outpam);

    pnm_writepaminit(&outpam);

    if (PNM_FORMAT_TYPE(outpam.format) == PBM_TYPE) {
        unsigned char * const bitrow = pbm_allocrow_packed(outpam.width);
        bitrow[pbm_packed_bytes(outpam.width) - 1] = 0x00;

        assembleTilesPbm(&outpam, inputFiles, inpam, bitrow);
        pnm_freerow(bitrow);

      } else {
        tuple * const tuplerow = pnm_allocpamrow(&outpam);
        assembleTiles(&outpam, inputFiles, inpam, tuplerow);
        pnm_freepamrow(tuplerow);
      }

    destroyInFileList(inputFiles.list, inputFiles.rankCt, inputFiles.fileCt);
    free(inpam);

    return 0;
}



