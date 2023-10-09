/*
 *  params.c:           Parameter file and command line parsing
 *
 *  Written by:         Stefan Frank
 *                      Ullrich Hafner
 *
 *  This file is part of FIASCO (Fractal Image And Sequence COdec)
 *  Copyright (C) 1994-2000 Ullrich Hafner
 */

/*
 *  $Date: 2000/07/15 17:24:21 $
 *  $Author: hafner $
 *  $Revision: 5.2 $
 *  $State: Exp $
 */

#define _DEFAULT_SOURCE 1 /* New name for SVID & BSD source defines */
#define _BSD_SOURCE 1
    /* Make sure strdup() is in string.h and strcaseeq() is in nstring.h */
#define _XOPEN_SOURCE 500  /* Make sure strdup() is in string.h */

#include "config.h"

#include <assert.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>                       /* strtod() on SUN sparc */

#include <stdlib.h>
#include <string.h>

#include <getopt.h>                     /* system or ../lib */

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"

#include "types.h"
#include "macros.h"
#include "bit-io.h"
#include "misc.h"
#include "fiasco.h"

#include "params.h"


static void
setParameter(param_t *    const parameter,
             const char * const value) {
/*----------------------------------------------------------------------------

  Set value of 'parameter' to 'value'.

  No return value.

  Side effects:
     'parameter.value' is changed accordingly

-----------------------------------------------------------------------------*/
    assert (parameter);

    switch (parameter->type) {
    case PFLAG:
        if (value != NULL && *value != '\0') {
            if (strcaseeq (value, "TRUE"))
                parameter->value.b = YES;
            else if (strcaseeq (value, "FALSE"))
                parameter->value.b = NO;
            else if (strcaseeq (value, "YES"))
                parameter->value.b = YES;
            else if (strcaseeq (value, "NO"))
                parameter->value.b = NO;
            else {
                long int data;
                char     *endptr;

                data = strtol (value, &endptr, 0);
                if (*endptr != '\0' || endptr == value)
                    pm_message("Invalid value `%s' converted to %d",
                               value, (int) data);
                parameter->value.b = data ? YES : NO;
            }
        } else
            parameter->value.b = !parameter->value.b;
        break;
    case PINT: {
        long int  data;
        char     *endptr;

        data = strtol (value, &endptr, 0);
        if (*endptr != '\0' || endptr == value)
           pm_message("Invalid value `%s' converted to %d",
                      value, (int) data);
        parameter->value.i = data;
    } break;
    case PFLOAT: {
        double      data;
        char        *endptr;

        data = strtod (value, &endptr);
        if (*endptr != '\0' || endptr == value)
           pm_message("Invalid value `%s' converted to %f",
                      value, (double) data);
        parameter->value.f = data;
    } break;
    case PSTR:
    case POSTR:
        parameter->value.s = value ? strdup (value) : NULL;
        break;
    default:
         pm_error("Invalid parameter type for %s", parameter->name);
    }
}



static int
getParameterIndex(const param_t * const params,
                  const char *    const search_string) {
/*----------------------------------------------------------------------------
  Search for parameter with name 'search_string' in parameter struct.

  Return value: index of parameter or -1 if no matching parameter has been
  found
-----------------------------------------------------------------------------*/
    int n;
    int index = -1;

    assert (params && search_string);

    for (n = 0; params [n].name != NULL; n++) {
        if (strcaseeq (params [n].name, search_string)) {
            index = n;
            break;
        }
    }

    return index;
}



