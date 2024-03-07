/* pgmtexture.c - calculate textural features of a PGM image
**


*/

#include <assert.h>
#include <math.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "pgm.h"


struct CmdlineInfo {
    /* All the information the user supplied in the command line,
     * in a form easy for the program to use.
     */
    const char * inputFileName;  /* Filespec of input file */
    unsigned int d;
};



static void
parseCommandLine(int argc, const char ** const argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that the file spec array we return is stored in the storage that
   was passed to us as the argv array.
-----------------------------------------------------------------------------*/
    optEntry * option_def;
    optStruct3 opt;
    unsigned int option_def_index;

    unsigned int dSpec;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0,   "d",          OPT_UINT,   &cmdlineP->d,  &dSpec,      0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = TRUE;  /* We may have parms that are negative numbers */

    pm_optParseOptions4(&argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (!dSpec)
        cmdlineP->d = 1;

    if (argc-1 < 1)
        cmdlineP->inputFileName = "-";
    else if (argc-1 == 1)
        cmdlineP->inputFileName = argv[1];
    else
        pm_error("Program takes at most 1 parameter: the file name.  "
                 "You specified %u", argc-1);
}



#define RADIX 2.0
#define EPSILON 0.000000001
#define BL  "Angle                 "

#define SWAP(a,b) do {float const y=(a);(a)=(b);(b)=y;} while (0)



static float
sign(float const x,
     float const y) {

    return y < 0 ? -fabs(x) : fabs(x);
}



static float *
vector(unsigned int const nl,
       unsigned int const nh) {
/*----------------------------------------------------------------------------
  Allocate a float vector with range [nl..nh]

  We do some seedy C here, subtracting an arbitrary integer from a pointer and
  calling the result a pointer.  It normally works because the only way we'll
  use that pointer is by adding that same integer or something greater to it.

  The point of this is not to allocate memory for vector elements that will
  never be referenced (component < nl).
-----------------------------------------------------------------------------*/
    float * v;
    unsigned int i;

    assert(nh >= nl); assert(nh <= UINT_MAX-1);

    MALLOCARRAY(v, (unsigned) (nh - nl + 1));

    if (v == NULL)
        pm_error("Unable to allocate memory for a vector.");

    for (i = 0; i < nh - nl +1; ++i)
        v[i] = 0;
    return v - nl;
}



static float **
matrix (unsigned int const nrl,
        unsigned int const nrh,
        unsigned int const ncl,
        unsigned int const nch) {
/*----------------------------------------------------------------------------
  Allocate a float matrix with range [nrl..nrh][ncl..nch]

  Return value is a pointer to array of pointers to rows
-----------------------------------------------------------------------------*/
    /* We do some seedy C here, subtracting an arbitrary integer from a
       pointer and calling the result a pointer.  It normally works because
       the only way we'll use that pointer is by adding that same integer or
       something greater to it.

       The point of this is not to allocate memory for matrix elements that
       will never be referenced (row < nrl or column < ncl).
    */

    unsigned int i;
    float ** matrix;  /* What we are creating */

    assert(nrh >= nrl); assert(nrh <= UINT_MAX-1);

    /* allocate pointers to rows */
    MALLOCARRAY(matrix, (unsigned) (nrh - nrl + 1));
    if (matrix == NULL)
        pm_error("Unable to allocate memory for a matrix.");

    matrix -= ncl;

    assert (nch >= ncl); assert(nch <= UINT_MAX-1);

    /* allocate rows and set pointers to them */
    for (i = nrl; i <= nrh; ++i) {
        MALLOCARRAY(matrix[i], (unsigned) (nch - ncl + 1));
        if (!matrix[i])
            pm_error("Unable to allocate memory for a matrix row.");
        matrix[i] -= ncl;
    }

    return matrix;
}



static void
printHeader() {

    fprintf(stdout,
            "%-22.22s %10.10s %10.10s %10.10s %10.10s %10.10s\n",
            "Angle", "0", "45", "90", "135", "Avg");
}



static void
printResults(const char *  const name,
             const float * const a) {

    unsigned int i;

    fprintf(stdout, "%-22.22s ", name);

    for (i = 0; i < 4; ++i)
        fprintf(stdout, "% 1.3e ", a[i]);

    fprintf(stdout, "% 1.3e\n", (a[0] + a[1] + a[2] + a[3]) / 4);
}



static void
makeGrayToneSpatialDependenceMatrix(gray **        const grays,
                                    unsigned int   const rows,
                                    unsigned int   const cols,
                                    unsigned int   const d,
                                    unsigned int * const tone,
                                    unsigned int   const toneCt,
                                    float *** const pmatrix0P,
                                    float *** const pmatrix45P,
                                    float *** const pmatrix90P,
                                    float *** const pmatrix135P) {

    float ** pmatrix0, ** pmatrix45, ** pmatrix90, ** pmatrix135;
    unsigned int row;

    pm_message("Computing spatial dependence matrix...");

    /* Allocate memory */
    pmatrix0   = matrix(0, toneCt, 0, toneCt);
    pmatrix45  = matrix(0, toneCt, 0, toneCt);
    pmatrix90  = matrix(0, toneCt, 0, toneCt);
    pmatrix135 = matrix(0, toneCt, 0, toneCt);

    for (row = 0; row < toneCt; ++row) {
        unsigned int col;
        for (col = 0; col < toneCt; ++col) {
            pmatrix0 [row][col] = pmatrix45 [row][col] = 0;
            pmatrix90[row][col] = pmatrix135[row][col] = 0;
        }
    }
    for (row = 0; row < rows; ++row) {
        unsigned int col;
        for (col = 0; col < cols; ++col) {
            unsigned int angle;
            unsigned int x;
            for (angle = 0, x = 0; angle <= 135; angle += 45) {
                while (tone[x] != grays[row][col])
                    ++x;
                if (angle == 0 && col + d < cols) {
                    unsigned int y;
                    y = 0;
                    while (tone[y] != grays[row][col + d])
                        ++y;
                    ++pmatrix0[x][y];
                    ++pmatrix0[y][x];
                }
                if (angle == 90 && row + d < rows) {
                    unsigned int y;
                    y = 0;
                    while (tone[y] != grays[row + d][col])
                        ++y;
                    ++pmatrix90[x][y];
                    ++pmatrix90[y][x];
                }
                if (angle == 45 && row + d < rows && col >= d) {
                    unsigned int y;
                    y = 0;
                    while (tone[y] != grays[row + d][col - d])
                        ++y;
                    ++pmatrix45[x][y];
                    ++pmatrix45[y][x];
                }
                if (angle == 135 && row + d < rows && col + d < cols) {
                    unsigned int y;
                    y = 0;
                    while (tone[y] != grays[row + d][col + d])
                        ++y;
                    ++pmatrix135[x][y];
                    ++pmatrix135[y][x];
                }
            }
        }
    }
    /* Gray-tone spatial dependence matrices are complete */

    {
    /* Find normalizing constants */
    unsigned int const r0  = 2 * rows * (cols - d);
    unsigned int const r45 = 2 * (rows - d) * (cols - d);
    unsigned int const r90 = 2 * (rows - d) * cols;

    unsigned int i;

    /* Normalize gray-tone spatial dependence matrix */
    for (i = 0; i < toneCt; ++i) {
        unsigned int j;
        for (j = 0; j < toneCt; ++j) {
            pmatrix0  [i][j] /= r0;
            pmatrix45 [i][j] /= r45;
            pmatrix90 [i][j] /= r90;
            pmatrix135[i][j] /= r45;
        }
    }
    }
    pm_message(" ...done.");

    *pmatrix0P   = pmatrix0;
    *pmatrix45P  = pmatrix45;
    *pmatrix90P  = pmatrix90;
    *pmatrix135P = pmatrix135;
}



static void
mkbalanced (float **     const a,
            unsigned int const n) {

    float const sqrdx = SQR(RADIX);

    unsigned int last, i;
    float s, r, g, f, c;

    last = 0;
    while (last == 0) {
        last = 1;
        for (i = 1; i <= n; ++i) {
            unsigned int j;
            r = c = 0.0;
            for (j = 1; j <= n; ++j) {
                if (j != i) {
                    c += fabs (a[j][i]);
                    r += fabs (a[i][j]);
                }
            }
            if (c && r) {
                g = r / RADIX;
                f = 1.0;
                s = c + r;
                while (c < g) {
                    f *= RADIX;
                    c *= sqrdx;
                }
                g = r * RADIX;
                while (c > g) {
                    f /= RADIX;
                    c /= sqrdx;
                }
                if ((c + r) / f < 0.95 * s) {
                    unsigned int j;
                    last = 0;
                    g = 1.0 / f;
                    for (j = 1; j <= n; ++j)
                        a[i][j] *= g;
                    for (j = 1; j <= n; ++j)
                        a[j][i] *= f;
                }
            }
        }
    }
}



static void
reduction(float **     const a,
          unsigned int const n) {

    unsigned int m;

    for (m = 2; m < n; ++m) {
        unsigned int j;
        unsigned int i;
        float x;
        x = 0.0;
        i = m;
        for (j = m; j <= n; ++j) {
            if (fabs(a[j][m - 1]) > fabs(x)) {
                x = a[j][m - 1];
                i = j;
            }
        }
        if (i != m) {
            for (j = m - 1; j <= n; ++j)
                SWAP(a[i][j], a[m][j]);
            for (j = 1; j <= n; j++)
                SWAP(a[j][i], a[j][m]);
            a[j][i] = a[j][i];
        }
        if (x != 0.0) {
            unsigned int i;
            for (i = m + 1; i <= n; ++i) {
                float y;
                y = a[i][m - 1];
                if (y) {
                    y /= x;
                    a[i][m - 1] = y;
                    for (j = m; j <= n; ++j)
                        a[i][j] -= y * a[m][j];
                    for (j = 1; j <= n; ++j)
                        a[j][m] += y * a[j][i];
                }
            }
        }
    }
}



static float
norm(float **     const a,
     unsigned int const n) {

    float anorm;
    unsigned int i;

    for (i = 2, anorm = fabs(a[1][1]); i <= n; ++i) {
        unsigned int j;
        for (j = (i - 1); j <= n; ++j)
            anorm += fabs(a[i][j]);
    }
    return anorm;
}



static void
hessenberg(float **     const a,
           unsigned int const n,
           float *      const wr,
           float *      const wi) {

    float const anorm = norm(a, n);

    int nn;
    float t;

    assert(n >= 1);

    for (nn = n, t = 0.0; nn >= 1; ) {
        unsigned int its;
        int l;
        its = 0;
        do {
            float x;
            for (l = nn; l >= 2; --l) {
                float s;
                s = fabs (a[l - 1][l - 1]) + fabs (a[l][l]);
                if (s == 0.0)
                    s = anorm;
                if ((float) (fabs (a[l][l - 1]) + s) == s)
                    break;
            }
            assert(nn >= 1);
            x = a[nn][nn];
            if (l == nn) {
                wr[nn] = x + t;
                wi[nn--] = 0.0;
            } else {
                float w, y;
                y = a[nn - 1][nn - 1];  /* initial value */
                w = a[nn][nn - 1] * a[nn - 1][nn];  /* initial value */
                if (l == (nn - 1)) {
                    float const p = 0.5 * (y - x);
                    float const q = p * p + w;
                    float const z = sqrt(fabs(q));
                    x += t;
                    if (q >= 0.0) {
                        float const z2 = p + sign(z, p);
                        wr[nn - 1] = wr[nn] = x + z2;
                        if (z2)
                            wr[nn] = x - w / z2;
                        wi[nn - 1] = wi[nn] = 0.0;
                    } else {
                        wr[nn - 1] = wr[nn] = x + p;
                        wi[nn - 1] = -(wi[nn] = z);
                    }
                    nn -= 2;
                } else {
                    int i, k, m;
                    float p, q, r;
                    if (its == 30)
                        pm_error("Too many iterations to required "
                                 "to find max correlation coefficient");
                    if (its == 10 || its == 20) {
                        int i;
                        float s;
                        t += x;
                        for (i = 1; i <= nn; ++i)
                            a[i][i] -= x;
                        s = fabs(a[nn][nn - 1]) + fabs(a[nn - 1][nn - 2]);
                        y = x = 0.75 * s;
                        w = -0.4375 * s * s;
                    }
                    ++its;
                    for (m = (nn - 2); m >= l; --m) {
                        float const z = a[m][m];
                        float s, u, v;
                        r = x - z;
                        s = y - z;
                        p = (r * s - w) / a[m + 1][m] + a[m][m + 1];
                        q = a[m + 1][m + 1] - z - r - s;
                        r = a[m + 2][m + 1];
                        s = fabs(p) + fabs(q) + fabs(r);
                        p /= s;
                        q /= s;
                        r /= s;
                        if (m == l)
                            break;
                        u = fabs(a[m][m - 1]) * (fabs(q) + fabs(r));
                        v = fabs(p) * (fabs(a[m - 1][m - 1]) + fabs(z) +
                                       fabs(a[m + 1][m + 1]));
                        if (u + v == v)
                            break;
                    }
                    for (i = m + 2; i <= nn; ++i) {
                        a[i][i - 2] = 0.0;
                        if (i != (m + 2))
                            a[i][i - 3] = 0.0;
                    }
                    for (k = m; k <= nn - 1; ++k) {
                        float s;
                        if (k != m) {
                            p = a[k][k - 1];
                            q = a[k + 1][k - 1];
                            r = 0.0;
                            if (k != (nn - 1))
                                r = a[k + 2][k - 1];
                            if ((x = fabs(p) + fabs(q) + fabs(r))) {
                                p /= x;
                                q /= x;
                                r /= x;
                            }
                        }
                        s = sign(sqrt(SQR(p) + SQR(q) + SQR(r)), p);
                        if (s) {
                            int const mmin = nn < k + 3 ? nn : k + 3;
                            float z;
                            int j;
                            if (k == m) {
                                if (l != m)
                                    a[k][k - 1] = -a[k][k - 1];
                            } else
                                a[k][k - 1] = -s * x;
                            p += s;
                            x = p / s;
                            y = q / s;
                            z = r / s;
                            q /= p;
                            r /= p;
                            for (j = k; j <= nn; ++j) {
                                p = a[k][j] + q * a[k + 1][j];
                                if (k != (nn - 1)) {
                                    p += r * a[k + 2][j];
                                    a[k + 2][j] -= p * z;
                                }
                                a[k + 1][j] -= p * y;
                                a[k][j] -= p * x;
                            }
                            for (i = l; i <= mmin; ++i) {
                                p = x * a[i][k] + y * a[i][k + 1];
                                if (k != (nn - 1)) {
                                    p += z * a[i][k + 2];
                                    a[i][k + 2] -= p * r;
                                }
                                a[i][k + 1] -= p * q;
                                a[i][k] -= p;
                            }
                        }
                    }
                }
            }
        } while (l < nn - 1);
    }
}



static float
f1_a2m(float **     const p,
       unsigned int const ng) {
/*----------------------------------------------------------------------------
  Angular Second Moment

  The angular second-moment feature (ASM) f1 is a measure of homogeneity of
  the image. In a homogeneous image, there are very few dominant gray-tone
  transitions. Hence the P matrix for such an image will have fewer entries of
  large magnitude.
-----------------------------------------------------------------------------*/
    unsigned int i;
    float sum;

    for (i = 0, sum = 0.0; i < ng; ++i) {
        unsigned int j;
        for (j = 0; j < ng; ++j)
            sum += p[i][j] * p[i][j];
    }
    return sum;
}



static float
f2_contrast(float **     const p,
            unsigned int const ng) {
/*----------------------------------------------------------------------------
   Contrast

   The contrast feature is a difference moment of the P matrix and is a
   measure of the contrast or the amount of local variations present in an
   image.
-----------------------------------------------------------------------------*/
    unsigned int n;
    float bigsum;

    for (n = 0, bigsum = 0.0; n < ng; ++n) {
        unsigned int i;
        float sum;
        for (i = 0, sum = 0.0; i < ng; ++i) {
            unsigned int j;
            for (j = 0; j < ng; ++j) {
                if ((i - j) == n || (j - i) == n)
                    sum += p[i][j];
            }
        }
        bigsum += SQR(n) * sum;
    }
    return bigsum;
}



static float
f3_corr(float **     const p,
        unsigned int const ng) {
/*----------------------------------------------------------------------------
   Correlation

   This correlation feature is a measure of gray-tone linear-dependencies in
   the image.
-----------------------------------------------------------------------------*/
    unsigned int i;
    float sumSqrx;
    float tmp;
    float * px;
    float meanx, meany, stddevx, stddevy;

    sumSqrx = 0.0;
    meanx = 0.0; meany = 0.0;

    px = vector(0, ng);
    for (i = 0; i < ng; ++i)
        px[i] = 0;

    /* px[i] is the (i-1)th entry in the marginal probability matrix obtained
       by summing the rows of p[i][j]
    */
    for (i = 0; i < ng; ++i) {
        unsigned int j;
        for (j = 0; j < ng; ++j)
            px[i] += p[i][j];
    }

    /* Now calculate the means and standard deviations of px and py */
    for (i = 0; i < ng; ++i) {
        meanx += px[i] * i;
        sumSqrx += px[i] * SQR(i);
    }

    meany = meanx;
    stddevx = sqrt(sumSqrx - (SQR(meanx)));
    stddevy = stddevx;

    /* Finally, the correlation ... */
    for (i = 0, tmp = 0; i < ng; ++i) {
        unsigned int j;
        for (j = 0; j < ng; ++j)
            tmp += i * j * p[i][j];
    }
    return (tmp - meanx * meany) / (stddevx * stddevy);
}



static float
f4_var (float **     const p,
        unsigned int const ng) {
/*----------------------------------------------------------------------------
  Sum of Squares: Variance
-----------------------------------------------------------------------------*/
    unsigned int i;
    float mean, var;

    for (i = 0, mean = 0.0; i < ng; ++i) {
        unsigned int j;
        for (j = 0; j < ng; ++j)
            mean += i * p[i][j];
    }
    for (i = 0, var = 0.0; i < ng; ++i) {
        unsigned int j;
        for (j = 0; j < ng; ++j)
            var += (i + 1 - mean) * (i + 1 - mean) * p[i][j];
    }
    return var;
}



static float
f5_idm (float **     const p,
        unsigned int const ng) {
/*----------------------------------------------------------------------------
  Inverse Difference Moment
-----------------------------------------------------------------------------*/
    unsigned int i;
    float idm;

    for (i = 0, idm = 0.0; i < ng; ++i) {
        unsigned int j;
        for (j = 0; j < ng; ++j)
            idm += p[i][j] / (1 + (i - j) * (i - j));
    }
    return idm;
}


static double *
newPxpy2(unsigned int const ng) {

    double * pxpy;

    if (ng > UINT_MAX-1)
        pm_error("Too many gray levels (%u) to do computations", ng);

    MALLOCARRAY(pxpy, ng+1);

    if (!pxpy)
        pm_error("Unable to allocate %u entries for the pxpy table",
                 ng+1);

    return pxpy;
}



static float *
newPxpy(unsigned int const ng) {

    float * pxpy;

    if (ng > (UINT_MAX-1)/2 -1)
        pm_error("Too many gray levels (%u) to do computations", ng);

    MALLOCARRAY(pxpy, 2 * (ng+1) + 1);

    if (!pxpy)
        pm_error("Unable to allocate %u entries for the pxpy table",
                 2* (ng+1) + 1);

    return pxpy;
}



static float
f6_savg (float **     const p,
         unsigned int const ng) {
/*----------------------------------------------------------------------------
   Sum Average
-----------------------------------------------------------------------------*/
    float * const pxpy = newPxpy(ng);

    unsigned int i;
    float savg;

    for (i = 0; i <= 2 * ng; ++i)
        pxpy[i] = 0.0;

    for (i = 0; i < ng; ++i) {
        unsigned int j;
        for (j = 0; j < ng; ++j)
            pxpy[i + j + 2] += p[i][j];
    }
    for (i = 2, savg = 0.0; i <= 2 * ng; ++i)
        savg += i * pxpy[i];

    free(pxpy);

    return savg;
}



static float
f7_svar (float **     const p,
         unsigned int const ng,
         float        const s) {
/*----------------------------------------------------------------------------
   Sum Variance
-----------------------------------------------------------------------------*/
    float * const pxpy = newPxpy(ng);

    unsigned int i;
    float var;

    for (i = 0; i <= 2 * ng; ++i)
        pxpy[i] = 0;

    for (i = 0; i < ng; ++i) {
        unsigned int j;
        for (j = 0; j < ng; ++j)
            pxpy[i + j + 2] += p[i][j];
    }
    for (i = 2, var = 0.0; i <= 2 * ng; ++i)
        var += (i - s) * (i - s) * pxpy[i];

    free(pxpy);

    return var;
}



static float
f8_sentropy (float **     const p,
             unsigned int const ng) {
/*----------------------------------------------------------------------------
   Sum Entropy
-----------------------------------------------------------------------------*/
    float * const pxpy = newPxpy(ng);

    unsigned int i;
    float sentropy;

    for (i = 0; i <= 2 * ng; ++i)
        pxpy[i] = 0;

    for (i = 0; i < ng; ++i) {
        unsigned int j;
        for (j = 0; j < ng; ++j)
            pxpy[i + j + 2] += p[i][j];
    }
    for (i = 2, sentropy = 0.0; i <= 2 * ng; ++i)
        sentropy -= pxpy[i] * log10(pxpy[i] + EPSILON);

    free(pxpy);

    return sentropy;
}



static float
f9_entropy (float **     const p,
            unsigned int const ng) {
/*----------------------------------------------------------------------------
   Entropy
-----------------------------------------------------------------------------*/
    unsigned int i;
    float entropy;

    for (i = 0, entropy = 0.0; i < ng; ++i) {
        unsigned int j;
        for (j = 0; j < ng; ++j)
            entropy += p[i][j] * log10(p[i][j] + EPSILON);
    }
    return -entropy;
}



static float
f10_dvar(float **     const p,
         unsigned int const ng) {
/*----------------------------------------------------------------------------
   Difference Variance
-----------------------------------------------------------------------------*/
    double * const pxpy = newPxpy2(ng);

    unsigned int i;
    double sqrNg;  /* Square of 'ng' */
    double sum;
    double sumSqr;
    double var;

    for (i = 0; i < ng; ++i)
        pxpy[i] = 0;

    for (i = 0; i < ng; ++i) {
        unsigned int j;
        for (j = 0; j < ng; ++j)
            pxpy[abs((int)i - (int)j)] += p[i][j];
    }
    /* Now calculate the variance of Pxpy (Px-y) */
    for (i = 0, sum = 0.0, sumSqr = 0.0; i < ng; ++i) {
        sum += pxpy[i];
        sumSqr += SQR(pxpy[i]);
    }
    sqrNg = SQR(ng);
    var = (sqrNg * sumSqr - SQR(sum)) / SQR(sqrNg);

    free(pxpy);

    return var;
}



static float
f11_dentropy (float **     const p,
              unsigned int const ng) {
/*----------------------------------------------------------------------------
   Difference Entropy
-----------------------------------------------------------------------------*/
    float * const pxpy = newPxpy(ng);

    unsigned int i;
    float sum;

    for (i = 0; i <= 2 * ng; ++i)
        pxpy[i] = 0;

    for (i = 0; i < ng; ++i) {
        unsigned int j;
        for (j = 0; j < ng; ++j)
            pxpy[abs((int)i - (int)j)] += p[i][j];
    }
    for (i = 0, sum = 0.0; i < ng; ++i)
        sum += pxpy[i] * log10(pxpy[i] + EPSILON);

    free(pxpy);

    return -sum;
}



static float
f12_icorr (float **     const p,
           unsigned int const ng) {
/*----------------------------------------------------------------------------
  Information Measures of Correlation
-----------------------------------------------------------------------------*/
    unsigned int i;
    float * px;
    float * py;
    float hx, hy, hxy, hxy1, hxy2;

    px = vector(0, ng);
    py = vector(0, ng);

    /*
     * px[i] is the (i-1)th entry in the marginal probability matrix obtained
     * by summing the rows of p[i][j]
     */
    for (i = 0; i < ng; ++i) {
        unsigned int j;
        for (j = 0; j < ng; ++j) {
            px[i] += p[i][j];
            py[j] += p[i][j];
        }
    }

    hx = 0.0; hy = 0.0; hxy = 0.0; hxy1 = 0.0; hxy2 = 0.0;

    for (i = 0; i < ng; ++i) {
        unsigned int j;
        for (j = 0; j < ng; ++j) {
            hxy1 -= p[i][j] * log10(px[i] * py[j] + EPSILON);
            hxy2 -= px[i] * py[j] * log10(px[i] * py[j] + EPSILON);
            hxy  -= p[i][j] * log10 (p[i][j] + EPSILON);
        }
    }
    /* Calculate entropies of px and py - is this right? */
    for (i = 0; i < ng; ++i) {
        hx -= px[i] * log10(px[i] + EPSILON);
        hy -= py[i] * log10(py[i] + EPSILON);
    }
    return (hxy - hxy1) / (hx > hy ? hx : hy);
}



static float
f13_icorr (float **     const p,
           unsigned int const ng) {
/*----------------------------------------------------------------------------
  Information Measures of Correlation
-----------------------------------------------------------------------------*/
    unsigned int i;
    float * px;
    float * py;
    float hx, hy, hxy, hxy1, hxy2;

    px = vector(0, ng);
    py = vector(0, ng);

    /*
     * px[i] is the (i-1)th entry in the marginal probability matrix obtained
     * by summing the rows of p[i][j]
     */
    for (i = 0; i < ng; ++i) {
        unsigned int j;
        for (j = 0; j < ng; ++j) {
            px[i] += p[i][j];
            py[j] += p[i][j];
        }
    }

    hx = 0.0; hy = 0.0; hxy = 0.0; hxy1 = 0.0; hxy2 = 0.0;

    for (i = 0; i < ng; ++i) {
        unsigned int j;
        for (j = 0; j < ng; ++j) {
            hxy1 -= p[i][j] * log10(px[i] * py[j] + EPSILON);
            hxy2 -= px[i] * py[j] * log10(px[i] * py[j] + EPSILON);
            hxy  -= p[i][j] * log10(p[i][j] + EPSILON);
        }
    }
    /* Calculate entropies of px and py */
    for (i = 0; i < ng; ++i) {
        hx -= px[i] * log10 (px[i] + EPSILON);
        hy -= py[i] * log10 (py[i] + EPSILON);
    }
    return sqrt(fabs(1 - exp (-2.0 * (hxy2 - hxy))));
}



static float
f14_maxcorr (float **     const p,
             unsigned int const ng) {
/*----------------------------------------------------------------------------
  The Maximal Correlation Coefficient
-----------------------------------------------------------------------------*/
    unsigned int i;
    float *px, *py;
    float ** q;
    float * x;
    float * iy;
    float tmp;

    px = vector(0, ng);
    py = vector(0, ng);
    q = matrix(1, ng + 1, 1, ng + 1);
    x = vector(1, ng);
    iy = vector(1, ng);

    /*
     * px[i] is the (i-1)th entry in the marginal probability matrix obtained
     * by summing the rows of p[i][j]
     */
    for (i = 0; i < ng; ++i) {
        unsigned int j;
        for (j = 0; j < ng; ++j) {
            px[i] += p[i][j];
            py[j] += p[i][j];
        }
    }

    /* Compute the Q matrix */
    for (i = 0; i < ng; ++i) {
        unsigned int j;
        for (j = 0; j < ng; ++j) {
            unsigned int k;
            q[i + 1][j + 1] = 0;
            for (k = 0; k < ng; ++k)
                q[i + 1][j + 1] += p[i][k] * p[j][k] / px[i] / py[k];
        }
    }

    /* Balance the matrix */
    mkbalanced(q, ng);
    /* Reduction to Hessenberg Form */
    reduction(q, ng);
    /* Finding eigenvalue for nonsymetric matrix using QR algorithm */
    hessenberg(q, ng, x, iy);

    /* Return the sqrt of the second largest eigenvalue of q */
    for (i = 2, tmp = x[1]; i <= ng; ++i)
        tmp = (tmp > x[i]) ? tmp : x[i];

    return sqrt(x[ng - 1]);
}



static void
printAngularSecondMom(float **     const pmatrix0,
                      float **     const pmatrix45,
                      float **     const pmatrix90,
                      float **     const pmatrix135,
                      unsigned int const toneCt) {

    float res[4];

    res[0] = f1_a2m(pmatrix0,   toneCt);
    res[1] = f1_a2m(pmatrix45,  toneCt);
    res[2] = f1_a2m(pmatrix90,  toneCt);
    res[3] = f1_a2m(pmatrix135, toneCt);

    printResults("Angular Second Moment", res);
}



static void
printContrast(float **     const pmatrix0,
              float **     const pmatrix45,
              float **     const pmatrix90,
              float **     const pmatrix135,
              unsigned int const toneCt) {

    float res[4];

    res[0] = f2_contrast(pmatrix0,   toneCt);
    res[1] = f2_contrast(pmatrix45,  toneCt);
    res[2] = f2_contrast(pmatrix90,  toneCt);
    res[3] = f2_contrast(pmatrix135, toneCt);

    printResults("Contrast", res);
}



static void
printCorrelation(float **     const pmatrix0,
                 float **     const pmatrix45,
                 float **     const pmatrix90,
                 float **     const pmatrix135,
                 unsigned int const toneCt) {

    float res[4];

    res[0] = f3_corr(pmatrix0,   toneCt);
    res[1] = f3_corr(pmatrix45,  toneCt);
    res[2] = f3_corr(pmatrix90,  toneCt);
    res[3] = f3_corr(pmatrix135, toneCt);

    printResults("Correlation", res);
}



static void
printVariance(float **     const pmatrix0,
              float **     const pmatrix45,
              float **     const pmatrix90,
              float **     const pmatrix135,
              unsigned int const toneCt) {

    float res[4];

    res[0] = f4_var(pmatrix0,   toneCt);
    res[1] = f4_var(pmatrix45,  toneCt);
    res[2] = f4_var(pmatrix90,  toneCt);
    res[3] = f4_var(pmatrix135, toneCt);

    printResults("Variance", res);
}



static void
printInverseDiffMoment(float **     const pmatrix0,
                       float **     const pmatrix45,
                       float **     const pmatrix90,
                       float **     const pmatrix135,
                       unsigned int const toneCt) {

    float res[4];

    res[0] = f5_idm(pmatrix0,   toneCt);
    res[1] = f5_idm(pmatrix45,  toneCt);
    res[2] = f5_idm(pmatrix90,  toneCt);
    res[3] = f5_idm(pmatrix135, toneCt);

    printResults("Inverse Diff Moment", res);
}



static void
printSumAverage(float **     const pmatrix0,
                float **     const pmatrix45,
                float **     const pmatrix90,
                float **     const pmatrix135,
                unsigned int const toneCt) {

    float res[4];

    res[0] = f6_savg(pmatrix0,  toneCt);
    res[1] = f6_savg(pmatrix45,  toneCt);
    res[2] = f6_savg(pmatrix90,  toneCt);
    res[3] = f6_savg(pmatrix135, toneCt);

    printResults("Sum Average", res);
}



static void
printSumVariance(float **     const pmatrix0,
                 float **     const pmatrix45,
                 float **     const pmatrix90,
                 float **     const pmatrix135,
                 unsigned int const toneCt) {

    float res[4];
    float savg[4];

    savg[0] = f6_savg(pmatrix0,   toneCt);
    savg[1] = f6_savg(pmatrix45,  toneCt);
    savg[2] = f6_savg(pmatrix90,  toneCt);
    savg[3] = f6_savg(pmatrix135, toneCt);

    res[0] = f7_svar(pmatrix0,   toneCt, savg[0]);
    res[1] = f7_svar(pmatrix45,  toneCt, savg[1]);
    res[2] = f7_svar(pmatrix90,  toneCt, savg[2]);
    res[3] = f7_svar(pmatrix135, toneCt, savg[3]);

    printResults("Sum Variance", res);
}



static void
printSumVarianceEnt(float **     const pmatrix0,
                    float **     const pmatrix45,
                    float **     const pmatrix90,
                    float **     const pmatrix135,
                    unsigned int const toneCt) {

    float res[4];

    res[0] = f8_sentropy(pmatrix0,   toneCt);
    res[1] = f8_sentropy(pmatrix45,  toneCt);
    res[2] = f8_sentropy(pmatrix90,  toneCt);
    res[3] = f8_sentropy(pmatrix135, toneCt);

    printResults("Sum Entropy", res);
}



static void
printEntropy(float **     const pmatrix0,
             float **     const pmatrix45,
             float **     const pmatrix90,
             float **     const pmatrix135,
             unsigned int const toneCt) {

    float res[4];


    res[0] = f9_entropy(pmatrix0,   toneCt);
    res[1] = f9_entropy(pmatrix45,  toneCt);
    res[2] = f9_entropy(pmatrix90,  toneCt);
    res[3] = f9_entropy(pmatrix135, toneCt);

    printResults("Entropy", res);
}



static void
printDiffVariance(float **     const pmatrix0,
                  float **     const pmatrix45,
                  float **     const pmatrix90,
                  float **     const pmatrix135,
                  unsigned int const toneCt) {

    float res[4];

    res[0] = f10_dvar(pmatrix0,   toneCt);
    res[1] = f10_dvar(pmatrix45,  toneCt);
    res[2] = f10_dvar(pmatrix90,  toneCt);
    res[3] = f10_dvar(pmatrix135, toneCt);

    printResults("Difference Variance", res);
}



static void
printDiffEntropy(float **     const pmatrix0,
                 float **     const pmatrix45,
                 float **     const pmatrix90,
                 float **     const pmatrix135,
                 unsigned int const toneCt) {

    float res[4];

    res[0] = f11_dentropy(pmatrix0,   toneCt);
    res[1] = f11_dentropy(pmatrix45,  toneCt);
    res[2] = f11_dentropy(pmatrix90,  toneCt);
    res[3] = f11_dentropy(pmatrix135, toneCt);

    printResults ("Difference Entropy", res);
}



static void
printCorrelation1(float **     const pmatrix0,
                  float **     const pmatrix45,
                  float **     const pmatrix90,
                  float **     const pmatrix135,
                  unsigned int const toneCt) {

    float res[4];

    res[0] = f12_icorr(pmatrix0,   toneCt);
    res[1] = f12_icorr(pmatrix45,  toneCt);
    res[2] = f12_icorr(pmatrix90,  toneCt);
    res[3] = f12_icorr(pmatrix135, toneCt);

    printResults("Meas of Correlation-1", res);
}



static void
printCorrelation2(float **     const pmatrix0,
                  float **     const pmatrix45,
                  float **     const pmatrix90,
                  float **     const pmatrix135,
                  unsigned int const toneCt) {

    float res[4];

    res[0] = f13_icorr(pmatrix0,   toneCt);
    res[1] = f13_icorr(pmatrix45,  toneCt);
    res[2] = f13_icorr(pmatrix90,  toneCt);
    res[3] = f13_icorr(pmatrix135, toneCt);

    printResults("Meas of Correlation2", res);
}



static void
printCorrelationMax(float **     const pmatrix0,
                    float **     const pmatrix45,
                    float **     const pmatrix90,
                    float **     const pmatrix135,
                    unsigned int const toneCt) {

    float res[4];

    res[0] = f14_maxcorr(pmatrix0,   toneCt);
    res[1] = f14_maxcorr(pmatrix45,  toneCt);
    res[2] = f14_maxcorr(pmatrix90,  toneCt);
    res[3] = f14_maxcorr(pmatrix135, toneCt);

    printResults("Max Correlation Coeff", res);
}



int
main (int argc, const char ** argv) {

    struct CmdlineInfo cmdline;
    FILE * ifP;
    gray ** grays;
    unsigned int * tone;  /* malloced array */
    unsigned int toneCt;
    unsigned int row;
    int rows, cols;
    unsigned int itone;
    float ** pmatrix0, ** pmatrix45, ** pmatrix90, ** pmatrix135;
    gray maxval;
    unsigned int i;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    grays = pgm_readpgm(ifP, &cols, &rows, &maxval);

    MALLOCARRAY(tone, maxval+1);

    /* Determine the number of different gray scales (not maxval) */
    for (i = 0; i <= maxval; ++i)
        tone[i] = -1;
    for (row = 0; row < rows; ++row) {
        unsigned int col;
        for (col = 0; col < cols; ++col)
            tone[grays[row][col]] = grays[row][col];
    }
    for (i = 0, toneCt = 0; i <= maxval; ++i) {
        if (tone[i] != -1)
            ++toneCt;
    }
    pm_message("(Image has %u gray levels.)", toneCt);

    /* Collapse array, taking out all zero values */
    for (row = 0, itone = 0; row <= maxval; ++row)
        if (tone[row] != -1)
            tone[itone++] = tone[row];
    /* Now array contains only the gray levels present (in ascending order) */

    if (cmdline.d > cols)
        pm_error("Image is narrower (%u columns) "
                 "than specified distance (%u)", cols, cmdline.d);

    makeGrayToneSpatialDependenceMatrix(
        grays, rows, cols, cmdline.d, tone, toneCt,
        &pmatrix0, &pmatrix45, &pmatrix90, &pmatrix135);

    pm_message("Computing textural features ...");

    fprintf(stdout, "\n");

    printHeader();

    printAngularSecondMom (pmatrix0, pmatrix45, pmatrix90, pmatrix135, toneCt);

    printContrast         (pmatrix0, pmatrix45, pmatrix90, pmatrix135, toneCt);

    printCorrelation      (pmatrix0, pmatrix45, pmatrix90, pmatrix135, toneCt);

    printVariance         (pmatrix0, pmatrix45, pmatrix90, pmatrix135, toneCt);

    printInverseDiffMoment(pmatrix0, pmatrix45, pmatrix90, pmatrix135, toneCt);

    printSumAverage       (pmatrix0, pmatrix45, pmatrix90, pmatrix135, toneCt);

    printSumVariance      (pmatrix0, pmatrix45, pmatrix90, pmatrix135, toneCt);

    printSumVarianceEnt   (pmatrix0, pmatrix45, pmatrix90, pmatrix135, toneCt);

    printEntropy          (pmatrix0, pmatrix45, pmatrix90, pmatrix135, toneCt);

    printDiffVariance     (pmatrix0, pmatrix45, pmatrix90, pmatrix135, toneCt);

    printDiffEntropy      (pmatrix0, pmatrix45, pmatrix90, pmatrix135, toneCt);

    printCorrelation1     (pmatrix0, pmatrix45, pmatrix90, pmatrix135, toneCt);

    printCorrelation2     (pmatrix0, pmatrix45, pmatrix90, pmatrix135, toneCt);

    printCorrelationMax   (pmatrix0, pmatrix45, pmatrix90, pmatrix135, toneCt);

    pm_message(" ...done.");

    pm_close (ifP);

    return 0;
}


/*
** Author: James Darrell McCauley
**         Texas Agricultural Experiment Station
**         Department of Agricultural Engineering
**         Texas A&M University
**         College Station, Texas 77843-2117 USA
**
** Code written partially taken from pgmtofs.c in the PBMPLUS package
** by Jef Poskanzer.
**
** Algorithms for calculating features (and some explanatory comments) are
** taken from:
**
**   Haralick, R.M., K. Shanmugam, and I. Dinstein. 1973. Textural features
**   for image classification.  IEEE Transactions on Systems, Man, and
**   Cybertinetics, SMC-3(6):610-621.
**
** Copyright (C) 1991 Texas Agricultural Experiment Station, employer for
** hire of James Darrell McCauley
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
**
** THE TEXAS AGRICULTURAL EXPERIMENT STATION (TAES) AND THE TEXAS A&M
** UNIVERSITY SYSTEM (TAMUS) MAKE NO EXPRESS OR IMPLIED WARRANTIES
** (INCLUDING BY WAY OF EXAMPLE, MERCHANTABILITY) WITH RESPECT TO ANY
** ITEM, AND SHALL NOT BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL
** OR CONSEQUENTAL DAMAGES ARISING OUT OF THE POSSESSION OR USE OF
** ANY SUCH ITEM. LICENSEE AND/OR USER AGREES TO INDEMNIFY AND HOLD
** TAES AND TAMUS HARMLESS FROM ANY CLAIMS ARISING OUT OF THE USE OR
** POSSESSION OF SUCH ITEMS.
**
** Modification History:
** 24 Jun 91 - J. Michael Carstensen <jmc@imsor.dth.dk> supplied fix for
**             correlation function.
**
** 05 Oct 05 - Marc Breithecker <Marc.Breithecker@informatik.uni-erlangen.de>
**             Fix calculation or normalizing constants for d > 1.
** 9 Jul 11  - Francois P. S. Luus <fpsluus@gmail.com> supplied fix for sum
**             variance calculation (use F6:savg instead of F8:sentropy in
**             F7:svar equation).
*/
