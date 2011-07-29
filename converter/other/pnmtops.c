/* pnmtops.c - read a PNM image and produce a PostScript program.

   Copyright information is at end of file.

   We produce two main kinds of Postscript program:

      1) Use built in Postscript filters /ASCII85Decode, /ASCIIHexDecode,
	 /RunLengthDecode, and /FlateDecode;

	 We use methods we learned from Dirk Krause's program Bmeps.
	 Previous versions used raster encoding code based on Bmeps
	 code.  This program does not used any code from Bmeps.

      2) Use our own filters and redefine /readstring .  This is aboriginal
	 Netpbm code, from when Postscript was young.  The filters are
	 nearly identical to /ASCIIHexDecode and /RunLengthDecode.  We
	 use the same raster encoding code with slight modifications.

   (2) is the default.  (1) gives more options, but relies on features
   introduced in Postscript Level 2, which appeared in 1991.  Postcript
   devices made before 1991 can't handle them.  The user selects (1)
   with the -psfilter option.

   We also do a few other bold new things only when the user specifies
   -psfilter, because we're not sure they work for everyone.

   (I actually don't know Postscript, so some of this description, not to
   mention the code, may be totally bogus.)

   NOTE: it is possible to put transparency information in an
   encapsulated Postscript program.  Bmeps does this.  We don't.  It
   might be hard to do, because in Postscript, the transparency information
   goes in separate from the rest of the raster.
*/

#define _BSD_SOURCE  /* Make sure string.h contains strdup() */
#define _XOPEN_SOURCE 500  /* Make sure strdup() is in string.h */
#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <assert.h>
#include <string.h>
#ifndef NOFLATE
#include <zlib.h>
#endif

#include "pm_c_util.h"
#include "pam.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "nstring.h"

struct cmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* Filespecs of input file */
    float scale;
    unsigned int dpiX;     /* horiz component of DPI option */
    unsigned int dpiY;     /* vert component of DPI option */
    unsigned int width;              /* in 1/72 inch */
    unsigned int height;             /* in 1/72 inch */
    unsigned int mustturn;
    bool         canturn;
    unsigned int rle;
    bool         center;
    unsigned int imagewidth;         /* in 1/72 inch; zero if unspec */
    unsigned int imageheight;        /* in 1/72 inch; zero if unspec */
    unsigned int equalpixels;
    unsigned int bitspersampleSpec;
    unsigned int bitspersample;
    unsigned int setpage;
    bool         showpage;
    unsigned int level;
    unsigned int levelSpec;
    unsigned int psfilter;
    unsigned int flate;
    unsigned int ascii85;
    unsigned int dict;
    unsigned int vmreclaim;
    unsigned int verbose;
};

static bool verbose;

static void
parseDpi(const char *   const dpiOpt, 
     unsigned int * const dpiXP, 
     unsigned int * const dpiYP) {

    char *dpistr2;
    unsigned long int dpiX, dpiY;

    dpiX = strtol(dpiOpt, &dpistr2, 10);
    if (dpistr2 == dpiOpt)
    pm_error("Invalid value for -dpi: '%s'.  Must be either number "
         "or NxN ", dpiOpt);
    else if (dpiX > INT_MAX)
    pm_error("Invalid value for -dpi: '%s'.  "
         "Value too large for computation", dpiOpt);
    else {
    if (*dpistr2 == '\0') {
        *dpiXP = dpiX;
        *dpiYP = dpiX;
    } else if (*dpistr2 == 'x') {
        char * dpistr3;

        dpistr2++;  /* Move past 'x' */
        dpiY = strtol(dpistr2, &dpistr3, 10);
        if (dpiY > INT_MAX)
        pm_error("Invalid value for -dpi: '%s'.  "
             "Value too large for computation", dpiOpt);
        else if (dpistr3 != dpistr2 && *dpistr3 == '\0') {
        *dpiXP = dpiX;
        *dpiYP = dpiY;
        } else {
        pm_error("Invalid value for -dpi: '%s'.  Must be either "
             "number or NxN", dpiOpt);
        }
    }
    }
}



static void
validateBps_1_2_4_8_12(unsigned int const bitsPerSample) {

    switch (bitsPerSample) {
    case 1:
    case 2:
    case 4:
    case 8:
    case 12:
    break;
    default:
    pm_error("Invalid -bitspersample value: %u.  Must be "
         "1, 2, 4, 8, or 12", bitsPerSample);
    }
}


static void
validateCompDimension(unsigned int const value,
              unsigned int const scaleFactor,
              const char * const vname) {
/*----------------------------------------------------------------------------
   Validate that the image dimension (width or height) 'value' isn't so big
   that in this program's calculations, involving scale factor 'scaleFactor',
   it would cause a register overflow.  If it is, abort the program and refer
   to the offending dimension as 'vname' in the error message.

   Note that this early validation approach (calling this function) means
   the actual computations don't have to be complicated with arithmetic
   overflow checks, so they're easier to read.
-----------------------------------------------------------------------------*/
    if (value > 0) {
    unsigned int const maxWidthHeight = INT_MAX - 2;
    unsigned int const maxScaleFactor = maxWidthHeight / value;

    if (scaleFactor > maxScaleFactor)
        pm_error("%s is too large for compuations: %u", vname, value);
    }
}



