#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <pam.h>
#include <pm_system.h>
#include <pm_gamma.h>
#include <netpbm/ppm.h>

#include "shhopt.h"
#include "mallocvar.h"

/* ----------------------------- Type aliases ------------------------------ */

typedef unsigned char uchar;
typedef unsigned int  uint;
typedef struct   pam  pam;

typedef struct Rgb    /* an RGB sample */
{   double _[3];
} Rgb;

typedef struct Float3 /* the coefficients of a quadratic polynom */
{   double _[3];
} Float3;

/* ------------------------- Parse transformations ------------------------- */

typedef struct Trans /* a color transformation */
{   Rgb from;
    Rgb to;
} Trans;

typedef struct TransArg
{   char* from;         /* color specifictaionts         */
    char* to;           /* as they appear on commandline */
    uint  hasfrom;      /* is the "from" part present? */
    uint  hasto;
    char  nameFromS[3]; /* short option name */
    char  nameToS  [3];
    char  nameFromL[6]; /* long option name */
    char  nameToL  [6];
} TransArg;

typedef struct Sets
{   uint     direct;        /* direct processing of brightness values */
    TransArg xlations[  3]; /* color mapping as read from commandline */
    Trans    xlats   [  3]; /* color mappings parsed                  */
    char     infname [255]; /* the input file name, "-" for stdin     */
} Sets;



static void optAddTrans
(   optEntry * const option_def,
    uint *     const option_def_indexP,
    TransArg * const xP,
    char       const index
)
{   char indexc;
    uint option_def_index;

    option_def_index = *option_def_indexP;

    indexc = '0' + index;

    strcpy( xP->nameFromL, "from " ); xP->nameFromL[4] = indexc;
    strcpy( xP->nameToL,   "to "   ); xP->nameToL  [2] = indexc;
    strcpy( xP->nameFromS, "f "    ); xP->nameFromS[1] = indexc;
    strcpy( xP->nameToS,   "t "    ); xP->nameToS  [1] = indexc;

    OPTENT3(0, xP->nameFromL, OPT_STRING, &xP->from, &xP->hasfrom, 0);
    OPTENT3(0, xP->nameFromS, OPT_STRING, &xP->from, &xP->hasfrom, 0);
    OPTENT3(0, xP->nameToL,   OPT_STRING, &xP->to,   &xP->hasto,   0);
    OPTENT3(0, xP->nameToS,   OPT_STRING, &xP->to,   &xP->hasto,   0);

    *option_def_indexP = option_def_index;
}



static void ungammaSample( Rgb * const rgb )
{   int i;
    for( i = 0; i < 3; i++ )
    {   rgb->_[i] = pm_ungamma709( rgb->_[i] );  }
}



static void parseSample
(   const char * const text,
    Rgb *        const sample,
    uint         const direct
)
{   uint const MAXMAXVAL = 0xffff; /* for maximum precision */
    char   *lastsc, *endP, *colorP, *mulstart;
    char   color[50];
    double mul;
    pixel pix;

    mul = 1.0;
    colorP = ( char* )text;
    lastsc = strrchr( text, ':' );
    do /* parse the optional color multiplier, if it present */
    {   if( lastsc == NULL ) break;

        if( strstr( text, "rgb" ) == text )
        {   if( strchr( text, ':' ) == lastsc ) break;  }

        mulstart = lastsc + 1;
        errno    = 0;
        mul      = strtof( mulstart, &endP );
        if( errno != 0 || endP == mulstart )
        {   pm_error("Wrong sample multiplier: %s", mulstart );  }
        strncpy( color, text, lastsc-text );
        color[lastsc-text] = '\0';
        colorP = color;
    } while ( 1 == 0 );

    pix = ppm_parsecolor( colorP, MAXMAXVAL);
    sample->_[0] = (double)PPM_GETR( pix ) / MAXMAXVAL * mul;
    sample->_[1] = (double)PPM_GETG( pix ) / MAXMAXVAL * mul;
    sample->_[2] = (double)PPM_GETB( pix ) / MAXMAXVAL * mul;
    if( !direct )
    {   ungammaSample( sample );  }
}



static void parseTran
(   const TransArg * const xP,
    Trans *          const rP,
    uint             const direct
)
{   parseSample( xP->from, &rP->from, direct );
    parseSample( xP->to,   &rP->to,   direct );
}



static void calcTrans( Sets * const setsP )
{   const char *const FIRST2_UNDEF  = "The first two transformatios must be completely specified.";
    const char * const THIRD_INCOMPL = "The third transformation is incompletely specified.";

    TransArg * xP;
    uchar      xi;

    for( xi = 0; xi < 3; xi++ )
    {   xP = &setsP->xlations[xi];
        if( ( xi < 2 ) && ( !xP->hasto || !xP->hasfrom ) )
        {   pm_error( FIRST2_UNDEF );  }
        if( xi == 2 )
        {   if( xP->hasto != xP->hasfrom )
            {   pm_error( THIRD_INCOMPL );  }
            if( !xP->hasto )
            {   break;  }
        }
        parseTran( xP, &setsP->xlats[xi], setsP->direct );
    }
}



