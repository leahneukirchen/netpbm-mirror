/*
 *  pnmpsnr.c: Compute error (RMSE, PSNR) between images
 *
 *
 *  Derived from pnmpnsmr by Ulrich Hafner, part of his fiasco package,
 *  On 2001.03.04.

 *  Copyright (C) 1994-2000 Ullrich Hafner <hafner@bigfoot.de>
 */

#include <string.h>
#include <stdio.h>
#include <math.h>

#include "pm_c_util.h"
#include "nstring.h"
#include "pam.h"

#define MAXFILES 16

static int
udiff(unsigned int const subtrahend, unsigned int const subtractor) {
    return subtrahend-subtractor;
}


static double
square(double const arg) {
    return(arg*arg);
}


static void
validate_input(const struct pam pam1, const struct pam pam2) {

    if (pam1.width != pam2.width)
        pm_error("images are not the same width, so can't be compared.  "
                 "The first is %d columns wide, "
                 "while the second is %d columns wide.",
                 pam1.width, pam2.width);
    if (pam1.height != pam2.height)
        pm_error("images are not the same height, so can't be compared.  "
                 "The first is %d rows high, "
                 "while the second is %d rows high.",
                 pam1.height, pam2.height);

    if (pam1.maxval != pam2.maxval)
        pm_error("images do not have the same maxval.  This programs works "
                 "only on like maxvals.  "
                 "The first image has maxval %u, "
                 "while the second has %u.  Use Pamdepth to change the "
                 "maxval of one of them.",
                 (unsigned int) pam1.maxval, (unsigned int) pam2.maxval);

    if (strcmp(pam1.tuple_type, pam2.tuple_type) != 0)
        pm_error("images are not of the same type.  The tuple types are "
                 "'%s' and '%s', respectively.",
                 pam1.tuple_type, pam2.tuple_type);

    if (strcmp(pam1.tuple_type, PAM_PBM_TUPLETYPE) != 0 &&
        strcmp(pam1.tuple_type, PAM_PGM_TUPLETYPE) != 0 &&
        strcmp(pam1.tuple_type, PAM_PPM_TUPLETYPE) != 0)
        pm_error("Images are not of a PNM type.  Tuple type is '%s'",
                 pam1.tuple_type);
}



static void
psnr_color(tuple    const tuple1,
           tuple    const tuple2,
           double * const ySqDiffP, 
           double * const cbSqDiffP,
           double * const crSqDiffP) {

    double y1, y2, cb1, cb2, cr1, cr2;
    
    pnm_YCbCrtuple(tuple1, &y1, &cb1, &cr1);
    pnm_YCbCrtuple(tuple2, &y2, &cb2, &cr2);
    
    *ySqDiffP  = square(y1  - y2);
    *cbSqDiffP = square(cb1 - cb2);
    *crSqDiffP = square(cr1 - cr2);
}



static void
reportPsnr(struct pam const pam,
           double     const ySumSqDiff, 
           double     const crSumSqDiff,
           double     const cbSumSqDiff,
           char       const filespec1[],
           char       const filespec2[]) {

    bool const color = streq(pam.tuple_type, PAM_PPM_TUPLETYPE);

    /* The PSNR is the ratio of the maximum possible mean square difference
       to the actual mean square difference.
    */
    double const yPsnr =
        square(pam.maxval) / (ySumSqDiff / (pam.width * pam.height));

    /* Note that in the important special case that the images are
       identical, the sum square differences are identically 0.0.  No
       precision error; no rounding error.
    */

    if (color) {
        double const cbPsnr =
            square(pam.maxval) / (cbSumSqDiff / (pam.width * pam.height));
        double const crPsnr =
            square(pam.maxval) / (crSumSqDiff / (pam.width * pam.height));

        pm_message("PSNR between %s and %s:", filespec1, filespec2);
        if (ySumSqDiff > 0)
            pm_message("Y  color component: %.2f dB", 10 * log10(yPsnr));
        else
            pm_message("Y color component does not differ.");
        if (cbSumSqDiff > 0)
            pm_message("Cb color component: %.2f dB", 10 * log10(cbPsnr));
        else
            pm_message("Cb color component does not differ.");
        if (crSumSqDiff > 0)
            pm_message("Cr color component: %.2f dB", 10 * log10(crPsnr));
        else
            pm_message("Cr color component does not differ.");
    } else {
        if (ySumSqDiff > 0)
            pm_message("PSNR between %s and %s: %.2f dB",
                       filespec1, filespec2, 10 * log10(yPsnr));
        else
            pm_message("Images %s and %s don't differ.",
                       filespec1, filespec2);
    }
}



int
main (int argc, const char **argv) {
    const char * fileName1;  /* name of first file to compare */
    const char * fileName2;  /* name of second file to compare */
    FILE * if1P;
    FILE * if2P;
    struct pam pam1, pam2;
    bool color;
        /* It's a color image */
    double ySumSqDiff, crSumSqDiff, cbSumSqDiff;
    tuple *tuplerow1, *tuplerow2;  /* malloc'ed */
    int row;
    
    pm_proginit(&argc, argv);

    if (argc-1 < 2) 
        pm_error("Takes two arguments:  names of the two files to compare");
    else {
        fileName1 = argv[1];
        fileName2 = argv[2];

        if (argc-1 > 2)
            pm_error("Too many arguments (%u).  The only arguments are "
                     "the names of the two files to compare", argc-1);
    }
    
    if1P = pm_openr(fileName1);
    if2P = pm_openr(fileName2);

    pnm_readpaminit(if1P, &pam1, PAM_STRUCT_SIZE(tuple_type));
    pnm_readpaminit(if2P, &pam2, PAM_STRUCT_SIZE(tuple_type));

    validate_input(pam1, pam2);

    if (streq(pam1.tuple_type, PAM_PPM_TUPLETYPE)) 
        color = TRUE;
    else
        color = FALSE;

    tuplerow1 = pnm_allocpamrow(&pam1);
    tuplerow2 = pnm_allocpamrow(&pam2);
    
    ySumSqDiff  = 0.0;
    cbSumSqDiff = 0.0;
    crSumSqDiff = 0.0;

    for (row = 0; row < pam1.height; ++row) {
        int col;
        
        pnm_readpamrow(&pam1, tuplerow1);
        pnm_readpamrow(&pam2, tuplerow2);

        for (col = 0; col < pam1.width; ++col) {
            if (color) {
                double ySqDiff, cbSqDiff, crSqDiff;
                psnr_color(tuplerow1[col], tuplerow2[col], 
                           &ySqDiff, &cbSqDiff, &crSqDiff);
                ySumSqDiff  += ySqDiff;
                cbSumSqDiff += cbSqDiff;
                crSumSqDiff += crSqDiff;
                
            } else {
                unsigned int const yDiffSq =
                    square(udiff(tuplerow1[col][0], tuplerow2[col][0]));
                ySumSqDiff += yDiffSq;
            }
        }
    }

    reportPsnr(pam1, ySumSqDiff, crSumSqDiff, cbSumSqDiff,
               fileName1, fileName2);

    pnm_freepamrow(tuplerow1);
    pnm_freepamrow(tuplerow2);

    return 0;
}