static void
parseCommandLine(int argc, const char ** argv,
         struct cmdlineInfo * const cmdlineP) {

    unsigned int imagewidthSpec, imageheightSpec;
    float imagewidth, imageheight;
    unsigned int center, nocenter;
    unsigned int nosetpage;
    float width, height;
    unsigned int noturn;
    unsigned int showpage, noshowpage;
    const char * dpiOpt;
    unsigned int dpiSpec, scaleSpec, widthSpec, heightSpec;

    optStruct3 opt;
    unsigned int option_def_index = 0;
    optEntry * option_def;

    MALLOCARRAY_NOFAIL(option_def, 100);

    OPTENT3(0, "scale",       OPT_FLOAT, &cmdlineP->scale, &scaleSpec,   0);
    OPTENT3(0, "dpi",         OPT_STRING, &dpiOpt,         &dpiSpec,     0);
    OPTENT3(0, "width",       OPT_FLOAT, &width,           &widthSpec,   0);
    OPTENT3(0, "height",      OPT_FLOAT, &height,          &heightSpec,  0);
    OPTENT3(0, "psfilter",    OPT_FLAG,  NULL, &cmdlineP->psfilter,      0);
    OPTENT3(0, "turn",        OPT_FLAG,  NULL, &cmdlineP->mustturn,      0);
    OPTENT3(0, "noturn",      OPT_FLAG,  NULL, &noturn,                  0);
    OPTENT3(0, "rle",         OPT_FLAG,  NULL, &cmdlineP->rle,           0);
    OPTENT3(0, "runlength",   OPT_FLAG,  NULL, &cmdlineP->rle,           0);
    OPTENT3(0, "ascii85",     OPT_FLAG,  NULL, &cmdlineP->ascii85,       0);
    OPTENT3(0, "center",      OPT_FLAG,  NULL, &center,                  0);
    OPTENT3(0, "nocenter",    OPT_FLAG,  NULL, &nocenter,                0);
    OPTENT3(0, "equalpixels", OPT_FLAG,  NULL, &cmdlineP->equalpixels,   0);
    OPTENT3(0, "imagewidth",  OPT_FLOAT, &imagewidth,  &imagewidthSpec,  0);
    OPTENT3(0, "imageheight", OPT_FLOAT, &imageheight, &imageheightSpec, 0);
    OPTENT3(0, "bitspersample", OPT_UINT, &cmdlineP->bitspersample,
        &cmdlineP->bitspersampleSpec, 0);
    OPTENT3(0, "nosetpage",   OPT_FLAG,  NULL, &nosetpage,               0);
    OPTENT3(0, "setpage",     OPT_FLAG,  NULL, &cmdlineP->setpage,       0);
    OPTENT3(0, "noshowpage",  OPT_FLAG,  NULL, &noshowpage,              0);
    OPTENT3(0, "flate",       OPT_FLAG,  NULL, &cmdlineP->flate,         0);
    OPTENT3(0, "dict",        OPT_FLAG,  NULL, &cmdlineP->dict,          0);
    OPTENT3(0, "vmreclaim",   OPT_FLAG,  NULL, &cmdlineP->vmreclaim,     0);
    OPTENT3(0, "showpage",    OPT_FLAG,  NULL, &showpage,                0);
    OPTENT3(0, "verbose",     OPT_FLAG,  NULL, &cmdlineP->verbose,       0);
    OPTENT3(0, "level",       OPT_UINT, &cmdlineP->level, 
        &cmdlineP->levelSpec,              0);
    
    opt.opt_table = option_def;
    opt.short_allowed = FALSE;
    opt.allowNegNum = FALSE;

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);

    if (cmdlineP->mustturn && noturn)
    pm_error("You cannot specify both -turn and -noturn");
    if (center && nocenter)
    pm_error("You cannot specify both -center and -nocenter");
    if (showpage && noshowpage)
    pm_error("You cannot specify both -showpage and -noshowpage");
    if (cmdlineP->setpage && nosetpage)
    pm_error("You cannot specify both -setpage and -nosetpage");

    if (!scaleSpec)
    cmdlineP->scale = 1.0;

    if (!widthSpec)
    width = 8.5;

    if (!heightSpec)
    height = 11.0;

    if (dpiSpec)
    parseDpi(dpiOpt, &cmdlineP->dpiX, &cmdlineP->dpiY);
    else {
    cmdlineP->dpiX = 300;
    cmdlineP->dpiY = 300;
    }

    cmdlineP->center  =  !nocenter;
    cmdlineP->canturn =  !noturn;
    cmdlineP->showpage = !noshowpage;

    validateCompDimension(width, 72, "-width value");
    validateCompDimension(height, 72, "-height value");
    
    cmdlineP->width  = width * 72;
    cmdlineP->height = height * 72;

    if (imagewidthSpec) {
    validateCompDimension(imagewidth, 72, "-imagewidth value");
    cmdlineP->imagewidth = imagewidth * 72;
    }
    else
    cmdlineP->imagewidth = 0;
    if (imageheightSpec) {
    validateCompDimension(imagewidth, 72, "-imageheight value");
    cmdlineP->imageheight = imageheight * 72;
    }
    else
    cmdlineP->imageheight = 0;

#ifdef NOFLATE
    if (cmdlineP->flate)
        pm_error("This program cannot handle flate compression. "
                 "Flate support suppressed at compile time.");
#endif

    if (!cmdlineP->psfilter &&
    (cmdlineP->flate || cmdlineP->ascii85))
    pm_error("You must specify -psfilter in order to specify "
         "-flate or -ascii85");

    if (cmdlineP->bitspersampleSpec)
    validateBps_1_2_4_8_12(cmdlineP->bitspersample);

    if (argc-1 == 0) 
    cmdlineP->inputFileName = "-";
    else if (argc-1 != 1)
    pm_error("Program takes zero or one argument (filename).  You "
         "specified %d", argc-1);
    else
    cmdlineP->inputFileName = argv[1];

    free(option_def); 
}


/*===========================================================================
  The output encoder
===========================================================================*/
    
enum output {AsciiHex, Ascii85};
enum compress {none, Runlength, Flate, RunlengthFlate};

typedef struct {
  enum output        output;
  enum compress      compress;
  unsigned int       runlengthRefresh;
  int                pid[3]; /* child process ID, transmitted to parent */
} OutputEncoder;


static void
initOutputEncoder(OutputEncoder  * const oeP,
          unsigned int     const icols,
          unsigned int     const bitsPerSample,
          bool             const rle,
          bool             const flate,
          bool             const ascii85,
          bool             const psFilter) {

    unsigned int const bytesPerRow = icols / (8/bitsPerSample) +
    (icols % (8/bitsPerSample) > 0 ? 1 : 0);
    /* Size of row buffer, padded up to byte boundary.

       A more straightforward calculation would be
         (icols * bitsPerSample + 7) / 8 ,
       but this overflows when icols is large.
    */

    oeP->output = ascii85 ? Ascii85 : AsciiHex;

    if (rle && flate) {
      assert(psFilter);
      oeP->compress = RunlengthFlate;
      oeP->runlengthRefresh = INT_MAX;
    }
    else if (rle) {
      oeP->compress = Runlength;
      oeP->runlengthRefresh = psFilter ? INT_MAX : bytesPerRow;
    }
    else if (flate) {
      assert(psFilter);
      oeP->compress = Flate;
    }
    else   /* neither rle nor flate */
      oeP->compress = none;

    if(ascii85) {
      assert(psFilter);
      oeP->output = Ascii85;
    }
    else
      oeP->output = AsciiHex;

}

/*
The following function flateFilter() is based on def() in zpipe.c.
zpipe is an example program which comes with the zlib source package.
zpipe.c is public domain and is available from the Zlib website:
http://www.zlib.net/

See zlib.h for details on zlib parameters Z_NULL, Z_OK, etc.
*/

static void 
flateFilter(int const fdsource, int const fddest, OutputEncoder * const oeP)
#ifndef NOFLATE
{

#define CHUNK 128*1024 /* recommended in zpipe.c */
               /* 4096 is not efficient but works */

    int ret, flush;
    unsigned have;
    z_stream strm;
    unsigned char * const  in = pm_allocrow(CHUNK, 1);
    unsigned char * const out = pm_allocrow(CHUNK, 1);
    const int level = 9; /* maximum compression.  see zlib.h */

    FILE * source;
    FILE * dest;
 
    /* allocate deflate state */
    strm.zalloc = Z_NULL;
    strm.zfree = Z_NULL;
    strm.opaque = Z_NULL;
    ret = deflateInit(&strm, level);
    if (ret != Z_OK)
      pm_error("Failed to initialize zlib.");

    /* open files */
    source = fdopen(fdsource, "r");
    dest = fdopen(fddest, "w");
    if (source ==NULL || dest==NULL)
      pm_error("Failed to open internal pipe(s) for flate compression.");

    /* compress until end of file */
    do {
    strm.avail_in = fread(in, 1, CHUNK, source);
    if (ferror(source)) {
      (void)deflateEnd(&strm);
      pm_error("Error reading from internal pipe during "
           "flate compression.");
    }
    flush = feof(source) ? Z_FINISH : Z_NO_FLUSH;
    strm.next_in = in;

    /* run deflate() on input until output buffer not full, finish
       compression if all of source has been read in */
    do {
        strm.avail_out = CHUNK;
        strm.next_out = out;
        ret = deflate(&strm, flush);    /* no bad return value */
        assert(ret != Z_STREAM_ERROR);  /* state not clobbered */
        have = CHUNK - strm.avail_out;
        if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
        (void)deflateEnd(&strm);
        pm_error("Error writing to internal pipe during "
             "flate compression.");
        }
    } while (strm.avail_out == 0);
    assert(strm.avail_in == 0);     /* all input will be used */

    /* done when last data in file processed */
    } while (flush != Z_FINISH);
    assert(ret == Z_STREAM_END);        /* stream will be complete */

    /* clean up and return */
    free(in);
    free(out); 
    (void)deflateEnd(&strm);
    fclose(source);
    fclose(dest);
}
#else
{
  assert(0==1);    /* should never be executed */ 
}
#endif


