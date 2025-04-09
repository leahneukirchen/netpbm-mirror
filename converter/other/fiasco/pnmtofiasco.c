/*
 *  FIASCO coder
 *
 *  Written by:     Ullrich Hafner
 *
 *  This file is part of FIASCO (Fractal Image And Sequence COdec)
 *  Copyright (C) 1994-2000 Ullrich Hafner
 */

/*
 *  $Date: 2000/10/28 17:39:29 $
 *  $Author: hafner $
 *  $Revision: 5.4 $
 *  $State: Exp $
 */

#include "config.h"
#include <stdlib.h>
#include <string.h>
#include <string.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "pnm.h"

#include "types.h"
#include "macros.h"

#include "misc.h"
#include "params.h"
#include "fiasco.h"



static param_t params[] = {
  /* Options for standard user */
  {"image-name", "FILE", 'i', PSTR, {0}, NULL,
   "Compress raw PPM/PGM image(s) `%s'."},
  {"output-name", "FILE", 'o', PSTR, {0}, "-",
   "Write automaton to `%s' (`-' means stdout)."},
  {"quality", "REAL", 'q', PFLOAT, {0}, "20.0",
   "Set quality of compression to `%s'."},
  {"title", "NAME", 't', PSTR, {0}, "",
   "Set title of FIASCO stream to `%s'."},
  {"comment", "NAME", 'c', PSTR, {0}, "",
   "Set comment of FIASCO stream to `%s'."},
  {"chroma-qfactor", "REAL", '\0', PFLOAT, {0}, "2",
   "Decrease chroma band quality `%s' times."},
  {"basis-name", "FILE", '\0', PSTR, {0}, "small.fco",
   "Preload basis `%s' into FIASCO."},
  {"optimize", "NUM", 'z', PINT, {0}, "0",
   "Set optimization level to `%s'."},
  {"dictionary-size", "NUM", '\0', PINT, {0}, "10000",
   "Set max# size of dictionary to `%s'."},
  {"chroma-dictionary", "NUM", '\0', PINT, {0}, "40",
   "Set max# size of chroma dictionary to `%s'.."},
  {"min-level", "NUM", '\0', PINT, {0}, "6",
   "Start prediction on block level `%s'."},
  {"max-level", "NUM", '\0', PINT, {0}, "10",
   "Stop prediction on block level `%s'."},
  {"tiling-exponent", "NUM", '\0', PINT, {0}, "4",
   "Set exponent of image permutation to `%s'."},
  {"tiling-method", "NAME", '\0', PSTR, {0}, "desc-variance",
   "Set type of permutation to `%s'."},
  {"rpf-range", "REAL", '\0', PFLOAT, {0}, "1.5",
   "Set quantization range to `%s'."},
  {"rpf-mantissa", "NUM", '\0', PINT, {0}, "3",
   "Set quantization mantissa to `%s' bits."},
  {"dc-rpf-range", "REAL", '\0', PFLOAT, {0}, "1",
   "Set quant. range (DC part) to `%s'."},
  {"dc-rpf-mantissa", "NUM", '\0', PINT, {0}, "5",
   "Set quant. mantissa (DC part) to `%s' bits."},
  {"pattern", "NAME", '\0', PSTR, {0}, "ippppppppp",
   "Set frame type sequence to `%s'."},
  {"fps", "NUM", '\0', PINT, {0}, "25",
   "Set display rate to `%s' frames per second."},
  {"half-pixel", NULL, '\0', PFLAG, {0}, "FALSE",
   "Use half-pixel precision for mc."},
  {"cross-B-search", NULL, '\0', PFLAG, {0}, "FALSE",
   "Use cross-B-search for interpolated mc."},
  {"B-as-past-ref", NULL, '\0', PFLAG, {0}, "FALSE",
   "Use B-frames as reference images." },
  {"prediction", NULL, '\0', PFLAG, {0}, "FALSE",
   "Use additional predictive coding."},
  {"progress-meter", "NUM", '\0', PINT, {0}, "2",
   "Set type of progress meter to `%s'."},
  {"smooth", "NUM", '\0', PINT, {0}, "70",
   "Smooth image(s) by factor `%s' (0-100)"},
#if 0
  /*
   *  Options currently not activated (maybe in future versions of FIASCO)
   */
  {"min-level", "NUM", 'm', PINT, {0}, "4",
   "Start compression on block level `%s'."},
  {"max-level", "NUM", 'M', PINT, {0}, "12",
   "Stop compression on block level `%s'."},
  {"max-elements", "NUM", 'N', PINT, {0}, "8",
   "Set max# of elements in an approx. to `%s'." },
  {"domain-pool", "NAME", '\0', PSTR, {0}, "rle",
   "Set domain pool of r-lc to `%s'."},
  {"coeff", "NAME", '\0', PSTR, {0}, "adaptive",
   "Set coefficients model to `%s'."},
  /*  DELTA APPROXIMATION  */
  {"d-domain-pool", "NAME", '\0', PSTR, {0}, "rle",
   "Set domain pool of d-lc to `%s'."},
  {"d-coeff", "NAME", '\0', PSTR, {0}, "adaptive",
   "Set d coefficients model to `%s'."},
  {"d-range", "REAL", '\0', PFLOAT, {0}, "1.5",
   "Set range of RPF for delta lc to `%s'."},
  {"d-mantissa", "NUM", '\0', PINT, {0}, "3",
   "Set #m-bits of RPF for delta lc to `%s'."},
  {"d-dc-range", "REAL", '\0', PFLOAT, {0}, "1",
   "Set DC range of RPF of delta lc to `%s'."},
  {"d-dc-mantissa", "NUM", '\0', PINT, {0}, "5",
   "Set #m-bits of delta RPF for DC domain to `%s'."},
  /*  ADVANCED  */
  {"images-level", "NUM", '\0', PINT, {0}, "5",
   "Compute state images up to level `%s'."},
  {"delta-domains", NULL, '\0', PFLAG, {0}, "FALSE",
   "Use delta domains every time."},
  {"normal-domains", NULL, '\0', PFLAG, {0}, "FALSE",
   "Use normal domains every time."},
  /*  VIDEO COMPRESSION  */
  {"smooth", "REAL", 's', PFLOAT, {0}, "1.0",
   "Smooth frames by factor `%s' (0.5 - 1.0)"},
  {"reference-frame", "FILE", '\0', PSTR, {0}, NULL,
   "Use PPM/PGM image `%s' as reference frame."},
#endif
  {NULL, NULL, 0, PSTR, {0}, NULL, NULL }
};



