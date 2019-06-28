/*
 *  basis.h
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

#ifndef _BASIS_H
#define _BASIS_H

#include "wfa.h"

bool_t
get_linked_basis (const char *basis_name, wfa_t *wfa);

#endif /* not _BASIS_H */

