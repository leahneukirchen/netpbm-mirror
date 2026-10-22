/*=============================================================================
                                  pamtojpegxl
===============================================================================
  Read a Netpbm image and produce a JPEG XL image.

  By Bryan Henderson, October 2026.  Contributed to the public domain by its
  author.
===============================================================================
  Design notes:

  This program reads the entire image into memory at once (with 32 bits per
  sample).  The only reason for that is coding expedience.  It ought to be
  enhanced to go row by row to save memory.

  The program converts samples to floating point before passing them to
  'libjxl'.  It is alternatively possible to pass the original integer samples
  if the maxval is 255 or 65535, as it usually is.  That might be faster (but
  it might not; that might just push the conversion to floating point into
  'libjxl').
=============================================================================*/
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <jxl/codestream_header.h>
#include <jxl/color_encoding.h>
#include <jxl/encode.h>
#include <jxl/types.h>


#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "pam.h"


static JxlMemoryManager * const memoryManagerP = NULL;



struct CmdlineInfo {
    const char * inputFileName;
    unsigned int verbose;
};



static void
parseCommandLine(int                  argc,
                 const char **        argv,
                 struct CmdlineInfo * const cmdlineP) {

    optEntry * option_def;
    optStruct3 opt;
    unsigned int option_def_index = 0;  /* incremented by OPTENT3 */

    MALLOCARRAY(option_def, 100);

    OPTENT3(0,  "verbose",      OPT_FLAG,       NULL,
            &cmdlineP->verbose,        0);

    opt.opt_table = option_def;
    opt.short_allowed = false;  /* we have no short (old-fashioned) options */
    opt.allowNegNum = false;  /* we have no parms that are negative numbers */

    /* uses and sets argc, argv, and some of *cmdlineP and others */
    pm_optParseOptions4(&argc, argv, opt, sizeof(opt), 0);

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else if (argc-1 == 1)
        cmdlineP->inputFileName = argv[1];
    else
        pm_error("Program takes at most one argument: input file name.");

    free(option_def);
}



struct PamPlaneDesc {
/*----------------------------------------------------------------------------
   Description of the planes of the Netpbm image and 'tuple' structures.
-----------------------------------------------------------------------------*/
    unsigned int colorPlaneCt;
    unsigned int alphaPlaneCt;
};



static struct PamPlaneDesc
planeDescFmDepth(unsigned int const depth) {
/*----------------------------------------------------------------------------
  Following Netpbm convention, we don't look at the tuple type ("RGBA", etc.)
  but just at the number of planes, so as to be more flexible.
-----------------------------------------------------------------------------*/

    struct PamPlaneDesc retval;

    retval.colorPlaneCt = depth < 3 ? 1 : 3;
    retval.alphaPlaneCt = depth > retval.colorPlaneCt ? 1 : 0;

    return retval;
}



static void
tuplesToRawSamples(struct pam  const inpam,
                   tuple **    const tuples,
                   float **    const samplesP,
                   size_t *    const samplesSzP) {

    float * samples;  /* malloced */
    size_t const samplesSz =
        inpam.width * inpam.height * inpam.depth * sizeof(samples[0]);
    struct PamPlaneDesc const planeDesc = planeDescFmDepth(inpam.depth);

    MALLOCARRAY(samples, samplesSz);
    if (!samples)
        pm_error("Failed to allocated buffer for %u 32-bit samples",
                 (unsigned)samplesSz);
    else {
        unsigned int row;
        unsigned int outCursor;

        for (row = 0, outCursor = 0; row < inpam.height; ++row) {
            unsigned int col;
            for (col = 0; col < inpam.width; ++col) {
                unsigned int plane;
                for (plane = 0;
                     plane < planeDesc.colorPlaneCt + planeDesc.alphaPlaneCt;
                     ++plane) {

                    samples[outCursor++] =
                        (float)tuples[row][col][plane]/inpam.maxval;
                }
            }
        }
        assert(outCursor * sizeof(samples[0]) == samplesSz);
    }
    *samplesP   = samples;
    *samplesSzP = samplesSz;
}



static void
setBasicInfo(JxlEncoder *   const encoderP,
             struct pam     const inpam,
             JxlBasicInfo * const basicInfoP) {

    struct PamPlaneDesc const planeDesc = planeDescFmDepth(inpam.depth);

    JxlEncoderStatus rc;

    JxlEncoderInitBasicInfo(basicInfoP);

    basicInfoP->xsize                 = inpam.width;
    basicInfoP->ysize                 = inpam.height;
    basicInfoP->bits_per_sample       = pm_maxvaltobits(inpam.maxval);
    basicInfoP->uses_original_profile = JXL_FALSE;
    basicInfoP->num_color_channels    = planeDesc.colorPlaneCt;
    basicInfoP->num_extra_channels    = planeDesc.alphaPlaneCt;

    rc = JxlEncoderSetBasicInfo(encoderP, basicInfoP);

    if (rc != JXL_ENC_SUCCESS)
        pm_error("JxlEncoderSetBasicInfo failed");
}



static void
setColorEncoding(JxlEncoder *   const encoderP,
                 struct pam     const inpam) {

    JXL_BOOL const isGray = inpam.depth < 3 ? JXL_TRUE : JXL_FALSE;

    JxlColorEncoding colorEncoding;
    JxlEncoderStatus rc;

    JxlColorEncodingSetToSRGB(&colorEncoding, isGray);
        /* This is a lie; true Netpbm formats use the older ITU-R
           Recommendation BT.709 color space.  sRGB is the closest 'libjxl'
           can get to that (far clsoer than the default when you supply
           floating point samples, which is linear sRGB).
        */

    rc = JxlEncoderSetColorEncoding(encoderP, &colorEncoding);

    if (rc != JXL_ENC_SUCCESS)
        pm_error("JxlEncoderSetColorEncoding failed");
}



