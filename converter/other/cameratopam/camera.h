#ifndef CAMERA_H_INCLUDED
#define CAMERA_H_INCLUDED

#include <stdio.h>

#include "cameratopam.h"

void 
parse_ciff(FILE * const ifp,
           int    const offset,
           int    const length);

void
parse_external_jpeg(const char * const ifname);

void
parse_tiff(FILE * const ifp, int base);

void
parse_minolta(FILE * const ifp);

void
parse_rollei(FILE * const ifp);

void
parse_mos(FILE * const ifp,
          int    const offset);

typedef void LoadRawFn(Image const image);

LoadRawFn adobe_dng_load_raw_lj;

LoadRawFn adobe_dng_load_raw_nc;

int
nikon_is_compressed(void);

LoadRawFn nikon_compressed_load_raw;

LoadRawFn nikon_e950_load_raw;

void
nikon_e950_coeff(void);

int
nikon_e990(void);

int
nikon_e2100(void);

LoadRawFn nikon_e2100_load_raw;

int
minolta_z2(void);

LoadRawFn fuji_s2_load_raw;

LoadRawFn fuji_s3_load_raw;

LoadRawFn fuji_s5000_load_raw;

LoadRawFn unpacked_load_raw;

LoadRawFn fuji_s7000_load_raw;

LoadRawFn fuji_f700_load_raw;

LoadRawFn packed_12_load_raw;

LoadRawFn eight_bit_load_raw;

LoadRawFn phase_one_load_raw;

LoadRawFn ixpress_load_raw;

LoadRawFn leaf_load_raw;

LoadRawFn olympus_e300_load_raw;

LoadRawFn olympus_cseries_load_raw;

LoadRawFn sony_load_raw;

LoadRawFn kodak_easy_load_raw;

LoadRawFn kodak_compressed_load_raw;

LoadRawFn kodak_yuv_load_raw;

void
kodak_dc20_coeff (float const juice);

LoadRawFn kodak_radc_load_raw;

LoadRawFn kodak_jpeg_load_raw;

LoadRawFn kodak_dc120_load_raw;

LoadRawFn rollei_load_raw;

LoadRawFn casio_qv5700_load_raw;

LoadRawFn nucore_load_raw;

LoadRawFn nikon_load_raw;

int
pentax_optio33(void);

#endif