/* Run length encoding

In this simple run-length encoding scheme, compressed and uncompressed
strings follow a single index byte N.  N 0-127 means the next N+1
bytes are uncompressed; 129-255 means the next byte is to be repeated
257-N times.

In native (non-psfilter) mode, the run length filter must flush at
the end of every row.  But the entire raster is sent to the run length
filter as one continuous stream.  The run length filter learns the
refresh interval from oeP->runlengthRefresh.
*/


static void
rlePutBuffer (unsigned int const repeat,
          unsigned int const count,
          unsigned int const repeatitem,
          unsigned char * const itembuf,
          FILE * const fp) {

  if (repeat) {
    fputc ( 257-count,  fp);
    fputc ( repeatitem, fp);
  }
  else {
    fputc ( count-1, fp);
    fwrite( itembuf, 1, count, fp);
  }
}



static void
rleFilter (int const in, int const out, OutputEncoder * const  oeP)
{

  unsigned int repeat = 1, count = 0, incount=0, repeatcount;
  unsigned int const refresh = oeP->runlengthRefresh;
  unsigned char repeatitem;
  int     rleitem;
  unsigned char itembuf[128];
  int     i;
  FILE   *fin;
  FILE   *fout;


  fin = fdopen (in, "r");
  fout = fdopen (out, "w");
  if (fin == NULL || fout == NULL)
    pm_error("Failed to open internal pipe(s) for run-length compression.");

  while ((rleitem = fgetc (fin)) != EOF) {
    incount++;

    if (repeat && count == 0) { /* Still initializing a repeat buf. */
      itembuf[count++] = repeatitem = rleitem;
    }
    else if (repeat) {          /* Repeating - watch for end of run. */
      if (rleitem == repeatitem) {      /* Run continues. */
    itembuf[count++] = rleitem;
      }
      else {                    /* Run ended - is it long enough to dump? */
    if (count > 2) {
    /* Yes, dump a repeat-mode buffer and start a new one. */
      rlePutBuffer (repeat, count, repeatitem, itembuf, fout);
      repeat = 1;
      count = 0;
      itembuf[count++] = repeatitem = rleitem;
    }
    else {
    /* Not long enough - convert to non-repeat mode. */
      repeat = 0;
      itembuf[count++] = repeatitem = rleitem;
      repeatcount = 1;
    }
      }
    }
    else {
    /* Not repeating - watch for a run worth repeating. */
      if (rleitem == repeatitem) {      /* Possible run continues. */
    ++repeatcount;
    if (repeatcount > 3) {
    /* Long enough - dump non-repeat part and start repeat. */
      count = count - (repeatcount - 1);
      rlePutBuffer (repeat, count, repeatitem, itembuf, fout);
      repeat = 1;
      count = repeatcount;
      for (i = 0; i < count; ++i)
        itembuf[i] = rleitem;
    }
    else {
    /* Not long enough yet - continue as non-repeat buf. */
      itembuf[count++] = rleitem;
    }
      }
      else {                    /* Broken run. */
    itembuf[count++] = repeatitem = rleitem;
    repeatcount = 1;

      }
    }

    if (incount == refresh) {
      rlePutBuffer (repeat, count, repeatitem, itembuf, fout);
      repeat = 1;
      count = incount = 0;
    }

    if (count == 128) {
      rlePutBuffer (repeat, count, repeatitem, itembuf, fout);
      repeat = 1;
      count = 0;
    }

  }

  if (count > 0)
    rlePutBuffer (repeat, count, repeatitem, itembuf, fout);

  fclose (fin);
  fclose (fout);
}



static void
asciiHexFilter (int const fd)
{
  FILE   *fp;
  int     c;
  unsigned char inbuff[40], outbuff[81];
  const char hexits[16] = "0123456789abcdef";

  fp = fdopen (fd, "r");
  if (fp == NULL)
    pm_error ("Ascii Hex filter input pipe open failed");

  while ((c = fread (inbuff, 1, 40, fp)) > 0) {
    int     i;
    for (i = 0; i < c; ++i) {
      int const item = inbuff[i]; 
      outbuff[i*2]   =hexits[item >> 4];
      outbuff[i*2+1] =hexits[item & 15];
      }

    outbuff[c*2] = '\n';
    fwrite(outbuff, 1, c*2+1, stdout);    
  }

  fclose (fp);
  fclose (stdout);
}



static void
ascii85Filter (int const fd) {
  FILE   *fp;
  int     c;
  char outbuff[5];
  unsigned long int value=0; /* requires 32 bits */
  int count=0;
  int outcount=0;

  fp = fdopen (fd, "r");
  if (fp == NULL)
    pm_error ("Ascii 85 filter input pipe open failed");

  while ((c = fgetc (fp)) !=EOF) {
    value = value*256 + c;
    count++;

    if (value == 0 && count == 4) {
      putchar('z');  /* Ascii85 encoding z exception */
      outcount++;
      count=0;
    }
    else if (count == 4) {

      outbuff[4] = value % 85 + 33;  value/=85; 
      outbuff[3] = value % 85 + 33;  value/=85;
      outbuff[2] = value % 85 + 33;  value/=85;
      outbuff[1] = value % 85 + 33;
      outbuff[0] = value / 85 + 33;

      fwrite(outbuff, 1, count+1, stdout);
      count = value = 0;
      outcount+=5; 
    }

    if (outcount > 75) {
      putchar('\n');
      outcount=0;
    }
  }

  if (count >0) { /* EOF, flush */
    assert (count < 4 );

    value <<= ( 4 - count ) * 8;   value/=85;
    outbuff[3] = value % 85 + 33;  value/=85;
    outbuff[2] = value % 85 + 33;  value/=85;
    outbuff[1] = value % 85 + 33;
    outbuff[0] = value / 85 + 33;
    outbuff[count+1] = '\n';

    fwrite(outbuff, 1, count+2, stdout);
  }

  fclose (fp);
  fclose (stdout);
}


/*
Open pipes and spawn child processes

Each filter is a separate child process.  The parent process feeds
raster data into the pipeline.

  convertRow | asciiHexFilter
  convertRow | ascii85Filter
  convertRow | rleFilter   | asciiHexFilter
  convertRow | flateFilter | asciiHexFilter
  convertRow | flateFilter | rleFilter | asciiHexFilter

When adding functionality, it should be done by writing new filters,
amending existing filters and/or the convertRow functions.  The
following activate*Filter functions and the bit accumulator functions
should be kept as simple as possible.

*/


static void
activateOneFilter (int * const feedP, OutputEncoder * const oeP)
{
  int     p1[2];
  /* open pipe */

  if (pipe (p1) == -1) {
    pm_error ("pipe() failed");
    exit (1);
  }

  switch (oeP->pid[0] = fork ()) {
  case -1:          /* error */
    pm_error("fork() of filter process failed.  errno=%d (%s)", 
          errno, strerror(errno));
    exit (EXIT_FAILURE);
    break;

  case 0:           /* child */
    close (p1[1]);
    if(oeP->output==Ascii85)
      ascii85Filter (p1[0]);
    else
      asciiHexFilter (p1[0]);

    exit (EXIT_SUCCESS);
    break;
  }

  /* parent (Neither child comes here) */
  close (p1[0]);
  *feedP = p1[1];
  oeP->pid[1] = oeP->pid[2] = 0;
}



