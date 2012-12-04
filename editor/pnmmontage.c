/* pnmmontage.c - build a montage of portable anymaps
 *
 * Copyright 2000 Ben Olmstead.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose and without fee is hereby granted, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation.  This software is provided "as is" without express or
 * implied warranty.
 */

#define _BSD_SOURCE  /* Make sure strdup() is in <string.h> */
#include <assert.h>
#include <limits.h>
#include <string.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pam.h"



struct cmdlineInfo {
    const char * header;
    const char * data;
    const char * prefix;
    unsigned int quality;
    unsigned int quality2;
    unsigned int nFiles;
    const char ** inFileName;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct cmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.  

   If command line is internally inconsistent (invalid options, etc.),
   issue error message to stderr and abort program.

   Note that the strings we return are stored in the storage that
   was passed to us as the argv array.  We also trash *argv.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options. */
    optStruct3 opt;
    unsigned int dataSpec, headerSpec, prefixSpec, qualitySpec;
    unsigned int option_def_index;
    unsigned int i;
    unsigned int q[10];

    MALLOCARRAY_NOFAIL(option_def, 100);
  
    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3( 0,  "data",    OPT_STRING, &cmdlineP->data, &dataSpec, 0);
    OPTENT3( 0,  "header",  OPT_STRING, &cmdlineP->header, &headerSpec, 0);
    OPTENT3('q', "quality", OPT_UINT,   &cmdlineP->quality,   &qualitySpec, 0);
    OPTENT3('p', "prefix",  OPT_STRING, &cmdlineP->prefix,    &prefixSpec, 0);
    OPTENT3('0', "0",       OPT_FLAG,   NULL, &q[0],      0);
    OPTENT3('1', "1",       OPT_FLAG,   NULL, &q[1],      0);
    OPTENT3('2', "2",       OPT_FLAG,   NULL, &q[2],      0);
    OPTENT3('3', "3",       OPT_FLAG,   NULL, &q[3],      0);
    OPTENT3('4', "4",       OPT_FLAG,   NULL, &q[4],      0);
    OPTENT3('5', "5",       OPT_FLAG,   NULL, &q[5],      0);
    OPTENT3('6', "6",       OPT_FLAG,   NULL, &q[6],      0);
    OPTENT3('7', "7",       OPT_FLAG,   NULL, &q[7],      0);
    OPTENT3('8', "8",       OPT_FLAG,   NULL, &q[8],      0);
    OPTENT3('9', "9",       OPT_FLAG,   NULL, &q[9],      0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;
    opt.allowNegNum = FALSE;

    optParseOptions3(&argc, (char**)argv, opt, sizeof(opt), 0);

    if (!dataSpec)
        cmdlineP->data = NULL;
    if (!headerSpec)
        cmdlineP->header = NULL;
    if (!prefixSpec)
        cmdlineP->prefix = "";
    if (!qualitySpec)
        cmdlineP->quality = 200;

    
    /* cmdlineP->quality2 is the greatest number from the --1, --2, etc.
       options, or 5 if none of those are specified.
    */
    cmdlineP->quality2 = 5;  /* initial value */
    for (i = 0; i < 10; ++i) {
        if (q[i])
            cmdlineP->quality2 = i;
    }

    cmdlineP->nFiles = argc-1;

    MALLOCARRAY_NOFAIL(cmdlineP->inFileName, argc-1);

    for (i = 0; i < argc-1; ++i) {
        if (cmdlineP->data && strchr(argv[i+1], ':'))
            pm_error("Filename '%s' contains a \":\", which is forbidden "
                     "with -data", argv[i+1]);
        else
            cmdlineP->inFileName[i] = strdup(argv[i+1]);
    }
}



typedef struct {
    int f[sizeof(int) * 8 + 1];
} factorset;

typedef struct {
    int x; int y;
} coord;

typedef struct {
    coord ul;
    coord size;
} rectangle;

static coord
lr(rectangle const r) {
/*----------------------------------------------------------------------------
   Return the coordinates of the lower right corner of 'r'
   (i.e. the pixel just beyond the lowest rightmost one).
-----------------------------------------------------------------------------*/
    coord retval;

    retval.x = r.ul.x + r.size.x;
    retval.y = r.ul.y + r.size.y;

    return retval;
}

static factorset 
factor(int n)
{
  int i, j;
  factorset f;
  for (i = 0; i < sizeof(int) * 8 + 1; ++i)
    f.f[i] = 0;
  for (i = 2, j = 0; n > 1; ++i)
  {
    if (n % i == 0)
      f.f[j++] = i, n /= i, --i;
  }
  return (f);
}

static int 
gcd(int n, int m)
{
  factorset nf, mf;
  int i, j;
  int g;

  nf = factor(n);
  mf = factor(m);

  i = j = 0;
  g = 1;
  while (nf.f[i] && mf.f[j])
  {
    if (nf.f[i] == mf.f[j])
      g *= nf.f[i], ++i, ++j;
    else if (nf.f[i] < mf.f[j])
      ++i;
    else
      ++j;
  }
  return (g);
}



static bool
overlaps(rectangle const a,
         rectangle const b) {

    return
        (a.ul.x < lr(b).x && a.ul.y < lr(b).y) &&
        (lr(a).x > b.ul.x && lr(a).y > b.ul.y);
}



static bool
collides(rectangle         const test,
         const rectangle * const fieldList,
         unsigned int      const n) {
/*----------------------------------------------------------------------------
   Return true iff the rectangle 'test' overlaps any of the 'n' rectangles
   fieldList[].
-----------------------------------------------------------------------------*/
    unsigned int i;

    for (i = 0; i < n; ++i)
        if (overlaps(fieldList[i], test))
            return true;

    return false;
}



static void 
recursefindpack(rectangle *    const current,
                coord          const currentsz,
                coord *        const best,
                unsigned int   const minarea,
                unsigned int * const maxareaP, 
                unsigned int   const depth,
                unsigned int   const n,
                unsigned int   const xinc,
                unsigned int   const yinc,
                unsigned int   const quality,
                unsigned int   const qfactor) {

    if (depth == n) {
        if (currentsz.x * currentsz.y < *maxareaP) {
            unsigned int i;
            for (i = 0; i < n; ++i)
                best[i] = current[i].ul;
            *maxareaP = currentsz.x * currentsz.y;
        }
    } else {
        unsigned int i;

        rectangle * const newP = &current[depth];

        for (i = 0; ; ++i) {
            for (newP->ul.x = 0, newP->ul.y = i * yinc;
                 newP->ul.y <= i * yinc;) {

                coord c;

                c.x = MAX(lr(*newP).x, currentsz.x);
                c.y = MAX(lr(*newP).y, currentsz.y);
                pm_message("current = (%u.%u, %u.%u) new = (%u.%u, %u.%u)",
                           current[0].ul.x, current[0].size.x,
                           current[0].ul.y, current[0].size.y,
                           newP->ul.x,   newP->size.x,
                           newP->ul.y,   newP->size.y);
                if (!collides(*newP, current, depth)) {
                    pm_message("Depth %u: Doesn't collide at i=%u", depth,i);
                    recursefindpack(current, c, best, minarea, maxareaP,
                                    depth + 1, n, xinc, yinc,
                                    quality, qfactor);
                    if (*maxareaP <= minarea)
                        return;
                }
                if (newP->ul.x == (i - 1) * xinc)
                    newP->ul.y = 0;
                if (newP->ul.x < i * xinc)
                    newP->ul.x += xinc;
                else
                    newP->ul.y += yinc;
            }
        }
    }
}



static void 
findpack(struct pam * const imgs,
         unsigned int const n,
         coord *      const coords,
         unsigned int const quality,
         unsigned int const qfactor) {

    int minarea;
    int i;
    int rdiv;
    int cdiv;
    int minx;
    int miny;
    rectangle * current;
    unsigned int z;
    coord c;

    minx = -1; miny = -1;  /* initial value */
    z = UINT_MAX;  /* initial value */
    c.x = 0; c.y = 0;  /* initial value */

    if (quality > 1) {
        unsigned int realMinarea;
        for (realMinarea = i = 0; i < n; ++i)
            realMinarea += imgs[i].height * imgs[i].width,
                minx = MAX(minx, imgs[i].width),
                miny = MAX(miny, imgs[i].height);

        minarea = realMinarea * qfactor / 100;
    } else {
        minarea = INT_MAX - 1;
    }

    /* It's relatively easy to show that, if all the images
     * are multiples of a particular size, then a best
     * packing will always align the images on a grid of
     * that size.
     *
     * This speeds computation immensely.
     */
    for (rdiv = imgs[0].height, i = 1; i < n; ++i)
        rdiv = gcd(imgs[i].height, rdiv);

    for (cdiv = imgs[0].width, i = 1; i < n; ++i)
        cdiv = gcd(imgs[i].width, cdiv);

    MALLOCARRAY(current, n);

    for (i = 0; i < n; ++i) {
        current[i].size.x = imgs[i].width;
        current[i].size.y = imgs[i].height;
    }
    recursefindpack(current, c, coords, minarea, &z, 0, n, cdiv, rdiv,
                    quality, qfactor);
}



static void 
adjustDepth(tuple *            const tuplerow,
            const struct pam * const inpamP,
            const struct pam * const outpamP,
            coord              const coord) {

    if (inpamP->depth < outpamP->depth) {
        unsigned int i;
        for (i = coord.x; i < coord.x + inpamP->width; ++i) {
            int j;
            for (j = inpamP->depth; j < outpamP->depth; ++j)
                tuplerow[i][j] = tuplerow[i][inpamP->depth - 1];
        }
    }
}



static void 
adjustMaxval(tuple *            const tuplerow,
             const struct pam * const inpamP,
             const struct pam * const outpamP,
             coord              const coord) {

    if (inpamP->maxval < outpamP->maxval) {
        int i;
        for (i = coord.x; i < coord.x + inpamP->width; ++i) {
            int j;
            for (j = 0; j < outpamP->depth; ++j)
                tuplerow[i][j] *= outpamP->maxval / inpamP->maxval;
        }
    }
}



static void
makeRowBlack(struct pam * const pamP,
             tuple *      const tuplerow) {

    unsigned int col;

    for (col = 0; col < pamP->width; ++col) {
        unsigned int plane;
        for (plane = 0; plane < pamP->depth; ++plane)
            tuplerow[col][plane] = 0;
    }
}



static void
writePam(struct pam *       const outpamP,
         unsigned int       const nfiles,
         const coord *      const coords,
         const struct pam * const imgs) {

    tuple *tuplerow;
    int i;
  
    pnm_writepaminit(outpamP);

    tuplerow = pnm_allocpamrow(outpamP);

    for (i = 0; i < outpamP->height; ++i) {
        int j;

        makeRowBlack(outpamP, tuplerow);  /* initial value */

        for (j = 0; j < nfiles; ++j) {
            if (coords[j].y <= i && i < coords[j].y + imgs[j].height) {
                pnm_readpamrow(&imgs[j], &tuplerow[coords[j].x]);
                adjustDepth(tuplerow, &imgs[j], outpamP, coords[j]);

                adjustMaxval(tuplerow, &imgs[j], outpamP, coords[j]);

            }
        }
        pnm_writepamrow(outpamP, tuplerow);
    }
    pnm_freepamrow(tuplerow);
}



static void
writeData(FILE *             const dataFileP,
          unsigned int       const width,
          unsigned int       const height,
          unsigned int       const nfiles,
          const char **      const names,
          const coord *      const coords,
          const struct pam * const imgs) {

    unsigned int i;

    fprintf(dataFileP, ":0:0:%u:%u\n", width, height);

    for (i = 0; i < nfiles; ++i) {
        fprintf(dataFileP, "%s:%u:%u:%u:%u\n", names[i], coords[i].x,
                coords[i].y, imgs[i].width, imgs[i].height);
    }
}



static void
writeHeader(FILE * const headerFileP,
            const char * const prefix,
            unsigned int const width,
            unsigned int const height,
            unsigned int const nfiles,
            const char ** const names,
            const coord * const coords,
            const struct pam * imgs) {

    unsigned int i;

    fprintf(headerFileP, "#define %sOVERALLX %u\n", prefix, width);

    fprintf(headerFileP, "#define %sOVERALLY %u\n", prefix, height);

    fprintf(headerFileP, "\n");

    for (i = 0; i < nfiles; ++i) {
        char * const buffer = strdup(names[i]);
        coord const coord = coords[i];
        struct pam const img = imgs[i];

        unsigned int j;
        
        *strchr(buffer, '.') = 0;
        for (j = 0; buffer[j]; ++j) {
            if (ISLOWER(buffer[j]))
                buffer[j] = TOUPPER(buffer[j]);
        }
        fprintf(headerFileP, "#define %s%sX %u\n", 
                prefix, buffer, coord.x);

        fprintf(headerFileP, "#define %s%sY %u\n",
                prefix, buffer, coord.y);

        fprintf(headerFileP, "#define %s%sSZX %u\n",
                prefix, buffer, img.width);

        fprintf(headerFileP, "#define %s%sSZY %u\n",
                prefix, buffer, img.height);

        fprintf(headerFileP, "\n");
    }
}



static void
sortImagesByArea(unsigned int  const nfiles,
                 struct pam *  const imgs,
                 const char ** const names) {
/*----------------------------------------------------------------------------
   Sort the images described by 'imgs' and 'names' in place, from largest
   area to smallest.
-----------------------------------------------------------------------------*/
    /* Bubble sort */

    unsigned int i;

    for (i = 0; i < nfiles - 1; ++i) {
        unsigned int j;
        for (j = i + 1; j < nfiles; ++j) {
            if (imgs[j].width * imgs[j].height >
                imgs[i].width * imgs[i].height) {

                struct pam p;
                const char * c;
                
                p = imgs[i]; imgs[i] = imgs[j]; imgs[j] = p;
                c = names[i]; names[i] = names[j]; names[j] = c;
            }
        }
    }
}



static void
computeOutputType(sample *           const maxvalP,
                  int *              const formatP,
                  char *             const tupleTypeP,
                  unsigned int *     const depthP,
                  unsigned int       const nfiles,
                  const struct pam * const imgs) {

    unsigned int i;

    sample maxval;
    int format;
    const char * tupleType;
    unsigned int depth;

    assert(nfiles > 0);

    /* initial guesses */
    maxval    = imgs[0].maxval;
    format    = imgs[0].format;
    depth     = imgs[0].depth;
    tupleType = imgs[0].tuple_type;

    for (i = 1; i < nfiles; ++i) {
        if (PAM_FORMAT_TYPE(imgs[i].format) > PAM_FORMAT_TYPE(format)) {
            format    = imgs[i].format;
            tupleType = imgs[i].tuple_type;
        }
        maxval = MAX(maxval, imgs[i].maxval);
        depth  = MAX(depth,  imgs[i].depth);
    }

    *maxvalP = maxval;
    *formatP = format;
    *depthP  = depth;
    memcpy(tupleTypeP, tupleType, sizeof(imgs[0].tuple_type));
}



static void
computeOutputDimensions(int * const widthP,
                        int * const heightP,
                        unsigned int const nfiles,
                        const struct pam * const imgs,
                        const coord * const coords) {

    unsigned int widthGuess, heightGuess;
    unsigned int i;

    widthGuess  = 0;  /* initial value */
    heightGuess = 0;  /* initial value */
    
    for (i = 0; i < nfiles; ++i) {
        widthGuess  = MAX(widthGuess,  imgs[i].width  + coords[i].x);
        heightGuess = MAX(heightGuess, imgs[i].height + coords[i].y);
    }

    *widthP  = widthGuess;
    *heightP = heightGuess;
}



int 
main(int argc, const char **argv) {

    struct cmdlineInfo cmdline;
    struct pam * imgs;
    struct pam outimg;
    unsigned int nfiles;
    coord * coords;
    FILE * header;
    FILE * data;
    const char ** names;
    unsigned int i;
    unsigned int qfactor;  /* In per cent */

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    header = cmdline.header ? pm_openw(cmdline.header) : NULL;
    data = cmdline.data ? pm_openw(cmdline.data) : NULL;

    switch (cmdline.quality2) {
    case 0: case 1:
        qfactor = cmdline.quality;
        break;
    case 2: case 3: case 4: case 5: case 6: 
        qfactor = 100 * (8 - cmdline.quality2); 
        break;
    case 7: qfactor = 150; break;
    case 8: qfactor = 125; break;
    case 9: qfactor = 100; break;
    default: pm_error("Internal error - impossible value of 'quality2': %u",
                      cmdline.quality2);
    }

    nfiles = cmdline.nFiles > 0 ? cmdline.nFiles : 1;

    MALLOCARRAY(imgs, nfiles);
    MALLOCARRAY(coords, nfiles);
    MALLOCARRAY(names, nfiles);
  
    if (!imgs || !coords || !names)
        pm_error("out of memory");

    if (cmdline.nFiles > 0) {
        unsigned int i;

        for (i = 0; i < cmdline.nFiles; ++i) {
            imgs[i].file = pm_openr(cmdline.inFileName[i]);
            names[i] = strdup(cmdline.inFileName[i]);
        }
    } else {
        imgs[0].file = stdin;
        names[0] = strdup("stdin");
    }

    for (i = 0; i < nfiles; ++i)
        pnm_readpaminit(imgs[i].file, &imgs[i], PAM_STRUCT_SIZE(tuple_type));

    sortImagesByArea(nfiles, imgs, names);

    findpack(imgs, nfiles, coords, cmdline.quality2, qfactor);

    computeOutputType(&outimg.maxval, &outimg.format, outimg.tuple_type,
                      &outimg.depth, nfiles, imgs);

    computeOutputDimensions(&outimg.width, &outimg.height, nfiles,
                            imgs, coords);

    pnm_setminallocationdepth(&outimg, outimg.depth);

    outimg.size = sizeof(outimg);
    outimg.len = PAM_STRUCT_SIZE(allocation_depth);
    pnm_setminallocationdepth(&outimg, outimg.depth);
    outimg.plainformat = false;
    outimg.file = stdout;
 
    writePam(&outimg, nfiles, coords, imgs);

    if (data)
        writeData(data, outimg.width, outimg.height,
                  nfiles, names, coords, imgs);

    if (header)
        writeHeader(header, cmdline.prefix, outimg.width, outimg.height,
                    nfiles, names, coords, imgs);

    for (i = 0; i < nfiles; ++i)
        pm_close(imgs[i].file);
    pm_close(stdout);
    if (header)
        pm_close(header);
    if (data)
        pm_close(data);

    return 0;
}
