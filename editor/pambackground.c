#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pam.h"

struct cmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  
    unsigned int verbose;
};



static void
parseCommandLine(int argc, char ** const argv,
                 struct cmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry *option_def;
        /* Instructions to OptParseOptions2 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "verbose",    OPT_FLAG,   NULL, &cmdlineP->verbose,        0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    optParseOptions3(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else {
        cmdlineP->inputFileName = argv[1];
        if (argc-1 > 1)
            pm_error("There is at most one argument:  input file name.  "
                     "You specified %d", argc-1);
    }
}        



static void
initOutpam(const struct pam * const inpamP,
           struct pam *       const outpamP) {

    outpamP->file             = stdout;
    outpamP->format           = PAM_FORMAT;
    outpamP->plainformat      = 0;
    outpamP->width            = inpamP->width;
    outpamP->height           = inpamP->height;
    outpamP->depth            = 1;
    outpamP->maxval           = 1;
    outpamP->bytes_per_sample = pnm_bytespersample(outpamP->maxval);
    outpamP->len              = PAM_STRUCT_SIZE(bytes_per_sample);
    outpamP->size             = sizeof(*outpamP);
}



static void
allocateOutputPointerRow(unsigned int const width,
                         tuple **     const tuplerowP) {

    MALLOCARRAY(*tuplerowP, width);

    if (*tuplerowP == NULL)
        pm_error("Could not allocate a %u-column tuple pointer array", width);
}



static void
createWhiteTuple(const struct pam * const pamP, 
                 tuple *            const whiteTupleP) {
/*----------------------------------------------------------------------------
   Create a "white" tuple.  By that we mean a tuple all of whose elements
   are zero.  If it's an RGB, grayscale, or b&w pixel, that means it's black.
-----------------------------------------------------------------------------*/
    tuple whiteTuple;
    unsigned int plane;

    whiteTuple = pnm_allocpamtuple(pamP);

    for (plane = 0; plane < pamP->depth; ++plane)
        whiteTuple[plane] = pamP->maxval;

    *whiteTupleP = whiteTuple;
}



static void
selectBackground(struct pam * const pamP,
                 tuple        const ul,
                 tuple        const ur,
                 tuple        const lr,
                 tuple        const ll,
                 tuple *      const bgColorP) {

    tuple bg;  /* Reference to one of ul, ur, ll, lr */

    if (pnm_tupleequal(pamP, ul, ur)) {
        if (pnm_tupleequal(pamP, ll, ul))
            bg = ul;
        else if (pnm_tupleequal(pamP, lr, ul))
            bg = ul;
    } else if (pnm_tupleequal(pamP, ll, lr)) {
        if (pnm_tupleequal(pamP, ul, ll))
            bg = ll;
        else if (pnm_tupleequal(pamP, ur, ll))
            bg = ll;
    } else {
        /* No 3 corners are same color; look for 2 corners */
        if (pnm_tupleequal(pamP, ul, ur))  /* top edge */
            bg = ul;
        else if (pnm_tupleequal(pamP, ul, ll)) /* left edge */
            bg = ul;
        else if (pnm_tupleequal(pamP, ur, lr)) /* right edge */
            bg = ur;
        else if (pnm_tupleequal(pamP, ll, lr)) /* bottom edge */
            bg = ll;
        else {
            /* No two corners are same color; just use upper left corner */
            bg = ul;
        }
    }
    
    *bgColorP = pnm_allocpamtuple(pamP);
    pnm_assigntuple(pamP, *bgColorP, bg);
}



