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

#include "limits_pamtris.h"
#include "framebuffer.h"
#include "triangle.h"

#include "input.h"

#define MAX_COORD       32767
#define MIN_COORD       -MAX_COORD

#define DRAW_MODE_TRIANGLES 1
#define DRAW_MODE_STRIP     2
#define DRAW_MODE_FAN       3

#define CMD_SET_MODE        "mode"
#define CMD_SET_ATTRIBS     "attribs"
#define CMD_VERTEX      "vertex"
#define CMD_PRINT       "print"
#define CMD_CLEAR       "clear"
#define CMD_RESET       "reset"
#define CMD_QUIT        "quit"

#define ARG_TRIANGLES       "triangles"
#define ARG_STRIP       "strip"
#define ARG_FAN         "fan"
#define ARG_IMAGE       "image"
#define ARG_DEPTH       "depth"

#define WARNING_EXCESS_ARGS "warning: ignoring excess arguments: line %lu."
#define SYNTAX_ERROR        "syntax error: line %lu."

typedef struct {
    Xy v_xy;
        /* X- and Y-coordinates of the vertices for the current triangle.
           int32_t v_attribs[3][MAX_NUM_ATTRIBS + 1]; // Vertex attributes for
           the current triangle. Includes the Z-coordinates.
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
clear_attribs(state_info * const si,
              int32_t      const maxval,
              int16_t      const num_attribs) {

    unsigned int i;

    for (i = 0; i < num_attribs; i++) {
        si->curr_attribs[i] = maxval;
    }
}



void
init_input_processor(input_info * const ii) {

    MALLOCARRAY_NOFAIL(ii->buffer, 128);

    ii->length = 128;
    ii->number = 1;
}



void
free_input_processor(input_info * const ii) {
    free(ii->buffer);
}



typedef struct {
/*----------------------------------------------------------------------------
  Indicates a whitespace-delimited input symbol. "begin" points to its first
  character, and "end" points to one position past its last character.
-----------------------------------------------------------------------------*/
    char * begin;
    char * end;
} token;



static token
next_token(char * const startPos) {

    token retval;
    char * p;

    for (p = startPos; *p && isspace(*p); ++p);

    retval.begin = p;

    for (; *p && !isspace(*p); ++p)

    retval.end = p;

    return retval;
}