static void
setExtraChannelInfo(JxlEncoder *   const encoderP,
                    struct pam     const inpam) {

    struct PamPlaneDesc const planeDesc = planeDescFmDepth(inpam.depth);

    if (planeDesc.alphaPlaneCt == 0) {
        /* No extra channel.  Just grayscale or RGB */
    } else {
        /* One extra plane: alpha */
        unsigned int const alphaExtraChanIdx = 0;

        JxlExtraChannelInfo extraChannelInfo;
        JxlEncoderStatus rc;

        JxlEncoderInitExtraChannelInfo(JXL_CHANNEL_ALPHA, &extraChannelInfo);

        extraChannelInfo.bits_per_sample = sizeof(float)*8;

        extraChannelInfo.exponent_bits_per_sample = 8;

        extraChannelInfo.dim_shift = 0;  /* Don't know what this is */

        extraChannelInfo.name_length = 0;

        extraChannelInfo.alpha_premultiplied = JXL_FALSE;

        rc = JxlEncoderSetExtraChannelInfo(
            encoderP, alphaExtraChanIdx, &extraChannelInfo);

        if (rc != JXL_ENC_SUCCESS)
            pm_error("JxlEncoderSetExtraChannel failed.");

        JxlEncoderSetExtraChannelName(
            encoderP, alphaExtraChanIdx, "alpha", strlen("alpha"));
    }
}



static void
writeJpegXlFmRawSamples(JxlEncoder * const encoderP,
                        FILE *       const ofP) {

    size_t const bufferSz = 4096;
    uint8_t * iobuffer;  /* malloced */

    MALLOCARRAY(iobuffer, bufferSz);
    if (!iobuffer)
        pm_error("Failed to allocate %u-byte I/O buffer",
                 (unsigned) bufferSz);
    else {
        bool done;

        for (done = false; !done; ) {
            JxlEncoderStatus rc;
            uint8_t * nextOut;
                /* Gets passed to libjxl as input/output parameter */
            size_t availOut;
                /* Gets passed to libjxl as input/output parameter */
            size_t bytesReturnedCt;
                /* Number of bytes of JPEG XL data libjxl returned */

            nextOut  = iobuffer; /* initial value */
            availOut = bufferSz; /* initial value */

            rc = JxlEncoderProcessOutput(encoderP, &nextOut, &availOut);

            bytesReturnedCt = bufferSz - availOut;

            fwrite(iobuffer, 1, bytesReturnedCt, ofP);

            if (ferror(ofP)) {
                pm_error("Error writing %u JPEG XL bytes to output file",
                         (unsigned)bytesReturnedCt);
            }

            if (rc == JXL_ENC_NEED_MORE_OUTPUT) {
            } else {
                done = true;

                if (rc != JXL_ENC_SUCCESS)
                    pm_error("JxlEncoderProcessOutput failed");
            }
        }
        free(iobuffer);
    }
}



static void
convert(struct pam const inpam,
        tuple **   const tuples,
        FILE *     const ofP) {

    JxlEncoder * const encoderP = JxlEncoderCreate(memoryManagerP);

    if (!encoderP)
        pm_error("JxlEncoderCreate failed");
    else {
        float *      samples;
        size_t       samplesSz;
        JxlBasicInfo basicInfo;

        tuplesToRawSamples(inpam, tuples, &samples, &samplesSz);

        setBasicInfo(encoderP, inpam, &basicInfo);

        setColorEncoding(encoderP, inpam);

        setExtraChannelInfo(encoderP, inpam);

        JxlEncoderFrameSettings * const frameSettingSourceP = NULL;
        JxlEncoderFrameSettings * const frameSettingsP =
            JxlEncoderFrameSettingsCreate(encoderP, frameSettingSourceP);

        if (!frameSettingsP)
            pm_error("Failed to create JxlEncoderFrameSettings object");
        else {
            JxlPixelFormat const pixelFormat = {inpam.depth,
                                                JXL_TYPE_FLOAT,
                                                JXL_NATIVE_ENDIAN,
                                                0};

            JxlEncoderStatus rc;

            rc = JxlEncoderAddImageFrame(frameSettingsP,
                                         &pixelFormat,
                                         samples, samplesSz);

            if (rc != JXL_ENC_SUCCESS)
                pm_error("JxlEncoderAddImageFrame failed");

            JxlEncoderCloseInput(encoderP);

            writeJpegXlFmRawSamples(encoderP, ofP);

            /* Note that we don't destroy *frameSettingsP here; in a
               modularity violation, 'JxlEncoderDestroy' does that.
            */
        }
        free(samples);

        JxlEncoderDestroy(encoderP);
    }
}



static void
pamtojpegxl(FILE *             const ifP,
            FILE *             const ofP,
            struct CmdlineInfo const cmdline) {

    tuple ** tuples;
    struct pam inpam;

    /* Design note: There is probably a way to make this convert row by row
       instead of passing the entire image to libjxl at once.  We'll find it
       some day.
    */

    tuples = pnm_readpam(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    convert(inpam, tuples, ofP);
}



int
main(int           argc,
     const char ** argv) {

    FILE * ifP;
    struct CmdlineInfo cmdline;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    pamtojpegxl(ifP, stdout, cmdline);

    pm_close(ifP);

    return 0;
}



