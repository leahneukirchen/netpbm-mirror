/*
 * This software is copyrighted as noted below.  It may be freely copied,
 * modified, and redistributed, provided that the copyright notice is
 * preserved on all copies.
 *
 * There is no warranty or other guarantee of fitness for this software,
 * it is provided solely "as is".  Bug reports or fixes may be sent
 * to the author, who may or may not act on them as he desires.
 *
 * You may not include this software in a program or other software product
 * without supplying the source, or without informing the end-user that the
 * source is available for no extra charge.
 *
 * If you modify this software, you should include a notice giving the
 * name of the person performing the modification, the date of modification,
 * and the reason for such modification.
 */
/*
 * rle.h - Global declarations for Utah Raster Toolkit RLE programs.
 *
 * Author:  Todd W. Fuqua
 *      Computer Science Dept.
 *      University of Utah
 * Date:    Sun Jul 29 1984
 * Copyright (c) 1984 Todd W. Fuqua
 *
 * $Id: rle.h,v 3.0.1.5 1992/04/30 14:05:56 spencer Exp $
 */

#ifndef RLE_H
#define RLE_H

#include <stdio.h>      /* Declare FILE. */

enum rle_dispatch {
    NO_DISPATCH = -1,
    RUN_DISPATCH = 0
};

/* ****************************************************************
 * TAG( rle_pixel rle_map )
 *
 * Typedef for 8-bit (or less) pixel data.
 *
 * Typedef for 16-bit color map data.
 */
typedef unsigned char rle_pixel;
typedef unsigned short rle_map;

/*
 * Defines for traditional channel numbers.
 */
#define RLE_RED    0   /* Red channel traditionally here. */
#define RLE_GREEN  1   /* Green channel traditionally here. */
#define RLE_BLUE   2   /* Blue channel traditionally here. */
#define RLE_ALPHA -1   /* Alpha channel here. */

/*
 * Return values from rle_get_setup.
 */
#define RLE_SUCCESS   0
#define RLE_NOT_RLE  -1
#define RLE_NO_SPACE -2
#define RLE_EMPTY    -3
#define RLE_EOF      -4

/*
 * "Magic" value for is_init field.  Pi * 2^29.
 */
#define RLE_INIT_MAGIC  0x6487ED51L

/*
 * TAG( rle_hdr )
 *
 * Definition of header structure used by RLE routines.
 */

#ifndef c_plusplus
typedef
#endif
    struct rle_hdr {
        enum rle_dispatch dispatch;  /* Type of file to create. */
        unsigned int ncolors;    /* Number of color channels. */
        int *     bg_color;   /* Pointer to bg color vector. */
        int       alpha;      /* If !0, save alpha channel. */
        int       background; /* 0->just save all pixels, */
        /* 1->overlay, 2->clear to bg first. */
        int       xmin;       /* Lower X bound (left.) */
        int       xmax;       /* Upper X bound (right.) */
        int       ymin;       /* Lower Y bound (bottom.) */
        int       ymax;       /* Upper Y bound (top.) */
        int       ncmap;      /* Number of color channels in color map. */
        /* Map only saved if != 0. */
        int       cmaplen;    /* Log2 of color map length. */
        rle_map * cmap;       /* Pointer to color map array. */
        const char ** comments; /* Pointer to array of pointers to comments. */
        FILE *    rle_file;   /* Input or output file. */
        /*
         * Bit map of channels to read/save.  Indexed by (channel mod 256).
         * Alpha channel sets bit 255.
         *
         * Indexing (0 <= c <= 255):
         *      bits[c/8] & (1 << (c%8))
         */
#define RLE_SET_BIT(glob,bit) \
            ((glob).bits[((bit)&0xff)/8] |= (1<<((bit)&0x7)))
#define RLE_CLR_BIT(glob,bit) \
            ((glob).bits[((bit)&0xff)/8] &= ~(1<<((bit)&0x7)))
#define RLE_BIT(glob,bit) \
            ((glob).bits[((bit)&0xff)/8] & (1<<((bit)&0x7)))
            char    bits[256/8];
            /* Set to magic pattern if following fields are initialized. */
            /* This gives a 2^(-32) chance of missing. */
            long int is_init;
            /* Command, file name and image number for error messages. */
            const char *cmd;
            const char *file_name;
            int img_num;
            /*
             * Local storage for rle_getrow & rle_putrow.
             * rle_getrow has
             *      scan_y  int     current Y scanline.
             *      vert_skip   int     number of lines to skip.
             * rle_putrow has
             *      nblank  int     number of blank lines.
             *      brun    short(*)[2] Array of background runs.
             *      fileptr long        Position in output file.
             */
            union {
                struct {
                    int scan_y,
                        vert_skip;
                    char is_eof,    /* Set when EOF or EofOp encountered. */
                        is_seek;    /* If true, can seek input file. */
                } get;
                struct {
                    int nblank;
                    short (*brun)[2];
                    long fileptr;
                } put;
            } priv;
    }
