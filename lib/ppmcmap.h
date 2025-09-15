#ifndef PPMCMAP_INCLUDED
#define PPMCMAP_INCLUDED
/* ppmcmap.h - header file for colormap routines in libppm
*/

#ifdef __cplusplus
extern "C" {
#endif
#if 0
} /* to fake out automatic code indenters */
#endif


/* Color histogram stuff. */

typedef struct colorhist_item* colorhist_vector;
struct colorhist_item {
    pixel color;
    int   value;
};

typedef struct colorhist_list_item* colorhist_list;
struct colorhist_list_item {
    struct colorhist_item ch;
    colorhist_list        next;
};

colorhist_vector
ppm_computecolorhist(pixel ** const pixels,
                     int      const cols,
                     int      const rows,
                     int      const maxcolorCt,
                     int *    const colorCtP);
colorhist_vector
ppm_computecolorhist2(FILE * const ifP,
                      int    const cols,
                      int    const rows,
                      pixval const maxval,
                      int    const format,
                      int    const maxcolorCt,
                      int *  const colorCtP);

void
ppm_addtocolorhist(colorhist_vector       chv,
                   int *            const colorCtP,
                   int              const maxcolorCt,
                   const pixel *    const colorP,
                   int              const value,
                   int              const position);

void
ppm_freecolorhist(colorhist_vector const chv);


/* Color hash table stuff. */

typedef colorhist_list* colorhash_table;

colorhash_table
ppm_computecolorhash(pixel ** const pixels,
                     int      const cols,
                     int      const rows,
                     int      const maxcolorCt,
                     int *    const colorCtP);

colorhash_table
ppm_computecolorhash2(FILE * const ifP,
                      int    const cols,
                      int    const rows,
                      pixval const maxval,
                      int    const format,
                      int    const maxcolorCt,
                      int *  const colorCtP);

int
ppm_lookupcolor(colorhash_table const cht,
                const pixel *   const colorP );

colorhist_vector
ppm_colorhashtocolorhist(colorhash_table const cht,
                         int             const maxcolorCt);

colorhash_table
ppm_colorhisttocolorhash(colorhist_vector const chv,
                         int              const colorCt);

int
ppm_addtocolorhash(colorhash_table const cht,
                   const pixel *   const colorP,
                   int             const value);

void
ppm_delfromcolorhash(colorhash_table const cht,
                     const pixel *   const colorP);


colorhash_table
ppm_alloccolorhash(void);

void
ppm_freecolorhash(colorhash_table const cht);


colorhash_table
ppm_colorrowtocolorhash(pixel * const colorrow,
                        int     const colorCt);

pixel *
ppm_computecolorrow(pixel ** const pixels,
                    int      const cols,
                    int      const rows,
                    int      const maxcolorCt,
                    int *    const colorCtP);

pixel *
ppm_mapfiletocolorrow(FILE *   const fileP,
                      int      const maxcolorCt,
                      int *    const colorCtP,
                      pixval * const maxvalP);

void
ppm_colorrowtomapfile(FILE *  const ofP,
                      pixel * const colormap,
                      int     const colorCt,
                      pixval  const maxval);

void
ppm_sortcolorrow(pixel * const colorrow,
                 int     const colorCt,
                 int (*cmpfunc)(pixel *, pixel *));

int
ppm_addtocolorrow(pixel * const colorrow,
                  int *   const colorCtP,
                  int     const maxcolorCt,
                  pixel * const pixelP);

int
ppm_findclosestcolor(const pixel * const colormap,
                     int           const colorCt,
                     const pixel * const pP);

/* standard sort function for ppm_sortcolorrow() */
#define PPM_STDSORT     (int (*)(pixel *, pixel *))0

#ifdef __cplusplus
}
#endif

#endif /* PPMCMAP_INCLUDED */
