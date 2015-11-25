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
#include <errno.h>
#include <signal.h>
#ifndef NOFLATE
#include <zlib.h>
#endif

#include "pm_c_util.h"
#include "pam.h"
#include "mallocvar.h"
#include "shhopt.h"
#include "nstring.h"
#include "runlength.h"



static void
setSignals() {
/*----------------------------------------------------------------------------
   Set up the process-global signal-related state.

   Note that we can't rely on defaults, because much of this is inherited
   from the process that forked and exec'ed this program.
-----------------------------------------------------------------------------*/
    /* See waitForChildren() for why we do this to SIGCHLD */

    struct sigaction sigchldAction;
    int rc;
    sigset_t emptySet;

    sigemptyset(&emptySet);

    sigchldAction.sa_handler = SIG_DFL;
    sigchldAction.sa_mask = emptySet;
    sigchldAction.sa_flags = SA_NOCLDSTOP;

    rc = sigaction(SIGCHLD, &sigchldAction, NULL);

    if (rc != 0)
        pm_error("sigaction() to set up signal environment failed, "
                 "errno = %d (%s)", errno, strerror(errno));
}



struct cmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;  /* Filespecs of input file */
    float        scale;
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
    unsigned int debug;
};

static bool debug;
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
    OPTENT3(0, "debug",       OPT_FLAG,  NULL, &cmdlineP->debug,         0);
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



