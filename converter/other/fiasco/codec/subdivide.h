/*
 *  subdivide.h
 *
 *  Written by:		Ullrich Hafner
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

#ifndef _SUBDIVIDE_H
#define _SUBDIVIDE_H

#include "types.h"
#include "cwfa.h"

real_t 
subdivide (real_t max_costs, unsigned band, int y_state, range_t *range,
	   wfa_t *wfa, coding_t *c, bool_t prediction, bool_t delta);
void
cut_to_bintree (real_t *dst, const word_t *src,
		unsigned src_width, unsigned src_height,
		unsigned x0, unsigned y0, unsigned width, unsigned height);

#endif /* not _SUBDIVIDE_H */


