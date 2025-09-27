/*=============================================================================
                              input.c
===============================================================================
   Input handling functions
=============================================================================*/
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>

#include "netpbm/mallocvar.h"
#include "netpbm/pm.h"
#include "netpbm/nstring.h"

#include "limits_pamtris.h"
#include "framebuffer.h"
#include "triangle.h"

#include "input.h"

#define DRAW_MODE_TRIANGLES 1
#define DRAW_MODE_STRIP     2
#define DRAW_MODE_FAN       3

#define CMD_SET_MODE        "mode"
#define CMD_SET_ATTRIBS     "attribs"
#define CMD_VERTEX          "vertex"
#define CMD_PRINT           "print"
#define CMD_CLEAR           "clear"
#define CMD_RESET           "reset"
#define CMD_QUIT            "quit"

#define ARG_TRIANGLES       "triangles"
#define ARG_STRIP           "strip"
#define ARG_FAN             "fan"
#define ARG_IMAGE           "image"
#define ARG_DEPTH           "depth"


typedef struct {
    Xy v_xy;
        /* X- and Y-coordinates of the vertices for the current triangle.
        */
    Attribs v_attribs;
        /* Vertex attributes for the current triangle. Includes the
           Z-coordinates.
        */
    int32_t curr_attribs[MAX_NUM_ATTRIBS];
        /* Attributes that will be assigned to the next vertex. Does not
           include the Z-coordinate.
        */
    uint8_t next;
        /* Index of the next vertex to be read. */
    bool draw;
        /* If true, draws a new triangle upon reading a new vertex. */

    uint8_t mode;
        /* Drawing mode. */

    bool initialized;
} state_info;



static void
clearAttribs(state_info * const si,
             int32_t      const maxval,
             int16_t      const num_attribs) {

    unsigned int i;

    for (i = 0; i < num_attribs; ++i) {
        si->curr_attribs[i] = maxval;
    }
}



void
input_init(Input * const inputP) {

    inputP->buffer = NULL;
    inputP->length = 0;
    inputP->number = 1;
}



void
input_term(Input * const inputP) {

    if (inputP->buffer)
        free(inputP->buffer);
}



typedef struct {
/*----------------------------------------------------------------------------
  Value of a whitespace-delimited input symbol.
-----------------------------------------------------------------------------*/
    bool null;
        /* This is a null token.  'begin' and 'end' are meaningless */
    char * begin;
        /* pointer to first character of token, in some unowned storage */
    char * end;
        /* pointer to one position past last character of token */
} Token;



static Token
nextToken(char * const startPos) {
/*----------------------------------------------------------------------------
   The token after the one that starts at 'startPos'.

   Tokens are separated by any amount of whitespace.

   Iff there is no next token, return a null token.
-----------------------------------------------------------------------------*/
    Token retval;
    char * p;

    for (p = startPos; *p && isspace(*p); ++p);

    if (*p) {
        retval.null = false;
        retval.begin = p;

        for (; *p && !isspace(*p); ++p);

        retval.end = p;
    } else
        retval.null = true;

    return retval;
}



static Token
tokenAfter(Token const currentToken) {

    if (currentToken.null)
        return currentToken;
    else
        return nextToken(currentToken.end);
}



static bool
tokenIsPrefix(Token        const token,
              const char * const target) {
/*----------------------------------------------------------------------------
   The value of token 'token' is a prefix of the NUL-terminated string
   'target'.
-----------------------------------------------------------------------------*/
    if (token.null)
        return false;
    else {
        unsigned int charsMatchedCt;
        const char * p;

        for (p = token.begin, charsMatchedCt = 0;
             p != token.end && target[charsMatchedCt] != '\0'; ++p) {

            if (*p == target[charsMatchedCt])
                ++charsMatchedCt;
            else
                break;
        }

        return (*p == '\0' || isspace(*p));
    }
}



static void
initState(state_info * const siP) {

    siP->next = 0;
    siP->draw = false;
    siP->mode = DRAW_MODE_TRIANGLES;
}



static void
makeLowercase(Token const t) {

    char * p;

    for (p = t.begin; p != t.end; ++p)
        *p = tolower(*p);
}



static void
removeComments(char * const str) {

    char * p;

    for (p = &str[0]; *p; ++p) {
        if (*p == '#') {
            *p = '\0';

            break;
        }
    }
}



