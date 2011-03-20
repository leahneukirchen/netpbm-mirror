/*
 * -------------------------------------------------------------
 *
 *  (C) 2002 Jochen Karrer, Linuxdata GbR
 *
 *      convert a pnm to PCL-XL image 
 *
 * -------------------------------------------------------------
 */

/* Engineering note: One PCL-XL printer prints an error message like
   this when it doesn't like the PCL it sees:

   PCL XL error
      Subsystem:  IMAGE
      Error:      IllegalAttributeValue
      Operator:   ReadImage
      Position:   8

   "Position" is the sequence number of the PCL operator it was trying
   to execute.
*/

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <ctype.h>

#include "pm_c_util.h"
#include "pam.h"
#include "shhopt.h"
#include "mallocvar.h"
#include "nstring.h"

#include "pclxl.h"

#define PAPERWIDTH(format) (xlPaperFormats[format].width)
#define PAPERHEIGHT(format) (xlPaperFormats[format].height)



typedef struct InputSource {
    const char *         name; 
    struct InputSource * next;
} InputSource;



struct cmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    InputSource * sourceP;
    int dpi;
    enum MediaSize format;
    unsigned int feederSpec;
    int feeder;
    unsigned int outtraySpec;
    int outtray;
    unsigned int duplexSpec;
    enum DuplexPageMode duplex;
    unsigned int copiesSpec;
    int copies;
    unsigned int center;
    float xoffs;
    float yoffs;
    unsigned int colorok;
    unsigned int verbose;
    const char * jobsetup;  /* -jobsetup option value.  NULL if none */
    unsigned int rendergray;
};



static void
parseCommandLine(int argc, char ** argv,
                 struct cmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   parse program command line described in Unix standard form by argc
   and argv.  Return the information in the options as *cmdlineP.  

   If command line is internally inconsistent (invalid options, etc.),
   issue error message to stderr and abort program.

   Note that the strings we return are stored in the storage that
   was passed to us as the argv array.  We also trash *argv.
-----------------------------------------------------------------------------*/
    optEntry *option_def = malloc( 100*sizeof( optEntry ) );
        /* Instructions to pm_optParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int option_def_index;

    char *formatOpt;
    char *duplexOpt;
    unsigned int dpiSpec, xoffsSpec, yoffsSpec, formatSpec, jobsetupSpec;

    option_def_index = 0;   /* incremented by OPTENT3 */
    OPTENT3(0, "dpi",       OPT_UINT,    &cmdlineP->dpi,
            &dpiSpec,         0);
    OPTENT3(0, "xoffs",     OPT_FLOAT,   &cmdlineP->xoffs, 
            &xoffsSpec,        0);
    OPTENT3(0, "yoffs",     OPT_FLOAT,   &cmdlineP->yoffs, 
            &yoffsSpec,        0);
    OPTENT3(0, "format",    OPT_STRING,  &formatOpt, 
            &formatSpec,        0);
    OPTENT3(0, "duplex",    OPT_STRING,  &duplexOpt, 
            &cmdlineP->duplexSpec,        0);
    OPTENT3(0, "copies",    OPT_UINT,    &cmdlineP->copies,
            &cmdlineP->copiesSpec,        0);
    OPTENT3(0, "colorok",   OPT_FLAG,    NULL,                  
            &cmdlineP->colorok, 0);
    OPTENT3(0, "center",    OPT_FLAG,    NULL,                  
            &cmdlineP->center, 0 );
    OPTENT3(0, "feeder",    OPT_UINT,    &cmdlineP->feeder,
            &cmdlineP->feederSpec,        0);
    OPTENT3(0, "outtray",   OPT_UINT,    &cmdlineP->outtray,
            &cmdlineP->outtraySpec,       0);
    OPTENT3(0, "verbose",   OPT_FLAG,    NULL,                  
            &cmdlineP->verbose, 0);
    OPTENT3(0, "jobsetup",  OPT_STRING,  &cmdlineP->jobsetup,
            &jobsetupSpec,      0);
    OPTENT3(0, "rendergray", OPT_FLAG,    NULL,
            &cmdlineP->rendergray, 0 );

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3( &argc, argv, opt, sizeof(opt), 0);
        /* Uses and sets argc, argv, and some of *cmdlineP and others. */

    if (!dpiSpec)
        cmdlineP->dpi = 300;
    if (!xoffsSpec)
        cmdlineP->xoffs = 0.0;
    if (!yoffsSpec)
        cmdlineP->yoffs = 0.0;

    if (cmdlineP->duplexSpec) {
        if (strncmp(duplexOpt, "vertical", strlen(duplexOpt)) == 0)
            cmdlineP->duplex = eDuplexVerticalBinding;
        else if (strncmp(duplexOpt, "horizontal", strlen(duplexOpt)) == 0)
            cmdlineP->duplex = eDuplexHorizontalBinding;
        else
            pm_error("Invalid value '%s' for -duplex option", duplexOpt);
    }

    if (formatSpec) {
        bool found;
        int i;
        for (i = 0, found=FALSE; xlPaperFormats[i].name && !found; ++i) {
            if (streq(xlPaperFormats[i].name, formatOpt)) {
                found = TRUE;
                cmdlineP->format = xlPaperFormats[i].xl_nr;
            }
        }
        if (!found) {
            int i;
            pm_message("Valid -format values:");
            for (i = 0; xlPaperFormats[i].name; ++i) {
                if (xlPaperFormats[i].width > 0)
                    pm_message("   %s", xlPaperFormats[i].name);
            }
            pm_error("Invalid -format option '%s' specified.", formatOpt);
        }
    } else
        cmdlineP->format = eLetterPaper;

    if (!jobsetupSpec)
        cmdlineP->jobsetup = NULL;

    if (argc-1 < 1) {
        MALLOCVAR(cmdlineP->sourceP);
        cmdlineP->sourceP->name = "-";
        cmdlineP->sourceP->next = NULL;
    } else {
        unsigned int i;
        InputSource ** nextLinkP;

        nextLinkP = &cmdlineP->sourceP;
        for (i = 1; i < argc; ++i) {
            InputSource * sourceP;
            MALLOCVAR(sourceP);
            sourceP->name = argv[i];
            *nextLinkP = sourceP;
            nextLinkP = &sourceP->next;
            *nextLinkP = NULL;
        }
    }
}



