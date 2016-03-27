/*===========================================================================*
 * mproto.h								     *
 *									     *
 *	basically a lot of miscellaneous prototypes			     *
 *									     *
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

/*  
 *  $Header: /n/picasso/project/mm/mpeg/mpeg_dist/mpeg_encode/headers/RCS/mproto.h,v 1.12 1995/03/29 20:14:29 smoot Exp $
 *  $Log: mproto.h,v $
 * Revision 1.12  1995/03/29  20:14:29  smoot
 * deleted unneeded dct prototype
 *
 * Revision 1.11  1995/01/19  23:55:02  eyhung
 * Changed copyrights
 *
 * Revision 1.10  1995/01/16  06:20:10  eyhung
 * Changed ReadYUV to ReadEYUV
 *
 * Revision 1.9  1993/07/22  22:24:23  keving
 * nothing
 *
 * Revision 1.8  1993/07/09  00:17:23  keving
 * nothing
 *
 * Revision 1.7  1993/06/03  21:08:53  keving
 * nothing
 *
 * Revision 1.6  1993/02/24  19:13:33  keving
 * nothing
 *
 * Revision 1.5  1993/02/17  23:18:20  dwallach
 * checkin prior to keving's joining the project
 *
 * Revision 1.4  1993/01/18  10:20:02  dwallach
 * *** empty log message ***
 *
 * Revision 1.3  1993/01/18  10:17:29  dwallach
 * RCS headers installed, code indented uniformly
 *
 * Revision 1.3  1993/01/18  10:17:29  dwallach
 * RCS headers installed, code indented uniformly
 *
 */


/*==============*
 * HEADER FILES *
 *==============*/

#include "general.h"
#include "bitio.h"


typedef short DCTELEM;
typedef DCTELEM DCTBLOCK[DCTSIZE2];



/*===============================*
 * EXTERNAL PROCEDURE prototypes *
 *===============================*/

/*  
 *  from mbasic.c:
 */
void mp_reset (void);
void mp_free (MpegFrame *mf);
MpegFrame *mp_new (int fnumber, char type, MpegFrame *oldFrame);
void mp_ycc_calc (MpegFrame *mf);
void mp_dct_blocks (MpegFrame *mf);
void	AllocDecoded (MpegFrame *frame);

/*  
 *  from moutput.c:
 */
boolean mp_quant_zig_block (Block in, FlatBlock out, int qscale, int iblock);
void	UnQuantZig (FlatBlock in, Block out, int qscale, boolean iblock);
void mp_rle_huff_block (FlatBlock in, BitBucket *out);
void mp_rle_huff_pblock (FlatBlock in, BitBucket *out);
void mp_create_blocks (MpegFrame *mf);




void	ReadEYUV (MpegFrame * mf, FILE *fpointer, int width,
			    int height);
boolean	ReadPPM (MpegFrame *mf, FILE *fpointer);
void PPMtoYCC (MpegFrame * mf);

void	ComputeHalfPixelData (MpegFrame *frame);
void mp_validate_size (int *x, int *y);
void AllocYCC (MpegFrame * mf);


/* jrevdct.c */
void init_pre_idct (void );
void j_rev_dct_sparse (DCTBLOCK data , int pos );
void j_rev_dct (DCTBLOCK data );
void j_rev_dct_sparse (DCTBLOCK data , int pos );
void j_rev_dct (DCTBLOCK data );

