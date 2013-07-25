/*----------------------------------------------------------------------------
                                 pstopnm
------------------------------------------------------------------------------
  Use Ghostscript to convert a Postscript file into a PBM, PGM, or PNM
  file.

  Implementation note: This program feeds the input file to Ghostcript
  directly (with possible statements preceding it), and uses
  Ghostscript's PNM output device drivers.  As an alternative,
  Ghostscript also comes with the Postscript program pstoppm.ps which
  we could run and it would read the input file and produce PNM
  output.  It isn't clear to me what pstoppm.ps adds to what you get
  from just feeding your input directly to Ghostscript as the main program.

-----------------------------------------------------------------------------*/

#define _BSD_SOURCE 1   /* Make sure strdup() is in string.h */
#define _XOPEN_SOURCE 500  
    /* Make sure fdopen() is in stdio.h and strdup() is in string.h */

#include <assert.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <sys/wait.h>  
#include <sys/stat.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "pnm.h"
#include "shhopt.h"
#include "nstring.h"

enum Orientation {PORTRAIT, LANDSCAPE, UNSPECIFIED};
struct Box {
    /* Description of a rectangle within an image; all coordinates 
       measured in points (1/72") with lower left corner of page being the 
       origin.
    */
    int llx;  /* lower left X coord */
        /* -1 for llx means whole box is undefined. */
    int lly;  /* lower left Y coord */
    int urx;  /* upper right X coord */
    int ury;  /* upper right Y coord */
};

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* Names of input files */
    unsigned int forceplain;
    struct Box extractBox;
    unsigned int nocrop;
    unsigned int formatType;
    unsigned int verbose;
    float xborder;
    unsigned int xmax;
    unsigned int xsize;  /* zero means unspecified */
    float yborder;
    unsigned int ymax;
    unsigned int ysize;  /* zero means unspecified */
    unsigned int dpi;    /* zero means unspecified */
    enum Orientation orientation;
    unsigned int stdout;
    unsigned int textalphabits;
};


