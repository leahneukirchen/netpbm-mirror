/*
 *  Decode WFA-files
 *
 *  Written by:     Ullrich Hafner
 *          Michael Unger
 *
 *  This file is part of FIASCO (Fractal Image And Sequence COdec)
 *  Copyright (C) 1994-2000 Ullrich Hafner
 */

/*
 *  $Date: 2000/10/28 17:39:29 $
 *  $Author: hafner $
 *  $Revision: 5.7 $
 *  $State: Exp $
 */

#define _DEFAULT_SOURCE 1 /* New name for SVID & BSD source defines */
#define _BSD_SOURCE 1   /* Make sure strdup() is in string.h */
#define _XOPEN_SOURCE 500  /* Make sure strdup() is in string.h */

#include "config.h"
#include "pnm.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <getopt.h>

#include "mallocvar.h"
#include "nstring.h"

#include "types.h"
#include "macros.h"

#include "misc.h"
#include "params.h"
#include "fiasco.h"



static void
getOutputTemplate(const char *  const imageName,
                  const char *  const wfaName,
                  bool_t        const color,
                  const char ** const basename,
                  const char ** const suffixP) {
/*----------------------------------------------------------------------------

  Generate image filename template for output of image sequences.
  'wfa_name' is the filename of the WFA stream.
  Images are either saved with filename 'basename'.'suffix' (still images)
  or 'basename'.%03d.'suffix' (videos).
-----------------------------------------------------------------------------*/
    char * suffixLoc;

    /* Generate filename template */
    if (!imageName || streq(imageName, "") || streq(imageName, "-")) {
        if (!wfaName || streq(wfaName, "-"))
            *basename = strdup("stdin");
        else
            *basename = strdup(wfaName);
        suffixLoc = NULL;
    } else {
        *basename = strdup(imageName);
        suffixLoc = strrchr(*basename, '.');
    }

    if (suffixLoc) {
        /* found name 'basename.suffix' */

        *suffixLoc = '\0';         /* remove dot from *basename*/

        if (*(suffixLoc+1) == '\0')
            *suffixP = strdup(color ? "ppm" : "pgm");
        else
            *suffixP = strdup(suffixLoc + 1);
    } else             /* no suffix found, generate one */
        *suffixP = strdup(color ? "ppm" : "pgm");
}



static param_t params [] = {
    {"output", "FILE", 'o', PSTR, {0}, "-",
     "Write raw PNM frame(s) to `%s'."},
    {"double", NULL, 'd', PFLAG, {0}, "FALSE",
     "Interpolate images to double size before display."},
    {"fast", NULL, 'r', PFLAG, {0}, "FALSE",
     "Use 4:2:0 format for fast, low quality output."},
    {"panel", NULL, 'p', PFLAG, {0}, "FALSE",
     "Display control panel."},
    {"magnify", "NUM", 'm', PINT, {0}, "0",
     "Magnify/reduce image size by a factor of 4^`%s'."},
    {"framerate", "NUM", 'F', PINT, {0}, "-1",
     "Set display rate to `%s' frames per second."},
    {"smoothing", "NUM", 's', PINT, {0}, "-1",
     "Smooth image(s) by factor `%s' (0-100)"},
    {NULL, NULL, 0, 0, {0}, NULL, NULL }
};

static int
checkargs(int                         argc,
          const char **         const argv,
          bool_t *              const double_resolution,
          bool_t *              const panel,
          int *                 const fps,
          const char **         const image_name,
          fiasco_d_options_t ** const options) {
/*----------------------------------------------------------------------------
  Check validness of command line parameters and of the parameter files.

  Return value: index in argv of the first argv-element that is not an option.

  Side effects:
-----------------------------------------------------------------------------*/
    int optind;              /* last processed commandline param */

    optind = parseargs(params, argc, argv,
                       "Decode FIASCO-FILEs and write frame(s) to disk.",
                       "With no FIASCO-FILE, or if FIASCO-FILE is -, "
                       "read standard input.\n"
                       "Environment:\n"
                       "FIASCO_DATA   Search path for automata files. "
                       "Default: ./\n"
                       "FIASCO_IMAGES Save path for image files. "
                        "Default: ./", " [FIASCO-FILE]...",
                       FIASCO_SHARE, "system.fiascorc", ".fiascorc");

    *image_name        =   (char *)   parameter_value (params, "output");
    *double_resolution = *((bool_t *) parameter_value (params, "double"));
    *panel             = *((bool_t *) parameter_value (params, "panel"));
    *fps               = *((int *)    parameter_value (params, "framerate"));

    /* Additional options ... (have to be set with the fiasco_set_... methods)
     */
    *options = fiasco_d_options_new();

    {
        int const n = *((int *)parameter_value(params, "smoothing"));

        if (!fiasco_d_options_set_smoothing(*options, MAX(-1, n)))
            pm_error("%s", fiasco_get_error_message());
    }

    {
        int const n = *((int *)parameter_value(params, "magnify"));

        if (!fiasco_d_options_set_magnification(*options, n))
            pm_error("%s", fiasco_get_error_message());
    }

    {
        bool_t const n = *((bool_t *)parameter_value(params, "fast"));

        if (!fiasco_d_options_set_4_2_0_format(*options, n > 0 ? YES : NO))
            pm_error("%s", fiasco_get_error_message ());
    }

    return optind;
}



