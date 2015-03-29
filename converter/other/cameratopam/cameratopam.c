/*
   This is derived from Dave Coffin's raw photo decoder, dcraw.c,
   Copyright 1997-2005 by Dave Coffin, dcoffin a cybercom o net.

   See the COPYRIGHT file in the same directory as this file for
   information on copyright and licensing.
 */


#define _BSD_SOURCE 1   /* Make sure string.h contains strdup() */
#define _XOPEN_SOURCE 500
   /* Make sure unistd.h contains swab(), string.h constains strdup() */

#include "pm_config.h"

#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef HAVE_IO_H
  #include <io.h>
#endif
#if !MSVCRT
  #include <unistd.h>
#endif

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "pam.h"

#include "global_variables.h"
#include "cameratopam.h"
#include "util.h"
#include "decode.h"
#include "identify.h"
#include "bayer.h"
#include "foveon.h"
#include "dng.h"

/*
   All global variables are defined here, and all functions that
   access them are prefixed with "CLASS".  Note that a thread-safe
   C++ class cannot have non-const static local variables.
 */
FILE * ifp;
short order;
char make[64], model[70], model2[64], *meta_data;
time_t timestamp;
int data_offset, meta_offset, meta_length;
int tiff_data_compression, kodak_data_compression;
int raw_height, raw_width, top_margin, left_margin;
int height, width, fuji_width, colors, tiff_samples;
int black, maximum, clip_max;
int iheight, iwidth, shrink;
int is_dng, is_canon, is_foveon, use_coeff, use_gamma;
int flip, xmag, ymag;
int zero_after_ff;
unsigned filters;
unsigned short  white[8][8];
unsigned short  curve[0x1000];
int fuji_secondary;
float cam_mul[4], coeff[3][4];
float pre_mul[4];
int histogram[3][0x2000];
jmp_buf failure;
int use_secondary;
bool verbose;

#define CLASS

#define FORC3 for (c=0; c < 3; c++)
#define FORC4 for (c=0; c < colors; c++)

static void CLASS merror (const void *ptr, const char *where)
{
    if (ptr == NULL)
        pm_error ("Out of memory in %s", where);
}

struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* "-" means Standard Input */
    float bright;
    float red_scale;
    float blue_scale;
    const char * profile;
    unsigned int identify_only;
    unsigned int verbose;
    unsigned int half_size;
    unsigned int four_color_rgb;
    unsigned int document_mode;
    unsigned int quick_interpolate;
    unsigned int use_auto_wb;
    unsigned int use_camera_wb;
    unsigned int use_camera_rgb;
    unsigned int use_secondary;
    unsigned int no_clip_color;
    unsigned int linear;
};


static struct CmdlineInfo cmdline;