static void
parseCommandLine(int argc, char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    unsigned int pbmOpt, pgmOpt, ppmOpt;
    unsigned int portraitOpt, landscapeOpt;
    float llx, lly, urx, ury;
    unsigned int llxSpec, llySpec, urxSpec, urySpec;
    unsigned int xmaxSpec, ymaxSpec, xsizeSpec, ysizeSpec, dpiSpec;
    unsigned int textalphabitsSpec;

    MALLOCARRAY_NOFAIL(option_def, 100);
    
    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "forceplain", OPT_FLAG,  NULL, &cmdlineP->forceplain,     0);
    OPTENT3(0, "llx",        OPT_FLOAT, &llx, &llxSpec,                  0);
    OPTENT3(0, "lly",        OPT_FLOAT, &lly, &llySpec,                  0);
    OPTENT3(0, "urx",        OPT_FLOAT, &urx, &urxSpec,                  0);
    OPTENT3(0, "ury",        OPT_FLOAT, &ury, &urySpec,                  0);
    OPTENT3(0, "nocrop",     OPT_FLAG,  NULL, &cmdlineP->nocrop,         0);
    OPTENT3(0, "pbm",        OPT_FLAG,  NULL, &pbmOpt ,                  0);
    OPTENT3(0, "pgm",        OPT_FLAG,  NULL, &pgmOpt,                   0);
    OPTENT3(0, "ppm",        OPT_FLAG,  NULL, &ppmOpt,                  0);
    OPTENT3(0, "verbose",    OPT_FLAG,  NULL, &cmdlineP->verbose,        0);
    OPTENT3(0, "xborder",    OPT_FLOAT, &cmdlineP->xborder, NULL,        0);
    OPTENT3(0, "xmax",       OPT_UINT,  &cmdlineP->xmax, &xmaxSpec,      0);
    OPTENT3(0, "xsize",      OPT_UINT,  &cmdlineP->xsize, &xsizeSpec,    0);
    OPTENT3(0, "yborder",    OPT_FLOAT, &cmdlineP->yborder, NULL,        0);
    OPTENT3(0, "ymax",       OPT_UINT,  &cmdlineP->ymax, &ymaxSpec,      0);
    OPTENT3(0, "ysize",      OPT_UINT,  &cmdlineP->ysize, &ysizeSpec,    0);
    OPTENT3(0, "dpi",        OPT_UINT,  &cmdlineP->dpi, &dpiSpec,        0);
    OPTENT3(0, "portrait",   OPT_FLAG,  NULL, &portraitOpt,             0);
    OPTENT3(0, "landscape",  OPT_FLAG,  NULL, &landscapeOpt,            0);
    OPTENT3(0, "stdout",     OPT_FLAG,  NULL, &cmdlineP->stdout,         0);
    OPTENT3(0, "textalphabits", OPT_UINT,
            &cmdlineP->textalphabits,  &textalphabitsSpec, 0);

    /* Set the defaults */
    cmdlineP->xborder = cmdlineP->yborder = 0.1;

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (xmaxSpec) {
        if (cmdlineP->xmax == 0)
            pm_error("zero is not a valid value for -xmax");
    } else
        cmdlineP->xmax = 612;

    if (ymaxSpec) {
        if (cmdlineP->ymax == 0)
            pm_error("zero is not a valid value for -ymax");
    } else 
        cmdlineP->ymax = 792;

    if (xsizeSpec) {
        if (cmdlineP->xsize == 0)
            pm_error("zero is not a valid value for -xsize");
    } else
        cmdlineP->xsize = 0;

    if (ysizeSpec) {
        if (cmdlineP->ysize == 0)
            pm_error("zero is not a valid value for -ysize");
    } else 
        cmdlineP->ysize = 0;

    if (portraitOpt && !landscapeOpt)
        cmdlineP->orientation = PORTRAIT;
    else if (!portraitOpt && landscapeOpt)
        cmdlineP->orientation = LANDSCAPE;
    else if (!portraitOpt && !landscapeOpt)
        cmdlineP->orientation = UNSPECIFIED;
    else
        pm_error("Cannot specify both -portrait and -landscape options");

    if (pbmOpt)
        cmdlineP->formatType = PBM_TYPE;
    else if (pgmOpt)
        cmdlineP->formatType = PGM_TYPE;
    else if (ppmOpt)
        cmdlineP->formatType = PPM_TYPE;
    else
        cmdlineP->formatType = PPM_TYPE;

    /* If any one of the 4 bounding box coordinates is given on the
       command line, we default any of the 4 that aren't.  
    */
    if (llxSpec || llySpec || urxSpec || urySpec) {
        if (!llxSpec) cmdlineP->extractBox.llx = 72;
        else cmdlineP->extractBox.llx = llx * 72;
        if (!llySpec) cmdlineP->extractBox.lly = 72;
        else cmdlineP->extractBox.lly = lly * 72;
        if (!urxSpec) cmdlineP->extractBox.urx = 540;
        else cmdlineP->extractBox.urx = urx * 72;
        if (!urySpec) cmdlineP->extractBox.ury = 720;
        else cmdlineP->extractBox.ury = ury * 72;
    } else {
        cmdlineP->extractBox.llx = -1;
    }

    if (dpiSpec) {
        if (cmdlineP->dpi == 0)
            pm_error("Zero is not a valid value for -dpi");
    } else
        cmdlineP->dpi = 0;

    if (dpiSpec && xsizeSpec + ysizeSpec + xmaxSpec + ymaxSpec > 0)
        pm_error("You may not specify both size options and -dpi");

    if (textalphabitsSpec) {
        if (cmdlineP->textalphabits != 1 && cmdlineP->textalphabits != 2
            && cmdlineP->textalphabits != 4) {
            /* Pstopnm won't take this value, and we don't want to inflict
               a Pstopnm failure error message on the user.
            */
            pm_error("Valid values for -textalphabits are 1, 2, and 4.  "
                     "You specified %u", cmdlineP->textalphabits );
        }
    } else
        cmdlineP->textalphabits = 4;

    if (argc-1 == 0)
        cmdlineP->inputFileName = "-";  /* stdin */
    else if (argc-1 == 1)
        cmdlineP->inputFileName = argv[1];
    else 
        pm_error("Too many arguments (%d).  "
                 "Only need one: the Postscript file name", argc-1);

    free(option_def);
}



