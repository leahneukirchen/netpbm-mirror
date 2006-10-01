#ifndef IFRAME_H_INCLUDED
#define IFRAME_H_INCLUDED

#include "frame.h"

void
SetFCode(void);

void
SetSlicesPerFrame(int const number);

void
SetBlocksPerSlice(void);

void
SetIQScale(int const qI);

int
GetIQScale(void);

void
GenIFrame(BitBucket * const bb, 
          MpegFrame * const current);

void
ResetIFrameStats(void);

float
IFrameTotalTime(void);


void
ShowIFrameSummary(unsigned int const inputFrameBits, 
                  unsigned int const totalBits, 
                  FILE *       const fpointer);

void
EncodeYDC(int32       const dc_term,
          int32 *     const pred_term,
          BitBucket * const bb);

void
EncodeCDC(int32       const dc_term,
          int32     * const pred_term,
          BitBucket * const bb);

void
BlockComputeSNR(MpegFrame * const current,
                float *     const snr,
                float *     const psnr);

void
WriteDecodedFrame(MpegFrame * const frame);

void
PrintItoIBitRate(int const numBits,
                 int const frameNum);

int32 time_elapsed(void);

/* assumes many things:
 * block indices are (y,x)
 * variables y_dc_pred, cr_dc_pred, and cb_dc_pred
 * flat block fb exists
 */
#define GEN_I_BLOCK(frameType, frame, bb, mbAI, qscale) {                   \
    boolean overflow, overflowChange=FALSE;                             \
        int overflowValue = 0;                                              \
        do {                                                                \
      overflow =  Mpost_QuantZigBlock(dct[y][x], fb[0],                 \
             qscale, TRUE)==MPOST_OVERFLOW;                     \
          overflow |= Mpost_QuantZigBlock(dct[y][x+1], fb[1],               \
                 qscale, TRUE)==MPOST_OVERFLOW;                     \
      overflow |= Mpost_QuantZigBlock(dct[y+1][x], fb[2],               \
                         qscale, TRUE)==MPOST_OVERFLOW;                     \
      overflow |= Mpost_QuantZigBlock(dct[y+1][x+1], fb[3],             \
                         qscale, TRUE)==MPOST_OVERFLOW;                     \
      overflow |= Mpost_QuantZigBlock(dctb[y >> 1][x >> 1],             \
                         fb[4], qscale, TRUE)==MPOST_OVERFLOW;              \
      overflow |= Mpost_QuantZigBlock(dctr[y >> 1][x >> 1],             \
             fb[5], qscale, TRUE)==MPOST_OVERFLOW;              \
          if ((overflow) && (qscale!=31)) {                                 \
           overflowChange = TRUE; overflowValue++;                          \
       qscale++;                                                        \
       } else overflow = FALSE;                                         \
    } while (overflow);                                                 \
        Mhead_GenMBHeader(bb,                           \
            frameType /* pict_code_type */, mbAI /* addr_incr */,   \
            qscale /* q_scale */,                               \
            0 /* forw_f_code */, 0 /* back_f_code */,           \
            0 /* horiz_forw_r */, 0 /* vert_forw_r */,          \
            0 /* horiz_back_r */, 0 /* vert_back_r */,          \
            0 /* motion_forw */, 0 /* m_horiz_forw */,          \
            0 /* m_vert_forw */, 0 /* motion_back */,           \
            0 /* m_horiz_back */, 0 /* m_vert_back */,          \
            0 /* mb_pattern */, TRUE /* mb_intra */);           \
                                        \
    /* Y blocks */                              \
        EncodeYDC(fb[0][0], &y_dc_pred, bb);                            \
    Mpost_RLEHuffIBlock(fb[0], bb);                         \
    EncodeYDC(fb[1][0], &y_dc_pred, bb);                        \
        Mpost_RLEHuffIBlock(fb[1], bb);                             \
    EncodeYDC(fb[2][0], &y_dc_pred, bb);                        \
    Mpost_RLEHuffIBlock(fb[2], bb);                         \
    EncodeYDC(fb[3][0], &y_dc_pred, bb);                        \
    Mpost_RLEHuffIBlock(fb[3], bb);                         \
                                        \
    /* CB block */                              \
    EncodeCDC(fb[4][0], &cb_dc_pred, bb);                   \
    Mpost_RLEHuffIBlock(fb[4], bb);                     \
                                        \
    /* CR block */                              \
    EncodeCDC(fb[5][0], &cr_dc_pred, bb);                   \
    Mpost_RLEHuffIBlock(fb[5], bb);                     \
    if (overflowChange) qscale -= overflowValue;                        \
    }

#endif
