#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "pm.h"
#include "pbmfont.h"



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * font;    
    const char * builtin; 
    unsigned int verbose;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int fontSpec, builtinSpec;

    unsigned int option_def_index;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0, "font",      OPT_STRING, &cmdlineP->font, &fontSpec,        0);
    OPTENT3(0, "builtin",   OPT_STRING, &cmdlineP->builtin, &builtinSpec,  0);
    OPTENT3(0, "verbose",   OPT_FLAG,   NULL, &cmdlineP->verbose,          0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);
    /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (!fontSpec)
        cmdlineP->font = NULL;

    if (!builtinSpec)
        cmdlineP->builtin = NULL;
}



static void
reportFont(struct font * const fontP) {

    unsigned int n;
    unsigned int c;

    pm_message("FONT:");
    pm_message("  character dimensions: %uw x %uh",
               fontP->maxwidth, fontP->maxheight);
    pm_message("  Additional vert white space: %d pixels", fontP->y);

    for (c = 0, n = 0; c < ARRAY_SIZE(fontP->glyph); ++c)
        if (fontP->glyph[c])
            ++n;

    pm_message("  # characters: %u", n);
}



static void
computeFont(const char *   const fontName,
            const char *   const builtinName,
            struct font ** const fontPP) {

    struct font * fontP;

    if (fontName)
        fontP = pbm_loadfont(fontName);
    else {
        if (builtinName)
            fontP = pbm_defaultfont(builtinName);
        else
            fontP = pbm_defaultfont("bdf");
    }

    *fontPP = fontP;
}



int
main(int argc, const char *argv[]) {

    struct CmdlineInfo cmdline;
    struct font * fontP;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);
    
    computeFont(cmdline.font, cmdline.builtin, &fontP);

    if (cmdline.verbose)
        reportFont(fontP);

    pbm_dumpfont(fontP, stdout);
}