static void
parseCommandLine(int argc, char ** argv,
                 struct CmdlineInfo *cmdlineP) {
/*----------------------------------------------------------------------------
   Note that many of the strings that this function returns in the
   *cmdlineP structure are actually in the supplied argv array.  And
   sometimes, one of these strings is actually just a suffix of an entry
   in argv!
-----------------------------------------------------------------------------*/
    optStruct3 opt;
    optEntry *option_def;
    unsigned int option_def_index;
    unsigned int brightSpec, red_scaleSpec, blue_scaleSpec,
        profileSpec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;
    opt.allowNegNum = FALSE;

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "bright", 
            OPT_FLOAT,   &cmdlineP->bright,     &brightSpec, 0);
    OPTENT3(0, "red_scale", 
            OPT_FLOAT,   &cmdlineP->red_scale,  &red_scaleSpec, 0);
    OPTENT3(0, "blue_scale", 
            OPT_FLOAT,   &cmdlineP->blue_scale, &blue_scaleSpec, 0);
    OPTENT3(0, "profile", 
            OPT_STRING,  &cmdlineP->profile,    &profileSpec, 0);
    OPTENT3(0,   "identify_only",   
            OPT_FLAG,    NULL, &cmdlineP->identify_only, 0);
    OPTENT3(0,   "verbose",   
            OPT_FLAG,    NULL, &cmdlineP->verbose, 0);
    OPTENT3(0,   "half_size",   
            OPT_FLAG,    NULL, &cmdlineP->half_size, 0);
    OPTENT3(0,   "four_color_rgb",   
            OPT_FLAG,    NULL, &cmdlineP->four_color_rgb, 0);
    OPTENT3(0,   "document_mode",   
            OPT_FLAG,    NULL, &cmdlineP->document_mode, 0);
    OPTENT3(0,   "quick_interpolate",   
            OPT_FLAG,    NULL, &cmdlineP->quick_interpolate, 0);
    OPTENT3(0,   "balance_auto",   
            OPT_FLAG,    NULL, &cmdlineP->use_auto_wb, 0);
    OPTENT3(0,   "balance_camera",   
            OPT_FLAG,    NULL, &cmdlineP->use_camera_wb, 0);
    OPTENT3(0,   "use_secondary",   
            OPT_FLAG,    NULL, &cmdlineP->use_secondary, 0);
    OPTENT3(0,   "no_clip_color",   
            OPT_FLAG,    NULL, &cmdlineP->no_clip_color, 0);
    OPTENT3(0,   "rgb",   
            OPT_FLAG,    NULL, &cmdlineP->use_camera_rgb, 0);
    OPTENT3(0,   "linear",   
            OPT_FLAG,    NULL, &cmdlineP->linear, 0);

    pm_optParseOptions3(&argc, argv, opt, sizeof(opt), 0);

    if (!brightSpec)
        cmdlineP->bright = 1.0;
    if (!red_scaleSpec)
        cmdlineP->red_scale = 1.0;
    if (!blue_scaleSpec)
        cmdlineP->blue_scale = 1.0;
    if (!profileSpec)
        cmdlineP->profile = NULL;


    if (argc - 1 == 0)
        cmdlineP->inputFileName = strdup("-");  /* he wants stdin */
    else if (argc - 1 == 1)
        cmdlineP->inputFileName = strdup(argv[1]);
    else 
        pm_error("Too many arguments.  The only argument accepted "
                 "is the input file name");
}

  

static void CLASS
fixBadPixels(Image const image) {
/*----------------------------------------------------------------------------
  Search from the current directory up to the root looking for
  a ".badpixels" file, and fix those pixels now.
-----------------------------------------------------------------------------*/
    if (filters) {
        FILE *fp;
        char *fname, *cp, line[128];
        int len, time, row, col, rad, tot, n, fixed=0;

        for (len=16 ; ; len *= 2) {
            fname = malloc (len);
            if (!fname) return;
            if (getcwd (fname, len-12))
                break;
            free (fname);
            if (errno != ERANGE)
                return;
        }
#if MSVCRT
        if (fname[1] == ':')
            memmove (fname, fname+2, len-2);
        for (cp=fname; *cp; cp++)
            if (*cp == '\\') *cp = '/';
#endif
        cp = fname + strlen(fname);
        if (cp[-1] == '/')
            --cp;
        fp = NULL; /* initial value */
        while (*fname == '/') {
            strcpy (cp, "/.badpixels");
            fp = fopen (fname, "r");
            if (fp)
                break;
            if (cp == fname)
                break;
            while (*--cp != '/');
        }
        free (fname);
        if (fp) {
            while (fgets (line, 128, fp)) {
                char * cp;
                cp = strchr (line, '#');
                if (cp) *cp = 0;
                if (sscanf (line, "%d %d %d", &col, &row, &time) != 3)
                    continue;
                if ((unsigned) col >= width || (unsigned) row >= height)
                    continue;
                if (time > timestamp) continue;
                for (tot=n=0, rad=1; rad < 3 && n==0; rad++) {
                    unsigned int r;
                    for (r = row-rad; r <= row+rad; ++r) {
                        unsigned int c;
                        for (c = col-rad; c <= col+rad; ++c) {
                            if ((unsigned) r < height &&
                                (unsigned) c < width  &&
                                (r != row || c != col) &&
                                FC(r,c) == FC(row,col)) {
                                tot += BAYER(r,c);
                                ++n;
                            }
                        }
                    }
                }
                BAYER(row,col) = tot/n;
                if (cmdline.verbose) {
                    if (!fixed++)
                        pm_message ("Fixed bad pixels at: %d,%d", col, row);
                }
            }
            fclose (fp);
        }
    }
}



