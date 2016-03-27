/*
 * Funcs for working with SRF (Garmin vehicle) files
 *
 * Written by Mike Frysinger <vapier@gentoo.org>
 * Released into the public domain
 */

#include <stdio.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "nstring.h"
#include "srf.h"


static unsigned char
csumRaw(void * const p,
       size_t const len) {

    unsigned char retval;

    unsigned int i;
    unsigned char * c;

    for (i = 0, retval = 0, c = p; i < len; ++i)
        retval += *c++;
    
    return retval;
}



static unsigned char
csumPstring(struct srf_pstring * const pstringP) {

  return
      csumRaw(&pstringP->len, 4) + csumRaw(pstringP->val, pstringP->len);
}



static bool
readPstring(FILE *               const ifP,
            struct srf_pstring * const pstringP) {

    size_t bytesRead;

    pm_readlittlelong2u(ifP, &pstringP->len);

    MALLOCARRAY(pstringP->val, pstringP->len + 1);

    if (!pstringP->val)
        pm_error("Failed to allocate buffer to read %u-byte pstring",
                 pstringP->len);

    bytesRead = fread(pstringP->val, 1, pstringP->len, ifP);
    if (bytesRead != pstringP->len)
        pm_error("Failed to read pstring.  Requested %u bytes, got %u",
                 (unsigned)pstringP->len,  (unsigned)bytesRead);

    pstringP->val[pstringP->len] = '\0';

    return true;
}



static bool
writePstring(FILE *               const ofP,
             struct srf_pstring * const pstringP) {

    bool retval;
    size_t bytesWritten;

    pm_writelittlelongu(ofP, pstringP->len);

    bytesWritten = fwrite(pstringP->val, 1, pstringP->len, ofP);

    if (bytesWritten == pstringP->len)
        retval = true;
    else
        retval = false;

    return retval;
}



static size_t
lenHeader(struct srf_header * const headerP) {

    return 16 + (4 * 4) + 4 + headerP->s578.len
        + 4 + 4 + headerP->ver.len
        + 4 + 4 + headerP->prod.len;
}



static unsigned char
csumHeader(struct srf_header * const headerP) {

    return
        csumRaw(headerP->magic, 16) +
        csumRaw(&headerP->_int4, 2 * 4) +
        csumRaw(&headerP->img_cnt, 4) +
        csumRaw(&headerP->_int5, 4) +
        csumPstring(&headerP->s578) +
        csumRaw(&headerP->_int6, 4) +
        csumPstring(&headerP->ver) +
        csumRaw(&headerP->_int7, 4) +
        csumPstring(&headerP->prod);
}



static bool
readHeader(FILE *              const ifP,
           struct srf_header * const headerP) {

    bool const retval =
        fread(headerP->magic, 1, 16, ifP) == 16 &&
        pm_readlittlelong2u(ifP, &headerP->_int4[0]) == 0 &&
        pm_readlittlelong2u(ifP, &headerP->_int4[1]) == 0 &&
        pm_readlittlelong2u(ifP, &headerP->img_cnt) == 0 &&
        pm_readlittlelong2u(ifP, &headerP->_int5) == 0 &&
        readPstring(ifP, &headerP->s578) &&
        pm_readlittlelong2u(ifP, &headerP->_int6) == 0 &&
        readPstring(ifP, &headerP->ver) &&
        pm_readlittlelong2u(ifP, &headerP->_int7) == 0 &&
        readPstring(ifP, &headerP->prod);

    headerP->magic[16] = '\0';

    return retval;
}



static bool
writeHeader(FILE *              const ofP,
            struct srf_header * const headerP) {

    return
        fwrite(headerP->magic, 1, 16, ofP) == 16 &&
        pm_writelittlelongu(ofP, headerP->_int4[0]) == 0 &&
        pm_writelittlelongu(ofP, headerP->_int4[1]) == 0 &&
        pm_writelittlelongu(ofP, headerP->img_cnt) == 0 &&
        pm_writelittlelongu(ofP, headerP->_int5) == 0 &&
        writePstring(ofP, &headerP->s578) &&
        pm_writelittlelongu(ofP, headerP->_int6) == 0 &&
        writePstring(ofP, &headerP->ver) &&
        pm_writelittlelongu(ofP, headerP->_int7) == 0 &&
        writePstring(ofP, &headerP->prod);
}



static bool
checkHeader(struct srf_header * const headerP) {

    return
        streq(headerP->magic, SRF_MAGIC) &&
        headerP->_int4[0] == 4 &&
        headerP->_int4[1] == 4 &&
        /* Should we require img_cnt to be multiple of 2 ? */
        headerP->img_cnt > 0 &&
        headerP->_int5 == 5 &&
        headerP->s578.len == 3 &&
        strcmp(headerP->s578.val, "578") == 0 &&
        headerP->_int6 == 6 &&
        headerP->ver.len == 4 &&
        /* Allow any headerP->ver value */
        headerP->_int7 == 7 &&
        headerP->prod.len == 12;
    /* Allow any headerP->prod value */
}