static void
addPsToFileName(char          const origFileName[],
                const char ** const newFileNameP,
                bool          const verbose) {
/*----------------------------------------------------------------------------
   If origFileName[] does not name an existing file, but the same
   name with ".ps" added to the end does, return the name with the .ps
   attached.  Otherwise, just return origFileName[].

   Return the name in newly malloc'ed storage, pointed to by
   *newFileNameP.
-----------------------------------------------------------------------------*/
    struct stat statbuf;
    int statRc;

    statRc = lstat(origFileName, &statbuf);
    
    if (statRc == 0)
        *newFileNameP = strdup(origFileName);
    else {
        const char * fileNamePlusPs;

        pm_asprintf(&fileNamePlusPs, "%s.ps", origFileName);

        statRc = lstat(fileNamePlusPs, &statbuf);
        if (statRc == 0)
            *newFileNameP = strdup(fileNamePlusPs);
        else
            *newFileNameP = strdup(origFileName);
        pm_strfree(fileNamePlusPs);
    }
    if (verbose)
        pm_message("Input file is %s", *newFileNameP);
}



static void
computeSizeResFromSizeSpec(unsigned int   const requestedXsize,
                           unsigned int   const requestedYsize,
                           unsigned int   const imageWidth,
                           unsigned int   const imageHeight,
                           unsigned int * const xsizeP,
                           unsigned int * const ysizeP,
                           unsigned int * const xresP,
                           unsigned int * const yresP) {

    if (requestedXsize) {
        *xsizeP = requestedXsize;
        *xresP = (unsigned int) (requestedXsize * 72 / imageWidth + 0.5);
        if (!requestedYsize) {
            *yresP = *xresP;
            *ysizeP = (unsigned int) (imageHeight * (float)*yresP/72 + 0.5);
            }
        }

    if (requestedYsize) {
        *ysizeP = requestedYsize;
        *yresP = (unsigned int) (requestedYsize * 72 / imageHeight + 0.5);
        if (!requestedXsize) {
            *xresP = *yresP;
            *xsizeP = (unsigned int) (imageWidth * (float)*xresP/72 + 0.5);
        }
    } 
}



static void
computeSizeResBlind(unsigned int   const xmax,
                    unsigned int   const ymax,
                    unsigned int   const imageWidth,
                    unsigned int   const imageHeight,
                    bool           const nocrop,
                    unsigned int * const xsizeP,
                    unsigned int * const ysizeP,
                    unsigned int * const xresP,
                    unsigned int * const yresP) {

    *xresP = *yresP = MIN(xmax * 72 / imageWidth, 
                          ymax * 72 / imageHeight);
    
    if (nocrop) {
        *xsizeP = xmax;
        *ysizeP = ymax;
    } else {
        *xsizeP = (unsigned int) (imageWidth * (float)*xresP / 72 + 0.5);
        *ysizeP = (unsigned int) (imageHeight * (float)*yresP / 72 + 0.5);
    }
}