#define XY_RLE_FBUFSIZE (1024)
typedef struct XY_rle {
    int error;
    unsigned char buf[128];
    int bpos;
    int state;  
    unsigned char *fbuf;
    int fbpos;
    int fbufsize;
    int fd;
} XY_rle;



static void 
XY_RLEreset(XY_rle * const rleP)  {   

    rleP->state = eSTART;
    rleP->bpos  = 0;
    rleP->fbpos = 0;
    rleP->error = 0;
}



static XY_rle * 
XY_RLEnew(size_t const size) {

    XY_rle * retval;
    XY_rle * rleP;

    MALLOCVAR(rleP);
    if (rleP) {
        rleP->fbuf = malloc(size);

        if (rleP->fbuf) {
            rleP->fbufsize = MAX(1024, size);
            retval = rleP;
        } else
            retval = NULL;

        if (retval == NULL)
            free(rleP);
    } else
        retval = NULL;

    return retval;
}



static void
XY_RLEdelete(XY_rle * const rleP) {

    free(rleP->fbuf);
    free(rleP);
}



static int 
out(XY_rle * const rleP,
    int      const count) {

    bool error;

    if (rleP->state == eRLE) {
        rleP->fbuf[rleP->fbpos++] = -count + 1;
        rleP->fbuf[rleP->fbpos++] = rleP->buf[0];
    } else if (rleP->bpos > 0) {
        rleP->fbuf[rleP->fbpos++] = count - 1;
        memcpy(rleP->fbuf + rleP->fbpos, rleP->buf, count);
        rleP->fbpos += count;
    }
    if (rleP->fbpos + 129 > rleP->fbufsize) {
        rleP->fbufsize *= 1.2; 
        rleP->fbuf = realloc(rleP->fbuf, rleP->fbufsize);
        if (rleP->fbuf == NULL) {
            rleP->error = -1;
            rleP->fbpos = 0;
            error = true;
        } else
            error = false;
    } else
        error = false;

    rleP->bpos = 0;
    rleP->state = eSTART;

    return error ? -1 : 0;
}



