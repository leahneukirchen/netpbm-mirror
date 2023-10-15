#ifndef RUNPUT_H_INCLUDED
#define RUNPUT_H_INCLUDED

void
RunSetup(rle_hdr * const hdrP);

void
RunSkipBlankLines(unsigned int const nblank,
                  rle_hdr *    const hdrP);

void
RunSetColor(int       const c,
            rle_hdr * const hdrP);

void
RunSkipPixels(unsigned int const nskip,
              int          const last,
              int          const wasrun,
              rle_hdr *    const hdrP);

void
RunNewScanLine(int       const flag,
               rle_hdr * const hdrP);

void
Runputdata(rle_pixel *  const buf,
           unsigned int const n,
           rle_hdr *    const hdrP);

void
Runputrun(int          const color,
          unsigned int const n,
          int          const last,
          rle_hdr *    const hdrP);

void
RunputEof(rle_hdr * const hdrP);

#endif
