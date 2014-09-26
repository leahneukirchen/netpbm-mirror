#include "camera.h"

struct jhead {
  int bits, high, wide, clrs, vpred[4];
  struct decode *huff[4];
  unsigned short *row;
};

LoadRawFn lossless_jpeg_load_raw;

int  
ljpeg_start (FILE * ifp, struct jhead *jh);

int 
ljpeg_diff (struct decode *dindex);

void
ljpeg_row (struct jhead *jh);