static void
activateTwoFilters (int * const feedP, OutputEncoder * const oeP)
{
  int     p1[2], p2[2];
  /* open pipes */

  if (pipe (p1) == -1) {
    pm_error ("pipe() failed");
    exit (1);
  }

  if (pipe (p2) == -1) {
    pm_error ("pipe() failed");
    exit (1);
  }

  switch (oeP->pid[0] = fork ()) {
  case -1:          /* error */
    pm_error("fork() of filter process failed.  errno=%d (%s)", 
          errno, strerror(errno));
    exit (EXIT_FAILURE);
    break;

  case 0:           /* child */
    close (p1[1]);
    close (p2[0]);

    switch(oeP->compress) {
    case(Runlength):
      rleFilter (p1[0], p2[1], oeP);
      break;
    case(Flate):
      flateFilter (p1[0], p2[1], oeP);
      break;
    default:
      /* error */
      break;
    }

    exit (EXIT_SUCCESS);
    break;
  }

  switch (oeP->pid[1] = fork ()) {
  case -1:          /* error */
    pm_error("fork() of filter process failed.  errno=%d (%s)", 
          errno, strerror(errno));
    exit (EXIT_FAILURE);
    break;

  case 0:           /* child */
    close (p1[0]);     close (p1[1]);     close (p2[1]);

    if(oeP->output==Ascii85)
      ascii85Filter (p2[0]);
    else
      asciiHexFilter (p2[0]);

    exit (EXIT_SUCCESS);
    break;
  }
  /* parent (Neither child comes here) */
  close (p1[0]);  close (p2[0]);  close (p2[1]);
  *feedP = p1[1];
  oeP->pid[2] = 0;
}



static void
activateThreeFilters (int * const feedP, OutputEncoder * const oeP)
{
  int     p1[2], p2[2], p3[2];
  /* open pipes */

  if (pipe (p1) == -1) {
    pm_error ("pipe() failed");
    exit (1);
  }

  if (pipe (p2) == -1) {
    pm_error ("pipe() failed");
    exit (1);
  }

  if (pipe (p3) == -1) {
    pm_error ("pipe() failed");
    exit (1);
  }

  switch (oeP->pid[0] = fork ()) {
  case -1:          /* error */
    pm_error("fork() of filter process failed.  errno=%d (%s)", 
          errno, strerror(errno));
    exit (EXIT_FAILURE);
    break;

  case 0:           /* child */
    close (p1[1]);    close (p2[0]);    close (p3[0]);    close (p3[1]);
    rleFilter (p1[0], p2[1], oeP);
    exit (EXIT_SUCCESS);
    break;
  }

  switch (oeP->pid[1] = fork ()) {
  case -1:          /* error */
    pm_error("fork() of filter process failed.  errno=%d (%s)", 
          errno, strerror(errno));
    exit (EXIT_FAILURE);
    break;

  case 0:           /* child */
    close (p1[0]);    close (p1[1]);    close (p2[1]);    close (p3[0]);
    flateFilter(p2[0], p3[1], oeP);
    exit (EXIT_SUCCESS);
    break;
  }

  switch (oeP->pid[2] = fork ()) {
  case -1:          /* error */
    pm_error("fork() of filter process failed.  errno=%d (%s)", 
          errno, strerror(errno));
    exit (EXIT_FAILURE);
    break;

  case 0:           /* child */
    close (p1[0]);    close (p1[1]);    close (p2[0]);    close (p2[1]);
    close (p3[1]);

    if(oeP->output==Ascii85)
      ascii85Filter (p3[0]);
    else
      asciiHexFilter (p3[0]);

    exit (EXIT_SUCCESS);
    break;
  }

  /* parent (Neither child comes here) */
  close (p1[0]);  close (p2[0]);  close (p2[1]);
  close (p3[0]);  close (p3[1]);
  *feedP = p1[1];
}


static void
activateFilters (int * const feedP, OutputEncoder * const oeP){

  switch(oeP->compress) {
  case none:
    activateOneFilter(feedP, oeP);
    break;
  case RunlengthFlate:
    activateThreeFilters(feedP, oeP);
    break;
  default:
    activateTwoFilters(feedP, oeP);
  }
}

static void
waitForChildren(int * const pid) {

  int i;
  int status;

  for (i = 0; i < 3; ++i) {
    if (pid[i] != 0) {
      if (waitpid (pid[i], &status, 0) == -1) {
    pm_error ("waitpid() failed");
    exit (EXIT_FAILURE);
      }
      else if (status != EXIT_SUCCESS) {
    pm_error ("Child process terminated abnoramally");
    exit (EXIT_FAILURE);
      }
    }
  }
}
/*============================================================================
   END OF OUTPUT ENCODERS
============================================================================*/



static void
validateComputableBoundingBox(float const scols, 
                  float const srows,
                  float const llx, 
                  float const lly) {

  float const bbWidth  = llx + scols + 0.5;
  float const bbHeight = lly + srows + 0.5;

  if (bbHeight < INT_MIN || bbHeight > INT_MAX ||
      bbWidth  < INT_MIN || bbWidth  > INT_MAX)
      pm_error("Bounding box dimensions %.1f x %.1f are too large "
           "for computations.  "
           "This probably means input image width, height, "
           "or scale factor is too large", bbWidth, bbHeight);
}



