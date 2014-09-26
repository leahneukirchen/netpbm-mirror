#include "pm.h"

#include "cameratopam.h"
#include "camera.h"

void 
parse_foveon(FILE * const ifp);

void  
foveon_interpolate(Image const image,
                   float coeff[3][4]);

LoadRawFn foveon_load_raw;

void  
foveon_coeff(int * const useCoeffP,
             float       coeff[3][4]);
