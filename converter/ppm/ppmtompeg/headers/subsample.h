#ifndef SUBSAMPLE_H_INCLUDED
#define SUBSAMPLE_H_INCLUDED

#include "frame.h"
#include "mtypes.h"

int32
LumMotionErrorA(const LumBlock * const currentBlockP,
                MpegFrame *      const prevFrame,
                int              const by,
                int              const bx,
                vector           const m,
                int32            const bestSoFar);

int32
LumMotionErrorB(const LumBlock * const currentP,
                MpegFrame *      const prevFrame,
                int              const by,
                int              const bx,
                vector           const m,
                int32            const bestSoFar);

int32
LumMotionErrorC(const LumBlock * const currentP,
                MpegFrame *      const prevFrame,
                int              const by,
                int              const bx,
                vector           const m,
                int32            const bestSoFar);

int32
LumMotionErrorD(const LumBlock * const currentP,
                MpegFrame *      const prevFrame,
                int              const by,
                int              const bx,
                vector           const m,
                int32            const bestSoFar);

#endif