static void CLASS
scaleColors(Image const image) {

    int row;
    int c;
    int val;
    int shift;
    int min[4], max[4], count[4];
    double sum[4], dmin;
    int scaleMax;

    scaleMax = maximum - black;  /* initial value */
    if (cmdline.use_auto_wb || (cmdline.use_camera_wb && camera_red == -1)) {
        unsigned int row;
        FORC4 min  [c] = INT_MAX;
        FORC4 max  [c] = 0;
        FORC4 count[c] = 0;
        FORC4 sum  [c] = 0;
        for (row = 0; row < height; ++row) {
            unsigned int col;
            for (col = 0; col < width; ++col) {
                FORC4 {
                    int val;
                    val = image[row*width+col][c];
                    if (val != 0) {
                        if (min[c] > val)
                            min[c] = val;
                        if (max[c] < val)
                            max[c] = val;
                        val -= black;
                        if (val <= scaleMax-25) {
                            sum  [c] += MAX(0, val);
                            count[c] += 1;
                        }
                    }
                }
            }
        }
        FORC4 pre_mul[c] = count[c] / sum[c];
    }
    if (cmdline.use_camera_wb && camera_red != -1) {
        unsigned int row;
        FORC4 count[c] = sum[c] = 0;
        for (row = 0; row < 8; ++row) {
            unsigned int col;
            for (col = 0; col < 8; ++col) {
                c = FC(row,col);
                if ((val = white[row][col] - black) > 0)
                    sum[c] += val;
                ++count[c];
            }
        }
        val = 1;
        FORC4 if (sum[c] == 0) val = 0;
        if (val)
            FORC4 pre_mul[c] = count[c] / sum[c];
        else if (camera_red && camera_blue)
            memcpy(pre_mul, cam_mul, sizeof pre_mul);
        else
            pm_message ("Cannot use camera white balance.");
    }
    if (!use_coeff) {
        pre_mul[0] *= cmdline.red_scale;
        pre_mul[2] *= cmdline.blue_scale;
    }
    dmin = DBL_MAX;
    FORC4 if (dmin > pre_mul[c])
        dmin = pre_mul[c];
    FORC4 pre_mul[c] /= dmin;

    for (shift = 0; scaleMax << shift < 0x8000; ++shift);

    FORC4 pre_mul[c] *= 1 << shift;
    scaleMax <<= shift;

    if (cmdline.linear || cmdline.bright < 1) {
        scaleMax = MIN(0xffff, scaleMax * cmdline.bright);
        FORC4 pre_mul[c] *= cmdline.bright;
    }
    if (cmdline.verbose) {
        fprintf(stderr, "Scaling with black=%d, ", black);
        fprintf(stderr, "pre_mul[] = ");
        FORC4 fprintf (stderr, " %f", pre_mul[c]);
        fprintf(stderr, "\n");
    }
    clip_max = cmdline.no_clip_color ? 0xffff : scaleMax;
    for (row = 0; row < height; ++row) {
        unsigned int col;
        for (col = 0; col < width; ++col) {
            unsigned int c;
            for (c = 0; c < colors; ++c) {
                int val;
                val = image[row*width+col][c];
                if (val != 0) {
                    val -= black;
                    val *= pre_mul[c];
                    image[row*width+col][c] = MAX(0, MIN(clip_max, val));
                }
            }
        }
    }
}



