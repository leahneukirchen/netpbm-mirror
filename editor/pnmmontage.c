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

#include <assert.h>
#include <limits.h>
#include <string.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "pam.h"

typedef struct { int f[sizeof(int) * 8 + 1]; } factorset;
typedef struct { int x; int y; } coord;

static int qfactor;
static int quality = 5;

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
collides(const coord * const locs,
         const coord * const szs,
         const coord * const cloc,
         const coord * const csz,
         unsigned int  const n) {

    unsigned int i;

    for (i = 0; i < n; ++i) {
        if ((locs[i].x < cloc->x + csz->x) &&
            (locs[i].y < cloc->y + csz->y) &&
            (locs[i].x + szs[i].x > cloc->x) &&
            (locs[i].y + szs[i].y > cloc->y))
            return true;
    }
    return false;
}



static void 
recursefindpack(coord *        const current,
                coord          const currentsz,
                coord *        const set, 
                coord *        const best,
                unsigned int   const minarea,
                unsigned int * const maxareaP, 
                unsigned int   const depth,
                unsigned int   const n,
                unsigned int   const xinc,
                unsigned int   const yinc) {

    if (depth == n) {
        if (currentsz.x * currentsz.y < *maxareaP) {
            unsigned int i;
            for (i = 0; i < n; ++i)
                best[i] = current[i];
            *maxareaP = currentsz.x * currentsz.y;
        }
    } else {
        unsigned int i;

        for (i = 0; ; ++i) {
            for (current[depth].x = 0, current[depth].y = i * yinc;
                 current[depth].y <= i * yinc;) {

                coord c;

                c.x = MAX(current[depth].x + set[depth].x, currentsz.x);
                c.y = MAX(current[depth].y + set[depth].y, currentsz.y);
                if (!collides(current, set, &current[depth],
                              &set[depth], depth)) {
                    recursefindpack(current, c, set, best, minarea, maxareaP,
                                    depth + 1, n, xinc, yinc);
                    if (*maxareaP <= minarea)
                        return;
                }
                if (current[depth].x == (i - 1) * xinc)
                    current[depth].y = 0;
                if (current[depth].x < i * xinc)
                    current[depth].x += xinc;
                else
                    current[depth].y += yinc;
            }
        }
    }
}



