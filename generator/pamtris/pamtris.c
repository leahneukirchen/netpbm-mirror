#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "netpbm/mallocvar.h"
#include "netpbm/shhopt.h"
#include "netpbm/pam.h"

#include "limits_pamtris.h"
#include "framebuffer.h"
#include "boundaries.h"
#include "input.h"

#define MAX_METRICS 8192



static int
parse_command_line(int *         const argc_ptr,
                   const char ** const argv,
                   int32_t *     const width,
                   int32_t *     const height,
                   int32_t *     const maxval,
                   int32_t *     const num_attribs,
                   char *        const tupletype) {

    optEntry * option_def;
    optStruct3 opt;
        /* Instructions to pm_optParseOptions3 on how to parse our options */
    unsigned int option_def_index;

    char * tupletype_tmp;

    unsigned int width_spec, height_spec, attribs_spec, tupletype_spec;
    unsigned int rgb_spec, grayscale_spec, maxval_spec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;  /* incremented by OPTENT3 */
    OPTENT3(0, "width",       OPT_INT,    width,          &width_spec,      0);
    OPTENT3(0, "height",      OPT_INT,    height,         &height_spec,     0);
    OPTENT3(0, "num_attribs", OPT_INT,    num_attribs,    &attribs_spec,    0);
    OPTENT3(0, "tupletype",   OPT_STRING, &tupletype_tmp, &tupletype_spec,  0);
    OPTENT3(0, "rgb",         OPT_FLAG,   NULL,           &rgb_spec,        0);
    OPTENT3(0, "grayscale",   OPT_FLAG,   NULL,           &grayscale_spec,  0);
    OPTENT3(0, "maxval",      OPT_INT,    maxval,         &maxval_spec,     0);

    opt.opt_table     = option_def;
    opt.short_allowed = false;
    opt.allowNegNum   = false;

    pm_optParseOptions3(argc_ptr, (char **)argv, opt, sizeof(opt), 0);

    if (!width_spec || !height_spec || (!attribs_spec && !(rgb_spec || grayscale_spec))) {
        pm_errormsg(
            "you must at least specify -width, -height and "
            "either -num_attribs, -rgb or -grayscale.");

        return 0;
    }

    if (rgb_spec + grayscale_spec + attribs_spec != 1) {
        pm_errormsg("you must provide either only -num_attribs, "
                    "-rgb or -grayscale; not a combination of those.");

        return 0;
    }

    if (*width < 1 || *width > MAX_METRICS) {
        pm_errormsg("invalid width.");

        return 0;
    }

    if (*height < 1 || *height > MAX_METRICS) {
        pm_errormsg("invalid height.");

        return 0;
    }

    if (maxval_spec) {
        if (*maxval < 1 || *maxval > PAM_OVERALL_MAXVAL) {
            pm_errormsg("invalid maxval.");

            return 0;
        }
    } else {
        *maxval = 255;
    }

    if (rgb_spec) {
        *num_attribs = 3;
        set_tupletype("RGB_ALPHA", tupletype);
    }

    if (grayscale_spec) {
        *num_attribs = 1;
        set_tupletype("GRAYSCALE_ALPHA", tupletype);
    }

    if (*num_attribs < 1 || *num_attribs > MAX_NUM_ATTRIBS) {
        pm_errormsg("invalid number of generic attributes per vertex.");

        return 0;
    }

    if (tupletype_spec) {
        if(rgb_spec || grayscale_spec) {
            pm_errormsg("you may not provide -tupletype together with "
                        "-rgb or -grayscale.");

            return 0;
        }

        if (!set_tupletype(tupletype_tmp, tupletype)) {
            pm_errormsg("warning: invalid tuple type; using empty string.");

            set_tupletype(NULL, tupletype);
        }
    }

    free(option_def);

    return 1;
}



int
main(int argc, const char ** argv) {

    framebuffer_info fbi;
    boundary_info bi;
    Input input;
    bool no_more_commands;

    pm_proginit(&argc, (const char**)argv);

    set_tupletype(NULL, fbi.outpam.tuple_type);

    if (!parse_command_line(&argc,
                            argv,
                            &fbi.width,
                            &fbi.height,
                            &fbi.maxval,
                            &fbi.num_attribs,
                            fbi.outpam.tuple_type)) {
        return 1;
    }

    if (!init_framebuffer(&fbi)) {
        pm_errormsg("out of memory.");

        return 3;
    }

    init_boundary_buffer(&bi, fbi.height);

    input_init(&input);

    for (no_more_commands = false; !no_more_commands; )
        input_process_next_command(&input, &bi, &fbi, &no_more_commands);

    input_term(&input);
    free_boundary_buffer(&bi);
    free_framebuffer(&fbi);

    return 0;
}