static void CLASS
vngInterpolate(Image const image) {

    /*
      This algorithm is officially called "Interpolation using a
      Threshold-based variable number of gradients," described in
      http://www-ise.stanford.edu/~tingchen/algodep/vargra.html

      I've extended the basic idea to work with non-Bayer filter arrays.
      Gradients are numbered clockwise from NW=0 to W=7.
    */

    static const signed char *cp, terms[] = {
  -2,-2,+0,-1,0,(char)0x01, -2,-2,+0,+0,1,(char)0x01, -2,-1,-1,+0,0,(char)0x01,
  -2,-1,+0,-1,0,(char)0x02, -2,-1,+0,+0,0,(char)0x03, -2,-1,+0,+1,1,(char)0x01,
  -2,+0,+0,-1,0,(char)0x06, -2,+0,+0,+0,1,(char)0x02, -2,+0,+0,+1,0,(char)0x03,
  -2,+1,-1,+0,0,(char)0x04, -2,+1,+0,-1,1,(char)0x04, -2,+1,+0,+0,0,(char)0x06,
  -2,+1,+0,+1,0,(char)0x02, -2,+2,+0,+0,1,(char)0x04, -2,+2,+0,+1,0,(char)0x04,
  -1,-2,-1,+0,0,(char)0x80, -1,-2,+0,-1,0,(char)0x01, -1,-2,+1,-1,0,(char)0x01,
  -1,-2,+1,+0,1,(char)0x01, -1,-1,-1,+1,0,(char)0x88, -1,-1,+1,-2,0,(char)0x40,
  -1,-1,+1,-1,0,(char)0x22, -1,-1,+1,+0,0,(char)0x33, -1,-1,+1,+1,1,(char)0x11,
  -1,+0,-1,+2,0,(char)0x08, -1,+0,+0,-1,0,(char)0x44, -1,+0,+0,+1,0,(char)0x11,
  -1,+0,+1,-2,1,(char)0x40, -1,+0,+1,-1,0,(char)0x66, -1,+0,+1,+0,1,(char)0x22,
  -1,+0,+1,+1,0,(char)0x33, -1,+0,+1,+2,1,(char)0x10, -1,+1,+1,-1,1,(char)0x44,
  -1,+1,+1,+0,0,(char)0x66, -1,+1,+1,+1,0,(char)0x22, -1,+1,+1,+2,0,(char)0x10,
  -1,+2,+0,+1,0,(char)0x04, -1,+2,+1,+0,1,(char)0x04, -1,+2,+1,+1,0,(char)0x04,
  +0,-2,+0,+0,1,(char)0x80, +0,-1,+0,+1,1,(char)0x88, +0,-1,+1,-2,0,(char)0x40,
  +0,-1,+1,+0,0,(char)0x11, +0,-1,+2,-2,0,(char)0x40, +0,-1,+2,-1,0,(char)0x20,
  +0,-1,+2,+0,0,(char)0x30, +0,-1,+2,+1,1,(char)0x10, +0,+0,+0,+2,1,(char)0x08,
  +0,+0,+2,-2,1,(char)0x40, +0,+0,+2,-1,0,(char)0x60, +0,+0,+2,+0,1,(char)0x20,
  +0,+0,+2,+1,0,(char)0x30, +0,+0,+2,+2,1,(char)0x10, +0,+1,+1,+0,0,(char)0x44,
  +0,+1,+1,+2,0,(char)0x10, +0,+1,+2,-1,1,(char)0x40, +0,+1,+2,+0,0,(char)0x60,
  +0,+1,+2,+1,0,(char)0x20, +0,+1,+2,+2,0,(char)0x10, +1,-2,+1,+0,0,(char)0x80,
  +1,-1,+1,+1,0,(char)0x88, +1,+0,+1,+2,0,(char)0x08, +1,+0,+2,-1,0,(char)0x40,
  +1,+0,+2,+1,0,(char)0x10
    }, chood[] = { -1,-1, -1,0, -1,+1, 0,+1, +1,+1, +1,0, +1,-1, 0,-1 };
    unsigned short (*brow[5])[4], *pix;
    int code[8][2][320], *ip, gval[8], gmin, gmax, sum[4];
    int row, col, shift, x, y, x1, x2, y1, y2, t, weight, grads, color, diag;
    int g, diff, thold, num, c;

    for (row=0; row < 8; row++) {     /* Precalculate for bilinear */
        for (col=1; col < 3; col++) {
            ip = code[row][col & 1];
            memset (sum, 0, sizeof sum);
            for (y=-1; y <= 1; y++)
                for (x=-1; x <= 1; x++) {
                    shift = (y==0) + (x==0);
                    if (shift == 2) continue;
                    color = FC(row+y,col+x);
                    *ip++ = (width*y + x)*4 + color;
                    *ip++ = shift;
                    *ip++ = color;
                    sum[color] += 1 << shift;
                }
            FORC4
                if (c != FC(row,col)) {
                    *ip++ = c;
                    *ip++ = sum[c];
                }
        }
    }
    for (row=1; row < height-1; row++) {  /* Do bilinear interpolation */
        for (col=1; col < width-1; col++) {
            pix = image[row*width+col];
            ip = code[row & 7][col & 1];
            memset (sum, 0, sizeof sum);
            for (g=8; g--; ) {
                diff = pix[*ip++];
                diff <<= *ip++;
                sum[*ip++] += diff;
            }
            for (g=colors; --g; ) {
                c = *ip++;
                pix[c] = sum[c] / *ip++;
            }
        }
    }
    if (cmdline.quick_interpolate)
        return;

    for (row=0; row < 8; row++) {     /* Precalculate for VNG */
        for (col=0; col < 2; col++) {
            ip = code[row][col];
            for (cp=terms, t=0; t < 64; t++) {
                y1 = *cp++;  x1 = *cp++;
                y2 = *cp++;  x2 = *cp++;
                weight = *cp++;
                grads = *cp++;
                color = FC(row+y1,col+x1);
                if (FC(row+y2,col+x2) != color) continue;
                diag =
                    (FC(row,col+1) == color && FC(row+1,col) == color) ? 2:1;
                if (abs(y1-y2) == diag && abs(x1-x2) == diag) continue;
                *ip++ = (y1*width + x1)*4 + color;
                *ip++ = (y2*width + x2)*4 + color;
                *ip++ = weight;
                for (g=0; g < 8; g++)
                    if (grads & 1<<g) *ip++ = g;
                *ip++ = -1;
            }
            *ip++ = INT_MAX;
            for (cp=chood, g=0; g < 8; g++) {
                y = *cp++;  x = *cp++;
                *ip++ = (y*width + x) * 4;
                color = FC(row,col);
                if (FC(row+y,col+x) != color && FC(row+y*2,col+x*2) == color)
                    *ip++ = (y*width + x) * 8 + color;
                else
                    *ip++ = 0;
            }
        }
    }
    brow[4] = calloc (width*3, sizeof **brow);
    merror (brow[4], "vngInterpolate()");
    for (row=0; row < 3; row++)
        brow[row] = brow[4] + row*width;
    for (row=2; row < height-2; row++) {      /* Do VNG interpolation */
        for (col=2; col < width-2; col++) {
            pix = image[row*width+col];
            ip = code[row & 7][col & 1];
            memset (gval, 0, sizeof gval);
            while ((g = ip[0]) != INT_MAX) {      /* Calculate gradients */
                num = (diff = pix[g] - pix[ip[1]]) >> 31;
                gval[ip[3]] += (diff = ((diff ^ num) - num) << ip[2]);
                ip += 5;
                if ((g = ip[-1]) == -1) continue;
                gval[g] += diff;
                while ((g = *ip++) != -1)
                    gval[g] += diff;
            }
            ip++;
            gmin = gmax = gval[0];            /* Choose a threshold */
            for (g=1; g < 8; g++) {
                if (gmin > gval[g]) gmin = gval[g];
                if (gmax < gval[g]) gmax = gval[g];
            }
            if (gmax == 0) {
                memcpy (brow[2][col], pix, sizeof *image);
                continue;
            }
            thold = gmin + (gmax >> 1);
            memset (sum, 0, sizeof sum);
            color = FC(row,col);
            for (num=g=0; g < 8; g++,ip+=2) {     /* Average the neighbors */
                if (gval[g] <= thold) {
                    FORC4
                        if (c == color && ip[1])
                            sum[c] += (pix[c] + pix[ip[1]]) >> 1;
                        else
                            sum[c] += pix[ip[0] + c];
                    num++;
                }
            }
            FORC4 {                   /* Save to buffer */
                t = pix[color];
                if (c != color) {
                    t += (sum[c] - sum[color])/num;
                    if (t < 0) t = 0;
                    if (t > clip_max) t = clip_max;
                }
                brow[2][col][c] = t;
            }
        }
        if (row > 3)                /* Write buffer to image */
            memcpy(image[(row-2)*width+2], brow[0]+2, (width-4)*sizeof *image);
        for (g=0; g < 4; g++)
            brow[(g-1) & 3] = brow[g];
    }
    memcpy (image[(row-2)*width+2], brow[0]+2, (width-4)*sizeof *image);
    memcpy (image[(row-1)*width+2], brow[1]+2, (width-4)*sizeof *image);
    free (brow[4]);
}



