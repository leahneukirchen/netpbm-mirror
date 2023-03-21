#ifndef EXIF_H_INCLUDED
#define EXIF_H_INCLUDED

#include <stdio.h>
#include "netpbm/pm_c_util.h"

#define MAX_COMMENT 2000

#if MSVCRT
    #define PATH_MAX _MAX_PATH
#endif

typedef struct {
/*--------------------------------------------------------------------------
  A structure of this type contains the information from an EXIF header
  Image File Directory (IFD).

  It appears that some of these members are possible only for certain kinds of
  IFD (e.g. ThumbnailSize does not appear in a legal IFD for a main image),
  but we recognize all tags in all IFDs all the same.
--------------------------------------------------------------------------*/
    char  cameraMake   [32];
    char  cameraModel  [40];
    char  dateTime     [20];
    float xResolution;
    float yResolution;
    int   orientation;
    int   isColor;
    int   flashUsed;
    float focalLength;
    float exposureTime;
    float apertureFNumber;
    float distance;
    float exposureBias;
    int   whiteBalance;
    int   meteringMode;
    int   exposureProgram;
    int   isoEquivalent;
    int   compressionLevel;
    char  comments[MAX_COMMENT];
    unsigned int thumbnailOffset;
    unsigned int thumbnailLength;
    unsigned int imageLength;
    unsigned int imageWidth;
    double       focalPlaneXRes;
    double       focalPlaneUnits;

    const unsigned char * thumbnail;  /* Pointer at the thumbnail */
    unsigned thumbnailSize;     /* Size of thumbnail. */
} exif_ifd;


typedef struct {
/*--------------------------------------------------------------------------
  A structure of this type contains the information from an EXIF header.
--------------------------------------------------------------------------*/
    exif_ifd mainImage;       /* aka IFD0 */
    exif_ifd thumbnailImage;  /* aka IFD1 */
    bool     haveCCDWidth;
    float    ccdWidth;
} exif_ImageInfo;


/* Prototypes for exif.c functions. */

void
exif_parse(const unsigned char * const exifSection,
           unsigned int          const length,
           exif_ImageInfo *      const imageInfoP,
           bool                  const wantTagTrace,
           const char **         const errorP);

void
exif_showImageInfo(const exif_ImageInfo * const imageInfoP);

#endif
