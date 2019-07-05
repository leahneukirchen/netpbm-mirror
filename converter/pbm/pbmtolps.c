/*
 * pbmtolps -- convert a Portable BitMap into Postscript.  The
 * output Postscript uses lines instead of the image operator to
 * generate a (device dependent) picture which will be imaged
 * much faster.
 *
 * The Postscript path length is constrained to be at most 1000
 * points so that no limits are overrun on the Apple Laserwriter
 * and (presumably) no other printers.  The typical limit is 1500.
 * See "4.4 Path Construction" and "Appendix B: Implementation Limits"
 * in: PostScript Language Reference Manual
 *     https://www.adobe.com/content/dam/acom/en/devnet/actionscript/
 *             articles/psrefman.pdf
 *
 * To do:
 *      make sure encapsulated format is correct
 *      repitition of black-white strips
 *      make it more device independent (is this possible?)
 *
 * Author:
 *      George Phillips <phillips@cs.ubc.ca>
 *      Department of Computer Science
 *      University of British Columbia
 */

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pbm.h"


static float const MaxDPI = 5000;
static float const MinDPI = 10;
static unsigned int const MaxPathPoints = 1000;


struct CmdlineInfo {
    /* All the information the user supplied in the command line, in a form
       easy for the program to use.
    */
    const char * inputFileName;  /* File name of input file */
    unsigned int inputFileSpec;  /* Input file name specified */
    float        lineWidth;      /* Line width, if specified */
    unsigned int lineWidthSpec;  /* Line width specified */
    float        dpi;            /* Resolution in DPI, if specified */
    unsigned int dpiSpec;        /* Resolution specified */
};



static void
parseCommandLine(int                        argc,
                 const char **        const argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.
-----------------------------------------------------------------------------*/
    optEntry * option_def;  /* malloc'ed */
        /* Instructions to OptParseOptions3 on how to parse our options.  */
    optStruct3 opt;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "linewidth", OPT_FLOAT, &cmdlineP->lineWidth,
                            &cmdlineP->lineWidthSpec,    0);
    OPTENT3(0, "dpi",       OPT_FLOAT,  &cmdlineP->dpi,
                            &cmdlineP->dpiSpec,          0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else {
        if (argc-1 > 1)
            pm_error("Program takes zero or one argument (filename).  You "
                     "specified %d", argc-1);
        else                      
            cmdlineP->inputFileName = argv[1];                     
    }

    if (cmdlineP->inputFileName[0] == '-' &&
        cmdlineP->inputFileName[1] == '\0')
        cmdlineP->inputFileSpec = FALSE;
    else 
        cmdlineP->inputFileSpec = TRUE;

    if (cmdlineP->dpiSpec == FALSE)
        cmdlineP->dpi = 300;

    free(option_def);
}



static void
validateLineWidth(float const sc_cols,
                  float const sc_rows,
                  float const lineWidth) {

    if (lineWidth >= sc_cols || lineWidth >= sc_rows)
        pm_error("Absurdly large -linewidth value (%f)", lineWidth);
}



static void
validateDpi(float const dpi) {
    if (dpi > MaxDPI || dpi < MinDPI)
        pm_error("Specified DPI value out of range (%f)", dpi);
}



int
main(int argc, const char *argv[]) {
    FILE*   fp;
    bit*    bits;
    int     row;
    int     col;
    int     rows;
    int     cols;
    int     format;
    int     white;
    int     black;
    float   sc_rows;
    float   sc_cols;
    struct CmdlineInfo cmdline;
    const char* psName;
    unsigned int pointCnt;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);
    
    if (cmdline.dpi)
        validateDpi(cmdline.dpi); 

    fp = pm_openr(cmdline.inputFileName);
    pbm_readpbminit(fp, &cols, &rows, &format);

    sc_rows = (float) rows / cmdline.dpi * 72.0;
    sc_cols = (float) cols / cmdline.dpi * 72.0;
    
    if (cmdline.lineWidthSpec)
        validateLineWidth(sc_cols, sc_rows, cmdline.lineWidth); 

    bits = pbm_allocrow(cols);
    psName = cmdline.inputFileSpec ? cmdline.inputFileName : "noname";

    puts("%!PS-Adobe-2.0 EPSF-2.0");
    puts("%%Creator: pbmtolps");
    printf("%%%%Title: %s\n", psName);
    printf("%%%%BoundingBox: %d %d %d %d\n",
           (int)(305.5 - sc_cols / 2.0),
           (int)(395.5 - sc_rows / 2.0),
           (int)(306.5 + sc_cols / 2.0),
           (int)(396.5 + sc_rows / 2.0));
    puts("%%EndComments");
    puts("%%EndProlog");
    puts("gsave");

    printf("%f %f translate\n", 306.0 - sc_cols / 2.0, 396.0 + sc_rows / 2.0);
    printf("72 %f div dup neg scale\n", cmdline.dpi);
    if (cmdline.lineWidthSpec)
        printf("%f setlinewidth\n", cmdline.lineWidth); 
    puts("/a { 0 rmoveto 0 rlineto } def");
    puts("/m { currentpoint stroke newpath moveto } def");
    puts("newpath 0 0 moveto");

    pointCnt = 0;
    for (row = 0; row < rows; row++) {
        bool firstRun = TRUE; /* Initial value */        
                
        pbm_readpbmrow(fp, bits, cols, format);
        /* output white-strip + black-strip sequences */
        for (col = 0; col < cols; ) {
        
            for (white = 0; col < cols && bits[col] == PBM_WHITE; col++)
                white++;
            for (black = 0; col < cols && bits[col] == PBM_BLACK; col++)
                black++;

            if (black != 0) {
                if (pointCnt > MaxPathPoints) {
                     printf("m ");
		     pointCnt = 0;
		}

                if (firstRun == TRUE) {
                    printf ("%d %d moveto %d 0 rlineto\n", white, row, black);
                    firstRun = FALSE;
                }
                else
                    printf("%d %d a\n", black, white);

                pointCnt += 2;
            }
        }
    }
    puts("stroke grestore showpage");
    puts("%%Trailer");

    pm_close(fp);

    exit(0);
}