static void CLASS
convertToRgb(Image        const image,
             unsigned int const trim) {
/*----------------------------------------------------------------------------
   Convert the entire image to RGB colorspace and build a histogram.

   We modify 'image' to change it from whatever it is now to RGB.
-----------------------------------------------------------------------------*/
    unsigned int row;
    unsigned int c;
    float rgb[3];  /* { red, green, blue } */

    c = 0;  /* initial value */

    if (cmdline.document_mode)
        colors = 1;

    memset(histogram, 0, sizeof histogram);

    for (row = 0 + trim; row < height - trim; ++row) {
        unsigned int col;
        for (col = 0 + trim; col < width - trim; ++col) {
            unsigned short * const img = image[row*width+col];

            if (cmdline.document_mode)
                c = FC(row,col);

            if (colors == 4 && !use_coeff)
                /* Recombine the greens */
                img[1] = (img[1] + img[3]) / 2;

            if (colors == 1) {
                /* RGB from grayscale */
                unsigned int i;
                for (i = 0; i < 3; ++i)
                    rgb[i] = img[c];
            } else if (use_coeff) {
                /* RGB via coeff[][] */
                unsigned int i;
                for (i = 0; i < 3; ++i) {
                    unsigned int j;
                    for (j = 0, rgb[i]= 0; j < colors; ++j)
                        rgb[i] += img[j] * coeff[i][j];
                }
            } else {
                /* RGB from RGB (easy) */
                unsigned int i;
                for (i = 0; i < 3; ++i)
                    rgb[i] = img[i];
            }
            {
                unsigned int i;
                for (i = 0; i < 3; ++i)
                    img[i] = MIN(clip_max, MAX(0, rgb[i]));
            }
            {
                unsigned int i;
                for (i = 0; i < 3; ++i)
                    ++histogram[i][img[i] >> 3];
            }
        }
    }
}