static void
processMode(Token         const firstArgToken,
            state_info *  const stateP,
            bool *        const excessArgumentsP,
            const char ** const errorP) {

    if (firstArgToken.null)
        pm_asprintf(errorP, "syntax error: no drawing mode argument");
    else {
        makeLowercase(firstArgToken);

        switch (*firstArgToken.begin) {
        case 't':
            if (!tokenIsPrefix(firstArgToken, ARG_TRIANGLES))
                pm_asprintf(errorP, "unrecognized drawing mode");
            else {
                stateP->mode = DRAW_MODE_TRIANGLES;
                stateP->draw = false;
                stateP->next = 0;

                *errorP = NULL;
            }
            break;
        case 's':
            if (!tokenIsPrefix(firstArgToken, ARG_STRIP))
                pm_asprintf(errorP, "unrecognized drawing mode");
            else {
                stateP->mode = DRAW_MODE_STRIP;
                stateP->draw = false;
                stateP->next = 0;

                *errorP = NULL;
            }
            break;
        case 'f':
            if (!tokenIsPrefix(firstArgToken, ARG_FAN))
                pm_asprintf(errorP, "unrecognized drawing mode");
            else {
                stateP->mode = DRAW_MODE_FAN;
                stateP->draw = false;
                stateP->next = 0;

                *errorP = NULL;
            }
            break;
        default:
            pm_asprintf(errorP, "unrecognized drawing mode");
        }
        if (!*errorP)
            *excessArgumentsP =  !tokenAfter(firstArgToken).null;
    }
}



static void
processAttrib(Token              const firstArgToken,
              state_info *       const stateP,
              framebuffer_info * const fbiP,
              bool *             const excessArgumentsP,
              long int *         const iArgs,
              const char **      const errorP) {

    Token nt;
    unsigned int i;

    for (i = 0, *errorP = NULL, nt = firstArgToken;
         i < fbiP->num_attribs && !*errorP;
         ++i, nt = tokenAfter(nt)) {

        char * strtolEnd;

        if (nt.null)
            pm_asprintf(errorP, "syntax error: only %u arguments", i);
        else {
            iArgs[i] = strtol(nt.begin, &strtolEnd, 10);

            if (strtolEnd != nt.end)
                pm_asprintf(errorP, "syntax error: argument not numeric");
            else {
                if (iArgs[i] < 0 || iArgs[i] > fbiP->maxval)
                    pm_asprintf(errorP, "argument(s) out of bounds");
            }
        }
    }

    if (!*errorP) {
        unsigned int i;

        for (i = 0; i < fbiP->num_attribs; ++i)
            stateP->curr_attribs[i] = iArgs[i];

        *excessArgumentsP =  !tokenAfter(nt).null;
    }
}



static void
processVertex(Token                  const firstArgToken,
              state_info *           const stateP,
              struct boundary_info * const biP,
              framebuffer_info *     const fbiP,
              bool *                 const excessArgumentsP,
              long int *             const iArgs,
              const char **          const errorP) {

    Token nt;
    unsigned int i;

    for (i = 0, *errorP = NULL, nt = firstArgToken;
         i < 4 && !*errorP;
         ++i, nt = tokenAfter(nt)) {

        if (nt.null) {
            if (i != 3)
                pm_asprintf(errorP, "syntax error");
            else
                iArgs[i] = 1;
        } else {
            char * strtolEnd;

            iArgs[i] = strtol(nt.begin, &strtolEnd, 10);

            if (strtolEnd != nt.end)
                pm_asprintf(errorP, "syntax error");
        }

        if (!*errorP) {
            if (i < 3) {
                if (iArgs[i] < MIN_COORD || iArgs[i] > MAX_COORD)
                    pm_asprintf(errorP, "coordinates out of bounds");
            } else {
                if (iArgs[i] < MIN_INPUT_W || iArgs[i] > MAX_INPUT_W)
                    pm_asprintf(errorP,
                                "perspective correction factor (w) "
                                "out of bounds");
            }
        }
    }

    if (!*errorP) {
        unsigned int i;

        for (i = 0; i < fbiP->num_attribs; ++i) {
            stateP->v_attribs._[stateP->next][i] = stateP->curr_attribs[i];
        }

        stateP->v_attribs._[stateP->next][fbiP->num_attribs + 0] =
            iArgs[2];
        stateP->v_attribs._[stateP->next][fbiP->num_attribs + 1] =
            iArgs[3];

        stateP->v_xy._[stateP->next][0] = iArgs[0];
        stateP->v_xy._[stateP->next][1] = iArgs[1];

        ++stateP->next;

        if (!stateP->draw) {
            if (stateP->next == 3)
                stateP->draw = true;
        }

        if (stateP->draw)
            draw_triangle(stateP->v_xy, stateP->v_attribs, biP, fbiP);

        if (stateP->next == 3) {
            switch(stateP->mode) {
            case DRAW_MODE_FAN:
                stateP->next = 1;
                break;
            case DRAW_MODE_TRIANGLES:
                stateP->draw = false;
                stateP->next = 0;
                break;
            case DRAW_MODE_STRIP:
                stateP->next = 0;
                break;
            default:
                stateP->next = 0;
            }
        }
        *excessArgumentsP =  !nt.null;
    }
}



