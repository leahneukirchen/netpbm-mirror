#include "camera.h"

struct jhead {
  int bits, high, wide, clrs, vpred[4];
  struct decode *huff[4];
  unsigned short *row;
};

LoadRawFn lossless_jpeg_load_raw;

int  
ljpeg_start (FILE *         const ifP,
             struct jhead * const jhP);

int 
ljpeg_diff (FILE *          const ifP,
            struct decode * const dindexP);

void
ljpeg_row(FILE *         const ifP,
          struct jhead * const jhP);