static void
computeSizeRes(struct CmdlineInfo const cmdline, 
               enum Orientation   const orientation, 
               struct Box         const borderedBox,
               unsigned int *     const xsizeP, 
               unsigned int *     const ysizeP,
               unsigned int *     const xresP, 
               unsigned int *     const yresP) {
/*----------------------------------------------------------------------------
  Figure out how big the output image should be (return as
  *xsizeP and *ysizeP) and what output device resolution Ghostscript
  should assume (return as *xresP, *yresP).

  A resolution number is the number of pixels per inch that the a
  printer prints.  Since we're emulating a printed page with a PNM
  image, and a PNM image has no spatial dimension (you can't say how
  many inches wide a PNM image is), it's kind of confusing.  

  If the user doesn't select a resolution, we choose the resolution
  that causes the image to be a certain number of pixels, knowing how
  big (in inches) Ghostscript wants the printed picture to be.  For
  example, the part of the Postscript image we are going to print is 2
  inches wide.  We want the PNM image to be 1000 pixels wide.  So we
  tell Ghostscript that our horizontal output device resolution is 500
  pixels per inch.
  
  *xresP and *yresP are in dots per inch.
-----------------------------------------------------------------------------*/
    unsigned int sx, sy;
        /* The horizontal and vertical sizes of the input image, in points
           (1/72 inch)
        */

    if (orientation == LANDSCAPE) {
        sx = borderedBox.ury - borderedBox.lly;
        sy = borderedBox.urx - borderedBox.llx;
    } else {
        sx = borderedBox.urx - borderedBox.llx;
        sy = borderedBox.ury - borderedBox.lly;
    }

    if (cmdline.dpi) {
        /* User gave resolution; we figure out output image size */
        *xresP = *yresP = cmdline.dpi;
        *xsizeP = (int) (cmdline.dpi * sx / 72 + 0.5);
        *ysizeP = (int) (cmdline.dpi * sy / 72 + 0.5);
    } else  if (cmdline.xsize || cmdline.ysize)
        computeSizeResFromSizeSpec(cmdline.xsize, cmdline.ysize, sx, sy,
                                   xsizeP, ysizeP, xresP, yresP);
    else 
        computeSizeResBlind(cmdline.xmax, cmdline.ymax, sx, sy, cmdline.nocrop,
                            xsizeP, ysizeP, xresP, yresP);

    if (cmdline.verbose) {
        pm_message("output is %u pixels wide X %u pixels high",
                   *xsizeP, *ysizeP);
        pm_message("output device resolution is %u dpi horiz, %u dpi vert",
                   *xresP, *yresP);
    }
}



enum PostscriptLanguage {COMMON_POSTSCRIPT, ENCAPSULATED_POSTSCRIPT};

static enum PostscriptLanguage
languageDeclaration(char const inputFileName[],
                    bool const verbose) {
/*----------------------------------------------------------------------------
  Return the Postscript language in which the file declares it is written.
  (Except that if the file is on Standard Input or doesn't validly declare
  a languages, just say it is Common Postscript).
-----------------------------------------------------------------------------*/
    enum PostscriptLanguage language;

    if (streq(inputFileName, "-"))
        /* Can't read stdin, because we need it to remain positioned for the 
           Ghostscript interpreter to read it.
        */
        language = COMMON_POSTSCRIPT;
    else {
        FILE *infile;
        char line[80];

        infile = pm_openr(inputFileName);

        if (fgets(line, sizeof(line), infile) == NULL)
            language = COMMON_POSTSCRIPT;
        else {
            const char epsHeader[] = " EPSF-";

            if (strstr(line, epsHeader))
                language = ENCAPSULATED_POSTSCRIPT;
            else
                language = COMMON_POSTSCRIPT;
        }
        fclose(infile);
    }
    if (verbose)
        pm_message("language is %s",
                   language == ENCAPSULATED_POSTSCRIPT ?
                   "encapsulated postscript" :
                   "not encapsulated postscript");
    return language;
}



static struct Box
computeBoxToExtract(struct Box const cmdlineExtractBox,
                    char       const inputFileName[],
                    bool       const verbose) {

    struct Box retval;

    if (cmdlineExtractBox.llx != -1)
        /* User told us what box to extract, so that's what we'll do */
        retval = cmdlineExtractBox;
    else {
        /* Try to get the bounding box from the DSC %%BoundingBox
           statement (A Postscript comment) in the input.
        */
        struct Box psBb;  /* Box described by %%BoundingBox stmt in input */

        if (streq(inputFileName, "-"))
            /* Can't read stdin, because we need it to remain
               positioned for the Ghostscript interpreter to read it.  
            */
            psBb.llx = -1;
        else {
            FILE * ifP;
            bool foundBb;
            bool eof;

            ifP = pm_openr(inputFileName);
            
            for (foundBb = FALSE, eof = FALSE; !foundBb && !eof; ) {
                char line[200];
                char * fgetsRc;

                fgetsRc = fgets(line, sizeof(line), ifP);

                if (fgetsRc == NULL)
                    eof = TRUE;
                else {
                    int rc;
                    rc = sscanf(line, "%%%%BoundingBox: %d %d %d %d",
                                &psBb.llx, &psBb.lly, 
                                &psBb.urx, &psBb.ury);
                    if (rc == 4) 
                        foundBb = TRUE;
                }
            }
            fclose(ifP);

            if (!foundBb) {
                psBb.llx = -1;
                pm_message("Warning: no %%%%BoundingBox statement "
                           "in the input or command line.  "
                           "Will use defaults");
            }
        }
        if (psBb.llx != -1) {
            if (verbose)
                pm_message("Using %%%%BoundingBox statement from input.");
            retval = psBb;
        } else { 
            /* Use the center of an 8.5" x 11" page with 1" border all around*/
            retval.llx = 72;
            retval.lly = 72;
            retval.urx = 540;
            retval.ury = 720;
        }
    }
    if (verbose)
        pm_message("Extracting the box ((%d,%d),(%d,%d))",
                   retval.llx, retval.lly, retval.urx, retval.ury);
    return retval;
}



