#include <string.h>
#include "global_variables.h"

#include "dng.h"

void 
dng_coeff (double cc[4][4],
           double cm[4][3],
           double xyz[3]) {
    static const double rgb_xyz[3][3] = {     /* RGB from XYZ */
        {  3.240479, -1.537150, -0.498535 },
        { -0.969256,  1.875992,  0.041556 },
        {  0.055648, -0.204043,  1.057311 } };
#if 0
    static const double xyz_rgb[3][3] = {     /* XYZ from RGB */
        { 0.412453, 0.357580, 0.180423 },
        { 0.212671, 0.715160, 0.072169 },
        { 0.019334, 0.119193, 0.950227 } };
#endif
    double cam_xyz[4][3], xyz_cam[3][4], invert[3][6];
    unsigned int i;

    for (i = 0; i < colors; ++i) {
        unsigned int j;
        for (j = 0; j < 3; ++j) {
            unsigned int k;
            for (k = 0, cam_xyz[i][j] = 0.0; k < colors; ++k) {
                cam_xyz[i][j] += cc[i][k] * cm[k][j] * xyz[j];
            }
        }
    }
    for (i = 0; i < colors; ++i) {
        unsigned int j;
        double camXyzSum;

        for (j = 0, camXyzSum = 0.0; j < 3; ++j)
            camXyzSum += cam_xyz[i][j];

        for (j = 0; j < 3; ++j)
            cam_xyz[i][j] /= camXyzSum;

        pre_mul[i] = 1 / camXyzSum;
    }
    for (i = 0; i < 3; ++i) {
        unsigned int j;
        for (j = 0; j < 6; ++j)
            invert[i][j] = j == i+3;
        for (j = 0; j < 3; ++j) {
            unsigned int k;
            for (k = 0; k < colors; ++k)
                invert[i][j] += cam_xyz[k][i] * cam_xyz[k][j];
        }
    }
    for (i = 0; i < 3; ++i) {
        double const num = invert[i][i];
        unsigned int j;
        unsigned int k;
        for (j = 0; j < 6; ++j)       /* Normalize row i */
            invert[i][j] /= num;
        for (k = 0; k < 3; ++k) {     /* Subtract it from other rows */
            if (k != i) {
                double const num = invert[k][i];
                unsigned int j;
                for (j = 0; j < 6; ++j)
                    invert[k][j] -= invert[i][j] * num;
            }
        }
    }

    memset(xyz_cam, 0, sizeof xyz_cam);

    for (i = 0; i < 3; ++i) {
        unsigned int j;
        for (j = 0; j < colors; ++j) {
            unsigned int k;
            for (k = 0; k < 3; ++k)
                xyz_cam[i][j] += invert[i][k+3] * cam_xyz[j][k];
        }
    }
    memset (coeff, 0, sizeof coeff);

    for (i = 0; i < 3; ++i) {
        unsigned int j;
        for (j = 0; j < colors; ++j) {
            unsigned int k;
            for (k = 0; k < 3; ++k)
                coeff[i][j] += rgb_xyz[i][k] * xyz_cam[k][j];
        }
    }
    {
        double greenSum;
        unsigned int j;
        unsigned int i;

        for (j = 0, greenSum = 0.0; j < colors; ++j)
            greenSum += coeff[1][j];

        for (i = 0; i < 3; ++i) {
            unsigned int j;
            for (j = 0; j < colors; ++j)
                coeff[i][j] /= greenSum;
        }
    }
    use_coeff = 1;
}