static size_t
lenImg(struct srf_img * const imgP) {

    return
        (4 * 3) + (2 * 2) + (1 * 2) + 2 + 4 +
        4 + 4 + imgP->alpha.data_len +
        4 + 4 + imgP->data.data_len;
}



static unsigned char
csumImg(struct srf_img * const imgP) {

    struct srf_img_header * const headerP = &imgP->header;
    struct srf_img_alpha *  const alphaP  = &imgP->alpha;
    struct srf_img_data *   const dataP   = &imgP->data;

    return
        csumRaw(&headerP->_ints, 4 * 3) +
        csumRaw(&headerP->height, 2) +
        csumRaw(&headerP->width, 2) +
        csumRaw(headerP->_bytes, 2) +
        csumRaw(&headerP->line_len, 2) +
        csumRaw(&headerP->zeros, 4) +
        csumRaw(&alphaP->type, 4) +
        csumRaw(&alphaP->data_len, 4) +
        csumRaw(alphaP->data, alphaP->data_len) +
        csumRaw(&dataP->type, 4) +
        csumRaw(&dataP->data_len, 4) +
        csumRaw(dataP->data, dataP->data_len);
}



static bool
readImgHeader(FILE *                  const ifP,
              struct srf_img_header * const headerP) {
    return
        pm_readlittlelong2u(ifP, &headerP->_ints[0]) == 0 &&
        pm_readlittlelong2u(ifP, &headerP->_ints[1]) == 0 &&
        pm_readlittlelong2u(ifP, &headerP->_ints[2]) == 0 &&
        pm_readlittleshortu(ifP, &headerP->height) == 0 &&
        pm_readlittleshortu(ifP, &headerP->width) == 0 &&
        fread(&headerP->_bytes[0], 1, 1, ifP) == 1 &&
        fread(&headerP->_bytes[1], 1, 1, ifP) == 1 &&
        pm_readlittleshortu(ifP, &headerP->line_len) == 0 &&
        pm_readlittlelong2u(ifP, &headerP->zeros) == 0;
}



static bool
writeImgHeader(FILE *                  const ofP,
               struct srf_img_header * const headerP) {
    return
        pm_writelittlelongu(ofP, headerP->_ints[0]) == 0 &&
        pm_writelittlelongu(ofP, headerP->_ints[1]) == 0 &&
        pm_writelittlelongu(ofP, headerP->_ints[2]) == 0 &&
        pm_writelittleshortu(ofP, headerP->height) == 0 &&
        pm_writelittleshortu(ofP, headerP->width) == 0 &&
        fwrite(&headerP->_bytes[0], 1, 1, ofP) == 1 &&
        fwrite(&headerP->_bytes[1], 1, 1, ofP) == 1 &&
        pm_writelittleshortu(ofP, headerP->line_len) == 0 &&
        pm_writelittlelongu(ofP, headerP->zeros) == 0;
}



static bool
checkImgHeader(struct srf_img_header * const headerP) {

    return
        headerP->_ints[0] == 0 &&
        headerP->_ints[1] == 16 &&
        headerP->_ints[2] == 0 &&
        headerP->_bytes[0] == 16 &&
        headerP->_bytes[1] == 8 &&
        headerP->line_len == headerP->width * 2 &&
        headerP->zeros == 0;
}



static bool
readImgAlpha(FILE *                 const ifP,
                   struct srf_img_alpha * const alphaP) {

    bool retval;

    pm_readlittlelong2u(ifP, &alphaP->type);
    pm_readlittlelong2u(ifP, &alphaP->data_len);

    MALLOCARRAY(alphaP->data, alphaP->data_len);

    if (!alphaP->data)
        retval = false;
    else {
        size_t bytesRead;

        bytesRead = fread(alphaP->data, 1, alphaP->data_len, ifP);
        retval = (bytesRead ==alphaP->data_len);
    }
    return retval;
}



static bool
writeImageAlpha(FILE *                 const ofP,
                    struct srf_img_alpha * const alphaP) {

    return
        pm_writelittlelongu(ofP, alphaP->type) == 0 &&
        pm_writelittlelongu(ofP, alphaP->data_len) == 0 &&
        fwrite(alphaP->data, 1, alphaP->data_len, ofP) == alphaP->data_len;
}



static bool
checkImgAlpha(struct srf_img_alpha * const alphaP) {

    return (alphaP->type == 11);
}