static void CLASS
fujiRotate(Image * const imageP) {

    int wide;
    int high;
    unsigned int row;
    double step;
    float r, c, fr, fc;
    unsigned short (*newImage)[4];
    unsigned short (*pix)[4];

    if (fuji_width > 0) {
        if (cmdline.verbose)
            pm_message ("Rotating image 45 degrees...");

        fuji_width = (fuji_width + shrink) >> shrink;
        step = sqrt(0.5);
        wide = fuji_width / step;
        high = (height - fuji_width) / step;
        newImage = calloc (wide*high, sizeof *newImage);
        merror (newImage, "fujiRotate()");

        for (row = 0; row < high; ++row) {
            unsigned int col;
            for (col = 0; col < wide; ++col) {
                unsigned int ur = r = fuji_width + (row-col)*step;
                unsigned int uc = c = (row+col)*step;

                unsigned int i;

                if (ur > height-2 || uc > width-2)
                    continue;

                fr = r - ur;
                fc = c - uc;
                pix = (*imageP) + ur * width + uc;

                for (i = 0; i < colors; ++i) {
                    newImage[row*wide+col][i] =
                        (pix[    0][i]*(1-fc) + pix[      1][i]*fc) * (1-fr) +
                        (pix[width][i]*(1-fc) + pix[width+1][i]*fc) * fr;
                } 
            }
        }        
        free(*imageP);
        width  = wide;
        height = high;
        *imageP  = newImage;
        fuji_width = 0;
    }
}



