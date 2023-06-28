/*
 *  tree.h
 *
 *  Written by:		Ullrich Hafner
 *		
 *  This file is part of FIASCO (Fractal Image And Sequence COdec)
 *  Copyright (C) 1994-2000 Ullrich Hafner
 */

/*
 *  $Date: 2000/06/14 20:50:13 $
 *  $Author: hafner $
 *  $Revision: 5.1 $
 *  $State: Exp $
 */

#ifndef _TREE_H
#define _TREE_H

#include "wfa.h"
#include "bit-io.h"
#include "tiling.h"

void
read_tree (wfa_t *wfa, tiling_t *tiling, bitfile_t *input);

#endif /* not _TREE_H */

