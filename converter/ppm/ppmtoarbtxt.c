/* ppmtoarbtxt.c - convert portable pixmap to cleartext
**
** Renamed from ppmtotxt.c by Bryan Henderson in January 2003.
**
** Copyright (C) 1995 by Peter Kirchgessner
**
** Permission to use, copy, modify, and distribute this software and its
** documentation for any purpose and without fee is hereby granted, provided
** that the above copyright notice appear in all copies and that both that
** copyright notice and this permission notice appear in supporting
** documentation.  This software is provided "as is" without express or
** implied warranty.
*/

#include <assert.h>
#include <string.h>

#include "mallocvar.h"
#include "nstring.h"
#include "shhopt.h"
#include "ppm.h"



struct CmdlineInfo {
    /* All the information the user supplied in the command line,
       in a form easy for the program to use.
    */
    const char * inputFileName;
    const char * bodySklFileName;
    const char * hd;
    const char * tl;
    unsigned int debug;
};



static void
parseCommandLine(int argc, const char ** argv,
                 struct CmdlineInfo * const cmdlineP) {
/*----------------------------------------------------------------------------
   Note that many of the strings that this function returns in the
   *cmdline_p structure are actually in the supplied argv array.  And
   sometimes, one of these strings is actually just a suffix of an entry
   in argv!
-----------------------------------------------------------------------------*/
    optEntry * option_def;
        /* Instructions to OptParseOptions3 on how to parse our options.
         */
    optStruct3 opt;

    unsigned int hdSpec, tlSpec;

    unsigned int option_def_index;
    
    MALLOCARRAY(option_def, 100);

    option_def_index = 0;   /* incremented by OPTENTRY */
    OPTENT3(0,   "hd",   OPT_STRING, &cmdlineP->hd, 
            &hdSpec,             0);
    OPTENT3(0,   "tl",   OPT_STRING, &cmdlineP->tl,
            &tlSpec,             0);
    OPTENT3(0,   "debug", OPT_FLAG, NULL,
            &cmdlineP->debug,      0);

    opt.opt_table = option_def;
    opt.short_allowed = FALSE;  /* We have no short (old-fashioned) options */
    opt.allowNegNum = FALSE;  /* We have no parms that are negative numbers */

    pm_optParseOptions3(&argc, (char **)argv, opt, sizeof(opt), 0);

    if (!hdSpec)
        cmdlineP->hd = NULL;

    if (!tlSpec)
        cmdlineP->tl = NULL;

    if (argc-1 < 1)
        pm_error("You must specify the body skeleton file name as an "
                 "argument");
    else {
        cmdlineP->bodySklFileName = strdup(argv[1]);

        if (argc-1 < 2)
            cmdlineP->inputFileName = strdup("-");  /* he wants stdin */
        else {
            cmdlineP->inputFileName = strdup(argv[2]);
            if (argc-1 > 2)
                pm_error("Too many arguments.  The only possible arguments "
                         "are the body skeleton file name and input image "
                         "file name");
        }
    }
}




typedef enum {
/* The types of object we handle */
    BDATA, IRED, IGREEN, IBLUE, ILUM, FRED, FGREEN, FBLUE, FLUM,
    WIDTH, HEIGHT, POSX, POSY
} SkeletonObjectType;

typedef enum {
    OBJTYP_ICOLOR, OBJTYP_FCOLOR, OBJTYP_INT, OBJTYP_BDATA
} SkeletonObjectClass;

/* Maximum size for a format string ("%d" etc.) */
#define MAXFORMAT 16

typedef union {
/* The data we keep for each object */
    struct Bndat {
        char * bdat;   /* Binary data (text with newlines etc.) */
        unsigned int ndat;
    } binData;
    
    struct Icdat {
        char icformat[MAXFORMAT];  /* Integer colors */
        unsigned int icolmin, icolmax;
    } icolData;

    struct Fcdat {
        char fcformat[MAXFORMAT];  /* Float colors */
        double fcolmin, fcolmax;
    } fcolData;
    
    struct Idat {
        char iformat[MAXFORMAT];   /* Integer data */
    } iData;
} SkeletonObjectData;


/* Each object has a type and some data */
typedef struct { 
    SkeletonObjectType objType;
    SkeletonObjectData odata;
} SkeletonObject;