static bool
string_is_valid(const char * const target,
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
init_state(state_info * const si) {

    si->next = 0;
    si->draw = false;
    si->mode = DRAW_MODE_TRIANGLES;
}



static void
make_lowercase(token const t) {

    char * p;

    for (p = t.begin; t.begin != t.end; ++p)
        *p = tolower(*p);
}



static void
remove_comments(char * const str) {

    char * p;

    for (p = &str[0]; *p; ++p) {
        if (*p == '#') {
            *p = '\0';

            break;
        }
    }
}



int
process_next_command(input_info           * const line,
                     struct boundary_info * const bi,
                     framebuffer_info     * const fbi) {
/*----------------------------------------------------------------------------
  Doesn't necessarily process a command, just the next line of input, which
  may be empty. Always returns 1, except when it cannot read any more lines of
  input, an image buffer reallocation fails, or a "q" command is found in the
  input -- in such cases it returns 0.
-----------------------------------------------------------------------------*/
    static state_info state;

    token nt;

    long int i_args[MAX_NUM_ATTRIBS];
        /* For storing potential integer arguments. */
    char * strtol_end;
        /* To compare against nt.end when checking for errors with strtol */
    bool unrecognized_cmd;
        /* To print out an error message in case an unrecognized command was
           given.
        */
    bool unrecognized_arg;
        /* To print out an error message in case an unrecognized argument was
           given.
        */
    bool must_break_out;
        /* To break out of the below switch statement when an invalid argument
           is found.
        */
    bool ok;
        /* Indicates whether the input line was OK so that we can print out a
           warning in case of excess arguments.
        */

    /* initial values */
    strtol_end = NULL;
    unrecognized_cmd = false;
    unrecognized_arg = false;
    must_break_out = false;
    ok = false;

    if (!state.initialized) {
        init_state(&state);
        clear_attribs(&state, fbi->maxval, fbi->num_attribs);

        state.initialized = true;
    }

    if (getline(&line->buffer, &line->length, stdin) == -1) {
        return 0;
    }

    remove_comments(line->buffer);

    nt = next_token(line->buffer);

    make_lowercase(nt);

    switch (*nt.begin) {
    case 'm':
        if (!string_is_valid(CMD_SET_MODE, nt.begin, nt.end)) {
            unrecognized_cmd = true;

            break;
        }

        nt = next_token(nt.end);

        if (*nt.begin == '\0') {
            pm_errormsg(SYNTAX_ERROR, line->number);

            break;
        }

        make_lowercase(nt);

        switch(*nt.begin) {
        case 't':
            if (!string_is_valid(ARG_TRIANGLES, nt.begin, nt.end)) {
                unrecognized_arg = true;

                break;
            }

            state.mode = DRAW_MODE_TRIANGLES;
            state.draw = false;
            state.next = 0;

            ok = true;

            break;
        case 's':
            if (!string_is_valid(ARG_STRIP, nt.begin, nt.end)) {
                unrecognized_arg = true;

                break;
            }

            state.mode = DRAW_MODE_STRIP;
            state.draw = false;
            state.next = 0;

            ok = true;

            break;
        case 'f':
            if (!string_is_valid(ARG_FAN, nt.begin, nt.end)) {
                unrecognized_arg = true;

                break;
            }

            state.mode = DRAW_MODE_FAN;
            state.draw = false;
            state.next = 0;

            ok = true;

            break;
        default:
            unrecognized_arg = true;
        }

        if (unrecognized_arg) {
            pm_errormsg("error: unrecognized drawing mode in line %lu.",
                        line->number);
        }

        break;
    case 'a': {
        uint8_t i;
        if (!string_is_valid(CMD_SET_ATTRIBS, nt.begin, nt.end)) {
            unrecognized_cmd = true;

            break;
        }

        for (i = 0; i < fbi->num_attribs; i++) {
            nt = next_token(nt.end);

            i_args[i] = strtol(nt.begin, &strtol_end, 10);

            if (*nt.begin == '\0' || strtol_end != nt.end) {
                pm_errormsg(SYNTAX_ERROR, line->number);

                must_break_out = true;

                break;
            }

            if (i_args[i] < 0 || i_args[i] > fbi->maxval) {
                pm_errormsg("error: argument(s) out of bounds: line %lu.",
                            line->number);

                must_break_out = true;

                break;
            }
        }

        if (must_break_out)
        {
            break;
        }

        for (i = 0; i < fbi->num_attribs; i++) {
            state.curr_attribs[i] = i_args[i];
        }

        ok = true;

    } break;
    case 'v': {
        uint8_t i;

        if (!string_is_valid(CMD_VERTEX, nt.begin, nt.end)) {
            unrecognized_cmd = true;

            break;
        }

        for (i = 0; i < 3; i++) {
            nt = next_token(nt.end);

            i_args[i] = strtol(nt.begin, &strtol_end, 10);

            if (*nt.begin == '\0' || strtol_end != nt.end) {
                pm_errormsg(SYNTAX_ERROR, line->number);

                must_break_out = true;

                break;
            }

            if (i < 2) {
                if (i_args[i] < MIN_COORD || i_args[i] > MAX_COORD) {
                    pm_errormsg(
                        "error: coordinates out of bounds: line %lu.",
                        line->number);

                    must_break_out = true;

                    break;
                }
            } else {
                if (i_args[i] < 0 || i_args[i] > MAX_Z) {
                    pm_errormsg(
                        "error: Z component out of bounds: line %lu.",
                        line->number);

                    must_break_out = true;

                    break;
                }
            }
        }

        if (must_break_out)
        {
            break;
        }

        for (i = 0; i < fbi->num_attribs; i++) {
            state.v_attribs._[state.next][i] = state.curr_attribs[i];
        }

        state.v_attribs._[state.next][fbi->num_attribs] = i_args[2];

        state.v_xy._[state.next][0] = i_args[0];
        state.v_xy._[state.next][1] = i_args[1];

        state.next++;

        if (!state.draw) {
            if (state.next == 3) {
                state.draw = true;
            }
        }

        if (state.draw) {
            draw_triangle(state.v_xy, state.v_attribs, bi, fbi);
        }

        if (state.next == 3) {
            switch(state.mode) {
            case DRAW_MODE_FAN:
                state.next = 1;
                break;
            case DRAW_MODE_TRIANGLES:
                state.draw = false;
            case DRAW_MODE_STRIP:
            default:
                state.next = 0;
            }
        }

        ok = true;

    } break;
    case 'p':
        if (!string_is_valid(CMD_PRINT, nt.begin, nt.end)) {
            unrecognized_cmd = true;

            break;
        }
    case '!':
        if (*nt.begin == '!') {
            if (nt.end - nt.begin > 1) {
                unrecognized_cmd = true;

                break;
            }
        }

        print_framebuffer(fbi);

        ok = true;

        break;
    case 'c':
        if (!string_is_valid(CMD_CLEAR, nt.begin, nt.end)) {
            unrecognized_cmd = true;

            break;
        }
    case '*':
        if (*nt.begin == '*') {
            if(nt.end - nt.begin > 1) {
                unrecognized_cmd = true;

                break;
            }
        }

        nt = next_token(nt.end);

        if (*nt.begin != '\0') {
            make_lowercase(nt);

            switch(*nt.begin) {
            case 'i':
                if (!string_is_valid("image", nt.begin, nt.end)) {
                    unrecognized_arg = true;

                    break;
                }

                clear_framebuffer(true, false, fbi);

                break;
            case 'd':
                if (!string_is_valid("depth", nt.begin, nt.end)) {
                    unrecognized_arg = true;

                    break;
                }
            case 'z':
                if (*nt.begin == 'z') {
                    if (nt.end - nt.begin > 1) {
                        unrecognized_arg = true;

                        break;
                    }
                }

                clear_framebuffer(false, true, fbi);

                break;
            default:
                unrecognized_arg = true;
            }

            if (unrecognized_arg) {
                pm_errormsg("error: unrecognized argument: line %lu.",
                            line->number);

                break;
            }
        } else {
            clear_framebuffer(true, true, fbi);
        }

        ok = true;

        break;
    case 'r': {
        uint8_t i;

        if (!string_is_valid(CMD_RESET, nt.begin, nt.end)) {
            unrecognized_cmd = true;

            break;
        }

        for (i = 0; i < 2; i++) {
            nt = next_token(nt.end);

            i_args[i] = strtol(nt.begin, &strtol_end, 10);

            if (*nt.begin == '\0' || nt.end != strtol_end) {
                pm_errormsg(SYNTAX_ERROR, line->number);

                must_break_out = true;

                break;
            }
        }

        if (must_break_out) {
            break;
        }

        if (i_args[0] < 1 || i_args[0] > PAM_OVERALL_MAXVAL) {
            pm_errormsg("error: invalid new maxval: line %lu.",
                        line->number);

            break;
        }

        if (i_args[1] < 1 || i_args[1] > MAX_NUM_ATTRIBS) {
            pm_errormsg("error: invalid new number of generic vertex "
                        "attributes: line %lu.", line->number);

            break;
        }

        nt = next_token(nt.end);

        if (*nt.begin != '\0') {
            if (!set_tupletype(nt.begin, fbi->outpam.tuple_type)) {
                pm_message("warning: could not set new tuple type; "
                           "using a null string: line %lu.",
                           line->number);

                set_tupletype(NULL, fbi->outpam.tuple_type);
            }
        } else {
            set_tupletype(NULL, fbi->outpam.tuple_type);
        }

        if (!realloc_image_buffer(i_args[0], i_args[1], fbi)) {
            pm_errormsg
                (
                    "fatal error upon reading line %lu: "
                    "could not reallocate image buffer -- "
                    "terminating pamtris.",
                    line->number
                    );

            return 0;
        }

        state.next = 0;
        state.draw = false;

        clear_attribs(&state, fbi->maxval, fbi->num_attribs);

    } break;
    case 'q':
        if (!string_is_valid(CMD_QUIT, nt.begin, nt.end)) {
            unrecognized_cmd = true;

            break;
        }

        return 0;
    case '\0':
        break;
    default:
        unrecognized_cmd = true;
    }

    {
        char const next = *next_token(nt.end).begin;

        if (unrecognized_cmd) {
            pm_errormsg("error: unrecognized command: line %lu.",
                        line->number);
        }
        else if (ok && next != '\0') {
            pm_message(WARNING_EXCESS_ARGS, line->number);
        }
    }
    line->number++;

    return 1;
}