static int
XY_RLEfinish (XY_rle *rle) {
    out(rle,rle->bpos);
    if(rle->error<0) 
        return rle->error;
    else
        return rle->fbpos;
}



static  void
rle_putbyte(XY_rle *      const rleP,
            unsigned char const u) {

    switch (rleP->state) {
        case eRLE:
            if (u != rleP->buf[0])
                out(rleP, rleP->bpos);
            break;
        case eLIT:
            if (u == rleP->buf[rleP->bpos - 1]
                && u == rleP->buf[rleP->bpos - 2]) {
                out(rleP,rleP->bpos - 2);
                rleP->buf[0] = u;
                rleP->bpos += 2;
                rleP->state = eRLE;
            }   
            break;
        case eSTART:
            if (rleP->bpos == 1) {
                if (u == rleP->buf[rleP->bpos - 1])
                    rleP->state = eRLE;
                else
                    rleP->state = eLIT;
            }
            break;
    }
    rleP->buf[rleP->bpos++] = u;
    if (rleP->bpos == 128) {
        out(rleP, rleP->bpos);
    }
}



static void
XY_RLEput(XY_rle *rle,const unsigned char buf[],int count) 
{
    int i;
    for(i=0;i<count;i++) {
        rle_putbyte(rle,buf[i]);
    }
    
}


static int
XY_Write(int          const fd,
         const void * const buf,
         int          const cnt) {

    int len;
    bool error;

    for (len =0, error = false; len < cnt && !error;) {
        ssize_t const rc = write(fd, (char*)buf + len , cnt - len);
        if (rc <= 0)
            error = true;
        else
            len += rc;
    }
    return error ? -1 : len;
}



#define XY_Puts(fd, str)  XY_Write(fd, str, strlen(str))



typedef struct pclGenerator {
    enum ColorDepth colorDepth;
    enum Colorspace colorSpace;
    int width,height;
    int linelen; /* bytes per line */
    unsigned char *data;
    void (*getnextrow)(const struct pclGenerator *, struct pam *);
} pclGenerator;



struct tPrinter { 
    const char *name;
    float topmargin;
    float bottommargin;
    float leftmargin;
    float rightmargin;
} xlPrinters[] = {
    { "lj2200",0,0,0,0 }
};



static int
out_ubyte(int           const fd,
          unsigned char const data) {

    return XY_Write(fd, &data, 1);
}



static int 
XL_Operator(int           const fd,
            enum Operator const data)  {

    return out_ubyte(fd, data);
}



static int
out_uint16(int            const fd,
           unsigned short const data ) {

    unsigned char c[2];

    c[0] = data & 0xff;
    c[1] = data >>8;

    return XY_Write(fd, c , ARRAY_SIZE(c));
}



static int
out_uint32(int fd,unsigned int data ) {
    unsigned char c[4];
    c[0] = data&0xff; c[1]=(data>>8)&0xff; c[2]=(data>>16)&0xff; c[3]=data>>24;
    return XY_Write(fd,c,4);
}



static int
out_sint16(int          const fd,
           signed short const sdata ) {

    unsigned short const data= (unsigned short)sdata;    

    unsigned char c[2];

    c[0] = data & 0xff;
    c[1] = data >> 8;

    return XY_Write(fd, c, ARRAY_SIZE(c));
}



static int
xl_ubyte(int           const fd,
         unsigned char const data) {

    unsigned char const tag = 0xc0;

    XY_Write(fd, &tag, 1);

    return out_ubyte(fd, data);
}



static int
xl_uint16(int            const fd,
          unsigned short const data) {

    unsigned char const tag = 0xc1;

    XY_Write(fd, &tag, 1);

    return out_uint16(fd, data);
}



static int
xl_ubyte_array(int                   const fd,
               const unsigned char * const data,
               int                   const len) {

    unsigned int i;
    unsigned char head[4];
    
    head[0] = 0xc8;
    head[1] = 0xc1;
    head[2] = len & 0xff;
    head[3] = (len >> 8) & 0xff;

    XY_Write(fd, head, ARRAY_SIZE(head));

    for (i = 0; i < len; ++i)
        out_ubyte(fd, data[i]);

    return 0;
}