static void
computeImagePosition(int     const dpiX, 
             int     const dpiY, 
             int     const icols, 
             int     const irows,
             bool    const mustturn,
             bool    const canturn,
             bool    const center,
             int     const pagewid, 
             int     const pagehgt, 
             float   const requestedScale,
             float   const imagewidth,
             float   const imageheight,
             bool    const equalpixels,
             float * const scolsP,
             float * const srowsP,
             float * const llxP, 
             float * const llyP,
             bool *  const turnedP ) {
/*----------------------------------------------------------------------------
   Determine where on the page the image is to go.  This means position,
   dimensions, and orientation.

   icols/irows are the dimensions of the PNM input in xels.

   'mustturn' means we are required to rotate the image.

   'canturn' means we may rotate the image if it fits better, but don't
   have to.

   *scolsP, *srowsP are the dimensions of the image in 1/72 inch.

   *llxP, *llyP are the coordinates, in 1/72 inch, of the lower left
   corner of the image on the page.

   *turnedP is true iff the image is to be rotated 90 degrees on the page.

   imagewidth/imageheight are the requested dimensions of the image on
   the page, in 1/72 inch.  Image will be as large as possible within
   those dimensions.  Zero means unspecified, so 'scale', 'pagewid',
   'pagehgt', 'irows', and 'icols' determine image size.

   'equalpixels' means the user wants one printed pixel per input pixel.
   It is inconsistent with imagewidth or imageheight != 0

   'requestedScale' is meaningful only when imageheight/imagewidth == 0
   and equalpixels == FALSE.  It tells how many inches the user wants
   72 pixels of input to occupy, if it fits on the page.
-----------------------------------------------------------------------------*/
    int cols, rows;
    /* Number of columns, rows of input xels in the output, as
       rotated if applicable
    */
    bool shouldturn;  /* The image fits the page better if we turn it */
    
    if (icols > irows && pagehgt > pagewid)
    shouldturn = TRUE;
    else if (irows > icols && pagewid > pagehgt)
    shouldturn = TRUE;
    else
    shouldturn = FALSE;

    if (mustturn || (canturn && shouldturn)) {
    *turnedP = TRUE;
    cols = irows;
    rows = icols;
    } else {
    *turnedP = FALSE;
    cols = icols;
    rows = irows;
    }
    if (equalpixels) {
    *scolsP = (72.0/dpiX)*cols;
    *srowsP = (72.0/dpiY)*rows;
    } else if (imagewidth > 0 || imageheight > 0) {
    float scale;

    if (imagewidth == 0)
        scale = (float) imageheight/rows;
    else if (imageheight == 0)
        scale = (float) imagewidth/cols;
    else
        scale = MIN((float)imagewidth/cols, (float)imageheight/rows);
    
    *scolsP = cols*scale;
    *srowsP = rows*scale;
    } else {
    /* He didn't give us a bounding box for the image so figure
       out output image size from other inputs.
    */
    const int devpixX = dpiX / 72.0 + 0.5;        
    const int devpixY = dpiY / 72.0 + 0.5;        
        /* How many device pixels make up 1/72 inch, rounded to
           nearest integer */
    const float pixfacX = 72.0 / dpiX * devpixX;  /* 1, approx. */
    const float pixfacY = 72.0 / dpiY * devpixY;  /* 1, approx. */
    float scale;

    scale = MIN(requestedScale, 
            MIN((float)pagewid/cols, (float)pagehgt/rows));

    *scolsP = scale * cols * pixfacX;
    *srowsP = scale * rows * pixfacY;
    
    if (scale != requestedScale)
        pm_message("warning, image too large for page, rescaling to %g", 
               scale );

    /* Before May 2001, Pnmtops enforced a 5% margin around the page.
       If the image would be too big to leave a 5% margin, Pnmtops would
       scale it down.  But people have images that are exactly the size
       of a page, e.g. because they created them with Sane's 'scanimage'
       program from a full page of input.  So we removed the gratuitous
       5% margin.  -Bryan.
    */
    }
    *llxP = (center) ? ( pagewid - *scolsP ) / 2 : 0;
    *llyP = (center) ? ( pagehgt - *srowsP ) / 2 : 0;

    validateComputableBoundingBox( *scolsP, *srowsP, *llxP, *llyP);

    if (verbose)
    pm_message("Image will be %3.2f points wide by %3.2f points high, "
           "left edge %3.2f points from left edge of page, "
           "bottom edge %3.2f points from top of page; "
           "%sturned to landscape orientation",
           *scolsP, *srowsP, *llxP, *llyP, *turnedP ? "" : "NOT ");
}



static void
determineDictionaryRequirement(bool           const userWantsDict,
                   bool           const psFilter,
                   unsigned int * const dictSizeP) {

    if (userWantsDict) {
    if (psFilter) {
        /* The Postscript this program generates to use built-in
           Postscript filters does not define any variables.
        */
        *dictSizeP = 0;
    } else
        *dictSizeP = 8;
    } else
    *dictSizeP = 0;
}



static void
defineReadstring(bool const rle) {
/*----------------------------------------------------------------------------
   Write to Standard Output Postscript statements to define /readstring.
-----------------------------------------------------------------------------*/
    if (rle) {
    printf("/rlestr1 1 string def\n");
    printf("/readrlestring {\n");             /* s -- nr */
    printf("  /rlestr exch def\n");           /* - */
    printf("  currentfile rlestr1 readhexstring pop\n");  /* s1 */
    printf("  0 get\n");                  /* c */
    printf("  dup 127 le {\n");               /* c */
    printf("    currentfile rlestr 0\n");         /* c f s 0 */
    printf("    4 3 roll\n");             /* f s 0 c */
    printf("    1 add  getinterval\n");           /* f s */
    printf("    readhexstring pop\n");            /* s */
    printf("    length\n");               /* nr */
    printf("  } {\n");                    /* c */
    printf("    257 exch sub dup\n");         /* n n */
    printf("    currentfile rlestr1 readhexstring pop\n");/* n n s1 */
    printf("    0 get\n");                /* n n c */
    printf("    exch 0 exch 1 exch 1 sub {\n");       /* n c 0 1 n-1*/
    printf("      rlestr exch 2 index put\n");
    printf("    } for\n");                /* n c */
    printf("    pop\n");                  /* nr */
    printf("  } ifelse\n");               /* nr */
    printf("} bind def\n");
    printf("/readstring {\n");                /* s -- s */
    printf("  dup length 0 {\n");             /* s l 0 */
    printf("    3 copy exch\n");              /* s l n s n l*/
    printf("    1 index sub\n");              /* s l n s n r*/
    printf("    getinterval\n");              /* s l n ss */
    printf("    readrlestring\n");            /* s l n nr */
    printf("    add\n");                  /* s l n */
    printf("    2 copy le { exit } if\n");        /* s l n */
    printf("  } loop\n");                 /* s l l */
    printf("  pop pop\n");                /* s */
    printf("} bind def\n");
    } else {
    printf("/readstring {\n");                /* s -- s */
    printf("  currentfile exch readhexstring pop\n");
    printf("} bind def\n");
    }
}



static void
setupReadstringNative(bool         const rle,
              bool         const color,
              unsigned int const icols, 
              unsigned int const bitsPerSample) {
/*----------------------------------------------------------------------------
   Write to Standard Output statements to define /readstring and also
   arguments for it (/picstr or /rpicstr, /gpicstr, and /bpicstr).
-----------------------------------------------------------------------------*/
    unsigned int const bytesPerRow = icols / (8/bitsPerSample) +
    (icols % (8/bitsPerSample) > 0 ? 1 : 0);
    /* Size of row buffer, padded up to byte boundary. */

    defineReadstring(rle);
    
    if (color) {
    printf("/rpicstr %d string def\n", bytesPerRow);
    printf("/gpicstr %d string def\n", bytesPerRow);
    printf("/bpicstr %d string def\n", bytesPerRow);
    } else
    printf("/picstr %d string def\n", bytesPerRow);
}



static void
putFilters(unsigned int const postscriptLevel,
       bool         const rle,
       bool         const flate,
       bool         const ascii85,
       bool         const color) {

    assert(postscriptLevel > 1);
    
    if (ascii85)
    printf("/ASCII85Decode filter ");
    else 
    printf("/ASCIIHexDecode filter ");
    if (flate)
    printf("/FlateDecode filter ");
    if (rle) /* activateThreeFilters() encodes rle before flate,
        so decode must be flate then rle */
    printf("/RunLengthDecode filter ");
}



static void
putReadstringNative(bool const color) {

    if (color) {
    printf("{ rpicstr readstring }\n");
    printf("{ gpicstr readstring }\n");
    printf("{ bpicstr readstring }\n");
    } else
    printf("{ picstr readstring }\n");
}



static void
putSetup(unsigned int const dictSize,
     bool         const psFilter,
     bool         const rle,
     bool         const color,
     unsigned int const icols,
     unsigned int const bitsPerSample) {
/*----------------------------------------------------------------------------
   Put the setup section in the Postscript program on Standard Output.
-----------------------------------------------------------------------------*/
    printf("%%%%BeginSetup\n");

    if (dictSize > 0)
    /* inputf {r,g,b,}pictsr readstring readrlestring rlestring */
    printf("%u dict begin\n", dictSize);
    
    if (!psFilter)
    setupReadstringNative(rle, color, icols, bitsPerSample);

    printf("%%%%EndSetup\n");
}



static void
putImage(bool const psFilter,
     bool const color) {
/*----------------------------------------------------------------------------
   Put the image/colorimage statement in the Postscript program on
   Standard Output.
-----------------------------------------------------------------------------*/
    if (color) {
    if (psFilter)
        printf("false 3\n");
    else
        printf("true 3\n");
    printf("colorimage");
    } else
    printf("image");
}