#define MAX_SKL_HEAD_OBJ 64
#define MAX_SKL_BODY_OBJ 256
#define MAX_SKL_TAIL_OBJ 64
#define MAX_LINE_BUF 1024
#define MAX_OBJ_BUF 80



static void
dumpSkeleton(SkeletonObject ** const skeletonPList,
             unsigned int      const nSkeleton) {

    unsigned int i;

    pm_message("%u objects", nSkeleton);

    for (i = 0; i < nSkeleton; ++i) {
        SkeletonObject * const skeletonP = skeletonPList[i];

        pm_message("  Object: Type %u", skeletonP->objType);
    }
}



static void
dumpAllSkeleton(SkeletonObject ** const bodySkeletonPList,
                unsigned int      const bodyNskl,
                SkeletonObject ** const headSkeletonPList, 
                unsigned int      const headNskl,
                SkeletonObject ** const tailSkeletonPList,
                unsigned int      const tailNskl) {
    
    pm_message("Body skeleton:");
    dumpSkeleton(bodySkeletonPList, bodyNskl);

    pm_message("Head skeleton:");
    dumpSkeleton(headSkeletonPList, headNskl);

    pm_message("Tail skeleton:");
    dumpSkeleton(tailSkeletonPList, tailNskl);
}



static void
writeBndat(FILE *           const ofP,
           SkeletonObject * const objectP) {

    struct Bndat * const bdataP = &objectP->odata.binData;

    fwrite(bdataP->bdat, bdataP->ndat, 1, ofP);
}



static void
writeIcol(FILE *           const ofP,
          SkeletonObject * const objectP,
          double           const value) {

    struct Icdat * const icdataP = &objectP->odata.icolData;
    
    fprintf(ofP, icdataP->icformat,
            (unsigned int)
            (icdataP->icolmin
             + (icdataP->icolmax - icdataP->icolmin) * value));
}



static void
writeFcol(FILE *           const ofP,
          SkeletonObject * const objectP,
          double           const value) {

    struct Fcdat * const fcdataP = &objectP->odata.fcolData;
    
    fprintf(ofP, fcdataP->fcformat,
            (double)
            (fcdataP->fcolmin
             + (fcdataP->fcolmax - fcdataP->fcolmin) * value));
}



static void
writeIdat(FILE *           const ofP,
          SkeletonObject * const objectP,
          unsigned int     const value) {

    struct Idat * const idataP = &objectP->odata.iData;
    
    fprintf(ofP, idataP->iformat, value);
}



static void
writeText(FILE *            const ofP,
          unsigned int      const nObj,
          SkeletonObject ** const obj,
          unsigned int      const width,
          unsigned int      const height,
          unsigned int      const x,
          unsigned int      const y,
          double            const red,
          double            const green,
          double            const blue) {
    
    unsigned int i;

    for (i = 0; i < nObj; ++i) {
        switch (obj[i]->objType) {
        case BDATA:
            writeBndat(ofP, obj[i]);
            break;
        case IRED:
            writeIcol(ofP, obj[i], red);
            break;
        case IGREEN:
            writeIcol(ofP, obj[i], green);
            break;
        case IBLUE:
            writeIcol(ofP, obj[i], blue);
            break;
        case ILUM:
            writeIcol(ofP, obj[i],
                      PPM_LUMINR*red + PPM_LUMING*green + PPM_LUMINB*blue);
            break;
        case FRED:
            writeFcol(ofP, obj[i], red);
            break;
        case FGREEN:
            writeFcol(ofP, obj[i], green);
            break;
        case FBLUE:
            writeFcol(ofP, obj[i], blue);
            break;
        case FLUM:
            writeFcol(ofP, obj[i],
                      PPM_LUMINR*red + PPM_LUMING*green + PPM_LUMINB*blue);
            break;
        case WIDTH:
            writeIdat(ofP, obj[i], width);
            break;
        case HEIGHT:
            writeIdat(ofP, obj[i], height);
            break;
        case POSX:
            writeIdat(ofP, obj[i], x);
            break;
        case POSY:
            writeIdat(ofP, obj[i], y);
            break;
        }
    }
}