static int
xl_uint16_xy(int            const fd,
             unsigned short const xdata,
             unsigned short const ydata ) {

    unsigned char const tag = 0xd1;

    XY_Write(fd, &tag, 1);
    out_uint16(fd, xdata);

    return out_uint16(fd, ydata);
}



static int
xl_sint16_xy(int          const fd,
             signed short const xdata,
             signed short const ydata ) {

    unsigned char const tag = 0xd3;

    XY_Write(fd, &tag, 1);

    out_sint16(fd, xdata);

    return out_sint16(fd, ydata);
}



static int
xl_attr_ubyte(int            const fd,
              enum Attribute const data) {

    unsigned char const tag = 0xf8;

    XY_Write(fd, &tag, 1);

    return out_ubyte(fd, data);
}



static int
xl_dataLength(int          const fd,
              unsigned int const dataLength ) {

    unsigned char const tag = 0xfa;

    XY_Write(fd, &tag, 1);

    return out_uint32(fd, dataLength);
}



static void
copyFile(const char * const sourceFileName,
         int          const destFd) {

    FILE * sourceFileP;

    sourceFileP = pm_openr(sourceFileName);

    while (!feof(sourceFileP)) {
        char buffer[1024];
        size_t bytesRead;
        size_t totalBytesWritten;

        bytesRead = fread(buffer, 1, sizeof(buffer), sourceFileP);

        if (ferror(sourceFileP))
            pm_error("Read from file failed.  errno=%d (%s)",
                     errno, strerror(errno));
        
        totalBytesWritten = 0;
        
        while (totalBytesWritten < bytesRead) {
            ssize_t rc;

            rc = write(destFd, buffer, bytesRead);

            if (rc < 0)
                pm_error("Write to file failed. errno=%d (%s)",
                         errno, strerror(errno));
            else
                totalBytesWritten += rc;
        }
    }
    pm_close(sourceFileP);
}



static void
jobHead(int          const outFd,
        bool         const renderGray,
        const char * const userJobSetupFileName) {
/*----------------------------------------------------------------------------
   Start a PJL job.

   Switch printer to PCL-XL mode.  This is called "entering a printer
   language" in PCL terms.  In particular, we enter the PCL-XL language,
   as opposed to e.g. Postscript.
-----------------------------------------------------------------------------*/
    /* Reset */
    XY_Puts(outFd,"\033%-12345X");  

    if (userJobSetupFileName)
        copyFile(userJobSetupFileName, outFd);

    if (renderGray)
        XY_Puts(outFd, "@PJL SET RENDERMODE=GRAYSCALE\n");  

    XY_Puts(outFd, "@PJL ENTER LANGUAGE=PCLXL\n");  
    XY_Puts(outFd, ") HP-PCL XL;1;1;Generated by Netpbm Pnmtopclxl\n");  
}



static void
jobEnd(int const outFd) {
/*----------------------------------------------------------------------------
   End a PJL job.

   Reset printer to quiescent mode.  Exit the printer language.
-----------------------------------------------------------------------------*/
    XY_Puts(outFd,"\033%-12345X");  
}



static void
beginPage(int                 const outFd,
          bool                const doDuplex,
          enum DuplexPageMode const duplex,
          bool                const doMediaSource,
          int                 const mediaSource,
          bool                const doMediaDestination,
          int                 const mediaDestination,
          enum MediaSize      const format) {
/*----------------------------------------------------------------------------
   Emit a BeginPage printer command.
-----------------------------------------------------------------------------*/
    if (doDuplex) {
        xl_ubyte(outFd, duplex);  xl_attr_ubyte(outFd, aDuplexPageMode);
    }

    if (doMediaSource) {
        /* if not included same as last time in same session is selected */
        xl_ubyte(outFd, mediaSource);  xl_attr_ubyte(outFd, aMediaSource);
    }

    if (doMediaDestination) {
        xl_ubyte(outFd, mediaDestination);  
        xl_attr_ubyte(outFd, aMediaDestination);
    }

    xl_ubyte(outFd, ePortraitOrientation); xl_attr_ubyte(outFd, aOrientation);
    xl_ubyte(outFd, format); xl_attr_ubyte(outFd, aMediaSize);

    XL_Operator(outFd, oBeginPage);
}



