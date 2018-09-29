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

    char * tupletype_ptr;

    unsigned int width_spec, height_spec, maxval_spec, attribs_spec;
    unsigned int tupletype_spec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;  /* incremented by OPTENT3 */
    OPTENT3(0, "width",       OPT_INT,    width,          &width_spec,      0);
    OPTENT3(0, "height",      OPT_INT,    height,         &height_spec,     0);
    OPTENT3(0, "maxval",      OPT_INT,    maxval,         &maxval_spec,     0);
    OPTENT3(0, "num_attribs", OPT_INT,    num_attribs,    &attribs_spec,    0);
    OPTENT3(0, "tupletype",   OPT_STRING, &tupletype_ptr, &tupletype_spec,  0);

    opt.opt_table     = option_def;
    opt.short_allowed = false;
    opt.allowNegNum   = false;

    pm_optParseOptions3(argc_ptr, (char **)argv, opt, sizeof(opt), 0);

    if (!width_spec || !height_spec || !attribs_spec) {
        pm_errormsg(
            "you must at least specify -width, -height and -num_attribs.");

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

    if (*num_attribs < 1 || *num_attribs > MAX_NUM_ATTRIBS) {
        pm_errormsg("invalid number of generic attributes per vertex.");

        return 0;
    }

    if (tupletype_spec) {
        if (!set_tupletype(tupletype_ptr, tupletype)) {
            pm_errormsg("warning: invalid tuple type; using the null string.");

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
    input_info ii;

    pm_proginit(&argc, (const char**)argv);

    set_tupletype(NULL, fbi.outpam.tuple_type);

    if (!parse_command_line(&argc, argv,
                            &fbi.width, &fbi.height, &fbi.maxval,
                            &fbi.num_attribs, fbi.outpam.tuple_type)) {
        return 1;
    }

    if (!init_framebuffer(&fbi)) {
        pm_errormsg("out of memory.");

        return 3;
    }

    init_boundary_buffer(&bi, fbi.height);

    init_input_processor(&ii);

    while (process_next_command(&ii, &bi, &fbi));

    free_input_processor(&ii);
    free_boundary_buffer(&bi);
    free_framebuffer(&fbi);

    return 0;
}


