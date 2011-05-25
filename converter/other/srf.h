#ifndef SRF_H_INCLUDED
#define SRF_H_INCLUDED

/*
 * Structures for working with SRF (Garmin vehicle) files
 * http://www.techmods.net/nuvi/
 *
 * Written by Mike Frysinger <vapier@gentoo.org>
 * Released into the public domain
 */

#include "pm_config.h"
#include "pam.h"

struct srf_pstring {
    uint32_t len;
    char *   val;
};

#define SRF_NUM_FRAMES 36

/*
  File Header
      16 bytes - string - "GARMIN BITMAP 01"
      32 bytes - two 32-bit ints, [4, 4] -- purpose unknown
      4 bytes - 32-bit int -- number of images (usually just 2)
      4 bytes - 32-bit int, [5] -- purpose unknown
      7 bytes - PString - "578"
      4 bytes - 32-bit int, [6] -- purpose unknown
      8 bytes - PString - version number ("1.00", "2.00", "2.10", or "2.20")
      4 bytes - 32-bit int, [7] -- purpose unknown
      16 bytes - PString - "006-D0578-XX" (where "XX" changes) --
                           I assume this is Garmin's product code?
*/
#define SRF_MAGIC "GARMIN BITMAP 01"

struct srf_header {
    char magic[16 + 1];

    uint32_t           _int4[2];

    uint32_t           img_cnt;

    uint32_t           _int5;

    struct srf_pstring s578;

    uint32_t           _int6;

    struct srf_pstring ver;

    uint32_t           _int7;

    struct srf_pstring prod;
};

/*
  Image Header
     12 bytes - three 32-bit ints, [0,16,0] -- purpose unknown
     2 bytes - 16-bit int -- height of image (just the 3D section, so it's 80)
     2 bytes - 16-bit int -- width of image (just the 3D section, 2880 or 2881)
     2 bytes - [16, 8] -- purpose unknown
     2 bytes - 16-bit int -- byte length of each line of image RGB data
                             (16-bit RGB), so "width * 2"
     4 bytes - all zeroes -- purpose unknown
*/
struct srf_img_header {
    uint32_t _ints[3];

    uint16_t height, width;

    char     _bytes[2];

    uint16_t line_len;

    uint32_t zeros;
};

/*
  Image Alpha Mask

      4 bytes - 32-bit int, [11] -- Might specify the type of data that
                follows?

      4 bytes - 32-bit int, length of following data (width*height of 3D
                section)

      width*height bytes - alpha mask data, 0 = opaque, 128 = transparent
                           (across, then down)

  Notes: The Garmin format has 129 values: [0..128] [opaque..transparent]
         The PNG format has 256 values:    [0..255] [transparent..opaque]
         So we have to do a little edge case tweaking to keep things lossless.
*/

#define SRF_ALPHA_OPAQUE 0
#define SRF_ALPHA_TRANS  128

struct srf_img_alpha {
    uint32_t        type;

    uint32_t        data_len;
    unsigned char * data;
};

/*
  Image RGB Data
      4 bytes - 32-bit int, [1] -- Might specify the type of data that follows?
      4 bytes - 32-bit int, length of following data (width*height*2 of 3D
                            section, as the RGB data is 16-bit)
      width*height*2 bytes - RBG values as "rrrrrggggg0bbbbb" bits
                             (across, then down)
*/

struct srf_img_data {
    uint32_t   type;

    uint32_t   data_len;
    uint16_t * data;
};

struct srf_img {
    struct srf_img_header header;
    struct srf_img_alpha  alpha;
    struct srf_img_data   data;
};

/*
  Footer
      arbitrary number of bytes - all 0xFF -- these are used (as well as the
                                              checksum byte) to pad the file
                                              size to a multiple of 256.

      1 byte - checksum byte -- use this byte to adjust so that the ascii sum
                                of all bytes in the file is a multiple of 256.
 */

struct srf {
    struct srf_header header;
    struct srf_img *  imgs;
};

uint8_t
srf_alpha_srftopam(uint8_t const d);

uint8_t
srf_alpha_pamtosrf(uint8_t const d);

void
srf_read(FILE *       const ifP,
         bool         const verbose,
         struct srf * const srfP);

void
srf_write(FILE *       const ofP,
          struct srf * const srfP);

void
srf_term(struct srf * const srfP);

void
srf_init(struct srf * const srfP);

void
srf_create_img(struct srf * const srfP,
               uint16_t     const width,
               uint16_t     const height);

#endif