static enum Orientation
computeOrientation(struct CmdlineInfo const cmdline, 
                   struct Box         const extractBox) {

    unsigned int const inputWidth  = extractBox.urx - extractBox.llx;
    unsigned int const inputHeight = extractBox.ury - extractBox.lly;

    enum Orientation retval;

    if (cmdline.orientation != UNSPECIFIED)
        retval = cmdline.orientation;
    else {
        if ((!cmdline.xsize || !cmdline.ysize) &
            (cmdline.xsize || cmdline.ysize)) {
            /* User specified one output dimension, but not the other,
               so we can't use output dimensions to make the decision.  So
               just use the input dimensions.
            */
            if (inputHeight > inputWidth) retval = PORTRAIT;
            else retval = LANDSCAPE;
        } else {
            unsigned int outputWidth, outputHeight;
            if (cmdline.xsize) {
                /* He gave xsize and ysize, so that's the output size */
                outputWidth  = cmdline.xsize;
                outputHeight = cmdline.ysize;
            } else {
                /* Well then we'll just use his (or default) xmax, ymax */
                outputWidth  = cmdline.xmax;
                outputHeight = cmdline.ymax;
            }

            if (inputHeight > inputWidth && outputHeight > outputWidth)
                retval = PORTRAIT;
            else if (inputHeight < inputWidth && 
                     outputHeight < outputWidth)
                retval = PORTRAIT;
            else 
                retval = LANDSCAPE;
        }
    }
    return retval;
}



static struct Box
addBorders(struct Box const inputBox, 
           float      const xborderScale,
           float      const yborderScale,
           bool       const verbose) {
/*----------------------------------------------------------------------------
   Return a box which is 'inputBox' plus some borders.

   Add left and right borders that are the fraction 'xborderScale' of the
   width of the input box; likewise for top and bottom borders with 
   'yborderScale'.
-----------------------------------------------------------------------------*/
    unsigned int const leftRightBorderSize = 
        ROUNDU((inputBox.urx - inputBox.llx) * xborderScale);
    unsigned int const topBottomBorderSize = 
        ROUNDU((inputBox.ury - inputBox.lly) * yborderScale);

    struct Box retval;


    assert(inputBox.urx >= inputBox.llx);
    assert(inputBox.ury >= inputBox.lly);

    assert(inputBox.llx >= leftRightBorderSize);
    assert(inputBox.lly >= topBottomBorderSize);

    retval.llx = inputBox.llx - leftRightBorderSize;
    retval.lly = inputBox.lly - topBottomBorderSize;
    retval.urx = inputBox.urx + leftRightBorderSize;
    retval.ury = inputBox.ury + topBottomBorderSize;

    if (verbose)
        pm_message("With borders, extracted box is ((%u,%u),(%u,%u))",
                   retval.llx, retval.lly, retval.urx, retval.ury);

    return retval;
}