static SkeletonObject *
newBinDataObj(unsigned int const nDat, 
              const char * const bdat) {
/*----------------------------------------------------------------------------
  Createa binary data object.
  -----------------------------------------------------------------------------*/
    SkeletonObject * objectP;

    objectP = malloc(sizeof(*objectP) + nDat);

    if (!objectP)
        pm_error("Failed to allocate memory for binary data object "
                 "with %u bytes", nDat);

    objectP->objType = BDATA;
    objectP->odata.binData.ndat = nDat;
    objectP->odata.binData.bdat = ((char *)objectP) + sizeof(SkeletonObject);
    memcpy(objectP->odata.binData.bdat, bdat, nDat);

    return objectP;
}



static SkeletonObject *
newIcolDataObj(SkeletonObjectType const ctyp,
               const char *       const format,
               unsigned int       const icolmin,
               unsigned int       const icolmax) {
/*----------------------------------------------------------------------------
  Create integer color data object.
  -----------------------------------------------------------------------------*/
    SkeletonObject * objectP;

    MALLOCVAR(objectP);

    if (!objectP)
        pm_error("Failed to allocate memory for an integer color data "
                 "object");

    objectP->objType = ctyp;
    strcpy(objectP->odata.icolData.icformat, format);
    objectP->odata.icolData.icolmin = icolmin;
    objectP->odata.icolData.icolmax = icolmax;

    return objectP;
}



static SkeletonObject *
newFcolDataObj(SkeletonObjectType  const ctyp,
               const char *        const format,
               double              const fcolmin,
               double              const fcolmax) {
/*----------------------------------------------------------------------------
  Create float color data object.
  -----------------------------------------------------------------------------*/
    SkeletonObject * objectP;

    MALLOCVAR(objectP);

    if (!objectP)
        pm_error("Failed to allocate memory for a float color data object");

    objectP->objType = ctyp;
    strcpy(objectP->odata.fcolData.fcformat, format);
    objectP->odata.fcolData.fcolmin = fcolmin;
    objectP->odata.fcolData.fcolmax = fcolmax;

    return objectP;
}



static SkeletonObject *
newIdataObj(SkeletonObjectType const ctyp,
            const char *       const format) {
/*----------------------------------------------------------------------------
  Create universal data object.
  -----------------------------------------------------------------------------*/
    SkeletonObject * objectP;

    MALLOCVAR(objectP);

    if (!objectP)
        pm_error("Failed to allocate memory for a universal data object");

    objectP->objType = ctyp;
    strcpy(objectP->odata.iData.iformat, format);

    return objectP;
}



static char const escape = '#';



static SkeletonObjectType
interpretObjType(const char * const typstr) {

    SkeletonObjectType objType;

    /* Check for integer colors */
    if      (streq(typstr, "ired")  ) objType = IRED;
    else if (streq(typstr, "igreen")) objType = IGREEN;
    else if (streq(typstr, "iblue") ) objType = IBLUE;
    else if (streq(typstr, "ilum")  ) objType = ILUM;
    /* Check for real colors */
    else if (streq(typstr, "fred")  ) objType = FRED;
    else if (streq(typstr, "fgreen")) objType = FGREEN;
    else if (streq(typstr, "fblue") ) objType = FBLUE;
    else if (streq(typstr, "flum")  ) objType = FLUM;
    /* Check for integer data */
    else if (streq(typstr, "width") ) objType = WIDTH;
    else if (streq(typstr, "height")) objType = HEIGHT;
    else if (streq(typstr, "posx")  ) objType = POSX;
    else if (streq(typstr, "posy")  ) objType = POSY;
    else                              objType = BDATA;

    return objType;
}



static SkeletonObjectClass
objClass(SkeletonObjectType const objType) {

    switch (objType) {
    case IRED:
    case IGREEN:
    case IBLUE:
    case ILUM:
        return OBJTYP_ICOLOR;

    case FRED:
    case FGREEN:
    case FBLUE:
    case FLUM:
        return OBJTYP_FCOLOR;

    case WIDTH:
    case HEIGHT:
    case POSX:
    case POSY:
        return OBJTYP_INT;
    case BDATA:
        return OBJTYP_BDATA;
    }
    return 999; /* quiet compiler warning */
}



