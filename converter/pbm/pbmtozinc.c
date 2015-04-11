/* pbmtozinc.c - read a portable bitmap and produce an bitmap file
**               in the format used by the Zinc Interface Library (v1.0)
**               November 1990.
**
** Author: James Darrell McCauley
**         Department of Agricultural Engineering
**         Texas A&M University
**         College Station, Texas 77843-2117 USA
**
** Copyright (C) 1988 by James Darrell McCauley (jdm5548@diamond.tamu.edu)
**                    and Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <stdio.h>
#include <string.h>

#include "nstring.h"
#include "pbm.h"

static int itemsperline;
static int bitsperitem;
static int item;
static int firstitem;
static const char * const hexchar = "084c2a6e195d3b7f";



static void
putitem() {
    if ( firstitem )
        firstitem = 0;
    else
        putchar( ',' );

    if ( itemsperline == 11 ) {
        putchar( '\n' );
        itemsperline = 0;
        }
    if ( itemsperline == 0 )
        putchar( ' ' );

    ++itemsperline;
    putchar('0');
    putchar('x');
    putchar(hexchar[item & 15]);
    putchar(hexchar[(item >> 4) & 15]);
    putchar(hexchar[(item >> 8) & 15]);
    putchar(hexchar[item >> 12]);
    bitsperitem = 0;
    item = 0;
}



static void
putbit(const bit b) {
    if ( bitsperitem == 16 )
      putitem();
    if ( (b) == PBM_BLACK )
      item += 1 << bitsperitem;
    ++bitsperitem;
}


int
main(int argc, char * argv[]) {

    FILE* ifp;
    bit* bitrow;
    bit* bP;
    int rows, cols, format, padright, row;
    int col;
    char name[100];
    char* cp;

    pbm_init( &argc, argv );

    if ( argc > 2 )
        pm_usage( "[pbmfile]" );

    if ( argc == 2 ) {
        ifp = pm_openr( argv[1] );
        strcpy( name, argv[1] );
        if ( streq( name, "-" ) )
            strcpy( name, "noname" );

        if ( ( cp = strchr( name, '.' ) ) != 0 )
            *cp = '\0';
        }
    else {
        ifp = stdin;
        strcpy( name, "noname" );
        }

    pbm_readpbminit( ifp, &cols, &rows, &format );
    bitrow = pbm_allocrow( cols );

    /* Compute padding to round cols up to the nearest multiple of 16. */
    padright = ( ( cols + 15 ) / 16 ) * 16 - cols;

    printf( "USHORT %s[] = {\n",name);
    printf( "  %d\n", cols );
    printf( "  %d\n", rows );

    itemsperline = 0;
    bitsperitem = 0;
    item = 0;
    firstitem = 1;


    for ( row = 0; row < rows; ++row ) {
        pbm_readpbmrow( ifp, bitrow, cols, format );
        for ( col = 0, bP = bitrow; col < cols; ++col, ++bP )
            putbit( *bP );
        for ( col = 0; col < padright; ++col )
            putbit( 0 );
    }

    pm_close( ifp );

    if ( bitsperitem > 0 )
        putitem();
    printf( "};\n" );

    return 0;
}