static const char *
computePstrans(struct Box       const box,
               enum Orientation const orientation,
               int              const xsize,
               int              const ysize, 
               int              const xres,
               int              const yres) {

    const char * retval;

    if (orientation == PORTRAIT) {
        int llx, lly;
        llx = box.llx - (xsize * 72 / xres - (box.urx - box.llx)) / 2;
        lly = box.lly - (ysize * 72 / yres - (box.ury - box.lly)) / 2;
        pm_asprintf(&retval, "%d neg %d neg translate", llx, lly);
    } else {
        int llx, ury;
        llx = box.llx - (ysize * 72 / yres - (box.urx - box.llx)) / 2;
        ury = box.ury + (xsize * 72 / xres - (box.ury - box.lly)) / 2;
        pm_asprintf(&retval, "90 rotate %d neg %d neg translate", llx, ury);
    }

    if (retval == NULL)
        pm_error("Unable to allocate memory for pstrans");

    return retval;
}



static const char *
computeOutfileArg(struct CmdlineInfo const cmdline) {
/*----------------------------------------------------------------------------
   Determine the value for the "OutputFile" variable to pass to Ghostscript,
   which is what tells Ghostscript where to put its output.  This is either
   a pattern such as "foo%03d.ppm" or "-" to indicate Standard Output.

   We go with "-" if, according to 'cmdline', the user asked for
   Standard Output or is giving his input on Standard Input.  Otherwise,
   we go with the pattern, based on the name of the input file and output
   format type the user requested.
-----------------------------------------------------------------------------*/
    const char * retval;  /* malloc'ed */

    if (cmdline.stdout)
        retval = strdup("-");
    else if (streq(cmdline.inputFileName, "-"))
        retval = strdup("-");
    else {
        char * basename;
        const char * suffix;
        
        basename  = strdup(cmdline.inputFileName);
        if (strlen(basename) > 3 && 
            streq(basename+strlen(basename)-3, ".ps")) 
            /* The input file name ends in ".ps".  Chop it off. */
            basename[strlen(basename)-3] = '\0';

        switch (cmdline.formatType) {
        case PBM_TYPE: suffix = "pbm"; break;
        case PGM_TYPE: suffix = "pgm"; break;
        case PPM_TYPE: suffix = "ppm"; break;
        default: pm_error("Internal error: invalid value for formatType: %d",
                          cmdline.formatType);
        }
        pm_asprintf(&retval, "%s%%03d.%s", basename, suffix);

        pm_strfree(basename);
    }
    return(retval);
}



static const char *
computeGsDevice(int  const formatType,
                bool const forceplain) {

    const char * basetype;
    const char * retval;

    switch (formatType) {
    case PBM_TYPE: basetype = "pbm"; break;
    case PGM_TYPE: basetype = "pgm"; break;
    case PPM_TYPE: basetype = "ppm"; break;
    default: pm_error("Internal error: invalid value formatType");
    }
    if (forceplain)
        retval = strdup(basetype);
    else
        pm_asprintf(&retval, "%sraw", basetype);

    if (retval == NULL)
        pm_error("Unable to allocate memory for gs device");

    return(retval);
}



static void
findGhostscriptProg(const char ** const retvalP) {
    
    *retvalP = NULL;  /* initial assumption */
    if (getenv("GHOSTSCRIPT"))
        *retvalP = strdup(getenv("GHOSTSCRIPT"));
    if (*retvalP == NULL) {
        if (getenv("PATH") != NULL) {
            char * pathwork;  /* malloc'ed */
            const char * candidate;

            pathwork = strdup(getenv("PATH"));
            
            candidate = strtok(pathwork, ":");

            *retvalP = NULL;
            while (!*retvalP && candidate) {
                struct stat statbuf;
                const char * filename;
                int rc;

                pm_asprintf(&filename, "%s/gs", candidate);
                rc = stat(filename, &statbuf);
                if (rc == 0) {
                    if (S_ISREG(statbuf.st_mode))
                        *retvalP = strdup(filename);
                } else if (errno != ENOENT)
                    pm_error("Error looking for Ghostscript program.  "
                             "stat(\"%s\") returns errno %d (%s)",
                             filename, errno, strerror(errno));
                pm_strfree(filename);

                candidate = strtok(NULL, ":");
            }
            free(pathwork);
        }
    }
    if (*retvalP == NULL)
        *retvalP = strdup("/usr/bin/gs");
}