static void CLASS
flipImage(Image const image) {

    unsigned *flag;
    int size, base, dest;

    struct imageCell {
        unsigned char contents[8];
    };
    struct imageCell * img;

    switch ((flip+3600) % 360) {
    case 270:  flip = 0x5;  break;
    case 180:  flip = 0x3;  break;
    case  90:  flip = 0x6;
    }
    img = (struct imageCell *) image;
    size = height * width;
    flag = calloc ((size+31) >> 5, sizeof *flag);
    merror (flag, "flipImage()");
    for (base = 0; base < size; ++base) {
        if (flag[base >> 5] & (1 << (base & 31))) {
            /* nothing */
        } else {
            struct imageCell const hold = img[base];
            dest = base;
            while (true) {
                unsigned int col;
                unsigned int row;
                int next;
                if (flip & 0x4) {
                    row = dest % height;
                    col = dest / height;
                } else {
                    row = dest / width;
                    col = dest % width;
                }
                if (flip & 0x2)
                    row = height - 1 - row;
                if (flip & 1)
                    col = width - 1 - col;
                next = row * width + col;
                if (next == base)
                    break;
                flag[next >> 5] |= 1 << (next & 31);
                img[dest] = img[next];
                dest = next;
            }
            img[dest] = hold;
        }
    }
    free (flag);
    if (flip & 0x4) {
        int const oldHeight = height;
        int const oldYmag = ymag;

        height = width;
        width = oldHeight;
        ymag = xmag;
        xmag = oldYmag;
    }
}



static void CLASS
writePamLinear(FILE *       const ofP,
               Image        const image,
               unsigned int const trim) {
/*----------------------------------------------------------------------------
   Write the image 'image' to a 16-bit PAM file with linear color space
-----------------------------------------------------------------------------*/
    unsigned int row;

    struct pam pam;
    tuple * tuplerow;

    pam.size   = sizeof(pam);
    pam.len    = PAM_STRUCT_SIZE(tuple_type);
    pam.file   = ofP;
    pam.width  = width - trim - trim;
    pam.height = height - trim - trim;
    pam.depth  = 3;
    pam.format = PAM_FORMAT;
    pam.maxval = MAX(maximum, 256);
    strcpy(pam.tuple_type, "RGB");

    pnm_writepaminit(&pam);

    tuplerow = pnm_allocpamrow(&pam);

    for (row = 0 + trim; row < height - trim; ++row) {
        unsigned int col;
        for (col = 0 + trim; col < width - trim; ++col) {
            unsigned int const pamCol = col - trim;
            unsigned int plane;
            for (plane = 0; plane < 3; ++plane)
                tuplerow[pamCol][plane] = image[row*width+col][plane];
        }
        pnm_writepamrow(&pam, tuplerow);
    }
    pnm_freepamrow(tuplerow);
}



static void CLASS
writePamNonlinear(FILE *       const ofP,
                  Image        const image,
                  unsigned int const trim) {
/*----------------------------------------------------------------------------
  Write the image 'image' as an RGB PAM image
-----------------------------------------------------------------------------*/
    unsigned char lut[0x10000];
    int perc;
    int c;
    int total;
    int i;
    unsigned int row;
    float white;
    float r;
    struct pam pam;
    tuple * tuplerow;

    white = 0;  /* initial value */

    pam.size   = sizeof(pam);
    pam.len    = PAM_STRUCT_SIZE(tuple_type);
    pam.file   = ofP;
    pam.width  = xmag*(width-trim*2);
    pam.height = ymag*(height-trim*2);
    pam.depth  = 3;
    pam.format = PAM_FORMAT;
    pam.maxval = 255;
    strcpy(pam.tuple_type, "RGB");

    pnm_writepaminit(&pam);

    tuplerow = pnm_allocpamrow(&pam);

    perc = width * height * 0.01;     /* 99th percentile white point */
    if (fuji_width) perc /= 2;
    FORC3 {
        int val;
        for (val=0x2000, total=0; --val > 32; )
            if ((total += histogram[c][val]) > perc) break;
        if (white < val)
            white = val;
    }
    white *= 8 / cmdline.bright;
    for (i=0; i < 0x10000; ++i) {
        int val;
        r = i / white;
        val = 256 * ( !use_gamma ? r :
                      r <= 0.018 ? r*4.5 : pow(r,0.45)*1.099-0.099 );
        lut[i] = MIN(255, val);
    }

    for (row = 0 + trim; row < height - trim; ++row) {
        unsigned int col;
        for (col = 0 + trim; col < width - trim; ++col) {
            unsigned int plane;
            for (plane = 0; plane < pam.depth; ++plane) {
                sample const value = lut[image[row*width+col][plane]];
                unsigned int copy;
                for (copy = 0; copy < xmag; ++copy) {
                    unsigned int const pamcol = xmag*(col-trim)+copy;
                    tuplerow[pamcol][plane] = value;
                }
            }
        }
        {
            unsigned int copy;
            for (copy = 0; copy < ymag; ++copy)
                pnm_writepamrow(&pam, tuplerow);
        }
    }
    pnm_freepamrow(tuplerow);
}