static void
video_decoder(const char *         const wfa_name,
              const char *         const image_name,
              bool_t               const panel,
              bool_t               const double_resolution,
              int                  const fpsArg,
              fiasco_d_options_t * const options) {
    do {
        int                fps;
        unsigned int       width, height;
        unsigned int       frames;
        unsigned int       n;
        fiasco_decoder_t * decoder_state;
        char *             filename;
        const char *       basename;   /* basename of decoded frame */
        const char *       suffix;     /* suffix of decoded frame */
        unsigned int       frame_time;

        if (!(decoder_state = fiasco_decoder_new(wfa_name, options)))
            pm_error("%s", fiasco_get_error_message ());

        if (fpsArg <= 0)         /* then use value of FIASCO file */
            fps = fiasco_decoder_get_rate(decoder_state);
        else
            fps = fpsArg;

        frame_time = fps ? (1000 / fps) : (1000 / 25);

        if (!(width = fiasco_decoder_get_width(decoder_state)))
            pm_error("%s", fiasco_get_error_message ());

        if (!(height = fiasco_decoder_get_height(decoder_state)))
            pm_error("%s", fiasco_get_error_message ());

        if (!(frames = fiasco_decoder_get_length(decoder_state)))
            pm_error("%s", fiasco_get_error_message ());

        getOutputTemplate(image_name, wfa_name,
                          fiasco_decoder_is_color(decoder_state),
                          &basename, &suffix);

        MALLOCARRAY_NOFAIL(filename,
                           strlen (basename) + strlen (suffix) + 2
                           + 10 + (int) (log10 (frames) + 1));

        for (n = 0; n < frames; ++n) {
            clock_t fps_timer;     /* frames per second timer struct */

            prg_timer(&fps_timer, START);

            if (image_name) {
                /* just write frame to disk */
                if (frames == 1) {
                    if (streq(image_name, "-"))
                        strcpy(filename, "-");
                    else
                        sprintf(filename, "%s.%s", basename, suffix);
                } else {
                    pm_message("Decoding frame %d to file `%s.%0*d.%s",
                               n, basename, (int) (log10 (frames - 1) + 1),
                               n, suffix);
                    sprintf(filename, "%s.%0*d.%s", basename,
                            (int) (log10 (frames - 1) + 1), n, suffix);
                }

                if (!fiasco_decoder_write_frame (decoder_state, filename))
                    pm_error("%s", fiasco_get_error_message ());
            }
            if (frame_time) {/* defeat compiler warning */}
        }
        free(filename);

        fiasco_decoder_delete(decoder_state);
    } while (panel);
}

int
main(int argc, const char **argv) {

    const char *         imageName; /* output filename */
    bool_t               doubleResolution;/* double resolution of image */
    bool_t               panel; /* control panel */
    int                  fps; /* frame display rate */
    fiasco_d_options_t * options;/* additional coder options */
    unsigned int         lastArg;    /* last processed cmdline parameter */

    lastArg = checkargs(argc, argv, &doubleResolution, &panel, &fps,
                        &imageName, &options);

    if (lastArg >= argc)
        video_decoder("-", imageName, panel, doubleResolution, fps, options);
    else
        while (lastArg++ < argc)
            video_decoder(argv [lastArg - 1], imageName, panel,
                          doubleResolution, fps, options);

    return 0;
}