static void
execGhostscript(int          const inputPipeFd,
                char         const ghostscriptDevice[],
                char         const outfileArg[], 
                int          const xsize,
                int          const ysize, 
                int          const xres,
                int          const yres,
                unsigned int const textalphabits,
                bool         const verbose) {
    
    const char * arg0;
    const char * ghostscriptProg;
    const char * deviceopt;
    const char * outfileopt;
    const char * gopt;
    const char * ropt;
    const char * textalphabitsopt;
    int rc;

    findGhostscriptProg(&ghostscriptProg);

    /* Put the input pipe on Standard Input */
    rc = dup2(inputPipeFd, STDIN_FILENO);
    close(inputPipeFd);

    pm_asprintf(&arg0, "gs");
    pm_asprintf(&deviceopt, "-sDEVICE=%s", ghostscriptDevice);
    pm_asprintf(&outfileopt, "-sOutputFile=%s", outfileArg);
    pm_asprintf(&gopt, "-g%dx%d", xsize, ysize);
    pm_asprintf(&ropt, "-r%dx%d", xres, yres);
    pm_asprintf(&textalphabitsopt, "-dTextAlphaBits=%u", textalphabits);

    /* -dSAFER causes Postscript to disable %pipe and file operations,
       which are almost certainly not needed here.  This prevents our
       Postscript program from doing crazy unexpected things, possibly
       as a result of a malicious booby trapping of our Postscript file.
    */

    if (verbose) {
        pm_message("execing '%s' with args '%s' (arg 0), "
                   "'%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s', '%s'",
                   ghostscriptProg, arg0,
                   deviceopt, outfileopt, gopt, ropt, textalphabitsopt,
                   "-q", "-dNOPAUSE", 
                   "-dSAFER", "-");
    }

    execl(ghostscriptProg, arg0, deviceopt, outfileopt, gopt, ropt, "-q",
          "-dNOPAUSE", "-dSAFER", "-", NULL);
    
    pm_error("execl() of Ghostscript ('%s') failed, errno=%d (%s)",
             ghostscriptProg, errno, strerror(errno));
}