static Sets readOpts
(   int           argc,
    const char ** argv
)
{   const char* PM_STDIN = "-";

    Sets       sets;
    optStruct3 opt;
    uchar      xi;
    uint       option_def_index;
    optEntry * option_def;

    sets.direct = 0;

    MALLOCARRAY_NOFAIL(option_def, 100);

    option_def_index = 0;  /* incremented by OPTENT3 */

    OPTENT3(0, "direct", OPT_FLAG, &sets.direct, NULL, 0);
    for( xi = 0; xi < 3; xi++ )
    {   optAddTrans( option_def, &option_def_index, &sets.xlations[xi], xi+1 );  }

    opt.opt_table     = option_def;
    opt.short_allowed = 0;
    opt.allowNegNum   = 0;

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);

    calcTrans( &sets );
    if( argc > 2 )
    {   pm_error( "Too many positional arguments." );  }

    if( argc == 1 )
    {   strcpy( sets.infname, PM_STDIN );  }
    else
    {   strcpy( sets.infname, argv[1] );  }

    free( option_def );
    return sets;
}



static void errResolve( void )
{   pm_error( "Cannot resolve the transormations.");  }



static double sqr( double const x )
{   return x * x;  }



/* Find the transformation that maps f[i] to t[i] for 0 <= i < n */
static void solve
(   Float3   const f,
    Float3   const t,
    int      const n,
    Float3 * const coeffs
)
{   double const eps = 0.00001;
    double a, a_denom, b, b_denom, c;

    /* I have decided against generic methods of solving systems of linear
     * equations in favour of simple explicit formulas, with no memory
     * allocation and tedious matrix processing. */

    switch( n )
    {   case 3:
            a_denom = sqr( f._[0] ) * ( f._[1] - f._[2] ) -
                      sqr( f._[2] ) * ( f._[1] - f._[0] ) -
                      sqr( f._[1] ) * ( f._[0] - f._[2] );

            if( fabs( a_denom ) < eps )
            {   errResolve();  }

            a = t._[1] * ( f._[2] - f._[0] ) - t._[0] * ( f._[2] - f._[1] ) -
                t._[2] * ( f._[1] - f._[0] );
            a = a / a_denom;
        break;
        case 2: a = 0.0; break;
        default: pm_error( "solve(): incorrect value of n: %i. This is a bug. Contact the maintainer.", n ); break;
    }
    b_denom = f._[1] - f._[0];
    if( fabs( b_denom ) < eps )
    {   errResolve();  }
    b = t._[1] - t._[0] + a * ( sqr( f._[0] ) - sqr( f._[1] ) );
    b = b / b_denom;
    c = -a * sqr( f._[0] ) - b * f._[0] + t._[0];
    coeffs->_[0] = a;  coeffs->_[1] = b;  coeffs->_[2] = c;
}



static double apply
(   const double value,
    const Float3 coeffs,
    const uint   direct
)
{   double res;
    res = value;
    if( !direct)
    {   res = pm_ungamma709( res );  }

    res = ( coeffs._[0] * res + coeffs._[1] ) * res + coeffs._[2];

    /* Clipping: */
    if( res > 1.0f )
    {   res = 1.0f;  }
    if( res < 0.0f )
    {   res = 0.0f;  }

    if( !direct )
    {   res = pm_gamma709( res );  }
    return res;
}



/* collate <tn> transformatons from <ta> for channel <ch> */
static void chanData
(   const Trans * const ta,
    uchar         const tn,
    uchar         const ch,
    Float3 *      const f,
    Float3 *      const t
)
{   uchar i;
    for( i = 0; i < tn; i++ )
    {   f->_[i] = ta[i].from._[ ch ];
        t->_[i] = ta[i].to  ._[ ch ];
    }
}



static void process( Sets const sets )
{   uint     x, y;
    uchar    l, tn;
    pam      inPam, outPam;
    Float3   sol[3];
    Float3   from, to;
    tuplen * row;
    FILE   * inF;

    inF = pm_openr( sets.infname );
    pnm_readpaminit( inF, &inPam, PAM_STRUCT_SIZE( tuple_type ) );
    outPam = inPam;
    outPam.file = stdout;

    if( sets.xlations[2].hasto )
    {   tn = 3;  }
    else
    {   tn = 2;  }

    for( l = 0; l < inPam.depth; l++ )
    {   chanData( sets.xlats, tn, l, &from, &to );
        solve( from, to, tn, &sol[ l ] );
    }

    row = pnm_allocpamrown( &inPam );
    pnm_writepaminit( &outPam );
    for( y = 0; y < inPam.height; y++ )
    {   pnm_readpamrown( &inPam, row );
        for( x = 0; x < inPam.width; x++ )
        {   for( l = 0; l < inPam.depth; l++ )
            {   row[x][l] = apply( row[x][l], sol[l], sets.direct);  }
        }
        pnm_writepamrown( &outPam, row );
    }
    pnm_freepamrown( row );
    pm_close( inF );
}



int main
(   int   argc,
    char *argv[]
)
{   const char** const argvc = (const char**)argv;
    Sets sets;

    pm_proginit( &argc, argvc );
    sets = readOpts( argc, argvc );
    process( sets );

    return 0;
}