static void
computeBackground(struct pam * const pamP,
                  bool         const verbose,
                  tuple *      const bgColorP) {
/*----------------------------------------------------------------------------
   Determine what color is the background color of the image in the
   file represented by *pamP.

   Expect the file to be positioned to the start of the raster, and leave
   it positioned arbitrarily.
-----------------------------------------------------------------------------*/
    unsigned int row;
    tuple * tuplerow;
    tuple ul, ur, ll, lr;
        /* Color of upper left, upper right, lower left, lower right */

    tuplerow  = pnm_allocpamrow(pamP);
    ul = pnm_allocpamtuple(pamP);
    ur = pnm_allocpamtuple(pamP);
    ll = pnm_allocpamtuple(pamP);
    lr = pnm_allocpamtuple(pamP);

    pnm_readpamrow(pamP, tuplerow);

    pnm_assigntuple(pamP, ul, tuplerow[0]);
    pnm_assigntuple(pamP, ur, tuplerow[pamP->width-1]);

    for (row = 1; row < pamP->height; ++row)
        pnm_readpamrow(pamP, tuplerow);

    pnm_assigntuple(pamP, ll, tuplerow[0]);
    pnm_assigntuple(pamP, lr, tuplerow[pamP->width-1]);

    selectBackground(pamP, ul, ur, ll, lr, bgColorP);

    if (verbose) {
        int const hexokTrue = 1;
        const char * const colorname =
            pnm_colorname(pamP, *bgColorP, hexokTrue);
        pm_message("Background color is %s", colorname);

        strfree(colorname);
    }

    pnm_freepamtuple(lr);
    pnm_freepamtuple(ll);
    pnm_freepamtuple(ur);
    pnm_freepamtuple(ul);
    pnm_freepamrow(tuplerow);
}



static void
computeOutputRow(struct pam * const inpamP,
                 tuple *      const inputTuplerow,
                 tuple        const backgroundColor,
                 struct pam * const outpamP,
                 tuple *      const outputTuplerow,
                 tuple        const foreground,
                 tuple        const background) {

    unsigned int col;
    unsigned int firstForegroundCol;
        // Column number of first column, going from the left, that is
        // not the background color.
    unsigned int endForegroundCol;
        // Column number of the last column, going from the right,
        // that is the background color.

    col = 0;
    while (col < inpamP->width &&
           pnm_tupleequal(inpamP, inputTuplerow[col], backgroundColor))
        ++col;

    firstForegroundCol = col;

    col = inpamP->width;
    while (col > firstForegroundCol && 
           pnm_tupleequal(inpamP, inputTuplerow[col-1], backgroundColor))
        --col;

    endForegroundCol = col;

    // If the row is all background, 'firstForegroundCol' and
    // 'endForegroundCol' are both one past the right edge.
            
    for (col = 0; col < firstForegroundCol; ++col)
        outputTuplerow[col] = background;

    for (col = firstForegroundCol; col < endForegroundCol; ++col)
        outputTuplerow[col] = foreground;

    for (col = endForegroundCol; col < outpamP->width; ++col)
        outputTuplerow[col] = background;
}



int
main(int argc, char *argv[]) {

    struct cmdlineInfo cmdline;
    struct pam inpam;
    struct pam outpam;
    FILE * ifP;
    tuple * inputTuplerow;
    tuple * outputTuplerow;
        // Not a regular tuple row -- just pointer array
    unsigned int row;
    pm_filepos rasterpos;
    tuple black, white;
    tuple backgroundColor;
    
    pnm_init(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr_seekable(cmdline.inputFileName);

    pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    pm_tell2(ifP, &rasterpos, sizeof(rasterpos));

    computeBackground(&inpam, cmdline.verbose, &backgroundColor);

    initOutpam(&inpam, &outpam);

    inputTuplerow  = pnm_allocpamrow(&inpam);

    allocateOutputPointerRow(outpam.width, &outputTuplerow);
    pnm_createBlackTuple(&outpam, &black);
    createWhiteTuple(&outpam, &white);

    pnm_writepaminit(&outpam);

    pm_seek2(ifP, &rasterpos, sizeof(rasterpos));

    for (row = 0; row < outpam.height; ++row) {
        pnm_readpamrow(&inpam, inputTuplerow);

        computeOutputRow(&inpam, inputTuplerow, backgroundColor,
                         &outpam, outputTuplerow, black, white);

        pnm_writepamrow(&outpam, outputTuplerow);
    }

    pm_close(ifP);

    pnm_freepamrow(inputTuplerow);
    free(outputTuplerow);
    pnm_freepamtuple(backgroundColor);
    pnm_freepamtuple(white);
    pnm_freepamtuple(black);
    
    return 0;
}
