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

static bool verbose;

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

struct Dimensions {
/*----------------------------------------------------------------------------
  Horizontal and vertical dimensions of something, both in pixels and
  spatial distance (points).

  Sizes are in pixels.  Resolutions are in dots per inch (pixels per inch);
-----------------------------------------------------------------------------*/
    unsigned int xsize;
    unsigned int ysize;
    unsigned int xres;
    unsigned int yres;
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
    unsigned int stdoutSpec;
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
    OPTENT3(0, "ppm",        OPT_FLAG,  NULL, &ppmOpt,                   0);
    OPTENT3(0, "verbose",    OPT_FLAG,  NULL, &cmdlineP->verbose,        0);
    OPTENT3(0, "xborder",    OPT_FLOAT, &cmdlineP->xborder, NULL,        0);
    OPTENT3(0, "xmax",       OPT_UINT,  &cmdlineP->xmax, &xmaxSpec,      0);
    OPTENT3(0, "xsize",      OPT_UINT,  &cmdlineP->xsize, &xsizeSpec,    0);
    OPTENT3(0, "yborder",    OPT_FLOAT, &cmdlineP->yborder, NULL,        0);
    OPTENT3(0, "ymax",       OPT_UINT,  &cmdlineP->ymax, &ymaxSpec,      0);
    OPTENT3(0, "ysize",      OPT_UINT,  &cmdlineP->ysize, &ysizeSpec,    0);
    OPTENT3(0, "dpi",        OPT_UINT,  &cmdlineP->dpi, &dpiSpec,        0);
    OPTENT3(0, "portrait",   OPT_FLAG,  NULL, &portraitOpt,              0);
    OPTENT3(0, "landscape",  OPT_FLAG,  NULL, &landscapeOpt,             0);
    OPTENT3(0, "stdout",     OPT_FLAG,  NULL, &cmdlineP->stdoutSpec,     0);
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
                const char ** const newFileNameP) {
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
computeSizeResFromSizeSpec(unsigned int        const requestedXsize,
                           unsigned int        const requestedYsize,
                           unsigned int        const imageWidth,
                           unsigned int        const imageHeight,
                           struct Dimensions * const imageDimP) {

    if (requestedXsize) {
        imageDimP->xsize = requestedXsize;
        imageDimP->xres = (unsigned int)
            (requestedXsize * 72 / imageWidth + 0.5);
        if (!requestedYsize) {
            imageDimP->yres = imageDimP->xres;
            imageDimP->ysize = (unsigned int)
                (imageHeight * (float)imageDimP->yres/72 + 0.5);
            }
        }

    if (requestedYsize) {
        imageDimP->ysize = requestedYsize;
        imageDimP->yres = (unsigned int)
            (requestedYsize * 72 / imageHeight + 0.5);
        if (!requestedXsize) {
            imageDimP->xres = imageDimP->yres;
            imageDimP->xsize = (unsigned int)
                (imageWidth * (float)imageDimP->xres/72 + 0.5);
        }
    } 
}



static void
computeSizeResBlind(unsigned int        const xmax,
                    unsigned int        const ymax,
                    unsigned int        const imageWidth,
                    unsigned int        const imageHeight,
                    bool                const nocrop,
                    struct Dimensions * const imageDimP) {
    
    imageDimP->xres = imageDimP->yres = MIN(xmax * 72 / imageWidth, 
                                            ymax * 72 / imageHeight);
    
    if (nocrop) {
        imageDimP->xsize = xmax;
        imageDimP->ysize = ymax;
    } else {
        imageDimP->xsize = (unsigned int)
            (imageWidth * (float)imageDimP->xres / 72 + 0.5);
        imageDimP->ysize = (unsigned int)
            (imageHeight * (float)imageDimP->yres / 72 + 0.5);
    }
}



static void
computeSizeRes(struct CmdlineInfo  const cmdline, 
               struct Box          const borderedBox,
               struct Dimensions * const imageDimP) {
/*----------------------------------------------------------------------------
  Figure out how big the output image should be and what output device
  resolution Ghostscript should assume (return as *imageDimP).

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
  
  X and Y in all returned values is with respect to the image, not the
  page.  Note that the image might be placed sideways on the page, so that
  page X and Y would be reversed from image X and Y.
-----------------------------------------------------------------------------*/
    /* The horizontal and vertical sizes of the input image, in points
       (1/72 inch)
    */
    unsigned int const sx = borderedBox.urx - borderedBox.llx;
    unsigned int const sy = borderedBox.ury - borderedBox.lly;

    if (cmdline.dpi) {
        /* User gave resolution; we figure out output image size */
        imageDimP->xres = imageDimP->yres = cmdline.dpi;
        imageDimP->xsize = ROUNDU(cmdline.dpi * sx / 72.0);
        imageDimP->ysize = ROUNDU(cmdline.dpi * sy / 72.0);
    } else  if (cmdline.xsize || cmdline.ysize)
        computeSizeResFromSizeSpec(cmdline.xsize, cmdline.ysize, sx, sy,
                                   imageDimP);
    else 
        computeSizeResBlind(cmdline.xmax, cmdline.ymax, sx, sy, cmdline.nocrop,
                            imageDimP);

    if (cmdline.verbose) {
        pm_message("output is %u pixels wide X %u pixels high",
                   imageDimP->xsize, imageDimP->ysize);
        pm_message("output device resolution is %u dpi horiz, %u dpi vert",
                   imageDimP->xres, imageDimP->yres);
    }
}



enum PostscriptLanguage {COMMON_POSTSCRIPT, ENCAPSULATED_POSTSCRIPT};

static enum PostscriptLanguage
languageDeclaration(char const inputFileName[]) {
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
                    char       const inputFileName[]) {

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
/*----------------------------------------------------------------------------
   The proper orientation of the image on the page, given the user's
   parameters 'cmdline' and the image dimensions 'extractBox'.
-----------------------------------------------------------------------------*/
    /* We're putting an _image_ on a _page_.  Either one can have portrait or
       landscape aspect ratio.  In our return value, orientation just means
       whether the image is rotated on the page: Portrait means it isn't
       Landscape means it is.  The result can be confusing: Consider an image
       which is a landscape, wider than it is tall, being printed on a page
       which is also wider than it is tall.  The orientation we would return
       for that case is Portrait.

       The decision is simple: if the user didn't request a particular
       orientation, we return the value that makes the image orientation match
       the page orientation.  If both possibilities match equally (because the
       image or the page is square), we return Portrait.
    */

    enum Orientation retval;

    if (cmdline.orientation != UNSPECIFIED)
        retval = cmdline.orientation;
    else {
        /* Dimensions of image to print, in points */
        unsigned int const imageWidPt = extractBox.urx - extractBox.llx;
        unsigned int const imageHgtPt = extractBox.ury - extractBox.lly;
        
        /* Dimensions of image to print, in pixels (possibly of assumed
           resolution)
        */
        unsigned int imageWidXel;
        unsigned int imageHgtXel;

        /* We have to deal with the awkward case that the printed pixels are
           not square.  We match up the aspect ratio of the image in _pixels_
           and the aspect ratio of the page in _pixels_.  But only the ratio
           matters; we don't care what the absolute size of the pixels is.
           And that's good, because if the user didn't specify xsize/ysize, we
           don't know the absolute size in pixels.  In that case, fortunately,
           the pixels are guaranteed to be square so we can just pretend it is
           one point per pixel and get the right result.
        */

        if (cmdline.xsize && cmdline.ysize) {
            imageWidXel = cmdline.xsize;
            imageHgtXel = cmdline.ysize;
        } else {
            /* Pixels are square, so it doesn't matter what the resolution
               is; just call it one pixel per point.
            */
            imageWidXel = imageWidPt;
            imageHgtXel = imageHgtPt;
        }

        if (imageHgtXel >= imageWidXel && cmdline.ymax >= cmdline.xmax) {
            /* Both image and page are higher than wide, so no rotation */
            retval = PORTRAIT;
        } else if (imageHgtXel < imageWidXel &&
                   cmdline.ymax < cmdline.xmax) {
            /* Both image and page are wider than high, so no rotation */
            retval = PORTRAIT;
        } else {
            /* Image and pixel have opposite aspect ratios, so rotate
               for best fit.
            */
            retval = LANDSCAPE;
        }
    }
    return retval;
}



static struct Box
addBorders(struct Box const inputBox, 
           float      const xborderScale,
           float      const yborderScale) {
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

    retval.llx = inputBox.llx - (int)leftRightBorderSize;
    retval.lly = inputBox.lly - (int)topBottomBorderSize;
    retval.urx = inputBox.urx + (int)leftRightBorderSize;
    retval.ury = inputBox.ury + (int)topBottomBorderSize;

    if (verbose)
        pm_message("With borders, extracted box is ((%d,%d),(%d,%d))",
                   retval.llx, retval.lly, retval.urx, retval.ury);

    return retval;
}



static void
writePstrans(struct Box        const box,
             struct Dimensions const d,
             enum Orientation  const orientation,
             FILE *            const pipeToGsP) {

    int const xsize = d.xsize;
    int const ysize = d.ysize;
    int const xres  = d.xres;
    int const yres  = d.yres;

    const char * pstrans;

    switch (orientation) {
    case PORTRAIT: {
        int llx, lly;
        llx = box.llx - (xsize * 72 / xres - (box.urx - box.llx)) / 2;
        lly = box.lly - (ysize * 72 / yres - (box.ury - box.lly)) / 2;
        pm_asprintf(&pstrans, "%d neg %d neg translate", llx, lly);
    } break;
    case LANDSCAPE: {
        int llx, ury;
        llx = box.llx - (xsize * 72 / xres - (box.urx - box.llx)) / 2;
        ury = box.ury + (ysize * 72 / yres - (box.ury - box.lly)) / 2;
        pm_asprintf(&pstrans, "90 rotate %d neg %d neg translate", llx, ury);
    } break;
    case UNSPECIFIED:
        assert(false);
    }

    if (pstrans == pm_strsol)
        pm_error("Unable to allocate memory for pstrans");

    if (verbose) 
        pm_message("Postscript prefix command: '%s'", pstrans);

    fprintf(pipeToGsP, "%s\n", pstrans);

    pm_strfree(pstrans);
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

    if (cmdline.stdoutSpec)
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
execGhostscript(int               const inputPipeFd,
                char              const ghostscriptDevice[],
                char              const outfileArg[], 
                struct Dimensions const pageDim,
                unsigned int      const textalphabits) {
/*----------------------------------------------------------------------------
   Exec the Ghostscript program and have it execute the Postscript program
   that it receives on 'inputPipeFd', then exit.

   'pageDim' describes the print area.  X and Y in 'pageDim' are with respect
   to the page, independent of whether the program we receive on 'inputPipeFd'
   puts an image in there sideways.
-----------------------------------------------------------------------------*/
    const char * arg0;
    const char * ghostscriptProg;
    const char * deviceopt;
    const char * outfileopt;
    const char * gopt;
    const char * ropt;
    const char * textalphabitsopt;

    findGhostscriptProg(&ghostscriptProg);

    /* Put the input pipe on Standard Input */
    dup2(inputPipeFd, STDIN_FILENO);
    close(inputPipeFd);

    pm_asprintf(&arg0, "gs");
    pm_asprintf(&deviceopt, "-sDEVICE=%s", ghostscriptDevice);
    pm_asprintf(&outfileopt, "-sOutputFile=%s", outfileArg);
    pm_asprintf(&gopt, "-g%dx%d", pageDim.xsize, pageDim.ysize);
    pm_asprintf(&ropt, "-r%dx%d", pageDim.xres, pageDim.yres);
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
feedPsToGhostScript(const char *            const inputFileName,
                    struct Box              const borderedBox,
                    struct Dimensions       const imageDim,
                    enum Orientation        const orientation,
                    int                     const pipeToGhostscriptFd,
                    enum PostscriptLanguage const language) {
/*----------------------------------------------------------------------------
   Send a Postscript program to the Ghostscript process running on the
   other end of the pipe 'pipeToGhostscriptFd'.  That program is mostly
   the contents of file 'inputFileName' (special value "-" means Standard
   Input), but we may add a little to it.

   The image has dimensions 'imageDim' and is oriented on the page according
   to 'orientation' ('imageDim' X and Y are with respect to the image itself,
   without regard to how it is oriented on the page).
-----------------------------------------------------------------------------*/
    FILE * pipeToGsP;  /* Pipe to Ghostscript's standard input */
    FILE * ifP;
    bool eof;  /* End of file on input */

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
 
    writePstrans(borderedBox, imageDim, orientation, pipeToGsP);

    /* If our child dies, it closes the pipe and when we next write to it,
       we get a SIGPIPE.  We must survive that signal in order to report
       on the fate of the child.  So we ignore SIGPIPE:
    */
    signal(SIGPIPE, SIG_IGN);

    eof = FALSE;
    while (!eof) {
        char buffer[4096];
        size_t readCt;
            
        readCt = fread(buffer, 1, sizeof(buffer), ifP);
        if (readCt == 0) 
            eof = TRUE;
        else 
            fwrite(buffer, 1, readCt, pipeToGsP);
    }
    pm_close(ifP);

    if (language == ENCAPSULATED_POSTSCRIPT)
        fprintf(pipeToGsP, "\nb4_Inc_state restore showpage\n");

    fclose(pipeToGsP);
}        



static struct Dimensions
pageDimFromImageDim(struct Dimensions const imageDim,
                    enum Orientation  const orientation) {
/*----------------------------------------------------------------------------
   The dimensions of the page of an image whose dimensions are
   'imageDim', if we place it on the page with orientation 'orientation'.

   (I.e. swap and X and Y if landscape orientation).

   'orientation' must not be UNSPECIFIED.
-----------------------------------------------------------------------------*/
    struct Dimensions retval;

    switch (orientation) {
    case PORTRAIT:
        retval = imageDim;
        break;
    case LANDSCAPE:
        retval.xsize = imageDim.ysize;
        retval.ysize = imageDim.xsize;
        retval.xres  = imageDim.yres;
        retval.yres  = imageDim.xres;
        break;
    case UNSPECIFIED:
        assert(false);
        break;
    }

    return retval;
}



static void
executeGhostscript(char                    const inputFileName[],
                   struct Box              const borderedBox,
                   struct Dimensions       const imageDim,
                   enum Orientation        const orientation,
                   char                    const ghostscriptDevice[],
                   char                    const outfileArg[], 
                   unsigned int            const textalphabits,
                   enum PostscriptLanguage const language) {

    int rc;
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
                        pageDimFromImageDim(imageDim, orientation),
                        textalphabits);
    } else {
        /* parent process */
        pid_t const ghostscriptPid = rc;
        int const pipeToGhostscriptFd = pipefd[1];

        int gsTermStatus;  /* termination status of Ghostscript process */
        pid_t rc;

        close(pipefd[0]);

        feedPsToGhostScript(inputFileName, borderedBox,
                            imageDim, orientation,
                            pipeToGhostscriptFd, language);

        rc = waitpid(ghostscriptPid, &gsTermStatus, 0);
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
    struct Dimensions imageDim;
        /* Size and resolution of the input image */
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

    pnm_init(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    verbose = cmdline.verbose;

    addPsToFileName(cmdline.inputFileName, &inputFileName);

    extractBox = computeBoxToExtract(cmdline.extractBox, inputFileName);

    language = languageDeclaration(inputFileName);
    
    orientation = computeOrientation(cmdline, extractBox);

    borderedBox = addBorders(extractBox, cmdline.xborder, cmdline.yborder);

    computeSizeRes(cmdline, borderedBox, &imageDim);
    
    outfileArg = computeOutfileArg(cmdline);

    ghostscriptDevice = 
        computeGsDevice(cmdline.formatType, cmdline.forceplain);
    
    pm_message("Writing %s format", ghostscriptDevice);
    
    executeGhostscript(inputFileName, borderedBox, imageDim, orientation,
                       ghostscriptDevice, outfileArg, cmdline.textalphabits,
                       language);

    pm_strfree(ghostscriptDevice);
    pm_strfree(outfileArg);
    
    return 0;
}