static void
readParameterFile(param_t * const params,
                  FILE *    const file) {
/*----------------------------------------------------------------------------

  Read parameter settings from 'file'.

  No return value.

  Side effects:
      'params [].value' are changed if specified in 'file'
-----------------------------------------------------------------------------*/
    char buffer [MAXSTRLEN];
    int  n = 0;

    assert (params && file);

    while (fgets (buffer, MAXSTRLEN, file) != NULL) {
        char *b;                          /* temporary variable */
        char *name;                       /* parameter name */
        char *value;                      /* parameter value */
        int   pind;                       /* current argument number */

        b = strchr (buffer, '#');
        if (b != NULL)                    /* Strip comments. */
            *b = '\0';

        b = strchr (buffer, '=');
        if (b == NULL)                    /* Strip lines that contain no '=' */
            continue;
        *b = '\0';                       /* Replace '=' by string terminator */

        /* Extract value of parameter */
        for (value = b + 1; ISSPACE (*value); value++)
            ;                              /* Delete leading spaces */

        for (b = value + strlen (value) - 1; b >= value && ISSPACE (*b); b--)
            *b = '\0';                     /* Delete trailing spaces. */

        /* Extract parameter name */
        for (name = buffer; ISSPACE (*name); name++)
            ;                              /* Delete leading spaces */

        for (b = name + strlen (name) - 1; b >= name && ISSPACE (*b); b--)
            *b = '\0';                     /* Delete trailing spaces. */

        pind = getParameterIndex(params, name);
        if (pind >= 0)
            setParameter(&params[pind], value);

        ++n;
    }
}



static void
usage(const param_t *  const params,
      const char *     const progname,
      const char *     const synopsis,
      const char *     const comment,
      const char *     const non_opt_string,
      bool_t           const show_all_options,
      const char *     const sys_file_name,
      const char *     const usr_file_name) {
/*----------------------------------------------------------------------------

  Generates and prints command line description from param_t struct 'params'.
  'progname' is the name of the executable, 'synopsis' a short program
  description, and 'comment' some more advice.
  If flag 'show_all_options' is set then print also options that are not
  associated with a short option character.
  'sys_file_name' and 'usr_file_name' are filenames to parameter files.

  No return value.
-----------------------------------------------------------------------------*/
    int   i;
    size_t width = 0;

    fprintf (stderr, "Usage: %s [OPTION]...%s\n", progname,
             non_opt_string ? non_opt_string : " ");
    if (synopsis != NULL)
        fprintf (stderr, "%s", synopsis);
    fprintf (stderr, "\n\n");
    fprintf (stderr, "Mandatory or optional arguments to long options "
             "are mandatory or optional\nfor short options too. "
             "Default values are surrounded by {}.\n");
    for (i = 0; params [i].name != NULL; i++) {
        if (params [i].optchar != '\0' || show_all_options) {
            if (params [i].type == POSTR)
                width = MAX(width, (strlen (params [i].name)
                                     + strlen (params [i].argument_name) + 2));
            else if (params [i].type != PFLAG)
                width = MAX(width, (strlen (params [i].name)
                                     + strlen (params [i].argument_name)));
            else
                width = MAX(width, (strlen (params [i].name)) - 1);
        }
    }
    for (i = 0; params [i].name != NULL; i++) {
        if (params [i].optchar != '\0' || show_all_options) {
            if (params [i].optchar != '\0')
                fprintf (stderr, "  -%c, --", params [i].optchar);
            else
                fprintf (stderr, "      --");

            if (params [i].type == POSTR)
                fprintf (stderr, "%s=[%s]%-*s  ", params [i].name,
                         params [i].argument_name,
                         (unsigned)
                         MAX(0, (width - 2 - strlen (params [i].name)
                                 - strlen (params [i].argument_name))), "");
            else if (params [i].type != PFLAG)
                fprintf (stderr, "%s=%-*s  ", params [i].name,
                         (unsigned)(width - strlen (params [i].name)),
                         params [i].argument_name);
            else
                fprintf (stderr, "%-*s  ",
                         (unsigned)(width + 1), params [i].name);

            fprintf (stderr, params [i].use, params [i].argument_name);

            switch (params [i].type) {
            case PFLAG:
                break;
            case PINT:
                fprintf (stderr, "{%d}", params [i].value.i);
                break;
            case PFLOAT:
                fprintf (stderr, "{%.2f}", (double) params [i].value.f);
                break;
            case PSTR:
            case POSTR:
                if (params [i].value.s)
                    fprintf (stderr, "{%s}", params [i].value.s);
                break;
            default:
               pm_error("type %d for %s invalid",
                        params [i].type, params [i].name);
            }
            fprintf (stderr, "\n");
        }
    }
    fprintf (stderr, "\n");

    fprintf (stderr, "Parameter initialization order:\n");
    fprintf (stderr,
             "1.) %s\n2.) $HOME/%s\t 3.) command line\t 4.) --config=file",
             sys_file_name, usr_file_name);
    fprintf (stderr, "\n\n");

    if (comment != NULL)
        fprintf (stderr, "%s\n", comment);

    exit (1);
}