static void
executeGhostscript(char                    const pstrans[],
                   char                    const ghostscriptDevice[],
                   char                    const outfileArg[], 
                   int                     const xsize,
                   int                     const ysize, 
                   int                     const xres,
                   int                     const yres,
                   unsigned int            const textalphabits,
                   char                    const inputFileName[], 
                   enum PostscriptLanguage const language,
                   bool                    const verbose) {

    int gsTermStatus;  /* termination status of Ghostscript process */
    FILE * pipeToGsP;  /* Pipe to Ghostscript's standard input */
    FILE * ifP;
    int rc;
    int eof;  /* End of file on input */
    int pipefd[2];

    if (strlen(outfileArg) > 80)
        pm_error("output file spec too long.");
    
    rc = pm_pipe(pipefd);
    if (rc < 0)
        pm_error("Unable to create pipe to talk to Ghostscript process.  "
                 "errno = %d (%s)", errno, strerror(errno));
    
    rc = fork();
    if (rc < 0)
        pm_error("Unable to fork a Ghostscript process.  errno=%d (%s)",
                 errno, strerror(errno));
    else if (rc == 0) {
        /* Child process */
        close(pipefd[1]);
        execGhostscript(pipefd[0], ghostscriptDevice, outfileArg,
                        xsize, ysize, xres, yres, textalphabits,
                        verbose);
    } else {
        pid_t const ghostscriptPid = rc;
        int const pipeToGhostscriptFd = pipefd[1];
        /* parent process */
        close(pipefd[0]);

        pipeToGsP = fdopen(pipeToGhostscriptFd, "w");
        if (pipeToGsP == NULL) 
            pm_error("Unable to open stream on pipe to Ghostscript process.");
    
        ifP = pm_openr(inputFileName);
        /*
          In encapsulated Postscript, we the encapsulator are supposed to
          handle showing the page (which we do by passing a showpage
          statement to Ghostscript).  Any showpage statement in the 
          input must be defined to have no effect.
          
          See "Enscapsulated PostScript Format File Specification",
          v. 3.0, 1 May 1992, in particular Example 2, p. 21.  I found
          it at 
          http://partners.adobe.com/asn/developer/pdfs/tn/5002.EPSF_Spec.pdf
          The example given is a much fancier solution than we need
          here, I think, so I boiled it down a bit.  JM 
        */
        if (language == ENCAPSULATED_POSTSCRIPT)
            fprintf(pipeToGsP, "\n/b4_Inc_state save def /showpage { } def\n");
 
        if (verbose) 
            pm_message("Postscript prefix command: '%s'", pstrans);

        fprintf(pipeToGsP, "%s\n", pstrans);

        /* If our child dies, it closes the pipe and when we next write to it,
           we get a SIGPIPE.  We must survive that signal in order to report
           on the fate of the child.  So we ignore SIGPIPE:
        */
        signal(SIGPIPE, SIG_IGN);

        eof = FALSE;
        while (!eof) {
            char buffer[4096];
            int bytes_read;
            
            bytes_read = fread(buffer, 1, sizeof(buffer), ifP);
            if (bytes_read == 0) 
                eof = TRUE;
            else 
                fwrite(buffer, 1, bytes_read, pipeToGsP);
        }
        pm_close(ifP);

        if (language == ENCAPSULATED_POSTSCRIPT)
            fprintf(pipeToGsP, "\nb4_Inc_state restore showpage\n");

        fclose(pipeToGsP);
        
        waitpid(ghostscriptPid, &gsTermStatus, 0);
        if (rc < 0)
            pm_error("Wait for Ghostscript process to terminated failed.  "
                     "errno = %d (%s)", errno, strerror(errno));

        if (gsTermStatus != 0) {
            if (WIFEXITED(gsTermStatus))
                pm_error("Ghostscript failed.  Exit code=%d\n", 
                         WEXITSTATUS(gsTermStatus));
            else if (WIFSIGNALED(gsTermStatus))
                pm_error("Ghostscript process died because of a signal %d.",
                         WTERMSIG(gsTermStatus));
            else 
                pm_error("Ghostscript process died with exit code %d", 
                         gsTermStatus);
        }
    }
}



int
main(int argc, char ** argv) {

    struct CmdlineInfo cmdline;
    const char * inputFileName;  /* malloc'ed */
        /* The file specification of our Postscript input file */
    unsigned int xres, yres;    /* Resolution in pixels per inch */
    unsigned int xsize, ysize;  /* output image size in pixels */
    struct Box extractBox;
        /* coordinates of the box within the input we are to extract; i.e.
           that will become the output. 
           */
    struct Box borderedBox;
        /* Same as above, but expanded to include borders */

    enum PostscriptLanguage language;
    enum Orientation orientation;
    const char * ghostscriptDevice;
    const char * outfileArg;
    const char * pstrans;

    pnm_init(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    addPsToFileName(cmdline.inputFileName, &inputFileName, cmdline.verbose);

    extractBox = computeBoxToExtract(cmdline.extractBox, inputFileName, 
                                      cmdline.verbose);

    language = languageDeclaration(inputFileName, cmdline.verbose);
    
    orientation = computeOrientation(cmdline, extractBox);

    borderedBox = addBorders(extractBox, cmdline.xborder, cmdline.yborder,
                             cmdline.verbose);

    computeSizeRes(cmdline, orientation, borderedBox, 
                   &xsize, &ysize, &xres, &yres);
    
    pstrans = computePstrans(borderedBox, orientation,
                             xsize, ysize, xres, yres);

    outfileArg = computeOutfileArg(cmdline);

    ghostscriptDevice = 
        computeGsDevice(cmdline.formatType, cmdline.forceplain);
    
    pm_message("Writing %s format", ghostscriptDevice);
    
    executeGhostscript(pstrans, ghostscriptDevice, outfileArg, 
                       xsize, ysize, xres, yres, cmdline.textalphabits,
                       inputFileName,
                       language, cmdline.verbose);

    pm_strfree(ghostscriptDevice);
    pm_strfree(outfileArg);
    pm_strfree(pstrans);
    
    return 0;
}