static bool
readImgData(FILE *                const ifP,
                  struct srf_img_data * const dataP) {

    bool retval;

    pm_readlittlelong2u(ifP, &dataP->type);
    pm_readlittlelong2u(ifP, &dataP->data_len);

    MALLOCARRAY(dataP->data, dataP->data_len / 2);

    if (!dataP->data)
        retval = false;
    else {
        size_t bytesRead;
        bytesRead = fread(dataP->data, 2, dataP->data_len / 2, ifP);

        retval = (bytesRead == dataP->data_len / 2);
    }
    return retval;
}



static bool
writeImgData(FILE *                const ofP,
                   struct srf_img_data * const dataP) {

    return
        pm_writelittlelongu(ofP, dataP->type) == 0 &&
        pm_writelittlelongu(ofP, dataP->data_len) == 0 &&
        fwrite(dataP->data, 2, dataP->data_len / 2, ofP)
        == dataP->data_len / 2;
}



static bool
checkImgData(struct srf_img_data * const dataP) {
  return dataP->type == 1;
}



static bool
readImg(FILE *           const ifP,
             bool             const verbose,
             uint32_t         const i,
             struct srf_img * const imgP) {

    if (!readImgHeader(ifP, &imgP->header))
        pm_error("short srf image %u header", i);
    if (!checkImgHeader(&imgP->header))
        pm_error("invalid srf image %u header", i);

    if (verbose)
        pm_message("reading srf 16-bit RGB %ux%u image %u",
                   imgP->header.width, imgP->header.height, i);

    if (!readImgAlpha(ifP, &imgP->alpha))
        pm_error("short srf image %u alpha mask", i);
    if (!checkImgAlpha(&imgP->alpha))
        pm_error("invalid srf image %u alpha mask", i);

    if (!readImgData(ifP, &imgP->data))
        pm_error("short srf image %u data", i);
    if (!checkImgData(&imgP->data))
        pm_error("invalid srf image %u data", i);

    return true;
}



static bool
writeImg(FILE *           const ofP,
              uint32_t         const i,
              struct srf_img * const imgP) {

    if (!checkImgHeader(&imgP->header))
        pm_error("invalid srf image %u header", i);
    if (!writeImgHeader(ofP, &imgP->header))
        pm_error("short srf image %u header", i);

    if (!checkImgAlpha(&imgP->alpha))
        pm_error("invalid srf image %u alpha mask", i);
    if (!writeImageAlpha(ofP, &imgP->alpha))
        pm_error("short srf image %u alpha mask", i);

    if (!checkImgData(&imgP->data))
        pm_error("invalid srf image %u data", i);
    if (!writeImgData(ofP, &imgP->data))
        pm_error("short srf image %u data", i);

    return true;
}



static uint8_t
csum(struct srf * const srfP,
     size_t       const padLen) {
/*----------------------------------------------------------------------------
   The sum of everything in the SRF image except the checksum byte.  The
   checksum byte is supposed to be the arithmetic opposite of this so that the
   sum of everything is zero.
-----------------------------------------------------------------------------*/
    unsigned char retval;
    unsigned int i;

    retval = csumHeader(&srfP->header);

    for (i = 0; i < srfP->header.img_cnt; ++i)
        retval += csumImg(&srfP->imgs[i]);
    
    for (i = 0; i < padLen; ++i)
        retval += 0xff;
    
    return retval;
}



void
srf_read(FILE *       const ifP,
         bool         const verbose,
         struct srf * const srfP) {

    uint8_t       trialCsum;
    size_t        padLen;
    unsigned char pad[256];
    unsigned int  i;

    if (!readHeader(ifP, &srfP->header))
        pm_error("short srf header");
    if (!checkHeader(&srfP->header))
        pm_error("invalid srf header");

    if (verbose)
        pm_message("reading srf ver %s with prod code %s and %u images",
                   srfP->header.ver.val, srfP->header.prod.val,
                   srfP->header.img_cnt);

    MALLOCARRAY(srfP->imgs, srfP->header.img_cnt);

    if (!srfP->imgs)
        pm_error("Could not allocate memory for %u images",
                 srfP->header.img_cnt);

    for (i = 0; i < srfP->header.img_cnt; ++i)
        if (!readImg(ifP, verbose, i, &srfP->imgs[i]))
            pm_error("invalid srf image %u", i);

    padLen = fread(pad, 1, sizeof(pad), ifP);
    if (!feof(ifP)) {
        pm_errormsg("excess data at end of file");
        return;
    }

    trialCsum = csum(srfP, 0);  /* initial value */
    for (i = 0; i < padLen; ++i)
        trialCsum += pad[i];
    if (trialCsum != 0)
        pm_errormsg("checksum does not match");
}