static void
putInitPsFilter(unsigned int const postscriptLevel,
        bool         const rle,
        bool         const flate,
        bool         const ascii85,
        bool         const color) {

    bool const filterTrue = TRUE;

    printf("{ currentfile ");

    putFilters(postscriptLevel, rle, flate, ascii85, color);

    putImage(filterTrue, color);
    
    printf(" } exec");
}



static void
putInitReadstringNative(bool const color) {

    bool const filterFalse = FALSE;

    putReadstringNative(color);
    
    putImage(filterFalse, color);
}



static void
putInit(unsigned int const postscriptLevel,
    char         const name[], 
    int          const icols, 
    int          const irows, 
    float        const scols, 
    float        const srows,
    float        const llx, 
    float        const lly,
    int          const bitsPerSample,
    int          const pagewid, 
    int          const pagehgt,
    bool         const color, 
    bool         const turned, 
    bool         const rle,
    bool         const flate,
    bool         const ascii85,
    bool         const setpage,
    bool         const psFilter,
    unsigned int const dictSize) {
/*----------------------------------------------------------------------------
   Write out to Standard Output the headers stuff for the Postscript
   program (everything up to the raster).
-----------------------------------------------------------------------------*/
    /* The numbers in the %! line often confuse people. They are NOT the
       PostScript language level.  The first is the level of the DSC comment
       spec being adhered to, the second is the level of the EPSF spec being
       adhered to.  It is *incorrect* to claim EPSF compliance if the file
       contains a setpagedevice.
    */
    printf("%%!PS-Adobe-3.0%s\n", setpage ? "" : " EPSF-3.0");
    printf("%%%%LanguageLevel: %u\n", postscriptLevel);
    printf("%%%%Creator: pnmtops\n");
    printf("%%%%Title: %s.ps\n", name);
    printf("%%%%Pages: 1\n");
    printf(
    "%%%%BoundingBox: %d %d %d %d\n",
    (int) llx, (int) lly,
    (int) (llx + scols + 0.5), (int) (lly + srows + 0.5));
    printf("%%%%EndComments\n");

    putSetup(dictSize, psFilter, rle, color, icols, bitsPerSample);

    printf("%%%%Page: 1 1\n");
    if (setpage)
    printf("<< /PageSize [ %d %d ] /ImagingBBox null >> setpagedevice\n",
           pagewid, pagehgt);
    printf("gsave\n");
    printf("%g %g translate\n", llx, lly);
    printf("%g %g scale\n", scols, srows);
    if (turned)
    printf("0.5 0.5 translate  90 rotate  -0.5 -0.5 translate\n");
    printf("%d %d %d\n", icols, irows, bitsPerSample);
    printf("[ %d 0 0 -%d 0 %d ]\n", icols, irows, irows);

    if (psFilter)
    putInitPsFilter(postscriptLevel, rle, flate, ascii85, color);
    else
    putInitReadstringNative(color);

    printf("\n");
    fflush(stdout);
}



static void
putEnd(bool         const showpage, 
       bool         const psFilter,
       bool         const ascii85,
       unsigned int const dictSize,
       bool         const vmreclaim) {

    if (psFilter) {
    if (ascii85)
        printf("%s\n", "~>");
    else
        printf("%s\n", ">");
    } else {
    printf("currentdict /inputf undef\n");
    printf("currentdict /picstr undef\n");
    printf("currentdict /rpicstr undef\n");
    printf("currentdict /gpicstr undef\n");
    printf("currentdict /bpicstr undef\n");
    }

    if (dictSize > 0)
    printf("end\n");

    if (vmreclaim)
    printf("1 vmreclaim\n");

    printf("grestore\n");

    if (showpage)
    printf("showpage\n");
    printf("%%%%Trailer\n");
}



static void
validateBpsRequest(unsigned int const bitsPerSampleReq,
           unsigned int const postscriptLevel,
           bool         const psFilter) {

    if (postscriptLevel < 2 && bitsPerSampleReq > 8)
    pm_error("You requested %u bits per sample, but in Postscript "
         "level 1, 8 is the maximum.  You can get 12 with "
         "-level 2 and -psfilter", bitsPerSampleReq);
    else if (!psFilter && bitsPerSampleReq > 8)
    pm_error("You requested %u bits per sample, but without "
         "-psfilter, the maximum is 8", bitsPerSampleReq);
}

    

static unsigned int
bpsFromInput(unsigned int const bitsRequiredByMaxval,
         unsigned int const postscriptLevel,
         bool         const psFilter) {

    unsigned int retval;

    if (bitsRequiredByMaxval <= 1)
    retval = 1;
    else if (bitsRequiredByMaxval <= 2)
    retval = 2;
    else if (bitsRequiredByMaxval <= 4)
    retval = 4;
    else if (bitsRequiredByMaxval <= 8)
    retval = 8;
    else {
    /* Post script level 2 defines a format with 12 bits per sample,
       but I don't know the details of that format (both RLE and
       non-RLE variations) and existing native raster generation code
       simply can't handle bps > 8.  But the built-in filters know
       how to do 12 bps.
    */
    if (postscriptLevel >= 2 && psFilter)
        retval = 12;
    else
        retval = 8;
    }
    return retval;
}



static void
warnUserAboutReducedDepth(unsigned int const bitsGot,
              unsigned int const bitsWanted,
              bool         const userRequested,
              unsigned int const postscriptLevel,
              bool         const psFilter) {

    if (bitsGot < bitsWanted) {
    pm_message("Postscript will have %u bits of color resolution, "
           "though the input has %u bits.",
           bitsGot, bitsWanted);

    if (!userRequested) {
        if (postscriptLevel < 2)
        pm_message("Postscript level %u has a maximum depth of "
               "8 bits.  "
               "You could get up to 12 with -level=2 "
               "and -psfilter.",
               postscriptLevel);
        else {
        if (!psFilter)
            pm_message("You can get up to 12 bits with -psfilter");
        else
            pm_message("The Postscript maximum is 12.");
        }
    }
    }
}



static void
computeDepth(xelval         const inputMaxval,
         unsigned int   const postscriptLevel, 
         bool           const psFilter,
         unsigned int   const bitsPerSampleReq,
         unsigned int * const bitsPerSampleP) {
/*----------------------------------------------------------------------------
   Figure out how many bits will represent each sample in the Postscript
   program, and the maxval of the Postscript program samples.  The maxval
   is just the maximum value allowable in the number of bits.

   'bitsPerSampleReq' is the bits per sample that the user requests, or
   zero if he made no request.
-----------------------------------------------------------------------------*/
    unsigned int const bitsRequiredByMaxval = pm_maxvaltobits(inputMaxval);

    if (bitsPerSampleReq != 0) {
    validateBpsRequest(bitsPerSampleReq, postscriptLevel, psFilter);
    *bitsPerSampleP = bitsPerSampleReq;
    } else {
    *bitsPerSampleP = bpsFromInput(bitsRequiredByMaxval,
                       postscriptLevel, psFilter);
    }
    warnUserAboutReducedDepth(*bitsPerSampleP, bitsRequiredByMaxval,
                  bitsPerSampleReq != 0,
                  postscriptLevel, psFilter);

    if (verbose) {
    unsigned int const psMaxval = pm_bitstomaxval(*bitsPerSampleP);
    pm_message("Input maxval is %u.  Postscript raster will have "
           "%u bits per sample, so maxval = %u",
           inputMaxval, *bitsPerSampleP, psMaxval);
    }
}    