static void
checkargs(int                         argc,
          const char **               argv,
          const char ***        const imageTemplateListP,
          char **               const wfa_name,
          float *               const quality,
          fiasco_c_options_t ** const options) {
/*----------------------------------------------------------------------------
  Check validness of command line parameters and of the parameter files.

  Return value:
    1 on success
    0 otherwise
-----------------------------------------------------------------------------*/
    int    optind;            /* last processed commandline param */
    char * image_name;            /* filename given by option '--input_name' */
    const char ** imageTemplateList;

    optind = parseargs(params, argc, argv,
                       "Compress raw PPM/PGM image FILEs to a FIASCO file.",
                       "With no image FILE, or if FILE is -, "
                       "read standard input.\n"
                       "FILE must be either a filename"
                       " or an image template of the form:\n"
                       "`prefix[start-end{+,-}step]suffix'\n"
                       "e.g., img0[12-01-1].pgm is substituted by"
                       " img012.pgm ... img001.pgm\n\n"
                       "Environment:\n"
                       "FIASCO_DATA   Search and save path for FIASCO files. "
                       "Default: ./\n"
                       "FIASCO_IMAGES Search path for image files. "
                       "Default: ./", " [FILE]...",
                       FIASCO_SHARE, "system.fiascorc", ".fiascorc");

    /* Default options  */
    image_name = (char *) parameter_value(params, "image-name");
    *wfa_name  = (char *) parameter_value(params, "output-name");
    for (;;) {
        *quality = * (float *) parameter_value(params, "quality");
        if (*quality > 100)
            pm_message("Typical range of quality: (0,100].  "
                       "Expect some trouble on slow machines.");
        if (*quality > 0)
            break;
        ask_and_set(params, "quality",
                    "Please enter coding quality 'q' ('q' > 0): ");
    }

    /* Non-option command line params */
    if (optind < argc) {
        unsigned int i;
        if (image_name)
            pm_error("Multiple image name template arguments.  "
                     "Option --image-name already specified with '%s'",
                     image_name);

        MALLOCARRAY_NOFAIL(imageTemplateList, argc - optind + 1);

        for (i = 0; optind < argc; ++i, ++optind)
            imageTemplateList[i] = argv[optind];
        imageTemplateList[i] = NULL;
    } else {
        /* option -i image_name */

        MALLOCARRAY_NOFAIL(imageTemplateList, 2);

        imageTemplateList[0] = image_name;
        imageTemplateList[1] = NULL;
    }
    /* Additional options ... (have to be set with the fiasco_set_... methods)
     */
    {
        *options = fiasco_c_options_new();

        {
            const char * const pattern = parameter_value(params, "pattern");

            if (!fiasco_c_options_set_frame_pattern (*options, pattern))
                pm_error("%s", fiasco_get_error_message ());
        }

        {
            const char *const basis = parameter_value(params, "basis-name");

            if (!fiasco_c_options_set_basisfile (*options, basis))
                pm_error("%s", fiasco_get_error_message ());
        }

        {
            int   const n =
                *(int *)parameter_value(params, "chroma-dictionary");
            float const q =
                *(float *)parameter_value(params, "chroma-qfactor");

            if (!fiasco_c_options_set_chroma_quality(*options, q, MAX(0, n)))
                pm_error("%s", fiasco_get_error_message ());
        }

        {
            int const n = *((int *)parameter_value(params, "smooth"));

            if (!fiasco_c_options_set_smoothing(*options, MAX(0, n)))
                pm_error("%s", fiasco_get_error_message ());
        }

        {
            int const n = *(int *)parameter_value(params, "progress-meter");
            fiasco_progress_e type = (n < 0) ?
                FIASCO_PROGRESS_NONE : (fiasco_progress_e) n;

            if (!fiasco_c_options_set_progress_meter(*options, type))
                pm_error("%s", fiasco_get_error_message ());
        }

        {
            const char * const t = parameter_value(params, "title");

            if (strlen(t) > 0 && !fiasco_c_options_set_title(*options, t))
                pm_error("%s", fiasco_get_error_message ());
        }

        {
            const char * const c = parameter_value(params, "comment");

            if (strlen (c) > 0 && !fiasco_c_options_set_comment (*options, c))
                pm_error("%s", fiasco_get_error_message ());
        }

        {
            fiasco_tiling_e method = FIASCO_TILING_VARIANCE_DSC;
            int    const e =
                *(int *)parameter_value(params, "tiling-exponent");
            const char * const m = parameter_value (params, "tiling-method");

            if (strcaseeq(m, "desc-variance"))
                method = FIASCO_TILING_VARIANCE_DSC;
            else if (strcaseeq(m, "asc-variance"))
                method = FIASCO_TILING_VARIANCE_ASC;
            else if (strcaseeq(m, "asc-spiral"))
                method = FIASCO_TILING_SPIRAL_ASC;
            else if (strcaseeq(m, "dsc-spiral"))
                method = FIASCO_TILING_SPIRAL_DSC;
            else
                pm_error("Invalid tiling method `%s' specified.", m);

            if (!fiasco_c_options_set_tiling(*options, method, MAX(0, e)))
                pm_error("%s", fiasco_get_error_message ());
        }

        {
            int const dictionarySize =
                *(int *)parameter_value(params, "dictionary-size");
            int const optimizeOpt =
                *(int *)parameter_value(params, "optimize");

            int maxBlockLevel;
            int minBlockLevel;
            int maxElements;
            int optimizationLevel;
            bool succeeded;

            if (optimizeOpt <= 0) {
                optimizationLevel =  0;
                maxBlockLevel     = 10;
                minBlockLevel     =  6;
                maxElements       =  3;
            } else {
                optimizationLevel -= 1;
                maxBlockLevel     = 12;
                minBlockLevel     =  4;
                maxElements       =  5;
            }

            fiasco_c_options_set_optimizations(
                *options, minBlockLevel, maxBlockLevel, maxElements,
                MAX(0, dictionarySize), optimizationLevel, &succeeded);
            if (!succeeded)
                pm_error("%s", fiasco_get_error_message ());
        }
        {
            int const M = *(int *)parameter_value(params, "max-level");
            int const m = *(int *)parameter_value(params, "min-level");
            int const p = *(int *)parameter_value(params, "prediction");

            if (!fiasco_c_options_set_prediction (*options,
                                                  p, MAX(0, m), MAX(0, M)))
                pm_error("%s", fiasco_get_error_message ());
        }
        {
            float const r    =
                *(float *)parameter_value(params, "rpf-range");
            float const dcR =
                *(float *)parameter_value(params, "dc-rpf-range");
            int   const m    =
                *(int *)parameter_value(params, "rpf-mantissa");
            int   const dcM =
                *(int *)parameter_value(params, "dc-rpf-mantissa");

            fiasco_rpf_range_e range, dcRange;

            if (r < 1)
                range = FIASCO_RPF_RANGE_0_75;
            else if (r < 1.5)
                range = FIASCO_RPF_RANGE_1_00;
            else if (r < 2.0)
                range = FIASCO_RPF_RANGE_1_50;
            else
                range = FIASCO_RPF_RANGE_2_00;

            if (dcR < 1)
                dcRange = FIASCO_RPF_RANGE_0_75;
            else if (dcR < 1.5)
                dcRange = FIASCO_RPF_RANGE_1_00;
            else if (dcR < 2.0)
                dcRange = FIASCO_RPF_RANGE_1_50;
            else
                dcRange = FIASCO_RPF_RANGE_2_00;

            if (!fiasco_c_options_set_quantization(*options,
                                                   MAX(0, m), range,
                                                   MAX(0, dcM), dcRange))
                pm_error("%s", fiasco_get_error_message ());
        }

        if (fiasco_get_verbosity() == FIASCO_ULTIMATE_VERBOSITY)
            write_parameters(params, stderr);
    }
    *imageTemplateListP = imageTemplateList;
}



int
main(int argc, const char **argv) {

    char const **        image_template; /* template for input image files */
    char *               wfa_name;   /* filename of output WFA */
    float                quality;    /* approximation quality */
    fiasco_c_options_t * options;    /* additional coder options */
    int                  retval;

    pm_proginit(&argc, argv);

    checkargs(argc, argv, &image_template, &wfa_name, &quality, &options);

    if (fiasco_coder(image_template, wfa_name, quality, options))
        retval = 0;
    else {
        pm_message("Encoding failed.  %s", fiasco_get_error_message());
        retval = 1;
    }
    return retval;
}