void
srf_write(FILE *       const ofP,
          struct srf * const srfP) {

    uint8_t      srfCsum;    /* checksum value in SRF image */
    size_t       padLen;
    unsigned int i;
    size_t       bytesWritten;

    padLen = 1;  /* initial value */

    if (!checkHeader(&srfP->header))
        pm_error("invalid srf header");
    if (!writeHeader(ofP, &srfP->header))
        pm_error("write srf header");
    padLen += lenHeader(&srfP->header);

    for (i = 0; i < srfP->header.img_cnt; ++i) {
        if (!writeImg(ofP, i, &srfP->imgs[i]))
            pm_error("invalid srf image %u", i);
        padLen += lenImg(&srfP->imgs[i]);
    }

    /* Pad to 256 bytes */
    padLen = 256 - (padLen % 256);
    if (padLen) {
        char * d;
        size_t bytesWritten;

        MALLOCARRAY(d, padLen);

        if (!d)
            pm_error("Could not allocate memory for %u bytes of padding",
                     (unsigned)padLen);

        memset(d, 0xff, padLen);

        bytesWritten = fwrite(d, 1, padLen, ofP);

        if (bytesWritten != padLen)
            pm_error("unable to 0xff pad file");

        free(d);
    }

    /* Write out checksum byte */
    srfCsum = 0xff - csum(srfP, padLen) + 1;
    bytesWritten = fwrite(&srfCsum, 1, 1, ofP);
    if (bytesWritten != 1)
        pm_error("unable to write checksum");
}



static void
freeImg(struct srf_img * const imgP) {

    free(imgP->alpha.data);
    free(imgP->data.data);
}



void
srf_term(struct srf * const srfP) {

    unsigned int i;
    
    free(srfP->header.s578.val);
    free(srfP->header.ver.val);
    free(srfP->header.prod.val);

    for (i = 0; i < srfP->header.img_cnt; ++i)
        freeImg(&srfP->imgs[i]);

    free(srfP->imgs);
}



static void
srf_img_init(struct srf_img * const imgP,
             uint16_t         const width,
             uint16_t         const height) {

    struct srf_img_header * const headerP = &imgP->header;
    struct srf_img_alpha *  const alphaP  = &imgP->alpha;
    struct srf_img_data *   const dataP   = &imgP->data;

    headerP->_ints[0]   = 0;
    headerP->_ints[1]   = 16;
    headerP->_ints[2]   = 0;
    headerP->height     = height;
    headerP->width      = width;
    headerP->_bytes[0]  = 16;
    headerP->_bytes[1]  = 8;
    headerP->line_len   = width * 2;
    headerP->zeros      = 0;

    alphaP->type     = 11;
    alphaP->data_len = height * width;
    MALLOCARRAY(alphaP->data, alphaP->data_len);

    if (!alphaP->data)
        pm_error("Could not allocate buffer for %u bytes of alpha",
                 alphaP->data_len);

    dataP->type     = 1;
    dataP->data_len = height * width * 2;
    MALLOCARRAY(dataP->data, dataP->data_len / 2);

    if (!dataP->data)
        pm_error("Could not allocation buffer for %u units of data",
                 dataP->data_len);
}



static void
initPstring(struct srf_pstring * const pstringP,
            const char *         const s) {

    pstringP->len = strlen(s);

    MALLOCARRAY(pstringP->val, pstringP->len + 1);

    if (!pstringP->val)
        pm_error("Could not allocate memory for string of length %u",
                 pstringP->len);

    memcpy(pstringP->val, s, pstringP->len + 1);
}



void
srf_init(struct srf * const srfP) {

    struct srf_header * const headerP = &srfP->header;

    strcpy(headerP->magic, SRF_MAGIC);
    headerP->_int4[0] = 4;
    headerP->_int4[1] = 4;
    headerP->img_cnt = 0;
    headerP->_int5 = 5;
    initPstring(&headerP->s578, "578");
    headerP->_int6 = 6;
    initPstring(&headerP->ver, "1.00");
    headerP->_int7 = 7;
    initPstring(&headerP->prod, "006-D0578-XX");

    srfP->imgs = NULL;
}



void
srf_create_img(struct srf * const srfP,
               uint16_t     const width,
               uint16_t     const height) {
/*----------------------------------------------------------------------------
   Add an "image" to the SRF.  An image is a horizontal series of 36 
   square frames, each showing a different angle view of an object, 10
   degrees about.  At least that's what it's supposed to be.  We don't
   really care -- it's just an arbitrary rectangular raster image to us.
-----------------------------------------------------------------------------*/
    
    ++srfP->header.img_cnt;

    REALLOCARRAY(srfP->imgs, srfP->header.img_cnt);

    if (!srfP->imgs)
        pm_error("Could not allocate memory for %u images",
                 srfP->header.img_cnt);

    srf_img_init(&srfP->imgs[srfP->header.img_cnt-1], width, height);
}
                 
