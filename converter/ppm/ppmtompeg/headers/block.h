#ifndef BLOCK_H_INCLUDED

#include "frame.h"
#include "mtypes.h"

/* DIFFERENCE FUNCTIONS */

int32
LumBlockMAD(const LumBlock * const currentBlockP,
            const LumBlock * const motionBlockP,
            int32            const bestSoFar);

int32
LumBlockMSE(const LumBlock * const currentBlockP,
            const LumBlock * const motionBlockP,
            int32            const bestSoFar);

int32
LumMotionError(const LumBlock * const currentBlockP,
               MpegFrame *      const prev,
               int              const by,
               int              const bx,
               vector           const m,
               int32            const bestSoFar);

int32
LumAddMotionError(const LumBlock * const currentBlockP,
                  const LumBlock * const blockSoFarP,
                  MpegFrame *      const prev,
                  int              const by,
                  int              const bx,
                  vector           const m,
                  int32            const bestSoFar);

int32
LumMotionErrorSubSampled(const LumBlock * const currentBlockP,
                         MpegFrame *      const prevFrame,
                         int              const by,
                         int              const bx,
                         vector           const m,
                         int              const startY,
                         int              const startX);

void
ComputeDiffDCTs(MpegFrame * const current,
                MpegFrame * const prev,
                int         const by,
                int         const bx,
                vector      const m,
                int *       const pattern);

void
ComputeDiffDCTBlock(Block           current,
                    Block           dest,
                    Block           motionBlock,
                    boolean * const significantDifferenceP);

void
ComputeMotionBlock(uint8 ** const prev,
                   int      const by,
                   int      const bx,
                   vector   const m,
                   Block *  const motionBlockP);

void
ComputeMotionLumBlock(MpegFrame * const prevFrame,
                      int         const by,
                      int         const bx,
                      vector      const m,
                      LumBlock *  const motionBlockP);

void
BlockToData(uint8 ** const data,
            Block          block,
            int      const by,
            int      const bx);

void
AddMotionBlock(Block          block,
               uint8 ** const prev,
               int      const by,
               int      const bx,
               vector   const m);

void
AddBMotionBlock(Block          block,
                uint8 ** const prev,
                uint8 ** const next,
                int      const by,
                int      const bx,
                int      const mode,
                motion   const motion);

void
BlockifyFrame(MpegFrame * const frameP);

#endif
