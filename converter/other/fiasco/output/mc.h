/*
 *  mc.h
 *
 *  Written by:		Michael Unger
 *			Ullrich Hafner
 *		
 *  This file is part of FIASCO (Fractal Image And Sequence COdec)
 *  Copyright (C) 1994-2000 Ullrich Hafner
 */

/*
 *  $Date: 2000/06/14 20:50:31 $
 *  $Author: hafner $
 *  $Revision: 5.1 $
 *  $State: Exp $
 */

#ifndef _MC_H
#define _MC_H

#include "wfa.h"
#include "bit-io.h"

void
write_mc (frame_type_e frame_type, const wfa_t *wfa, bitfile_t *output);

#endif /* not _MC_H */