static void
openDataSource(int             const outFd,
               enum DataOrg    const dataOrg,
               enum DataSource const dataSource) {

    xl_ubyte(outFd, dataOrg); xl_attr_ubyte(outFd,aDataOrg);
    xl_ubyte(outFd, dataSource); xl_attr_ubyte(outFd,aSourceType);
    XL_Operator(outFd,oOpenDataSource);
}



static void
setColorSpace(int                   const outFd,
              enum Colorspace       const colorSpace,
              const unsigned char * const palette,
              unsigned int          const paletteSize,
              enum ColorDepth       const paletteDepth) {
/*----------------------------------------------------------------------------
   Emit printer control to set the color space.

   'palette' == NULL means no palette (raster contains colors, not indexes
   into a palette).

   'paletteSize' is the number of bytes in the palette (undefined if
   'palette' is NULL)

   'paletteDepth' is the color depth of the entries in the palette.
   e8Bit means the palette contains 8 bit values.

   The palette is a "direct color" palette: A separate table for each
   color component (i.e. one table for grayscale; three tables for
   RGB).  Each table is indexed by a value from the raster and yields
   a byte of color component value.

   The palette has to be the right size to fit the number of color
   components and raster color depth (bits per component in the raster).

   E.g. with raster color depth of 1 bit (e1Bit) and RGB color (eRGB),
   'paletteSize' would have to be 6 -- a table for each of R, G, and
   B, of two elements each.

   It is not clear from the documentation what the situation is when
   paletteDepth is not e8Bit.  Is each palette entry still a byte and only
   some of the byte gets used?  Or are there multiple entries per byte?
-----------------------------------------------------------------------------*/
    xl_ubyte(outFd, colorSpace); xl_attr_ubyte(outFd, aColorSpace);   
    if (palette) {
        xl_ubyte(outFd, paletteDepth); 
        xl_attr_ubyte(outFd, aPaletteDepth);   
        xl_ubyte_array(outFd, palette, paletteSize); 
        xl_attr_ubyte(outFd, aPaletteData);
    }
    XL_Operator(outFd, oSetColorSpace);
}



static void
positionCursor(int            const outFd,
               bool           const center,
               float          const xoffs,
               float          const yoffs,
               int            const imageWidth,
               int            const imageHeight,
               int            const dpi,
               enum MediaSize const format) {
/*----------------------------------------------------------------------------
   Emit printer control to position the cursor to start the page.
-----------------------------------------------------------------------------*/
    float xpos, ypos;

    if (center) {
        float const width  = 1.0 * imageWidth/dpi;  
        float const height = 1.0 * imageHeight/dpi;    
        xpos = (PAPERWIDTH(format) - width)/2;
        ypos = (PAPERHEIGHT(format) - height)/2;
    } else {
        xpos = xoffs;
        ypos = yoffs;
    }
    /* cursor positioning */
    xl_sint16_xy(outFd, xpos * dpi, ypos * dpi); xl_attr_ubyte(outFd, aPoint);
    XL_Operator(outFd, oSetCursor);
}



static void
convertAndWriteRleBlock(int                  const outFd,
                        const pclGenerator * const pclGeneratorP,
                        struct pam *         const pamP,
                        int                  const firstLine,
                        int                  const nlines,
                        XY_rle *             const rle) {

    unsigned char const pad[4] = {0,0,0,0};
    unsigned int const paddedLinelen = ((pclGeneratorP->linelen+3)/4)*4;
    int rlelen;
    unsigned int line;
    
    XY_RLEreset(rle);

    for (line = firstLine; line < firstLine + nlines; ++line) {
        pclGeneratorP->getnextrow(pclGeneratorP, pamP);
        XY_RLEput(rle, pclGeneratorP->data, pclGeneratorP->linelen);
        XY_RLEput(rle, pad, paddedLinelen - pclGeneratorP->linelen);
    }
    rlelen = XY_RLEfinish(rle);
    if (rlelen<0) 
        pm_error("Error on Making rle");

    xl_dataLength(outFd, rlelen); 
    XY_Write(outFd, rle->fbuf, rlelen);
}



/*
 * ------------------------------------------------------------
 * XL_WriteImage
 *  Write a PCL-XL image to the datastream 
 * ------------------------------------------------------------
 */
