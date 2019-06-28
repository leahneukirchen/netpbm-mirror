#include "pbm.h"
#include "pbmfont.h"
#include "pbmfontdata.h"

struct font2 const * pbm_builtinFonts[] = {
    &pbm_defaultFixedfont2,
    &pbm_defaultBdffont2,
    NULL,
};
