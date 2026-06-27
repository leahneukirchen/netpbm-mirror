#include <stdio.h>
#include <netpbm/pbm.h>

#define HEADERSIZE 16   /* 16 is just a guess at the header size */

int main (int argc, const char **argv)
{
  FILE *ifP;
  int cols, rows, format;
  unsigned char header[HEADERSIZE];
  unsigned char *bitrow;
  int c, r;
  
  /* Initialize Netpbm and begin reading the input file. */
  pm_proginit(&argc, argv);
  if (argc == 1)
    ifP = stdin;
  else
    ifP = pm_openr(argv[1]);
  pbm_readpbminit(ifP, &cols, &rows, &format);
  if (cols <= 0 || cols > 65535)
    pm_error("Image width must lie within the range [1, 65535]");
  if (rows <= 0 || rows > 65535)
    pm_error("Image height must lie within the range [1, 65535]");

  /* Write the output file header. */
  header[0] = 1;
  header[1] = 0;
  header[2] = (unsigned char)((unsigned int)cols>>8 & 0xFF);
  header[3] = (unsigned char)cols;
  header[4] = (unsigned char)((unsigned int)rows>>8 & 0xFF);
  header[5] = (unsigned char)rows;
  if (fwrite(header, 1, HEADERSIZE, stdout) != HEADERSIZE)
    pm_error("Failed to write the brush header");

  /* Write each image row in turn. */
  bitrow = pbm_allocrow_packed(cols + 16);
  for (r = 0; r < rows; ++r) {
    pbm_readpbmrow_packed(ifP, bitrow, cols, format);
    for (c = 0; c < cols; ++c)
      bitrow[c] = ~bitrow[c];
    pbm_cleanrowend_packed(bitrow, cols);
    fwrite(bitrow, 1, 2*((cols + 15)/16), stdout);
  }
  pbm_freerow_packed(bitrow);

  /* Return successfully. */
  if (ifP != stdin)
    pm_close(ifP);
  return 0;
}
