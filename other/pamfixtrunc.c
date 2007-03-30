/*============================================================================
                             pamfixtrunc
==============================================================================
  Fix a Netpbm image that has been truncated, e.g. by I/O error.

  By Bryan Henderson, January 2007.

  Contributed to the public domain by its author.

============================================================================*/

#include <setjmp.h>

#include "pam.h"
#include "shhopt.h"
#include "mallocvar.h"

struct cmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFilespec;  /* Filespec of input file */
    unsigned int verbose;
};



static void
parseCommandLine(int argc, char ** const argv,
                 struct cmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;

    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */

    OPTENT3(0, "verbose",   OPT_FLAG,    NULL, &cmdlineP->verbose,       0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We don't parms that are negative numbers */

    optParseOptions3(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc-1 == 0) 
        cmdlineP->inputFilespec = "-";
    else if (argc-1 != 1)
        pm_error("Program takes zero or one argument (filename).  You "
                 "specified %d", argc-1);
    else
        cmdlineP->inputFilespec = argv[1];
}



static unsigned int readErrRow;
static bool readErrVerbose;

static pm_usererrormsgfn discardMsg;

static void
discardMsg(const char * const msg) {
    if (readErrVerbose)
        pm_message("Error reading row %u: %s", readErrRow, msg);
}



static void
countRows(const struct pam * const inpamP,
          bool               const verbose,
          unsigned int *     const goodRowCountP) {

    tuple * tuplerow;
    unsigned int row;
    jmp_buf jmpbuf;
    int rc;
    unsigned int goodRowCount;
    
    tuplerow = pnm_allocpamrow(inpamP);

    pm_setusererrormsgfn(discardMsg);

    rc = setjmp(jmpbuf);
    if (rc == 0) {
        pm_setjmpbuf(&jmpbuf);

        readErrVerbose = verbose;
        goodRowCount = 0;  /* initial value */
        for (row = 0; row < inpamP->height; ++row) {
            readErrRow = row;
            pnm_readpamrow(inpamP, tuplerow);
            /* The above does not return if it can't read the next row from
               the file.  Instead, it longjmps out of this loop.
            */
            ++goodRowCount;
        }
    }
    *goodRowCountP = goodRowCount;

    pnm_freepamrow(tuplerow);
}



static void
copyGoodRows(const struct pam * const inpamP,
             FILE *             const ofP,
             unsigned int       const goodRowCount) {

    struct pam outpam;
    tuple * tuplerow;
    unsigned int row;

    outpam = *inpamP;  /* initial value */

    outpam.file = ofP;
    outpam.height = goodRowCount;

    tuplerow = pnm_allocpamrow(inpamP);

    pnm_writepaminit(&outpam);

    for (row = 0; row < outpam.height; ++row) {
        pnm_readpamrow(inpamP, tuplerow);
        pnm_writepamrow(&outpam, tuplerow);
    }
    
    pnm_freepamrow(tuplerow);
}



int
main(int argc, char * argv[]) {
    struct cmdlineInfo cmdline;
    struct pam inpam;
    FILE * ifP;
    pm_filepos rasterPos;
    unsigned int goodRowCount;

    pnm_init(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr_seekable(cmdline.inputFilespec);

    pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    pm_tell2(ifP, &rasterPos, sizeof(rasterPos));

    countRows(&inpam, cmdline.verbose, &goodRowCount);

    pm_message("Copying %u good rows; %u bottom rows missing",
               goodRowCount, inpam.height - goodRowCount);
    
    pm_seek2(ifP, &rasterPos, sizeof(rasterPos));

    copyGoodRows(&inpam, stdout, goodRowCount);

    pm_close(inpam.file);
    
    return 0;
}