#ifndef c_plusplus
rle_hdr             /* End of typedef. */
#endif
;

/*
 * TAG( rle_dflt_hdr )
 *
 * Global variable with possibly useful default values.
 */
extern rle_hdr rle_dflt_hdr;


/* Declare RLE library routines. */

/*****************************************************************
 * TAG( rle_get_error )
 *
 * Print an error message based on the error code returned by
 * rle_get_setup.
 */
extern int rle_get_error( int code,
                          const char *pgmname,
                          const char *fname );

/* From rle_getrow.c */

int
rle_get_setup(rle_hdr * const the_hdr);

/*****************************************************************
 * TAG( rle_get_setup_ok )
 *
 * Call rle_get_setup.  If it returns an error code, call
 * rle_get_error to print the error message, then exit with the error
 * code.
 */
extern void rle_get_setup_ok( rle_hdr *the_hdr,
                              const char *prog_name,
                              const char *file_name);

/*****************************************************************
 * TAG( rle_getrow )
 *
 * Read a scanline worth of data from an RLE file.
 */
extern int rle_getrow( rle_hdr * the_hdr,
                       rle_pixel * scanline[] );

/* From rle_getskip.c */

/*****************************************************************
 * TAG( rle_getskip )
 * Skip a scanline, return the number of the next one.
 */
extern unsigned int rle_getskip( rle_hdr *the_hdr );

/* From rle_hdr.c. */

/*****************************************************************
 * TAG( rle_names )
 *
 * Load the command and file names into the rle_hdr.
 */
extern void rle_names( rle_hdr *the_hdr,
                       const char *pgmname,
                       const char *fname,
                       int img_num );

/*****************************************************************
 * TAG( rle_hdr_cp )
 *
 * Make a "safe" copy of a rle_hdr structure.
 */
extern rle_hdr * rle_hdr_cp( rle_hdr *from_hdr,
                             rle_hdr *to_hdr );

/*****************************************************************
 * TAG( rle_hdr_init )
 *
 * Initialize a rle_hdr structure.
 */
extern rle_hdr * rle_hdr_init( rle_hdr *the_hdr );

/*****************************************************************
 * TAG( rle_hdr_clear )
 *
 */
extern void rle_hdr_clear( rle_hdr *the_hdr );

/* From rle_putrow.c. */


/*****************************************************************
 * TAG( rle_puteof )
 *
 * Write an End-of-image opcode to the RLE file.
 */
extern void rle_puteof( rle_hdr *the_hdr );

/*****************************************************************
 * TAG( rle_putrow )
 *
 * Write a scanline of data to the RLE file.
 */
extern void rle_putrow( rle_pixel *rows[], int rowlen, rle_hdr *the_hdr );

/*****************************************************************
 * TAG( rle_put_init )
 *
 * Initialize header for output, but don't write it to the file.
 */
extern void rle_put_init( rle_hdr * the_hdr );

/*****************************************************************
 * TAG( rle_put_setup )
 *
 * Write header information to a new RLE image file.
 */
extern void rle_put_setup( rle_hdr * the_hdr );

/*****************************************************************
 * TAG( rle_skiprow )
 *
 * Skip nrow scanlines in the output file.
 */
extern void rle_skiprow( rle_hdr *the_hdr, int nrow );

/* From rle_row_alc.c. */
/*****************************************************************
 * TAG( rle_row_alloc )
 *
 * Allocate scanline memory for use by rle_getrow.
 */
extern int rle_row_alloc( rle_hdr * the_hdr,
                          rle_pixel *** scanp );

/*****************************************************************
     * TAG( rle_row_free )
     *
     * Free the above.
     */
extern void rle_row_free( rle_hdr *the_hdr, rle_pixel **scanp );


/* From rle_getcom.c. */
/*****************************************************************
 * TAG( rle_getcom )
 *
 * Get a specific comment from the image comments.
 */
const char *
rle_getcom(const char * const name,
           rle_hdr *    const the_hdr);

/* From rle_putcom.c. */

/* Put (or replace) a comment into the image comments. */
const char *
rle_putcom(const char * const value,
           rle_hdr *    const the_hdr);

/* From rle_open_f.c. */
/*****************************************************************
 * TAG( rle_open_f )
 *
 * Open an input/output file with default.
 */
FILE *
rle_open_f(const char * prog_name, const char * file_name,
           const char * mode);

/*****************************************************************
 * TAG( rle_open_f_noexit )
 *
 * Open an input/output file with default.
 */
FILE *
rle_open_f_noexit(const char * const prog_name,
                  const char * const file_name,
                  const char * const mode);


/* From rle_addhist.c. */

/* Append history information to the HISTORY comment. */
void
rle_addhist(const char ** const argv,
            rle_hdr *     const in_hdr,
            rle_hdr *     const out_hdr);

/* From cmd_name.c. */
/*****************************************************************
 * TAG( cmd_name )
 * Extract command name from argv.
 */
extern char *cmd_name( char **argv );


#endif /* RLE_H */