static void CLASS
writePam(FILE *       const ofP,
         Image        const image,
         bool         const linear,
         unsigned int const trim) {

    if (linear)
        writePamLinear(ofP, image, trim);
    else
        writePamNonlinear(ofP, image, trim);
}



static void CLASS
convertIt(FILE *       const ifP,
          FILE *       const ofP,
          LoadRawFn *  const load_raw) {

    Image image;
    unsigned int trim;

    shrink = cmdline.half_size && filters;
    iheight = (height + shrink) >> shrink;
    iwidth  = (width  + shrink) >> shrink;
    image = calloc (iheight*iwidth*sizeof(*image) + meta_length, 1);
    merror (image, "main()");
    meta_data = (char *) (image + iheight*iwidth);
    if (cmdline.verbose)
        pm_message ("Loading %s %s image ...", make, model);

    use_secondary = cmdline.use_secondary;  /* for load_raw() */

    ifp = ifP;  /* Set global variable for (*load_raw)() */

    load_raw(image);
    fixBadPixels(image);
    height = iheight;
    width  = iwidth;
    if (is_foveon) {
        if (cmdline.verbose)
            pm_message ("Foveon interpolation...");
        foveon_interpolate(image, coeff);
    } else {
        scaleColors(image);
    }
    if (shrink)
        filters = 0;

    if (filters && !cmdline.document_mode) {
        trim = 1;
        if (cmdline.verbose)
            pm_message ("%s interpolation...",
                        cmdline.quick_interpolate ? "Bilinear":"VNG");
        vngInterpolate(image);
    } else
        trim = 0;

    fujiRotate(&image);

    if (cmdline.verbose)
        pm_message ("Converting to RGB colorspace...");
    convertToRgb(image, trim);

    if (flip) {
        if (cmdline.verbose)
            pm_message ("Flipping image %c:%c:%c...",
                        flip & 1 ? 'H':'0', flip & 2 ? 'V':'0', 
                        flip & 4 ? 'T':'0');
        flipImage(image);
    }
    writePam(ofP, image, cmdline.linear, trim);

    free(image);
}



int 
main (int argc, char **argv) {

    FILE * const ofP = stdout;

    FILE * ifP;
    int rc;
    LoadRawFn * load_raw;

    pnm_init(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    verbose = cmdline.verbose;

    ifP = pm_openr(cmdline.inputFileName);

    rc = identify(ifP,
                  cmdline.use_secondary, cmdline.use_camera_rgb,
                  cmdline.red_scale, cmdline.blue_scale,
                  cmdline.four_color_rgb, cmdline.inputFileName,
                  &load_raw);
    if (rc != 0)
        pm_error("Unable to identify the format of the input image");
    else {
        if (cmdline.identify_only) {
            pm_message ("Input is a %s %s image.", make, model);
        } else {
            if (cmdline.verbose)
                pm_message ("Input is a %s %s image.", make, model);
            convertIt(ifP, ofP, load_raw);
        }
    }
    pm_close(ifP);
    pm_close(ofP);

    return 0;
}



