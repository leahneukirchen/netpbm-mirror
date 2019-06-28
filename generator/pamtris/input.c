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
  Indicates a whitespace-delimited input symbol. "begin" points to its first
  character, and "end" points to one position past its last character.
-----------------------------------------------------------------------------*/
    char * begin;
    char * end;
} Token;



static Token
nextToken(char * const startPos) {

    Token retval;
    char * p;

    for (p = startPos; *p && isspace(*p); ++p);

    retval.begin = p;

    for (; *p && !isspace(*p); ++p);

    retval.end = p;

    return retval;
}



static bool
stringIsValid(const char * const target,
              const char * const srcBegin,
              const char * const srcEnd) {

    unsigned int charsMatched;
    const char * p;

    for (p = srcBegin, charsMatched = 0;
         p != srcEnd && target[charsMatched] != '\0'; ++p) {

        if (*p == target[charsMatched])
            ++charsMatched;
        else
            break;
    }

    return (*p == '\0' || isspace(*p));
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
processM(Token *       const ntP,
         state_info *  const stateP,
         bool *        const unrecognizedCmdP,
         const char ** const errorP) {

    if (!stringIsValid(CMD_SET_MODE, ntP->begin, ntP->end)) {
        *unrecognizedCmdP = true;
    } else {
        *ntP = nextToken(ntP->end);

        *unrecognizedCmdP = false;

        if (*ntP->begin == '\0')
            pm_asprintf(errorP, "syntax error");
        else {
            makeLowercase(*ntP);

            switch (*ntP->begin) {
            case 't':
                if (!stringIsValid(ARG_TRIANGLES, ntP->begin, ntP->end))
                    pm_asprintf(errorP, "unrecognized drawing mode");
                else {
                    stateP->mode = DRAW_MODE_TRIANGLES;
                    stateP->draw = false;
                    stateP->next = 0;

                    *errorP = NULL;
                }
                break;
            case 's':
                if (!stringIsValid(ARG_STRIP, ntP->begin, ntP->end))
                    pm_asprintf(errorP, "unrecognized drawing mode");
                else {
                    stateP->mode = DRAW_MODE_STRIP;
                    stateP->draw = false;
                    stateP->next = 0;

                    *errorP = NULL;
                }
                break;
            case 'f':
                if (!stringIsValid(ARG_FAN, ntP->begin, ntP->end))
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
        }
    }
}



static void
processA(Token *            const ntP,
         state_info *       const stateP,
         framebuffer_info * const fbiP,
         bool *             const unrecognizedCmdP,
         long int *         const iArgs,
         const char **      const errorP) {

    if (!stringIsValid(CMD_SET_ATTRIBS, ntP->begin, ntP->end)) {
        *unrecognizedCmdP = true;
    } else {
        unsigned int i;

        *unrecognizedCmdP = false;

        for (i = 0, *errorP = NULL; i < fbiP->num_attribs && !*errorP; ++i) {
            char * strtolEnd;

            *ntP = nextToken(ntP->end);

            iArgs[i] = strtol(ntP->begin, &strtolEnd, 10);

            if (*ntP->begin == '\0' || strtolEnd != ntP->end)
                pm_asprintf(errorP, "syntax error");
            else {
                if (iArgs[i] < 0 || iArgs[i] > fbiP->maxval)
                    pm_asprintf(errorP, "argument(s) out of bounds");
            }
        }

        if (!*errorP) {
            unsigned int i;

            for (i = 0; i < fbiP->num_attribs; ++i)
                stateP->curr_attribs[i] = iArgs[i];
        }
    }
}



static void
processV(Token *                const ntP,
         state_info *           const stateP,
         struct boundary_info * const biP,
         framebuffer_info *     const fbiP,
         bool *                 const unrecognizedCmdP,
         long int *             const iArgs,
         const char **          const errorP) {

    if (!stringIsValid(CMD_VERTEX, ntP->begin, ntP->end))
        *unrecognizedCmdP = true;
    else {
        unsigned int i;

        *unrecognizedCmdP = false;

        for (i = 0, *errorP = NULL; i < 4 && !*errorP; ++i) {
            char * strtolEnd;

            *ntP = nextToken(ntP->end);

            iArgs[i] = strtol(ntP->begin, &strtolEnd, 10);

            if (*ntP->begin == '\0') {
                if (i != 3)
                    pm_asprintf(errorP, "syntax error");
                else
                    iArgs[i] = 1;
            } else {
                if (strtolEnd != ntP->end)
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
        }
    }
}



static void
processP(Token *            const ntP,
         framebuffer_info * const fbiP,
         bool *             const unrecognizedCmdP,
         const char **      const errorP) {

    if (!stringIsValid(CMD_PRINT, ntP->begin, ntP->end))
        *unrecognizedCmdP = true;
    else {
        *unrecognizedCmdP = false;

        print_framebuffer(fbiP);

        *errorP = NULL;
    }
}




static void
processExcl(Token *            const ntP,
            framebuffer_info * const fbiP,
            bool *             const unrecognizedCmdP,
            const char **      const errorP) {

    if (ntP->end - ntP->begin > 1)
        *unrecognizedCmdP = true;
    else {
        *unrecognizedCmdP = false;

        print_framebuffer(fbiP);

        *errorP = NULL;
    }
}



static void
clear(Token *            const ntP,
      framebuffer_info * const fbiP,
      const char **      const errorP) {

    *ntP = nextToken(ntP->end);

    if (*ntP->begin != '\0') {
        makeLowercase(*ntP);

        switch(*ntP->begin) {
        case 'i':
            if (!stringIsValid("image", ntP->begin, ntP->end))
                pm_asprintf(errorP, "unrecognized argument");
            else {
                clear_framebuffer(true, false, fbiP);
                *errorP = NULL;
            }
            break;
        case 'd':
            if (!stringIsValid("depth", ntP->begin, ntP->end))
                pm_asprintf(errorP, "unrecognized argument");
            else {
                clear_framebuffer(false, true, fbiP);
                *errorP = NULL;
            }
            break;
        case 'z':
            if (ntP->end - ntP->begin > 1)
                pm_asprintf(errorP, "unrecognized argument");
            else {
                clear_framebuffer(false, true, fbiP);
                *errorP = NULL;
            }
            break;
        default:
            pm_asprintf(errorP, "unrecognized argument");
        }
    } else {
        clear_framebuffer(true, true, fbiP);
        *errorP = NULL;
    }
}



static void
processC(Token *            const ntP,
         framebuffer_info * const fbiP,
         bool *             const unrecognizedCmdP,
         const char **      const errorP) {

    if (!stringIsValid(CMD_CLEAR, ntP->begin, ntP->end))
        *unrecognizedCmdP = true;
    else {
        *unrecognizedCmdP = false;

        clear(ntP, fbiP, errorP);
    }
}



static void
processAsterisk(Token *            const ntP,
                framebuffer_info * const fbiP,
                bool *             const unrecognizedCmdP,
                const char **      const errorP) {

    if (ntP->end - ntP->begin > 1)
        *unrecognizedCmdP = true;
    else {
        *unrecognizedCmdP = false;

        clear(ntP, fbiP, errorP);
    }
}



static void
processR(Token *                const ntP,
         state_info *           const stateP,
         framebuffer_info *     const fbiP,
         bool *                 const unrecognizedCmdP,
         long int *             const iArgs,
         const char **          const errorP) {

    if (!stringIsValid(CMD_RESET, ntP->begin, ntP->end))
        *unrecognizedCmdP = true;
    else {
        unsigned int i;

        *unrecognizedCmdP = false;

        for (i = 0, *errorP = NULL; i < 2 && !*errorP; ++i) {
            char * strtolEnd;

            *ntP = nextToken(ntP->end);

            iArgs[i] = strtol(ntP->begin, &strtolEnd, 10);

            if (*ntP->begin == '\0' || ntP->end != strtolEnd)
                pm_asprintf(errorP, "syntax error");
        }

        if (!*errorP) {
            if (iArgs[0] < 1 || iArgs[0] > PAM_OVERALL_MAXVAL)
                pm_asprintf(errorP, "invalid new maxval");
            else {
                if (iArgs[1] < 1 || iArgs[1] > MAX_NUM_ATTRIBS)
                    pm_asprintf(errorP, "invalid new number of generic vertex "
                                "attributes");
                else {
                    *ntP = nextToken(ntP->end);

                    if (*ntP->begin != '\0') {
                        if (!set_tupletype(ntP->begin,
                                           fbiP->outpam.tuple_type)) {
                            pm_message(
                                "warning: could not set new tuple type; "
                                "using a null string");
                            set_tupletype(NULL, fbiP->outpam.tuple_type);
                        }
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
            }
        }
    }
}



static void
processQ(Token *                const ntP,
         bool *                 const unrecognizedCmdP,
         bool *                 const noMoreCommandsP,
         const char **          const errorP) {

    if (!stringIsValid(CMD_QUIT, ntP->begin, ntP->end))
        *unrecognizedCmdP = true;
    else {
        *unrecognizedCmdP = false;

        *noMoreCommandsP = true;

        *errorP = NULL;
    }
}



void
input_process_next_command(Input *                const inputP,
                           struct boundary_info * const biP,
                           framebuffer_info *     const fbiP,
                           bool *                 const noMoreCommandsP) {
/*----------------------------------------------------------------------------
  Doesn't necessarily process a command, just the next line of input, which
  may be empty.

  Return *noMoreCommandsP true iff the next command is a quit command of
  there is no next command.
-----------------------------------------------------------------------------*/
    static state_info state;

    Token nt;

    long int iArgs[MAX_NUM_ATTRIBS];
        /* For storing potential integer arguments. */
    bool unrecognizedCmd;
        /* Unrecognized command detected */
    bool noMoreCommands;
    const char * error;
        /* Description of problem with the command; NULL if no problem.
           Meaningful only when 'unrecognizedCmd' is false.
        */

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

    nt = nextToken(inputP->buffer);

    makeLowercase(nt);

    noMoreCommands = false;  /* initial assumption */

    switch (nt.begin[0]) {
    case 'm':
        processM(&nt, &state, &unrecognizedCmd, &error);
        break;
    case 'a':
        processA(&nt, &state, fbiP, &unrecognizedCmd, iArgs, &error);
        break;
    case 'v':
        processV(&nt, &state, biP, fbiP, &unrecognizedCmd, iArgs, &error);
        break;
    case 'p':
        processP(&nt, fbiP, &unrecognizedCmd, &error);
        break;
    case '!':
        processExcl(&nt, fbiP, &unrecognizedCmd, &error);
        break;
    case 'c':
        processC(&nt, fbiP, &unrecognizedCmd, &error);
        break;
    case '*':
        processAsterisk(&nt, fbiP, &unrecognizedCmd, &error);
        break;
    case 'r':
        processR(&nt, &state, fbiP, &unrecognizedCmd, iArgs, &error);
        break;
    case 'q':
        processQ(&nt, &unrecognizedCmd, &noMoreCommands, &error);
        break;
    case '\0':
        break;
    default:
        unrecognizedCmd = true;
    }

    if (!noMoreCommands) {
        char const next = *nextToken(nt.end).begin;

        if (unrecognizedCmd) {
            pm_errormsg("error: unrecognized command: line %u.",
                        (unsigned)inputP->number);
        } else {
            if (error) {
                pm_errormsg("Error in line %u: %s",
                            (unsigned)inputP->number, error);
                pm_strfree(error);
            } else {
                if (next != '\0')
                    pm_message("warning: ignoring excess arguments: line %u",
                               (unsigned)inputP->number);
            }
        }
    }
    ++inputP->number;

    *noMoreCommandsP = noMoreCommands;
}