static SkeletonObject *
newIcSkelFromReplString(const char *       const objstr,
                        SkeletonObjectType const objType) {

    SkeletonObject * retval;
    unsigned int icolmin, icolmax;
    char formstr[MAX_OBJ_BUF];
    unsigned int nOdata;

    nOdata = sscanf(objstr, "%*s%s%u%u", formstr, &icolmin, &icolmax);

    if (nOdata == 3)
        retval = newIcolDataObj(objType, formstr, icolmin, icolmax);
    else if (nOdata == EOF) {
        /* No arguments specified.  Use defaults */
        retval = newIcolDataObj(objType, "%u", 0, 255);
    } else
        retval = NULL;

    return retval;
}



static SkeletonObject *
newFcSkelFromReplString(const char *       const objstr,
                        SkeletonObjectType const objType) {

    SkeletonObject * retval;
    double fcolmin, fcolmax;
    char formstr[MAX_OBJ_BUF];
    unsigned int nOdata;

    nOdata = sscanf(objstr, "%*s%s%lf%lf", formstr,
                    &fcolmin, &fcolmax);

    if (nOdata == 3)
        retval = newFcolDataObj(objType, formstr, fcolmin, fcolmax);
    else if (nOdata == EOF) {
        /* No arguments specified.  Use defaults */
        retval = newFcolDataObj(objType, "%f", 0.0, 1.0);
    } else
        retval = NULL;

    return retval;
} 



static SkeletonObject *
newISkelFromReplString(const char *        const objstr,
                        SkeletonObjectType const objType) {

    SkeletonObject * retval;
    char formstr[MAX_OBJ_BUF];
    unsigned int const nOdata = sscanf(objstr, "%*s%s", formstr);
    
    if (nOdata == 1)
        retval = newIdataObj(objType, formstr);
    else if (nOdata == EOF) {
        /* No arguments specified.  Use defaults */
        retval = newIdataObj(objType, "%u");
    } else
        retval = NULL;

    return retval;
} 


static SkeletonObject *
newSkeletonFromReplString(const char * const objstr) {
/*----------------------------------------------------------------------------
  Create a skeleton from the replacement string 'objstr' (the stuff
  between the parentheses in #(...) ).

  Return NULL if it isn't a valid replacement string.
-----------------------------------------------------------------------------*/
    SkeletonObject * retval;
    char typstr[MAX_OBJ_BUF];
    SkeletonObjectType objType;

    typstr[0] = '\0';  /* initial value */

    sscanf(objstr, "%s", typstr);

    objType = interpretObjType(typstr);

    switch (objClass(objType)) {
    case OBJTYP_ICOLOR:
        retval = newIcSkelFromReplString(objstr, objType);
        break;
    case OBJTYP_FCOLOR:
        retval = newFcSkelFromReplString(objstr, objType);
        break;
    case OBJTYP_INT:
        retval = newISkelFromReplString(objstr, objType);
        break;
    case OBJTYP_BDATA:
        retval = NULL;
    }
    return retval;
}



static void
readThroughCloseParen(FILE * const ifP,
                      char * const objstr,
                      size_t const objstrSize,
                      bool * const unclosedP) {
/*----------------------------------------------------------------------------
   Read *ifP up through close parenthesis ( ')' ) into 'objstr', which
   is of size 'objstrSize'.

   Return *unclosedP true iff we run out of file or run out of objstr
   before we see a close parenthesis.
-----------------------------------------------------------------------------*/
    unsigned int i;
    bool eof;
    bool gotEscSeq;

    for (i= 0, eof = false, gotEscSeq = false;
         i < objstrSize - 1 && !gotEscSeq && !eof;
         ++i) {
        int rc;
        rc = getc(ifP);
        if (rc == EOF)
            eof = true;
        else {
            char const chr = rc;
            if (chr == ')')
                gotEscSeq = true;
            else
                objstr[i] = chr;
        }
    }
    objstr[i] = '\0';

    *unclosedP = !gotEscSeq;
}



typedef struct {
    unsigned int      capacity;
    SkeletonObject ** skeletonPList;
    unsigned int      nSkeleton;
} SkeletonBuffer;



static void
SkeletonBuffer_init(SkeletonBuffer *  const bufferP,
                    unsigned int      const capacity,
                    SkeletonObject ** const skeletonPList) {

    bufferP->capacity      = capacity;
    bufferP->skeletonPList = skeletonPList;
    bufferP->nSkeleton     = 0;
}