/*===========================================================================
  The bit accumulator
===========================================================================*/

typedef struct {
  unsigned int value;
  int consumed;
} BitAccumulator;


static void
bits12_add(BitAccumulator * const baP,
       unsigned int     const new12,
       FILE           * const fp) {
/*----------------------------------------------------------------------------
   Read a 12-bit string into the bit accumulator baP->value.
   On every other call, combine two 12-bit strings and write out three bytes.
-----------------------------------------------------------------------------*/

  assert (baP->consumed ==12 || baP->consumed ==0);

      if ( baP->consumed == 12){
    char const oldHi8 = (baP->value) >> 4;
    char const oldLo4 = (baP->value) & 0x0f;
    char const newHi4 = new12 >> 8;
    char const newLo8 = new12 & 0xff;

    fputc(oldHi8, fp);
    fputc( (oldLo4 << 4) | newHi4 , fp);
    fputc(newLo8, fp);
    baP->value = 0; baP->consumed = 0;
      } 
      else {
    baP->value = new12;  baP->consumed = 12;
      }
}



static void
bits_add(BitAccumulator * const baP,
        unsigned int     const b,
        int              const bitsPerSample,
        FILE           * const fp) {
/*----------------------------------------------------------------------------
   Combine bit sequences that do not fit into a byte.

   Used when bitsPerSample =1, 2, 4.  
   Logic also works for bitsPerSample = 8, 16.

   The accumulator, baP->value is unsigned int (usually 32 bits), but
   only 8 bits are used.
-----------------------------------------------------------------------------*/
      int const bufSize = 8;

      assert (bitsPerSample ==1 || bitsPerSample ==2 ||
          bitsPerSample ==4 );

      baP->value = (baP->value << bitsPerSample) | b ;
      baP->consumed += bitsPerSample;
      if ( baP->consumed == bufSize ) {     /* flush */
    fputc( baP->value, fp);
    baP->value = 0;
    baP->consumed = 0;
      }
}



static void
bits_flush(BitAccumulator * const baP, FILE * const fp) {
/*----------------------------------------------------------------------------
    Flush partial bits in baP->consumed.
-----------------------------------------------------------------------------*/

    if (baP->consumed == 12) {
      char const oldHi8 = (baP->value) >> 4;
      char const oldLo4 = (baP->value) & 0x0f;
      fputc(oldHi8, fp);
      fputc(oldLo4 << 4, fp);
    }

    else if (baP->consumed == 8)
      fputc( baP->value , fp);

    else if (baP->consumed > 0) {
      int const leftShift = 8 - baP->consumed;
      baP->value <<= leftShift;
      fputc( baP->value , fp);
    }

    baP->value = 0;
    baP->consumed = 0;
}


static __inline__ void
outputSample(BitAccumulator      * const baP,
          unsigned int     const sampleValue,
          unsigned int     const bitsPerSample,
          FILE           * const fp) {

    if (bitsPerSample == 8)
      fputc( sampleValue, fp);
    else if (bitsPerSample == 12)
      bits12_add(baP, sampleValue, fp);
    else {
      bits_add(baP, sampleValue, bitsPerSample, fp); 
    }
}



static void
flushOutput(BitAccumulator * const baP, FILE * const fp) {
  bits_flush(baP, fp);
}

/*----------------------------------------------------------------------
  Row converters

  convertRowPbm is a fast routine for PBM images.
  It is used only when the input is PBM and the user does not specify
  a -bitspersample value greater than 1.  It is not used when the input
  image is PGM or PPM and the output resolution is brought down to one
  bit per pixel by -bitpersample=1 .

  convertRowNative and convertRowPsFilter are the general converters.
  They are quite similar, the differences being:
  (1) Native output separates the color planes: 
      (RRR...RRR GGG...GGG BBB...BBB),
      whereas psFilter does not:
      (RGB RGB RGB RGB ......... RGB).
  (2) Native flushes the run-length encoder at the end of each row if
      grayscale, at the end of each plane if color.

  Both convertRowNative and convertRowPsFilter can handle PBM, though we
  don't use them.

  If studying the code, read convertRowPbm first.  convertRowNative and
  convertRowPsFilter are constructs that pack raster data into a form
  similar to a binary PBM bitrow.
----------------------------------------------------------------------*/

static void
convertRowPbm(struct pam *     const pamP,
          unsigned char  * const bitrow,
          bool             const psFilter,
          FILE * fp) {
/*---------------------------------------------------------------------
  Feed PBM raster data directly to the output encoder.
  Invert bits: 0 is "white" in PBM, 0 is "black" in postscript.
----------------------------------------------------------------------*/
    unsigned int colChar;
    unsigned int const colChars = pbm_packed_bytes(pamP->width);
    unsigned int const padRight = (8 - pamP->width %8) %8;

    pbm_readpbmrow_packed(pamP->file, bitrow, pamP->width, pamP->format);

    for (colChar = 0; colChar < colChars; ++colChar)
      bitrow[colChar] =  ~ bitrow[colChar];

    if(padRight > 0) {
      bitrow[colChars-1] >>= padRight;  /* Zero clear padding beyond */
      bitrow[colChars-1] <<= padRight;  /* right edge */
    }

    fwrite(bitrow, 1, colChars, fp); 
}



static void
convertRowNative(struct pam *     const pamP, 
         tuple *                tuplerow, 
         unsigned int     const bitsPerSample,
         FILE           * const fp) { 

    unsigned int plane;
    unsigned int const psMaxval = pm_bitstomaxval(bitsPerSample);
    BitAccumulator ba;
    ba.value = ba.consumed =0;

    pnm_readpamrow(pamP, tuplerow);
    pnm_scaletuplerow(pamP, tuplerow, tuplerow, psMaxval);

    for (plane = 0; plane < pamP->depth; ++plane) {
    unsigned int col;
    for (col= 0; col < pamP->width; ++col) {
      outputSample(&ba, tuplerow[col][plane], bitsPerSample, fp);
    }

    flushOutput(&ba, fp);
    }

}



static void
convertRowPsFilter(struct pam *     const pamP,
           tuple *                tuplerow,
           unsigned int     const bitsPerSample,
           FILE           * const fp) { 
    unsigned int col;
    unsigned int const psMaxval = pm_bitstomaxval(bitsPerSample);
    BitAccumulator ba;
    ba.value = ba.consumed =0;

    pnm_readpamrow(pamP, tuplerow);
    pnm_scaletuplerow(pamP, tuplerow, tuplerow, psMaxval);

    for (col = 0; col < pamP->width; ++col) {
    unsigned int plane;
    for (plane = 0; plane < pamP->depth; ++plane)
      outputSample(&ba, tuplerow[col][plane], bitsPerSample, fp);
    }
    flushOutput(&ba, fp);

}