static void
processPrint(Token              const firstArgToken,
             framebuffer_info * const fbiP,
             bool *             const excessArgumentsP,
             const char **      const errorP) {

    print_framebuffer(fbiP);

    *excessArgumentsP =  !firstArgToken.null;

    *errorP = NULL;
}




static void
processExcl(Token              const firstArgToken,
            framebuffer_info * const fbiP,
            bool *             const excessArgumentsP,
            const char **      const errorP) {


    print_framebuffer(fbiP);

    *excessArgumentsP =  !firstArgToken.null;

    *errorP = NULL;
}



static void
processClear(Token              const firstArgToken,
             framebuffer_info * const fbiP,
             bool *             const excessArgumentsP,
             const char **      const errorP) {

    if (firstArgToken.null) {
        clear_framebuffer(true, true, fbiP);
        *excessArgumentsP = false;
        *errorP = NULL;
    } else {
        makeLowercase(firstArgToken);

        switch(*firstArgToken.begin) {
        case 'i':
            if (!tokenIsPrefix(firstArgToken, "image"))
                pm_asprintf(errorP, "unrecognized argument");
            else {
                clear_framebuffer(true, false, fbiP);
                *errorP = NULL;
            }
            break;
        case 'd':
            if (!tokenIsPrefix(firstArgToken, "depth"))
                pm_asprintf(errorP, "unrecognized argument");
            else {
                clear_framebuffer(false, true, fbiP);
                *errorP = NULL;
            }
            break;
        case 'z':
            if (firstArgToken.end - firstArgToken.begin > 1)
                pm_asprintf(errorP, "unrecognized argument");
            else {
                clear_framebuffer(false, true, fbiP);
                *errorP = NULL;
            }
            break;
        default:
            pm_asprintf(errorP, "unrecognized argument");
        }
        *excessArgumentsP =  !tokenAfter(firstArgToken).null;
    }
}



static void
processReset(Token              const firstArgToken,
             state_info *       const stateP,
             framebuffer_info * const fbiP,
             bool *             const excessArgumentsP,
             long int *         const iArgs,
             const char **      const errorP) {

    Token nt;
    unsigned int i;

    for (i = 0, *errorP = NULL, nt = firstArgToken;
         i < 2 && !*errorP;
         ++i, nt = tokenAfter(nt)) {

        if (nt.null)
            pm_asprintf(errorP, "syntax error: only %u arguments", i);
        else {
            char * strtolEnd;

            iArgs[i] = strtol(nt.begin, &strtolEnd, 10);

            if (nt.end != strtolEnd) {
                pm_asprintf(errorP, "syntax error: non-numeric junk "
                            "in argument %u", i);
            }
        }
    }

    if (!*errorP) {
        if (iArgs[0] < 1 || iArgs[0] > PAM_OVERALL_MAXVAL)
            pm_asprintf(errorP, "invalid new maxval");
        else {
            if (iArgs[1] < 1 || iArgs[1] > MAX_NUM_ATTRIBS)
                pm_asprintf(errorP, "invalid new number of generic vertex "
                            "attributes");
            else {
                if (!nt.null) {
                    if (!set_tupletype(nt.begin,
                                       fbiP->outpam.tuple_type)) {
                        pm_message(
                            "warning: could not set new tuple type; "
                            "using a null string");
                        set_tupletype(NULL, fbiP->outpam.tuple_type);
                    }
                    nt = tokenAfter(nt);
                } else
                    set_tupletype(NULL, fbiP->outpam.tuple_type);

                if (!realloc_image_buffer(iArgs[0], iArgs[1], fbiP)) {
                    pm_error("Unable to allocate memory for "
                             "image buffer");
                }

                stateP->next = 0;
                stateP->draw = false;

                clearAttribs(stateP, fbiP->maxval, fbiP->num_attribs);
            }
            *excessArgumentsP =  !nt.null;
        }
    }
}



static void
processQuit(Token         const firstArgToken,
            bool *        const excessArgumentsP,
            const char ** const errorP) {

    *excessArgumentsP =  !firstArgToken.null;

    *errorP = NULL;
}