static void
SkeletonBuffer_add(SkeletonBuffer * const bufferP,
                   SkeletonObject * const skeletonP) {

    if (bufferP->nSkeleton >= bufferP->capacity)
        pm_error("Too many skeletons.  Max = %u", bufferP->capacity);

    bufferP->skeletonPList[bufferP->nSkeleton++] = skeletonP;
}                   



typedef struct {

    char data[MAX_LINE_BUF + MAX_OBJ_BUF + 16];

    unsigned int length;

    SkeletonBuffer * skeletonBufferP;
        /* The buffer to which we flush.  Flushing means turning all the
           characters currently in our buffer into a binary skeleton object
           here.
        */

} Buffer;



static void
Buffer_init(Buffer *         const bufferP,
            SkeletonBuffer * const skeletonBufferP) {

    bufferP->skeletonBufferP = skeletonBufferP;
    bufferP->length = 0;
}



static void
Buffer_flush(Buffer * const bufferP) {
/*----------------------------------------------------------------------------
   Flush the buffer out to a binary skeleton object.
-----------------------------------------------------------------------------*/
    SkeletonBuffer_add(bufferP->skeletonBufferP,
                       newBinDataObj(bufferP->length, bufferP->data));

    bufferP->length = 0;
}



static void
Buffer_add(Buffer * const bufferP,
           char     const newChar) {

    if (bufferP->length >= MAX_LINE_BUF)
        Buffer_flush(bufferP);

    assert(bufferP->length < MAX_LINE_BUF);

    bufferP->data[bufferP->length++] = newChar;
}



static void
Buffer_dropFinalNewline(Buffer * const bufferP) {
/*----------------------------------------------------------------------------
   If the last thing in the buffer is a newline, remove it.
-----------------------------------------------------------------------------*/
    if (bufferP->length >= 1 && bufferP->data[bufferP->length-1] == '\n') {
            /* Drop finishing newline character */
            --bufferP->length;
    }
}



static void
addImpostorReplacementSeq(Buffer *     const bufferP,
                          const char * const seqContents) {
/*----------------------------------------------------------------------------
  Add to buffer *bufferP something that looks like a replacement sequence but
  doesn't have the proper contents (the stuff between the parentheses) to be
  one.  For example,

  "#(fread x)"

  seqContents[] is the contents, NUL-terminated.
-----------------------------------------------------------------------------*/
    const char * p;

    Buffer_add(bufferP, escape);
    Buffer_add(bufferP, '(');

    for (p = &seqContents[0]; *p; ++p)
        Buffer_add(bufferP, *p);

    Buffer_add(bufferP, ')');
}



static void
readSkeletonFile(const char *      const filename,
                 unsigned int      const maxskl,
                 const char **     const errorP,
                 unsigned int *    const nSkeletonP,
                 SkeletonObject ** const skeletonPList) {
/*----------------------------------------------------------------------------
-----------------------------------------------------------------------------*/
    FILE * sklfileP;
    SkeletonBuffer skeletonBuffer;
        /* A buffer for accumulating skeleton objects */
    Buffer buffer;
        /* A buffer for accumulating binary (literal; unsubstituted) data, on
           its way to becoming a binary skeleton object. 
        */
    bool eof;
    const char * error;

    SkeletonBuffer_init(&skeletonBuffer, maxskl, skeletonPList);

    Buffer_init(&buffer, &skeletonBuffer);

    sklfileP = pm_openr(filename);

    for (eof = false, error = NULL; !eof && !error; ) {

        int rc;

        rc = getc(sklfileP);

        if (rc == EOF)
            eof = true;
        else {
            char const chr = rc;

            if (chr != escape) {
                /* Not a replacement sequence; just a literal character */
                Buffer_add(&buffer, chr);
            } else {
                int rc;
                rc = getc(sklfileP);
                if (rc == EOF) {
                    /* Not a replacement sequence, just an escape caharacter
                       at the end of the file.
                    */
                    Buffer_add(&buffer, escape);
                    eof = true;
                } else {
                    char const chr = rc;

                    if (chr != '(') {
                        /* Not a replacement sequence, just a lone escape
                           character
                        */
                        Buffer_add(&buffer, escape);
                        Buffer_add(&buffer, chr);
                    } else {
                        char objstr[MAX_OBJ_BUF];
                        bool unclosed;
                        readThroughCloseParen(sklfileP,
                                              objstr, sizeof(objstr),
                                              &unclosed);
                        if (unclosed)
                            pm_asprintf(&error, "Unclosed parentheses "
                                        "in #() escape sequence");
                        else {
                            SkeletonObject * const skeletonP =
                                newSkeletonFromReplString(objstr);

                            if (skeletonP) {
                                Buffer_flush(&buffer);
                                SkeletonBuffer_add(&skeletonBuffer, skeletonP);
                            } else
                                addImpostorReplacementSeq(&buffer, objstr);
                        }
                    }
                }
            }
        }
    }

    if (!error) {
        Buffer_dropFinalNewline(&buffer);
        Buffer_flush(&buffer);
    }
    *errorP = error;
    *nSkeletonP = skeletonBuffer.nSkeleton;

    fclose(sklfileP);
}