const void *
parameter_value(const param_t * const params,
                const char *    const name) {
/*----------------------------------------------------------------------------
  Extract value of parameter 'name.' of the given parameters 'params'.

  Return value: value of given parameter

-----------------------------------------------------------------------------*/
    int pind = getParameterIndex(params, name);

    if (pind < 0)
        pm_error("Invalid parameter '%s'.", name);

    if (params[pind].type == PSTR || params[pind].type == POSTR)
        return params[pind].value.s;

    return &(params[pind].value);
}



int
parseargs(param_t *    const usr_params,
          int argc, char **argv,
          const char * const synopsis,
          const char * const comment,
          const char * const non_opt_string,
          const char * const path,
          const char * const sys_file_name,
          const char * const usr_file_name) {
/*----------------------------------------------------------------------------
  Perform the command line parsing.
  List of allowed parameters is given by 'usr_params'.
  Command line and number of parameters are given by 'argv' and 'argc'.
  'synopsis' contains a brief description of the program and
  'comment' may contain some additional advice.
  Initialization order of parameters:
     1.) Default values given by the param_t struct
     2.) System parameter-file ('path'/'sys_file_name')
     3.) User parameter-file ($HOME/'usr_file_name')
     4.) Command line parameters
     5.) Parameter-file forced by option -f (--config-file)

  Return value:
      index in ARGV of the first ARGV-element that is not an option.

  Side effects:
      the elements of ARGV are permuted
      usr_params [].value is modified
-----------------------------------------------------------------------------*/
    extern int optind;                   /* index in ARGV of the 1st element
                                            that is not an option */
    bool_t     detailed_help = NO;      /* NO if all parameters can be modified
                                            with short options too */
    unsigned   n1;                       /* number of user parameters */
    unsigned   n2;                       /* number of system parameters */
    bool_t     read_config_file = NO;    /* will override command line */
    param_t    *params;                  /* array of user and system params */
    param_t    *sys_params;              /* array of system parameters */
    param_t    detailed_sys_params [] =  /* detailed system parameters */
        {
            {"version", NULL, 'v', PFLAG, {0}, NULL,
             "Print program version number, then exit."},
            {"verbose", "NUM", 'V', PINT, {0}, "1",
             "Set level of verbosity to `%s'."},
            {"config", "FILE", 'f', PSTR, {0}, NULL,
             "Load `%s' to initialize parameters."},
            {"info", NULL, 'h', PFLAG, {0}, NULL,
             "Print brief help, then exit."},
            {"help", NULL, 'H', PFLAG, {0}, NULL,
             "Print detailed help, then exit."},
            {NULL, NULL, 0, PSTR, {0}, NULL, NULL }
        };
    param_t    short_sys_params [] =     /* short system parameters */
        {
            {"version", NULL, 'v', PFLAG, {0}, NULL,
             "Print program version number, then exit."},
            {"verbose", "NUM", 'V', PINT, {0}, "1",
             "Set level of verbosity to `%s'."},
            {"config", "FILE", 'f', PSTR, {0}, NULL,
             "Load `%s' to initialize parameters."},
            {"help", NULL, 'h', PFLAG, {0}, NULL,
             "Print this help, then exit."},
            {NULL, NULL, 0, PSTR, {0}, NULL, NULL }
        };
    const char * sys_path;                     /* path to system config file */

    pm_asprintf(&sys_path, "%s/%s", path, sys_file_name);

    /* Set parameters defaults */
    {
        param_t *p;

        for (p = usr_params; p->name != NULL; p++)
        {
            setParameter(p, p->default_value);
            if (p->optchar == '\0')
                detailed_help = YES;
        }

        sys_params = detailed_help ? detailed_sys_params : short_sys_params;

        for (p = sys_params; p->name != NULL; p++)
            setParameter(p, p->default_value);
    }
    /* Append system command line option to user parameters */
    for (n1 = 0; usr_params [n1].name != NULL; n1++)
        ;
    for (n2 = 0; sys_params [n2].name != NULL; n2++)
        ;
    MALLOCARRAY_NOFAIL(params, n1 + n2 + 1);

    memcpy(params, usr_params, n1 * sizeof(param_t));
    memcpy(params + n1, sys_params, (n2 + 1) * sizeof(param_t));

    {
        /* Try to open the system resource file 'path'/'sys_file_name' */

        FILE *parameter_file = open_file(sys_path, NULL, READ_ACCESS);

        if (parameter_file) {
            readParameterFile(params, parameter_file);
            fclose(parameter_file);
        }
    }
    {
        /* Try to read user resource file $HOME/'usr_file_name' */

        FILE *parameter_file = open_file(usr_file_name, "HOME", READ_ACCESS);

        if (parameter_file) {
            readParameterFile(params, parameter_file);
            fclose(parameter_file);
        }
    }
    {
        /* Parse command line options */

        extern char   *optarg;            /* argument of current option */
        struct option *long_options;      /* array of long options */
        int            option_index = 0;
        char           optstr [MAXSTRLEN]; /* string containing the legitimate
                                              option characters */
        int            optchar;           /* found option character */

        {
            /* Build short option string for getopt_long (). */
            param_t *p;                    /* counter */
            char    *ptr_optstr;           /* pointer to position in string */

            ptr_optstr = optstr;
            for (p = params; p->name != NULL; p++)
                if (p->optchar != '\0')
                {
                    *ptr_optstr++ = p->optchar;
                    if (p->type == POSTR)
                    {
                        *ptr_optstr++ = ':';
                        *ptr_optstr++ = ':';
                    }
                    else if (p->type != PFLAG)
                        *ptr_optstr++ = ':';
                }
            *ptr_optstr = '\0';
        }

        {
            /* Build long option string for getopt_long (). */

            int i;

            MALLOCARRAY_NOFAIL(long_options, n1 + n2 + 1);

            for (i = 0; params [i].name != NULL; i++) {
                long_options [i].name    = params [i].name;
                switch (params [i].type)
                {
                case PFLAG:
                    long_options [i].has_arg = 0;
                    break;
                case POSTR:
                    long_options [i].has_arg = 2;
                    break;
                case PINT:
                case PSTR:
                case PFLOAT:
                default:
                    long_options [i].has_arg = 1;
                    break;
                }
                long_options [i].has_arg = params [i].type != PFLAG;
                long_options [i].flag    = NULL;
                long_options [i].val     = 0;
            }
        }

        /* Parse command line */

        while ((optchar = getopt_long(argc, argv, optstr, long_options,
                                      &option_index)) != EOF) {
            int param_index = -1;

            switch (optchar) {
            case 0:
                param_index = option_index;
                break;
            case ':':
                if (detailed_help)
                    fprintf (stderr,
                             "Try `%s -h' or `%s --help' for "
                             "more information.\n",
                             argv [0], argv [0]);
                else
                    fprintf (stderr, "Try `%s --help' for more information.\n",
                             argv [0]);
                exit (2);
                break;
            case '?':
                if (detailed_help)
                    fprintf (stderr,
                             "Try `%s -h' or `%s --help' "
                             "for more information.\n",
                             argv [0], argv [0]);
                else
                    fprintf (stderr, "Try `%s --help' for more information.\n",
                             argv [0]);
                exit (2);
                break;
            default: {
                int i;

                for (i = 0; params [i].name != NULL; i++) {
                    if (params [i].optchar == optchar) {
                        param_index = i;
                        break;
                    }
                }
            }
            }
            /* Check for system options  */

            if (param_index >= 0) {
                setParameter(params + param_index, optarg ? optarg : "");
                if (streq(params[param_index].name, "help"))
                    usage(params, argv [0], synopsis, comment, non_opt_string,
                          YES, sys_path, usr_file_name);
                else if (streq(params[param_index].name, "info"))
                    usage(params, argv [0], synopsis, comment, non_opt_string,
                           NO, sys_path, usr_file_name);
                else if (streq(params[param_index].name, "version")) {
                    fprintf(stderr, "%s " VERSION "\n", argv [0]);
                    {
                        /* Kludge for standard Netpbm version announcement */
                        char * modified_argv[2];
                        int argc;
                        modified_argv[0] = argv[0];
                        modified_argv[1] = (char *) "--version";
                        argc = 2;
                        pm_proginit(&argc, (const char **) modified_argv);
                    }
                    exit (2);
                } else if (streq(params[param_index].name, "verbose"))
                    fiasco_set_verbosity(
                        * (fiasco_verbosity_e *) parameter_value(params,
                                                                 "verbose"));
                else if (streq(params[param_index].name, "config"))
                    read_config_file = YES;
                param_index = -1;           /* clear index flag */
            }
        }
        free(long_options);
    }

    /* Read config-file if specified by option -f */
    if (read_config_file) {
        char * filename;

        if ((filename = (char *) parameter_value(params, "config")) != NULL) {
            FILE * parameter_file;          /* input file */

            pm_message("Options set in file `%s' will override"
                       " command line options.", filename);
            parameter_file = open_file(filename, NULL, READ_ACCESS);
            if (parameter_file != NULL) {
                readParameterFile(params, parameter_file);
                fclose(parameter_file);
            } else
                pm_error("Failed to open config file '%s'", filename);
        } else
            pm_error("Invalid config filename.");
    }

    memcpy(usr_params, params, n1 * sizeof (param_t)); /* fill user struct */
    pm_strfree(sys_path);

    return optind;
}