static void 
convertAndWriteImage(int                  const outFd,
                     const pclGenerator * const pclGenP,
                     struct pam *         const pamP) {

    int blockStartLine;
    XY_rle * rle;

    xl_ubyte(outFd, eDirectPixel); xl_attr_ubyte(outFd, aColorMapping);
    xl_ubyte(outFd, pclGenP->colorDepth); xl_attr_ubyte(outFd, aColorDepth);
    xl_uint16(outFd, pclGenP->width); xl_attr_ubyte(outFd, aSourceWidth);  
    xl_uint16(outFd, pclGenP->height); xl_attr_ubyte(outFd, aSourceHeight);    
    xl_uint16_xy(outFd, pclGenP->width*1, pclGenP->height*1); 
    xl_attr_ubyte(outFd, aDestinationSize);   
    XL_Operator(outFd, oBeginImage);

    rle = XY_RLEnew(pclGenP->linelen*20);
    if (!rle) 
        pm_error("Unable to allocate %d bytes for the RLE buffer",
                 pclGenP->linelen * 20);

    blockStartLine = 0;
    while (blockStartLine < pclGenP->height) {
        unsigned int const blockHeight =
            MIN(20, pclGenP->height-blockStartLine);
        xl_uint16(outFd, blockStartLine); xl_attr_ubyte(outFd, aStartLine); 
        xl_uint16(outFd, blockHeight); xl_attr_ubyte(outFd, aBlockHeight);
        xl_ubyte(outFd, eRLECompression); xl_attr_ubyte(outFd, aCompressMode);
        /* In modern PCL-XL, we could use a PadBytesMultiple attribute
           here to avoid having to pad the data to a multiple of 4
           bytes.  But PCL-XL 1.1 didn't have PadBytesMultiple.
           xl_ubyte(outFd, 1); xl_attr_ubyte(outFd, aPadBytesMultiple); 
        */
        XL_Operator(outFd, oReadImage);
        convertAndWriteRleBlock(outFd, pclGenP, pamP,
                                blockStartLine, blockHeight, rle);
        blockStartLine += blockHeight;
    }
    XY_RLEdelete(rle);
    XL_Operator(outFd, oEndImage);
}



static void
endPage(int          const outFd,
        bool         const doCopies,
        unsigned int const copies) {
/*----------------------------------------------------------------------------
   Emit an EndPage printer command.
-----------------------------------------------------------------------------*/
    if (doCopies) {
        /* wrong in example in PCL-XL manual. Type is uint16 ! */
        xl_uint16(outFd, copies); xl_attr_ubyte(outFd, aPageCopies);
    }
    XL_Operator(outFd, oEndPage);
}



static void
convertAndPrintPage(int                  const outFd,
                    const pclGenerator * const pclGeneratorP,
                    struct pam *         const pamP,
                    enum MediaSize       const format,
                    int                  const dpi,
                    bool                 const center,
                    float                const xoffs,
                    float                const yoffs,
                    bool                 const doDuplex,
                    enum DuplexPageMode  const duplex,
                    bool                 const doCopies,
                    unsigned int         const copies,
                    bool                 const doMediaSource,
                    int                  const mediaSource,
                    bool                 const doMediaDestination,
                    int                  const mediaDestination) {

    beginPage(outFd, doDuplex, duplex, doMediaSource, mediaSource,
              doMediaDestination, mediaDestination, format);

    /* Before Netpbm 10.27 (March 2005), we always set up a two-byte 8
       bit deep palette: {0, 255}.  I don't know why, because this
       works only for e1Bit color depth an eGray color space, and in
       that case does the same thing as having no palette at all.  But
       in other cases, it doesn't work.  E.g. with eRGB, e8Bit, we got
       an IllegalArraySize error from the printer on the SetColorSpace
       command.

       So we don't use a palette at all now.  
    */
    setColorSpace(outFd, pclGeneratorP->colorSpace, NULL, 0, 0);

    positionCursor(outFd, center, xoffs, yoffs, 
                   pclGeneratorP->width, pclGeneratorP->height, dpi, format);

    convertAndWriteImage(outFd, pclGeneratorP, pamP);

    endPage(outFd, doCopies, copies);
}