static void
processCommandToken(Token                  const cmdToken,
                    state_info *           const stateP,
                    struct boundary_info * const biP,
                    framebuffer_info *     const fbiP,
                    bool *                 const noMoreCommandsP,
                    bool *                 const excessArgumentsP,
                    const char **          const errorP) {
/*----------------------------------------------------------------------------
   Process command whose command token is 'commandToken'.

   We may modify the storage pointed to by 'cmdToken', converting upper case
   to lower case.
-----------------------------------------------------------------------------*/
    long int iArgs[MAX_NUM_ATTRIBS];
        /* For storing potential integer arguments. */
    bool noMoreCommands;
    const char * error;
        /* Description of problem with the command; NULL if no problem.
        */
    bool excessArguments;
        /* The command line has extra tokens beyond the end of a valid
           command.

           This is meaningful only when 'error' is null.
        */

    makeLowercase(cmdToken);

    noMoreCommands = false;  /* initial assumption */

    if (tokenIsPrefix(cmdToken, CMD_SET_MODE)) {
        processMode(tokenAfter(cmdToken), stateP,
                    &excessArguments, &error);
    } else if (tokenIsPrefix(cmdToken, CMD_SET_ATTRIBS)) {
        processAttrib(tokenAfter(cmdToken), stateP, fbiP,
                      &excessArguments, iArgs, &error);
    } else if (tokenIsPrefix(cmdToken, CMD_VERTEX)) {
        processVertex(tokenAfter(cmdToken), stateP, biP, fbiP,
                      &excessArguments, iArgs, &error);
    } else if (tokenIsPrefix(cmdToken, CMD_PRINT)) {
        processPrint(tokenAfter(cmdToken), fbiP,
                     &excessArguments, &error);
    } else if (*cmdToken.begin == '!' && cmdToken.end == cmdToken.begin + 1) {
        processExcl(tokenAfter(cmdToken), fbiP,
                    &excessArguments, &error);
    } else if (tokenIsPrefix(cmdToken, CMD_CLEAR) ||
               (*cmdToken.begin == '*' &&
                cmdToken.end == cmdToken.begin + 1)) {
        processClear(tokenAfter(cmdToken), fbiP,
                     &excessArguments, &error);
    } else if (tokenIsPrefix(cmdToken, CMD_RESET)) {
        processReset(tokenAfter(cmdToken), stateP, fbiP,
                 &excessArguments, iArgs, &error);
    } else if (tokenIsPrefix(cmdToken, CMD_QUIT)) {
        processQuit(tokenAfter(cmdToken),
                    &excessArguments, &error);
        noMoreCommands = true;
    } else
        pm_asprintf(&error, "unrecognized command");

    if (!noMoreCommands) {
        if (error) {
            *errorP = error;
        } else {
            *errorP = NULL;
            *excessArgumentsP = excessArguments;
        }
    }

    *noMoreCommandsP = noMoreCommands;
}



void
input_process_next_command(Input *                const inputP,
                           struct boundary_info * const biP,
                           framebuffer_info *     const fbiP,
                           bool *                 const noMoreCommandsP) {
/*----------------------------------------------------------------------------
  Doesn't necessarily process a command, just the next line of input, which
  may be empty.

  Return *noMoreCommandsP true iff the next command is a quit command or
  there is no next command.

  We may modify the input buffer, converting upper case to lower.
-----------------------------------------------------------------------------*/
    static state_info state;

    Token cmdToken;

    if (!state.initialized) {
        initState(&state);
        clearAttribs(&state, fbiP->maxval, fbiP->num_attribs);

        state.initialized = true;
    }

    {
        int eof;
        size_t lineLen;

        pm_getline(stdin, &inputP->buffer, &inputP->length, &eof, &lineLen);

        if (eof) {
            *noMoreCommandsP = true;
            return;
        }
    }

    removeComments(inputP->buffer);

    cmdToken = nextToken(inputP->buffer);

    if (cmdToken.null)
        *noMoreCommandsP = false;
    else {
        const char * error;
        bool excessArguments;

        processCommandToken(cmdToken, &state, biP, fbiP,
                            noMoreCommandsP, &excessArguments, &error);

        if (!*noMoreCommandsP) {
            if (error) {
                pm_errormsg("Error in line %u: %s",
                            (unsigned)inputP->number, error);
                pm_strfree(error);
            } else if (excessArguments) {
                pm_message("warning: ignoring excess arguments: line %u",
                           (unsigned)inputP->number);
            }
        }
    }
    ++inputP->number;
}


