/*===========================================================================*
 * prototypes.h                                  *
 *                                       *
 *  miscellaneous prototypes                         *
 *                                       *
 *===========================================================================*/

/*
 * Copyright (c) 1995 The Regents of the University of California.
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without written agreement is
 * hereby granted, provided that the above copyright notice and the following
 * two paragraphs appear in all copies of this software.
 *
 * IN NO EVENT SHALL THE UNIVERSITY OF CALIFORNIA BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES ARISING OUT
 * OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN IF THE UNIVERSITY OF
 * CALIFORNIA HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * THE UNIVERSITY OF CALIFORNIA SPECIFICALLY DISCLAIMS ANY WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE UNIVERSITY OF CALIFORNIA HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 */
/*==============*
 * HEADER FILES *
 *==============*/

#include "general.h"
#include "frame.h"


/*===============================*
 * EXTERNAL PROCEDURE prototypes *
 *===============================*/

int GetBQScale (void);
int GetPQScale (void);
void    ResetBFrameStats (void);
void    ResetPFrameStats (void);
void SetSearchRange (int const pixelsP,
                     int const pixelsB);
void
SetPixelSearch(const char * const searchType);
void    SetPQScale (int qP);
void    SetBQScale (int qB);
float   EstimateSecondsPerPFrame (void);
float   EstimateSecondsPerBFrame (void);
void    SetGOPSize (int size);
void
SetStatFileName(const char * const fileName);


void DCTFrame (MpegFrame * mf);

void PPMtoYCC (MpegFrame * mf);

void    MotionSearchPreComputation (MpegFrame *frame);

void    ComputeHalfPixelData (MpegFrame *frame);
void mp_validate_size (int *x, int *y);


/* psearch.c */
void    ShowPMVHistogram (FILE *fpointer);
void    ShowBBMVHistogram (FILE *fpointer);
void    ShowBFMVHistogram (FILE *fpointer);