static void
beginSession(int              const outFd,
             unsigned int     const xdpi,
             unsigned int     const ydpi,
             enum Measure     const measure,
             bool             const noReporting,
             enum ErrorReport const errorReport) {

    xl_uint16_xy(outFd, xdpi, ydpi); xl_attr_ubyte(outFd, aUnitsPerMeasure); 
    xl_ubyte(outFd, measure);  xl_attr_ubyte(outFd, aMeasure);
    /* xl_ubyte(outFd,eNoReporting); xl_attr_ubyte(outFd,aErrorReport); */
    xl_ubyte(outFd,errorReport); xl_attr_ubyte(outFd,aErrorReport);
    XL_Operator(outFd,oBeginSession);
}


             
static void 
endSession(int outFd) {
    XL_Operator(outFd,oEndSession);
}



static void
pnmToPcllinePackbits(const pclGenerator * const pclGeneratorP,
                     struct pam *         const pamP) {

    tuple * tuplerow;
    unsigned int pcl_cursor;
    unsigned char accum;
    unsigned char bitmask;
    unsigned int col;
        
    tuplerow = pnm_allocpamrow(pamP);

    pnm_readpamrow(pamP, tuplerow);

    pcl_cursor = 0; bitmask = 0x80; accum = 0x00;
    for (col = 0; col < pamP->width; ++col) {
        if (tuplerow[col][0] == PAM_PBM_WHITE)
            accum |= bitmask;
        bitmask >>= 1;
        if (bitmask == 0) {
            pclGeneratorP->data[pcl_cursor++] = accum;
            bitmask = 0x80; accum = 0x0;
        } 
    }
    if (bitmask != 0x80)
        pclGeneratorP->data[pcl_cursor++] = accum;

    pnm_freepamrow(tuplerow);
}



static void
createPclGeneratorPackbits(struct pam *    const pamP,
                           pclGenerator ** const pclGeneratorPP) {

    /* Samples are black or white and packed 8 to a byte */

    pclGenerator * pclGeneratorP;

    MALLOCVAR_NOFAIL(pclGeneratorP);

    pclGeneratorP->colorDepth = e1Bit;
    pclGeneratorP->colorSpace = eGray;
    pclGeneratorP->linelen = (pamP->width+7)/8;
    pclGeneratorP->height = pamP->height;
    pclGeneratorP->width = (pamP->width);

    pclGeneratorP->data = malloc(pclGeneratorP->linelen);
    
    if (pclGeneratorP->data == NULL)
        pm_error("Unable to allocate row buffer.");

    pclGeneratorP->getnextrow = pnmToPcllinePackbits;

    *pclGeneratorPP = pclGeneratorP;
}



static void
pnmToPcllineWholebytes(const pclGenerator * const pclGeneratorP,
                       struct pam *         const pamP) {

    tuple * tuplerow;
    unsigned int pcl_cursor;
    unsigned int col;

    tuplerow = pnm_allocpamrow(pamP);

    pnm_readpamrow(pamP, tuplerow);

    pcl_cursor = 0; /* initial value */
    
    for (col = 0; col < pamP->width; ++col) {
        unsigned int plane;
        for (plane = 0; plane < pamP->depth; ++plane) {
            pclGeneratorP->data[pcl_cursor++] = 
                pnm_scalesample(tuplerow[col][plane], pamP->maxval, 255);
        }
    }
    pnm_freepamrow(tuplerow);
}



static void
createPclGeneratorWholebytes(struct pam *    const pamP,
                             pclGenerator ** const pclGenPP) {
    /* One sample per byte */

    pclGenerator * pclGenP;

    MALLOCVAR_NOFAIL(pclGenP);
    
    if (pamP->depth <  3)
        pclGenP->colorSpace = eGray;
    else
        pclGenP->colorSpace = eRGB;

    pclGenP->colorDepth = e8Bit;
    pclGenP->height     = pamP->height;
    pclGenP->width      = pamP->width;
    pclGenP->linelen    = pamP->width * pamP->depth;

    if (UINT_MAX / pamP->width < pamP->depth)
        pm_error("Image to big to process");
    else
        pclGenP->data = malloc(pamP->width * pamP->depth);

    if (pclGenP->data == NULL)
        pm_error("Unable to allocate row buffer.");
    
    pclGenP->getnextrow = pnmToPcllineWholebytes;

    *pclGenPP = pclGenP;
}