void
ask_and_set(param_t *    const params,
            const char * const name,
            const char * const msg) {
/*----------------------------------------------------------------------------
  Ask user (print given message 'msg') for missing mandatory
  parameter 'name' of the given parameters 'params'.

  No return value.

  Side effects:
     'params ['name'].value' is changed
-----------------------------------------------------------------------------*/
    char answer[MAXSTRLEN];
    int  index = getParameterIndex(params, name);

    if (index < 0)
        pm_error("Invalid parameter '%s'.", name);

    if (msg)
        pm_message("%s", msg);

    switch (params[index].type) {
    case PFLAG:                       /* Unusual, at least. */
        pm_message("Flags should be initialized and set on demand, "
                   "not request");
    case PINT:
    case PSTR:
    case POSTR:
    case PFLOAT:
        scanf(MAXSTRLEN_SCANF, answer);
        setParameter(&params [index], answer);
        break;
    default:
        pm_error("Invalid parameter type for %s", name);
    }
}



void
write_parameters(const param_t * const params,
                 FILE *          const output) {
/*----------------------------------------------------------------------------
  Write all parameter settings to 'output'.

  No return value.
-----------------------------------------------------------------------------*/
    unsigned int pind;

    if (!params || !output)
        pm_error("Parameters must be not NULL.");

    for (pind = 0; params[pind].name != NULL; pind++) {
        fprintf(output, "# %s = ", params[pind].name);
        switch(params[pind].type) {
        case PFLAG:
            fprintf(output, "%s\n", params[pind].value.b ? "TRUE" : "FALSE");
            break;
        case PINT:
            fprintf(output, "%d\n", params[pind].value.i);
            break;
        case PFLOAT:
            fprintf(output, "%.4f\n", (double) params[pind].value.f);
            break;
        case PSTR:
        case POSTR:
            fprintf(output, "%s\n", params[pind].value.s);
            break;
        default:
            pm_error("Invalid type %d for parameter %s",
                     params[pind].type, params[pind].name);
        }
    }
    fputc ('\n', output);
}