static void 
findpack(struct pam *imgs, int n, coord *coords)
{
  int minarea;
  int i;
  int rdiv;
  int cdiv;
  int minx = -1;
  int miny = -1;
  coord *current;
  coord *set;
  unsigned int z = UINT_MAX;
  coord c = { 0, 0 };

  if (quality > 1)
  {
    for (minarea = i = 0; i < n; ++i)
      minarea += imgs[i].height * imgs[i].width,
      minx = MAX(minx, imgs[i].width),
      miny = MAX(miny, imgs[i].height);

    minarea = minarea * qfactor / 100;
  }
  else
  {
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
  MALLOCARRAY(set, n);
  for (i = 0; i < n; ++i)
    set[i].x = imgs[i].width,
    set[i].y = imgs[i].height;
  recursefindpack(current, c, set, coords, minarea, &z, 0, n, cdiv, rdiv);
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



struct cmdlineInfo {
    const char * header;
    const char * data;
    const char * prefix;
    unsigned int quality;
    unsigned int q[10];
};



int 
main(int argc, char **argv) {
  struct cmdlineInfo cmdline;
  struct pam *imgs;
  struct pam outimg;
  int nfiles;
  coord *coords;
  FILE *header;
  FILE *data;
  const char **names;
  unsigned int i;

  optEntry * option_def;
      /* Instructions to OptParseOptions3 on how to parse our options.
       */
  optStruct3 opt;
  unsigned int dataSpec, headerSpec, prefixSpec, qualitySpec;
  unsigned int option_def_index;

  pm_proginit(&argc, argv);

  MALLOCARRAY_NOFAIL(option_def, 100);
  
  option_def_index = 0;   /* incremented by OPTENTRY */
  OPTENT3( 0,  "data",    OPT_STRING, &cmdline.data, &dataSpec, 0);
  OPTENT3( 0,  "header",  OPT_STRING, &cmdline.header, &headerSpec, 0);
  OPTENT3('q', "quality", OPT_UINT,   &cmdline.quality,   &qualitySpec, 0);
  OPTENT3('p', "prefix",  OPT_STRING, &cmdline.prefix,    &prefixSpec, 0);
  OPTENT3('0', "0",       OPT_FLAG,   NULL, &cmdline.q[0],      0);
  OPTENT3('1', "1",       OPT_FLAG,   NULL, &cmdline.q[1],      0);
  OPTENT3('2', "2",       OPT_FLAG,   NULL, &cmdline.q[2],      0);
  OPTENT3('3', "3",       OPT_FLAG,   NULL, &cmdline.q[3],      0);
  OPTENT3('4', "4",       OPT_FLAG,   NULL, &cmdline.q[4],      0);
  OPTENT3('5', "5",       OPT_FLAG,   NULL, &cmdline.q[5],      0);
  OPTENT3('6', "6",       OPT_FLAG,   NULL, &cmdline.q[6],      0);
  OPTENT3('7', "7",       OPT_FLAG,   NULL, &cmdline.q[7],      0);
  OPTENT3('8', "8",       OPT_FLAG,   NULL, &cmdline.q[8],      0);
  OPTENT3('9', "9",       OPT_FLAG,   NULL, &cmdline.q[9],      0);

  opt.opt_table = option_def;
  opt.short_allowed = FALSE;
  opt.allowNegNum = FALSE;

  /* Check for flags. */
  optParseOptions3(&argc, argv, opt, sizeof(opt), 0);

  if (!dataSpec)
      cmdline.data = NULL;
  if (!headerSpec)
      cmdline.header = NULL;
  if (!prefixSpec)
      cmdline.prefix = "";
  if (!qualitySpec)
      cmdline.quality = 200;

  header = cmdline.header ? pm_openw(cmdline.header) : NULL;
  data = cmdline.data ? pm_openw(cmdline.data) : NULL;
  qfactor = cmdline.quality;

  for (i = 0; i < 10; ++i)
  {
    if (cmdline.q[i])
    {
      quality = i;
      switch (quality)
      {
        case 0: case 1: break;
        case 2: case 3: case 4: case 5: case 6: 
            qfactor = 100 * (8 - quality); 
            break;
        case 7: qfactor = 150; break;
        case 8: qfactor = 125; break;
        case 9: qfactor = 100; break;
      }
    }
  }

  if (1 < argc)
    nfiles = argc - 1;
  else
    nfiles = 1;

  MALLOCARRAY(imgs, nfiles);
  MALLOCARRAY(coords, nfiles);
  MALLOCARRAY(names, nfiles);
  
  if (!imgs || !coords || !names)
    pm_error("out of memory");

  if (1 < argc)
  {
    for (i = 0; i < nfiles; ++i)
    {
      if (strchr(argv[i+1], ':'))
      {
        imgs[i].file = pm_openr(strchr(argv[i+1], ':') + 1);
        *strchr(argv[i+1], ':') = 0;
        names[i] = argv[i+1];
      }
      else
      {
        imgs[i].file = pm_openr(argv[i+1]);
        names[i] = argv[i+1];
      }
    }
  }
  else
  {
    imgs[0].file = stdin;
  }

  for (i = 0; i < nfiles; ++i)
      pnm_readpaminit(imgs[i].file, &imgs[i], PAM_STRUCT_SIZE(tuple_type));

  sortImagesByArea(nfiles, imgs, names);

  findpack(imgs, nfiles, coords);

  computeOutputType(&outimg.maxval, &outimg.format, outimg.tuple_type,
                    &outimg.depth, nfiles, imgs);

  computeOutputDimensions(&outimg.width, &outimg.height, nfiles, imgs, coords);

  pnm_setminallocationdepth(&outimg, outimg.depth);

  outimg.size = sizeof(outimg);
  outimg.len = sizeof(outimg);
  outimg.file = stdout;
  outimg.bytes_per_sample = 0;
  for (i = outimg.maxval; i; i >>= 8)
    ++outimg.bytes_per_sample;
 
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