static void
destroyPclGenerator(pclGenerator * const pclGenP) {

    free(pclGenP->data);

    free(pclGenP);
}



static void 
createPclGenerator(struct pam *        const pamP,
                   pclGenerator **     const pclGeneratorPP,
                   bool                const colorok) {

    if (pamP->depth > 1 && !colorok)
        pm_message("WARNING: generating a color print stream because the "
                   "input image is PPM.  "
                   "To generate a black and white print stream, run the input "
                   "through Ppmtopgm.  To suppress this warning, use the "
                   "-colorok option.");

    if (pamP->depth == 1 && pamP->maxval == 1) 
        createPclGeneratorPackbits(pamP, pclGeneratorPP);
    else 
        createPclGeneratorWholebytes(pamP, pclGeneratorPP);
}




static void
printPages(int                 const outFd,
           InputSource *       const firstSourceP,
           enum MediaSize      const format,
           int                 const dpi,
           bool                const center,
           float               const xoffs,
           float               const yoffs,
           bool                const doDuplex,
           enum DuplexPageMode const duplex,
           bool                const doCopies,
           unsigned int        const copies,
           bool                const doMediaSource,
           int                 const mediaSource,
           bool                const doMediaDestination,
           int                 const mediaDestination,
           bool                const colorok) {
/*----------------------------------------------------------------------------
  Loop over all input files, and each file, all images.
-----------------------------------------------------------------------------*/
    InputSource * sourceP;
    unsigned int sourceNum;

    sourceP = firstSourceP;    

    openDataSource(outFd, eBinaryLowByteFirst, eDefaultSource);

    sourceNum = 0;   /* initial value */

    while (sourceP) {
        FILE * in_file;
        struct pam pam;
        bool eof;
        unsigned int pageNum;

        in_file = pm_openr(sourceP->name);

        ++sourceNum;

        pageNum = 0;  /* initial value */

        eof = FALSE;
        while(!eof) {
            pnm_nextimage(in_file, &eof);
            if (!eof) {
                pclGenerator * pclGeneratorP;

                ++pageNum;
                pm_message("Processing File %u, Page %u", sourceNum, pageNum);

                pnm_readpaminit(in_file, &pam, PAM_STRUCT_SIZE(tuple_type));
                
                createPclGenerator(&pam, &pclGeneratorP, colorok);
                
                convertAndPrintPage(
                    outFd, pclGeneratorP, &pam,
                    format, dpi, center, xoffs, yoffs, doDuplex, duplex,
                    doCopies, copies, doMediaSource, mediaSource,
                    doMediaDestination, mediaDestination);

                destroyPclGenerator(pclGeneratorP);
            }
        }
        pm_close(in_file);
        sourceP = sourceP->next; 
    }
    XL_Operator(outFd, oCloseDataSource);
}



static void
freeSource(InputSource * const firstSourceP) {
    
    InputSource * sourceP;

    sourceP = firstSourceP;
    while(sourceP) {
        InputSource * const nextP = sourceP->next;
        free(sourceP);
        sourceP = nextP;
    }
}



int
main(int argc, char *argv[]) {

    int const outFd = STDOUT_FILENO;

    struct cmdlineInfo cmdline;
    
    /* In case you're wondering why we do direct file descriptor I/O
       instead of stream (FILE *), it's because Jochen originally 
       wrote this code for an embedded system with diet-libc.  Without
       the stream library, the statically linked binary was only about
       5K big.
    */
    pnm_init(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    jobHead(outFd, cmdline.rendergray, cmdline.jobsetup);

    beginSession(outFd, cmdline.dpi, cmdline.dpi, eInch, 
                 FALSE, eBackChAndErrPage);

    printPages(outFd,cmdline.sourceP,
               cmdline.format, cmdline.dpi, cmdline.center,
               cmdline.xoffs, cmdline.yoffs,
               cmdline.duplexSpec, cmdline.duplex,
               cmdline.copiesSpec, cmdline.copies,
               cmdline.feederSpec, cmdline.feeder,
               cmdline.outtraySpec, cmdline.outtray,
               cmdline.colorok
        );
    endSession(outFd);

    jobEnd(outFd);

    freeSource(cmdline.sourceP);

    return 0;
}
