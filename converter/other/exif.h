#ifndef EXIF_H_INCLUDED
#define EXIF_H_INCLUDED

#include <stdio.h>
#include "netpbm/pm_c_util.h"

#define MAX_COMMENT 2000

#if MSVCRT
    #define PATH_MAX _MAX_PATH
#endif

/*--------------------------------------------------------------------------
  This structure stores Exif header image elements in a simple manner
  Used to store camera data as extracted from the various ways that it can be
  stored in an exif header
--------------------------------------------------------------------------*/
typedef struct {
    char  CameraMake   [32];
    char  CameraModel  [40];
    char  DateTime     [20];
    float XResolution;
    float YResolution;
    int   Orientation;
    int   IsColor;
    int   FlashUsed;
    float FocalLength;
    float ExposureTime;
    float ApertureFNumber;
    float Distance;
    int   HaveCCDWidth;  /* boolean */
    float CCDWidth;
    float ExposureBias;
    int   Whitebalance;
    int   MeteringMode;
    int   ExposureProgram;
    int   ISOequivalent;
    int   CompressionLevel;
    char  Comments[MAX_COMMENT];

    const unsigned char * ThumbnailPointer;  /* Pointer at the thumbnail */
    unsigned ThumbnailSize;     /* Size of thumbnail. */

    const char * DatePointer;
} exif_ImageInfo;


/* Prototypes for exif.c functions. */

void 
exif_parse(const unsigned char * const exifSection, 
           unsigned int          const length,
           exif_ImageInfo *      const imageInfoP, 
           bool                  const wantTagTrace,
           const char **         const errorP);

void 
exif_showImageInfo(const exif_ImageInfo * const imageInfoP,
                   FILE *                 const fileP);

#endif
