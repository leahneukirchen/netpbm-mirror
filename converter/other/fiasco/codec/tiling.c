/*
 *  tiling.c:           Subimage permutation
 *
 *  Written by:         Ullrich Hafner
 *
 *  This file is part of FIASCO (Fractal Image And Sequence COdec)
 *  Copyright (C) 1994-2000 Ullrich Hafner
 */

/*
 *  $Date: 2000/06/14 20:50:51 $
 *  $Author: hafner $
 *  $Revision: 5.1 $
 *  $State: Exp $
 */

#include "config.h"

#include <stdbool.h>
#include <stdlib.h>

#include "pm_c_util.h"

#include "types.h"
#include "macros.h"
#include "error.h"

#include "image.h"
#include "misc.h"
#include "wfalib.h"
#include "tiling.h"



typedef struct VarList {
    int    address;                      /* bintree address */
    real_t variance;                     /* variance of tile */
} VarList;

#ifndef LITERAL_FN_DEF_MATCH
static qsort_comparison_fn cmpincvar;
#endif

static int
cmpincvar(const void * const value1P,
          const void * const value2P) {
/*----------------------------------------------------------------------------
  Sort by increasing variances (quicksort sorting function)
-----------------------------------------------------------------------------*/
    return
        ((VarList *)value1P)->variance - ((VarList *)value2P)->variance;
}



#ifndef LITERAL_FN_DEF_MATCH
static qsort_comparison_fn cmpdecvar;
#endif

static int
cmpdecvar(const void * const value1P,
          const void * const value2P) {
/*----------------------------------------------------------------------------
  Sort by decreasing variances (quicksort sorting function).
-----------------------------------------------------------------------------*/
    return
        ((VarList *)value2P)->variance - ((VarList *)value1P)->variance;
}



tiling_t *
new_tiling(fiasco_tiling_e const method,
           unsigned int    const reqTilingExponent,
           unsigned int    const imageLevel,
           bool            const isVideo) {
/*----------------------------------------------------------------------------
   Construct an image tiling object.

   'method' defines the tiling method (spiral or variance; ascending or
   descending).

   'reqTilingExponent' is the requested tiling exponent, though if it is too
   large for the 'imageLevel', we issue a warning and make the object say
   tiling exponent 6.  Also, if we're doing video ('isVideo'), make it
   zero and issue a warning.

   Return value is pointer to newly malloced object.
-----------------------------------------------------------------------------*/
    tiling_t * const tilingP = Calloc(1, sizeof(tiling_t));

    if (isVideo && reqTilingExponent > 0)
        warning(_("Image tiling valid only with still image compression."));

    if (isVideo)
        tilingP->exponent = 0;
    else {
        if ((int)imageLevel - (int)reqTilingExponent < 6) {
            tilingP->exponent = 6;
            warning(_("Image tiles must be at least 8x8 pixels large.\n"
                      "Setting tiling size to 8x8 pixels."));
        } else
            tilingP->exponent = reqTilingExponent;
    }

    return tilingP;
}



void
free_tiling (tiling_t * const tilingP) {
/*----------------------------------------------------------------------------
   Destroy tiling object *tilingP.
-----------------------------------------------------------------------------*/
    if (tilingP->vorder)
        Free(tilingP->vorder);

    Free(tilingP);
}



static void
performTilingVariance(const image_t * const imageP,
                      tiling_t *      const tilingP) {

    bool_t * tileIsValid;  /* malloced array */
        /* tileIsValid[i] means i is in valid range */

    unsigned int const tileCt = 1 << tilingP->exponent;
        /* number of image tiles */

    tileIsValid = Calloc(tileCt, sizeof(bool_t));

    unsigned int const lx      = log2 (imageP->width - 1) + 1; /* x level */
    unsigned int const ly      = log2 (imageP->height - 1) + 1; /* y level */
    unsigned int const level   = MAX(lx, ly) * 2 - ((ly == lx + 1) ? 1 : 0);
    VarList *    const varList = Calloc(tileCt, sizeof(VarList));  /* array */

    unsigned int address;           /* bintree address of tile */
    unsigned int number;            /* number of image tiles */

    /* Compute variances of image tiles */

    for (number = 0, address = 0; address < tileCt; ++address) {
        unsigned width, height;     /* size of image tile */
        unsigned x0, y0;            /* NW corner of image tile */

        locate_subimage (level, level - tilingP->exponent, address,
                         &x0, &y0, &width, &height);
        if (x0 < imageP->width && y0 < imageP->height) {
            /* valid range */
            if (x0 + width > imageP->width)   /* outside image area */
                width = imageP->width - x0;
            if (y0 + height > imageP->height) /* outside image area */
                height = imageP->height - y0;

            varList[number].variance =
                variance(imageP->pixels [GRAY], x0, y0,
                         width, height, imageP->width);
            varList[number].address  = address;
            ++number;
            tileIsValid[address] = YES;
        } else
            tileIsValid[address] = NO;
    }

    /* Sort image tiles according to sign of 'tilingP->exp' */
    if (tilingP->method == FIASCO_TILING_VARIANCE_DSC)
        qsort(varList, number, sizeof(VarList), cmpdecvar);
    else
        qsort(varList, number, sizeof(VarList), cmpincvar);

    for (number = 0, address = 0; address < tileCt; ++address)
        if (tileIsValid[address]) {
            tilingP->vorder[address] = varList[number].address;
            ++number;
            debug_message("tile number %d has original address %d",
                          number, tilingP->vorder[address]);
        } else
            tilingP->vorder[address] = -1;

    Free(varList);
}



void
perform_tiling(const image_t * const imageP,
               tiling_t *      const tilingP) {
/*----------------------------------------------------------------------------
  Compute image tiling permutation, updating it in *tilingP.

  The image is split into 2**'tilingP->exponent' tiles.

  Depending on 'tilingP->method', we use one of the following algorithms:

     "VARIANCE_ASC" :  Tiles are sorted by variance.
                       The first tile has the lowest variance
     "VARIANCE_DSC" :  Tiles are sorted by variance.
                       The first tile has the largest variance
     "SPIRAL_ASC" :    Tiles are sorted like a spiral starting
                       in the middle of the image.
     "SPIRAL_DSC" :    Tiles are sorted like a spiral starting
                       in the upper left corner.
-----------------------------------------------------------------------------*/
    if (tilingP->exponent) {
        unsigned int const tileCt = 1 << tilingP->exponent;
            /* number of image tiles */

        tilingP->vorder = Calloc(tileCt, sizeof(int));

        if (tilingP->method == FIASCO_TILING_VARIANCE_ASC
            || tilingP->method == FIASCO_TILING_VARIANCE_DSC) {

            performTilingVariance(imageP, tilingP);
        } else if (tilingP->method == FIASCO_TILING_SPIRAL_DSC
                   || tilingP->method == FIASCO_TILING_SPIRAL_ASC) {
            compute_spiral(tilingP->vorder, imageP->width, imageP->height,
                           tilingP->exponent,
                           tilingP->method == FIASCO_TILING_SPIRAL_ASC);
        } else {
            warning("We do not know the tiling method.\n"
                    "Skipping image tiling step.");
            tilingP->exponent = 0;
        }
    }
}