static void
convertIt(FILE *            const ifP,
          FILE *            const ofP,
          SkeletonObject ** const bodySkeletonPList,
          unsigned int      const bodyNskl,
          SkeletonObject ** const headSkeletonPList, 
          unsigned int      const headNskl,
          SkeletonObject ** const tailSkeletonPList,
          unsigned int      const tailNskl) {

    pixel * pixelrow;
    pixval maxval;
    double dmaxval;
    int rows, cols;
    int format;
    unsigned int row;

    ppm_readppminit(ifP, &cols, &rows, &maxval, &format);

    pixelrow = ppm_allocrow(cols);

    dmaxval = (double)maxval;

    if (headNskl > 0)    /* Write header */
        writeText(ofP, headNskl, headSkeletonPList, 
                  cols, rows , 0, 0, 0.0, 0.0, 0.0);

    for (row = 0; row < rows; ++row) {
        unsigned int col;
        ppm_readppmrow(ifP, pixelrow, cols, maxval, format);

        for (col = 0; col < cols; ++col) {
            pixel const thisPixel = pixelrow[col];

            writeText(ofP, bodyNskl, bodySkeletonPList,
                      cols, rows, col, row,
                      PPM_GETR(thisPixel)/dmaxval,
                      PPM_GETG(thisPixel)/dmaxval,
                      PPM_GETB(thisPixel)/dmaxval);
        }
    }

    if (tailNskl > 0)
        /* Write trailer */
        writeText(ofP, tailNskl, tailSkeletonPList, 
                  cols, rows, 0, 0, 0.0, 0.0, 0.0);
}



int
main(int           argc,
     const char ** argv) {
    
    struct CmdlineInfo cmdline;

    unsigned int headNskl, bodyNskl, tailNskl;
    SkeletonObject * headSkeletonPList[MAX_SKL_HEAD_OBJ];
    SkeletonObject * bodySkeletonPList[MAX_SKL_BODY_OBJ];
    SkeletonObject * tailSkeletonPList[MAX_SKL_TAIL_OBJ];
    FILE * ifP;
    const char * error;

    pm_proginit(&argc, argv);

    parseCommandLine(argc, argv, &cmdline);

    ifP = pm_openr(cmdline.inputFileName);

    readSkeletonFile(cmdline.bodySklFileName, ARRAY_SIZE(bodySkeletonPList),
                     &error, &bodyNskl, bodySkeletonPList);
    if (error)
        pm_error("Invalid body skeleton file '%s'.  %s",
                 cmdline.bodySklFileName, error);

    if (cmdline.hd) {
        readSkeletonFile(cmdline.hd, ARRAY_SIZE(headSkeletonPList),
                         &error, &headNskl, headSkeletonPList);
        if (error)
            pm_error("Invalid head skeleton file '%s'.  %s",
                     cmdline.hd, error);
    } else
        headNskl = 0;

    if (cmdline.tl) {
        readSkeletonFile(cmdline.tl, ARRAY_SIZE(tailSkeletonPList),
                         &error, &tailNskl, tailSkeletonPList);
        if (error)
            pm_error("Invalid tail skeleton file '%s'.  %s",
                     cmdline.tl, error);
    } else
        tailNskl = 0;

    if (cmdline.debug)
        dumpAllSkeleton(bodySkeletonPList, bodyNskl,
                        headSkeletonPList, headNskl,
                        tailSkeletonPList, tailNskl);

    convertIt(ifP, stdout,
              bodySkeletonPList, bodyNskl,
              headSkeletonPList, headNskl,
              tailSkeletonPList, tailNskl);

    pm_close(ifP);

    return 0;
}