static bool
progIsFlateCapable(void) {

    return
#ifdef NOFLATE
        false
#else
        true
#endif
        ;
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



static void
writeFile(const unsigned char * const buffer,
          size_t                const writeCt,
          const char *          const name,
          FILE *                const ofP) {

    size_t writtenCt;

    writtenCt = fwrite(buffer, 1, writeCt, ofP);

    if (writtenCt != writeCt)
        pm_error("Error writing to %s output file", name);
}



static void
writeFileChar(const char * const buffer,
              size_t       const writeCt,
              const char * const name,
              FILE *       const ofP) {

    writeFile((const unsigned char *)buffer, writeCt, name, ofP);
}



#define MAX_FILTER_CT 10
    /* The maximum number of filters this code is capable of applying */



static void
initPidList(pid_t * const pidList) {

    pidList[0] = (pid_t)0;  /* end of list marker */
}



static void
addToPidList(pid_t * const pidList,
             pid_t   const newPid) {

    unsigned int i;

    for (i = 0; i < MAX_FILTER_CT && pidList[i]; ++i);

    assert(i < MAX_FILTER_CT);

    pidList[i] = newPid;
    pidList[i+1] = (pid_t)0;  /* end of list marker */
}



/*===========================================================================
  The output encoder
  ===========================================================================*/
    
enum OutputType {AsciiHex, Ascii85};

typedef struct {
    enum OutputType    outputType;
    bool               compressRle;
    bool               compressFlate;
    unsigned int       runlengthRefresh;
} OutputEncoder;



static unsigned int
bytesPerRow (unsigned int const cols,
             unsigned int const bitsPerSample) {
/*----------------------------------------------------------------------------
  Size of row buffer, padded up to byte boundary, given that the image
  has 'cols' samples per row, 'bitsPerSample' bits per sample.
-----------------------------------------------------------------------------*/
    unsigned int retval;

    assert(bitsPerSample==1 || bitsPerSample==2 || bitsPerSample==4 || 
           bitsPerSample==8 || bitsPerSample==12);

    switch (bitsPerSample) {
    case 1:
    case 2:
    case 4:
        retval = cols / (8/bitsPerSample)
            + (cols % (8/bitsPerSample) > 0 ? 1 : 0);
        /* A more straightforward calculation would be
           (cols * bitsPerSample + 7) / 8 ,
           but this overflows when icols is large.
        */
        break;
    case 8:
        retval = cols;
        break;
    case 12:
        retval = cols + (cols+1)/2;
        break;
    }

    return retval;
}



static void
initOutputEncoder(OutputEncoder  * const oeP,
                  unsigned int     const icols,
                  unsigned int     const bitsPerSample,
                  bool             const rle,
                  bool             const flate,
                  bool             const ascii85,
                  bool             const psFilter) {

    oeP->outputType = ascii85 ? Ascii85 : AsciiHex;

    if (rle) {
        oeP->compressRle = true;
        oeP->runlengthRefresh =
             psFilter ? 1024*1024*16 : bytesPerRow(icols, bitsPerSample);
    } else
        oeP->compressRle = false;

    if (flate) {
        assert(psFilter);
        oeP->compressFlate = true;
    } else
        oeP->compressFlate = false;

    if (ascii85) {
        assert(psFilter);
        oeP->outputType = Ascii85;
    } else
        oeP->outputType = AsciiHex;
}



typedef void FilterFn(FILE *          const ifP,
                      FILE *          const ofP,
                      OutputEncoder * const oeP);
    /* This is a function that can be run in a separate process to do
       arbitrary modifications of the raster data stream.
    */
       


#ifndef NOFLATE
static void
initZlib(z_stream * const strmP) {

    int const level = 9; /* maximum compression.  see zlib.h */

    int ret;

    /* allocate deflate state */
    strmP->zalloc = Z_NULL;
    strmP->zfree  = Z_NULL;
    strmP->opaque = Z_NULL;

    ret = deflateInit(strmP, level);
    if (ret != Z_OK)
        pm_error("Failed to initialize zlib.");
}
#endif



static FilterFn flateFilter;

static void 
flateFilter(FILE *          const ifP,
            FILE *          const ofP,
            OutputEncoder * const oeP) {

#ifndef NOFLATE

    /* This code is based on def() in zpipe.c.  zpipe is an example program
       which comes with the zlib source package.  zpipe.c is public domain and
       is available from the Zlib website: http://www.zlib.net/

       See zlib.h for details on zlib parameters Z_NULL, Z_OK, etc.
    */
    unsigned int const chunkSz = 128*1024;
        /* 128K recommended in zpipe.c.  4096 is not efficient but works. */

    int flush;
    z_stream strm;
    unsigned char * in;
    unsigned char * out;

    in  = pm_allocrow(chunkSz, 1);
    out = pm_allocrow(chunkSz, 1);

    initZlib(&strm);

    /* compress until end of file */
    do {
        strm.avail_in = fread(in, 1, chunkSz, ifP);
        if (ferror(ifP)) {
            deflateEnd(&strm);
            pm_error("Error reading from internal pipe during "
                     "flate compression.");
        }
        flush = feof(ifP) ? Z_FINISH : Z_NO_FLUSH;
        strm.next_in = in;

        /* run deflate() on input until output buffer not full, finish
           compression if we have reached end of input.
        */
        do {
            unsigned int have;

            strm.avail_out = chunkSz;
            strm.next_out = out;
            deflate(&strm, flush);
            have = chunkSz - strm.avail_out;
            writeFile(out, have, "flate filter", ofP);
        } while (strm.avail_out == 0);
        assert(strm.avail_in == 0);     /* all input is used */

        /* done when last data in file processed */
    } while (flush != Z_FINISH);

    free(in);
    free(out); 
    deflateEnd(&strm);
    fclose(ifP);
    fclose(ofP);
#else
    assert(false);    /* filter is never used */ 
#endif
}



/* Run length encoding

   In this simple run-length encoding scheme, compressed and uncompressed
   strings follow a single index byte N.  N 0-127 means the next N+1
   bytes are uncompressed; 129-255 means the next byte is to be repeated
   257-N times.

   In native (non-psfilter) mode, the run length filter must flush at
   the end of every row.  But the entire raster is sent to the run length
   filter as one continuous stream.  The run length filter learns the
   refresh interval from oeP->runlengthRefresh.  In ps-filter mode the
   run length filter ignores row boundaries and flushes every 4096 bytes.
*/

static FilterFn rleFilter;

static void
rleFilter (FILE *          const ifP,
           FILE *          const ofP,
           OutputEncoder * const oeP) {

    unsigned int const inSize = oeP->runlengthRefresh;

    bool eof;
    unsigned char * inbuf;
    unsigned char * outbuf;
    size_t outSize;

    MALLOCARRAY(inbuf, inSize);
    if (inbuf == NULL)
        pm_error("Failed to allocate %u bytes of memory for RLE filter",
                  inSize);
    pm_rlenc_allocoutbuf(&outbuf, inSize, PM_RLE_PACKBITS);

    for (eof = false; !eof; ) {
        size_t const bytesRead = fread(inbuf, 1, inSize, ifP);

        if (feof(ifP))
            eof = true;
        else if (ferror(ifP) || bytesRead == 0)
            pm_error("Internal read error: RLE compression");

        pm_rlenc_compressbyte(inbuf, outbuf, PM_RLE_PACKBITS,
                              bytesRead, &outSize);
        writeFile(outbuf, outSize, "rlePutBuffer", ofP);
    }

    fclose(ifP);
    fclose(ofP);
}



static FilterFn asciiHexFilter;

static void
asciiHexFilter(FILE *          const ifP,
               FILE *          const ofP,
               OutputEncoder * const oeP) {

    char const hexits[16] = "0123456789abcdef";

    bool eof;
    unsigned char inbuff[40], outbuff[81];

    for (eof = false; !eof; ) {
        size_t readCt;

        readCt = fread(inbuff, 1, 40, ifP);

        if (readCt == 0)
            eof = true;
        else {
            unsigned int i;

            for (i = 0; i < readCt; ++i) {
                int const item = inbuff[i]; 
                outbuff[i*2]   = hexits[item >> 4];
                outbuff[i*2+1] = hexits[item & 15];
            }
            outbuff[readCt * 2] = '\n';
            writeFile(outbuff, readCt * 2 + 1, "asciiHex filter", ofP);
        }
    }

    fclose(ifP);
    fclose(ofP);
}



static FilterFn ascii85Filter;

static void
ascii85Filter(FILE *          const ifP,
              FILE *          const ofP,
              OutputEncoder * const oeP) {

    bool eof;
    char outbuff[5];
    unsigned long int value; /* requires 32 bits */
    int count;
    int outcount;

    value = 0;  /* initial value */
    count = 0;  /* initial value */
    outcount = 0; /* initial value */

    for (eof = false; !eof; ) {
        int c;

        c = fgetc(ifP);

        if (c == EOF)
            eof = true;
        else {
            value = value*256 + c;
            ++count;

            if (value == 0 && count == 4) {
                writeFileChar("z", 1, "ASCII 85 filter", ofP);
                    /* Ascii85 encoding z exception */
                ++outcount;
                count = 0;
            } else if (count == 4) {
                outbuff[4] = value % 85 + 33;  value/=85; 
                outbuff[3] = value % 85 + 33;  value/=85;
                outbuff[2] = value % 85 + 33;  value/=85;
                outbuff[1] = value % 85 + 33;
                outbuff[0] = value / 85 + 33;

                writeFileChar(outbuff, count + 1, "ASCII 85 filter", ofP);

                count = value = 0;
                outcount += 5; 
            }

            if (outcount > 75) {
                writeFileChar("\n", 1, "ASCII 85 filter", ofP);
                outcount = 0;
            }
        }
    }

    if (count > 0) { /* EOF, flush */
        assert (count < 4);

        value <<= (4 - count) * 8;   value/=85;
        outbuff[3] = value % 85 + 33;  value/=85;
        outbuff[2] = value % 85 + 33;  value/=85;
        outbuff[1] = value % 85 + 33;
        outbuff[0] = value / 85 + 33;
        outbuff[count + 1] = '\n';

        writeFileChar(outbuff, count + 2, "ASCII 85 filter", ofP);
    }

    fclose(ifP);
    fclose(ofP);
}



static void
makePipe(int * const pipeFdArray) {

    int rc;
    rc = pm_pipe(pipeFdArray);
    if (rc == -1)
        pm_error("pipe() failed, errno = %d (%s)", errno, strerror(errno));
}



static void
closeAllBut(int const saveFd0,
            int const saveFd1,
            int const saveFd2) {
/*----------------------------------------------------------------------------
   Close every file descriptor in this process except 'saveFd0',
   'saveFd1', and 'saveFd2'.

   This is helpful because even if this process doesn't touch other file
   desriptors, its very existence will keep the files open.
-----------------------------------------------------------------------------*/
    
    /* Unix provides no good way to do this; we just assume file descriptors
       above 9 are not used in this program; Caller must ensure that is true.
    */
    int fd;

    for (fd = 0; fd < 10; ++fd) {
        if (fd != saveFd0 && fd != saveFd1 && fd != saveFd2)
            close(fd);
    }
}



static void
spawnFilter(FILE *          const ofP,
            FilterFn *      const filterFn,
            OutputEncoder * const oeP,
            FILE **         const feedFilePP,
            pid_t *         const pidP) {
/*----------------------------------------------------------------------------
   Fork a child process to run filter function 'filterFn' and send its
   output to *ofP.

   Create a pipe for feeding the filter and return as *feedFilePP the
   stream to which Caller can write to push stuff into the filter.

   *oeP is the parameter to 'filterFn'.
-----------------------------------------------------------------------------*/
    int pipeFd[2];
    pid_t rc;

    makePipe(pipeFd);
    
    rc = fork();

    if (rc == (pid_t)-1)
        pm_error("fork() of filter process failed.  errno=%d (%s)", 
                 errno, strerror(errno));
    else if (rc == 0) {
        /* This is the child process */
 
        FILE * ifP;

        ifP = fdopen(pipeFd[0], "r");

        if (!ifP)
            pm_error("filter process failed to make "
                     "file stream (\"FILE\") "
                     "out of the file descriptor which is input to the "
                     "filter.  errno=%d (%s)",
                     errno, strerror(errno));

        closeAllBut(fileno(ifP), fileno(ofP), STDERR_FILENO);

        filterFn(ifP, ofP, oeP);

        exit(EXIT_SUCCESS);
    } else {
        /* This is the parent process */

        pid_t const childPid = rc;

        close(pipeFd[0]);

        *feedFilePP = fdopen(pipeFd[1], "w");

        *pidP = childPid;
    }
}



static void
addFilter(const char *    const description,
          FilterFn *      const filter,
          OutputEncoder * const oeP,
          FILE **         const feedFilePP,
          pid_t *         const pidList) {
/*----------------------------------------------------------------------------
   Add a filter to the front of the chain.

   Spawn a process to do the filtering, by running function 'filter'.

   *feedFilePP is the present head of the chain.  We make the new filter
   process write its output to that and get its input from a new pipe.
   We update *feedFilePP to the sending end of the new pipe.

   Add to the list pidList[] the PID of the process we spawn.
-----------------------------------------------------------------------------*/
    FILE * const oldFeedFileP = *feedFilePP;

    FILE * newFeedFileP;
    pid_t pid;

    spawnFilter(oldFeedFileP, filter, oeP, &newFeedFileP, &pid);
            
    if (verbose)
        pm_message("%s filter spawned: pid %u",
                   description, (unsigned)pid);
    
    if (debug) {
        int const outFd    = fileno(oldFeedFileP);
        int const supplyFd = fileno(newFeedFileP);
        pm_message("PID %u writes to FD %u, its supplier writes to FD %u",
                   (unsigned)pid, outFd, supplyFd);
    }
    fclose(oldFeedFileP);  /* Child keeps this open now */

    addToPidList(pidList, pid);

    *feedFilePP = newFeedFileP;
}



static void
spawnFilters(FILE *          const ofP,
             OutputEncoder * const oeP,
             FILE **         const feedFilePP,
             pid_t *         const pidList) {
/*----------------------------------------------------------------------------
   Get all the child processes for the filters running and connected.
   Return at *feedFileP the file stream to which to write the raw data,
   with the filtered data going to *ofP.

   Filter according to *oeP.
-----------------------------------------------------------------------------*/

    /* Build up the pipeline from the final to the initial stage.  The
       result is one of:

          FEED | convertRow | asciiHexFilter | *ofP
          FEED | convertRow | ascii85Filter | *ofP
          FEED | convertRow | rleFilter   | asciiHexFilter | *ofP
          FEED | convertRow | flateFilter | asciiHexFilter | *ofP
          FEED | convertRow | flateFilter | rleFilter | asciiHexFilter | *ofP
    */

    FILE * feedFileP;
        /* The current head of the filter chain; changes as we add filters */

    initPidList(pidList);

    feedFileP = ofP;  /* Initial state: no filter at all */

    addFilter(
        "output",
        oeP->outputType == Ascii85 ? &ascii85Filter : asciiHexFilter,
        oeP,
        &feedFileP,
        pidList);

    if (oeP->compressFlate)
        addFilter("flate", flateFilter, oeP, &feedFileP, pidList);

    if (oeP->compressRle)
        addFilter("rle", rleFilter, oeP, &feedFileP, pidList);

    *feedFilePP = feedFileP;
}



static void
waitForChildren(const pid_t * const pidList) {
/*----------------------------------------------------------------------------
   Wait for all child processes with PIDs in pidList[] to exit.
   In pidList[], end-of-list is marked with a special zero value.
-----------------------------------------------------------------------------*/
    /* There's an odd behavior in Unix such that if you have set the
       action for SIGCHLD to ignore the signal (even though ignoring the
       signal is the default), the process' children do not become
       zombies.  Consequently, waitpid() always fails with ECHILD - but
       nonetheless waits for the child to exit.
    
       We expect the process not to have the action for SIGCHLD set that
       way.
    */
    unsigned int i;

    for (i = 0; pidList[i]; ++i) {
        pid_t rc;
        int status;

        if (verbose)
            pm_message("Waiting for PID %u to exit", (unsigned)pidList[i]);

        rc = waitpid(pidList[i], &status, 0);
        if (rc == -1)
            pm_error ("waitpid() for child %u failed, errno=%d (%s)",
                      i, errno, strerror(errno));
        else if (status != EXIT_SUCCESS)
            pm_error ("Child process %u terminated abnormally", i);
    }
    if (verbose)
        pm_message("All children have exited");
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
warnUserRescaling(float const scale) {

    const char * const baseMsg = "warning, image too large for page";

    if (pm_have_float_format())
        pm_message("%s; rescaling to %g", baseMsg, scale);
    else
        pm_message("%s; rescaling", baseMsg);
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

  *llxP, *llyP are the coordinates in the Postcript frame, of the lower left
  corner of the image on the page.  The Postscript frame is different from the
  Neptbm frame: units are 1/72 inch (1 point) and (0,0) is the lower left
  corner.

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
            warnUserRescaling(scale);

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
                   "bottom edge %3.2f points from bottom of page; "
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
    
    /* We say to decode flate, then rle, so Caller must ensure it encodes
       rel, then flate.
    */

    if (ascii85)
        printf("/ASCII85Decode filter ");
    else 
        printf("/ASCIIHexDecode filter ");
    if (flate)
        printf("/FlateDecode filter ");
    if (rle) 
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
    unsigned int consumed;
} BitAccumulator;



static void
ba_init(BitAccumulator * const baP) {

    baP->value    = 0;
    baP->consumed = 0;
}



static void
ba_add12(BitAccumulator * const baP,
         unsigned int     const new12,
         FILE           * const fP) {
/*----------------------------------------------------------------------------
  Read a 12-bit string into the bit accumulator baP->value.
  On every other call, combine two 12-bit strings and write out three bytes.
-----------------------------------------------------------------------------*/
    assert (baP->consumed == 12 || baP->consumed == 0);

    if (baP->consumed == 12){
        char const oldHi8 = (baP->value) >> 4;
        char const oldLo4 = (baP->value) & 0x0f;
        char const newHi4 = new12 >> 8;
        char const newLo8 = new12 & 0xff;

        fputc(oldHi8, fP);
        fputc((oldLo4 << 4) | newHi4 , fP);
        fputc(newLo8, fP);
        baP->value = 0; baP->consumed = 0;
    } else {
        baP->value = new12;  baP->consumed = 12;
    }
}



static void
ba_add(BitAccumulator * const baP,
       unsigned int     const b,
       unsigned int     const bitsPerSample,
       FILE           * const fP) {
/*----------------------------------------------------------------------------
  Combine bit sequences that do not fit into a byte.

  Used when bitsPerSample =1, 2, 4.  
  Logic also works for bitsPerSample = 8, 16.

  The accumulator, baP->value is unsigned int (usually 32 bits), but
  only 8 bits are used.
-----------------------------------------------------------------------------*/
    unsigned int const bufSize = 8;

    assert (bitsPerSample == 1 || bitsPerSample == 2 || bitsPerSample == 4);

    baP->value = (baP->value << bitsPerSample) | b ;
    baP->consumed += bitsPerSample;
    if (baP->consumed == bufSize) {
        /* flush */
        fputc( baP->value, fP);
        baP->value = 0;
        baP->consumed = 0;
    }
}



static void
ba_flush(BitAccumulator * const baP,
         FILE *           const fP) {
/*----------------------------------------------------------------------------
  Flush partial bits in baP->consumed.
-----------------------------------------------------------------------------*/
    if (baP->consumed == 12) {
        char const oldHi8 = (baP->value) >> 4;
        char const oldLo4 = (baP->value) & 0x0f;
        fputc(oldHi8, fP);
        fputc(oldLo4 << 4, fP);
    } else if (baP->consumed == 8)
        fputc(baP->value , fP);
    else if (baP->consumed > 0) {
        unsigned int const leftShift = 8 - baP->consumed;
        assert(baP->consumed <= 8);  /* why? */
        baP->value <<= leftShift;
        fputc(baP->value , fP);
    }
    baP->value = 0;
    baP->consumed = 0;
}



static void
outputSample(BitAccumulator * const baP,
             unsigned int     const sampleValue,
             unsigned int     const bitsPerSample,
             FILE           * const fP) {

    if (bitsPerSample == 8)
        fputc(sampleValue, fP);
    else if (bitsPerSample == 12)
        ba_add12(baP, sampleValue, fP);
    else
        ba_add(baP, sampleValue, bitsPerSample, fP);
}



static void
flushOutput(BitAccumulator * const baP,
            FILE *           const fP) {
    ba_flush(baP, fP);
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
              FILE *           const fP) {
/*---------------------------------------------------------------------
  Feed PBM raster data directly to the output encoder.
  Invert bits: 0 is "white" in PBM, 0 is "black" in postscript.
----------------------------------------------------------------------*/
    unsigned int colChar;
    unsigned int const colChars = pbm_packed_bytes(pamP->width);

    pbm_readpbmrow_packed(pamP->file, bitrow, pamP->width, pamP->format);

    for (colChar = 0; colChar < colChars; ++colChar)
        bitrow[colChar] =  ~ bitrow[colChar];

    /* Zero clear padding beyond right edge */
    pbm_cleanrowend_packed(bitrow, pamP->width);
    writeFile(bitrow, colChars, "PBM reader", fP);
}



static void
convertRowNative(struct pam *     const pamP, 
                 tuple *                tuplerow, 
                 unsigned int     const bitsPerSample,
                 FILE           * const fP) { 

    unsigned int const psMaxval = pm_bitstomaxval(bitsPerSample);

    unsigned int plane;
    BitAccumulator ba;

    ba_init(&ba);

    pnm_readpamrow(pamP, tuplerow);
    pnm_scaletuplerow(pamP, tuplerow, tuplerow, psMaxval);

    for (plane = 0; plane < pamP->depth; ++plane) {
        unsigned int col;
        for (col= 0; col < pamP->width; ++col)
            outputSample(&ba, tuplerow[col][plane], bitsPerSample, fP);

        flushOutput(&ba, fP);
    }
}



static void
convertRowPsFilter(struct pam *     const pamP,
                   tuple *                tuplerow,
                   unsigned int     const bitsPerSample,
                   FILE           * const fP) { 

    unsigned int const psMaxval = pm_bitstomaxval(bitsPerSample);

    unsigned int col;
    BitAccumulator ba;

    ba_init(&ba);

    pnm_readpamrow(pamP, tuplerow);
    pnm_scaletuplerow(pamP, tuplerow, tuplerow, psMaxval);

    for (col = 0; col < pamP->width; ++col) {
        unsigned int plane;
        for (plane = 0; plane < pamP->depth; ++plane)
            outputSample(&ba, tuplerow[col][plane], bitsPerSample, fP);
    }
    flushOutput(&ba, fP);

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
convertRaster(struct pam * const inpamP,
              unsigned int const bitsPerSample,
              bool         const psFilter,
              FILE *       const fP) {
/*----------------------------------------------------------------------------
   Read the raster described by *inpamP, and write a bit stream of samples
   to *fP.  This stream has to be compressed and converted to text before it
   can be part of a Postscript program.
   
   'psFilter' means to do the conversion using built in Postscript filters, as
   opposed to our own filters via /readstring.

   'bitsPerSample' is how many bits each sample is to take in the Postscript
   output.
-----------------------------------------------------------------------------*/
    if (PAM_FORMAT_TYPE(inpamP->format) == PBM_TYPE && bitsPerSample == 1)  {
        unsigned char * bitrow;
        unsigned int row;

        bitrow = pbm_allocrow_packed(inpamP->width);

        for (row = 0; row < inpamP->height; ++row)
            convertRowPbm(inpamP, bitrow, psFilter, fP);

        pbm_freerow(bitrow);
    } else  {
        tuple *tuplerow;
        unsigned int row;
        
        tuplerow = pnm_allocpamrow(inpamP);

        for (row = 0; row < inpamP->height; ++row) {
            if (psFilter)
                convertRowPsFilter(inpamP, tuplerow, bitsPerSample, fP);
            else
                convertRowNative(inpamP, tuplerow, bitsPerSample, fP);
        }
        pnm_freepamrow(tuplerow);
    }
}



/* FILE MANAGEMENT: File management is pretty hairy here.  A filter, which
   runs in its own process, needs to be able to cause its output file to
   close because it might be an internal pipe and the next stage needs to
   know output is done.  So the forking process must close its copy of the
   file descriptor.  BUT: if the output of the filter is not an internal
   pipe but this program's output, then we don't want it closed when the
   filter terminates because we'll need it to be open for the next image
   the program converts (with a whole new chain of filters).
   
   To prevent the progam output file from getting closed, we pass a
   duplicate of it to spawnFilters() and keep the original open.
*/



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
            unsigned int const levelGiven) {
    
    struct pam inpam;
    float scols, srows;
    float llx, lly;
    bool turned;
    bool color;
    unsigned int postscriptLevel;
    unsigned int bitsPerSample;
    unsigned int dictSize;  
        /* Size of Postscript dictionary we should define */
    OutputEncoder oe;
    pid_t filterPidList[MAX_FILTER_CT + 1];

    FILE * feedFileP;
        /* The file stream which is the head of the filter chain; we write to
           this and filtered stuff comes out the other end.
        */
    FILE * filterChainOfP;

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

    initOutputEncoder(&oe, inpam.width, bitsPerSample,
                      rle, flate, ascii85, psFilter);

    fflush(stdout);
    filterChainOfP = fdopen(dup(fileno(stdout)), "w");
        /* spawnFilters() closes this.  See FILE MANAGEMENT above */

    spawnFilters(filterChainOfP, &oe, &feedFileP, filterPidList);
 
    convertRaster(&inpam, bitsPerSample, psFilter, feedFileP);

    fflush(feedFileP);
    fclose(feedFileP);

    waitForChildren(filterPidList);

    putEnd(showpage, psFilter, ascii85, dictSize, vmreclaim);
}



int
main(int argc, const char * argv[]) {

    FILE * ifP;
    const char * name;  /* malloc'ed */
    struct cmdlineInfo cmdline;

    pm_proginit(&argc, argv);

    setSignals();

    parseCommandLine(argc, argv, &cmdline);

    verbose = cmdline.verbose || cmdline.debug;
    debug   = cmdline.debug;

    if (cmdline.flate && !progIsFlateCapable())
        pm_error("This program cannot do flate compression.  "
                 "(There are other versions of the program that do, "
                 "though -- it's a build-time option");

    ifP = pm_openr(cmdline.inputFileName);

    if (streq(cmdline.inputFileName, "-"))
        name = strdup("noname");
    else
        name = basebasename(cmdline.inputFileName);

    /* This program manages file descriptors in a way that assumes
       that new files will get file descriptor numbers less than 10,
       so we close superfluous files now to make sure that's true.
    */
    closeAllBut(fileno(ifP), fileno(stdout), fileno(stderr));

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