static void
selectPostscriptLevel(bool           const levelIsGiven,
              unsigned int   const levelGiven,
              bool           const color,
              bool           const dict,
              bool           const flate,
              bool           const ascii85,
              bool           const psFilter,
              unsigned int * const postscriptLevelP) {

    unsigned int const maxPermittedLevel = 
    levelIsGiven ? levelGiven : UINT_MAX;
    unsigned int minPossibleLevel;

    /* Until we know, later in this function, that we needs certain
       features, we assume we can get by with classic Postscript Level 1:
    */
    minPossibleLevel = 1;

    /* Now we increase 'minPossibleLevel' as we notice that each of
       various features are required:
    */
    if (color) {
    minPossibleLevel = MAX(minPossibleLevel, 2);
    if (2 > maxPermittedLevel)
        pm_error("Color requires at least Postscript level 2");
    }
    if (flate) {
    minPossibleLevel = MAX(minPossibleLevel, 3);
    if (2 > maxPermittedLevel)
        pm_error("flate compression requires at least Postscript level 3");
    }
    if (ascii85) {
    minPossibleLevel = MAX(minPossibleLevel, 2);
    if (2 > maxPermittedLevel)
        pm_error("ascii85 encoding requires at least Postscript level 2");
    }
    if (psFilter) {
    minPossibleLevel = MAX(minPossibleLevel, 2);
    if (2 > maxPermittedLevel)
        pm_error("-psfilter requires at least Postscript level 2");
    }
    if (levelIsGiven)
    *postscriptLevelP = levelGiven;
    else
    *postscriptLevelP = minPossibleLevel;
}



static void
convertPage(FILE *       const ifP, 
        int          const turnflag, 
        int          const turnokflag, 
        bool         const psFilter,
        bool         const rle, 
        bool         const flate,
        bool         const ascii85,
        bool         const setpage,
        bool         const showpage,
        bool         const center, 
        float        const scale,
        int          const dpiX, 
        int          const dpiY, 
        int          const pagewid, 
        int          const pagehgt,
        int          const imagewidth, 
        int          const imageheight, 
        bool         const equalpixels,
        unsigned int const bitsPerSampleReq,
        char         const name[],
        bool         const dict,
        bool         const vmreclaim,
        bool         const levelIsGiven,
        bool         const levelGiven) {
    
    struct pam inpam;
    int row;
    float scols, srows;
    float llx, lly;
    bool turned;
    bool color;
    unsigned int postscriptLevel;
    unsigned int bitsPerSample;
    unsigned int dictSize;
    /* Size of Postscript dictionary we should define */
    OutputEncoder * oeP;

    int feed;
    FILE * fp;

    pnm_readpaminit(ifP, &inpam, PAM_STRUCT_SIZE(tuple_type));

    validateCompDimension(inpam.width, 16, "Input image width");
    
    if (!STRSEQ(inpam.tuple_type, PAM_PBM_TUPLETYPE) &&
    !STRSEQ(inpam.tuple_type, PAM_PGM_TUPLETYPE) &&
    !STRSEQ(inpam.tuple_type, PAM_PPM_TUPLETYPE))
    pm_error("Unrecognized tuple type %s.  This program accepts only "
         "PBM, PGM, PPM, and equivalent PAM input images", 
         inpam.tuple_type);

    color = STRSEQ(inpam.tuple_type, PAM_PPM_TUPLETYPE);
    
    selectPostscriptLevel(levelIsGiven, levelGiven, color, 
              dict, flate, ascii85, psFilter, &postscriptLevel);
    
    if (color)
    pm_message("generating color Postscript program.");

    computeDepth(inpam.maxval, postscriptLevel, psFilter, bitsPerSampleReq,
         &bitsPerSample);

    /* In positioning/scaling the image, we treat the input image as if
       it has a density of 72 pixels per inch.
    */
    computeImagePosition(dpiX, dpiY, inpam.width, inpam.height, 
             turnflag, turnokflag, center,
             pagewid, pagehgt, scale, imagewidth, imageheight,
             equalpixels,
             &scols, &srows, &llx, &lly, &turned);

    determineDictionaryRequirement(dict, psFilter, &dictSize);
    
    putInit(postscriptLevel, name, inpam.width, inpam.height, 
        scols, srows, llx, lly, bitsPerSample, 
        pagewid, pagehgt, color,
        turned, rle, flate, ascii85, setpage, psFilter, dictSize);

    MALLOCVAR_NOFAIL(oeP);

    initOutputEncoder (oeP, inpam.width, bitsPerSample, rle,
               flate, ascii85, psFilter);
    activateFilters(&feed, oeP);
 
    fp=fdopen (feed, "w");

    if( PAM_FORMAT_TYPE(inpam.format) == PBM_TYPE && bitsPerSample==1 )  {
    unsigned char * const bitrow = pbm_allocrow_packed(inpam.width);

    for (row = 0; row < inpam.height; ++row)
      convertRowPbm(&inpam, bitrow, psFilter, fp);
    pbm_freerow(bitrow);
    }
    else  {
    tuple * const tuplerow = pnm_allocpamrow(&inpam);

    for (row = 0; row < inpam.height; ++row) {
        if (psFilter)
        convertRowPsFilter(&inpam, tuplerow, bitsPerSample, fp);
        else
        convertRowNative(&inpam, tuplerow, bitsPerSample, fp);
        }

    pnm_freepamrow(tuplerow);
    }

    fclose (fp);

    waitForChildren(oeP->pid);
    free(oeP);

    putEnd(showpage, psFilter, ascii85, dictSize, vmreclaim);
}



static const char *
basebasename(const char * const filespec) {
/*----------------------------------------------------------------------------
    Return filename up to first period
-----------------------------------------------------------------------------*/
    char const dirsep = '/';
    const char * const lastSlashPos = strrchr(filespec, dirsep);

    char * name;
    const char * filename;

    if (lastSlashPos)
    filename = lastSlashPos + 1;
    else
    filename = filespec;

    name = strdup(filename);
    if (name != NULL) {
    char * const dotPosition = strchr(name, '.');

    if (dotPosition)
        *dotPosition = '\0';
    }
    return name;
}



int
main(int argc, const char * argv[]) {

    FILE * ifP;
    const char * name;  /* malloc'ed */
    struct cmdlineInfo cmdline;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    verbose = cmdline.verbose;

    ifP = pm_openr(cmdline.inputFileName);

    if (streq(cmdline.inputFileName, "-"))
    name = strdup("noname");
    else
    name = basebasename(cmdline.inputFileName);
    {
    int eof;  /* There are no more images in the input file */
    unsigned int imageSeq;

    /* I don't know if this works at all for multi-image PNM input.
       Before July 2000, it ignored everything after the first image,
       so this probably is at least as good -- it should be identical
       for a single-image file, which is the only kind which was legal
       before July 2000.

       Maybe there needs to be some per-file header and trailers stuff
       in the Postscript program, with some per-page header and trailer
       stuff inside.  I don't know Postscript.  - Bryan 2000.06.19.
    */

    eof = FALSE;  /* There is always at least one image */
    for (imageSeq = 0; !eof; ++imageSeq) {
        convertPage(ifP, cmdline.mustturn, cmdline.canturn, 
            cmdline.psfilter,
            cmdline.rle, cmdline.flate, cmdline.ascii85, 
            cmdline.setpage, cmdline.showpage,
            cmdline.center, cmdline.scale,
            cmdline.dpiX, cmdline.dpiY,
            cmdline.width, cmdline.height, 
            cmdline.imagewidth, cmdline.imageheight, 
            cmdline.equalpixels,
            cmdline.bitspersampleSpec ? cmdline.bitspersample : 0,
            name, 
            cmdline.dict, cmdline.vmreclaim,
            cmdline.levelSpec, cmdline.level);
        pnm_nextimage(ifP, &eof);
    }
    }
    pm_strfree(name);

    pm_close(ifP);

    return 0;
}



/*
** Copyright (C) 1989 by Jef Poskanzer.
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
**
**
** -nocenter option added November 1993 by Wolfgang Stuerzlinger,
**  wrzl@gup.uni-linz.ac.at.
**
** July 2011 afu
** row convertors rewritten, fast PBM-only row convertor added,
** rle compression slightly modified, flate compression added
** ascii85 output end added.
**
*/
