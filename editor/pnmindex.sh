#!/bin/sh
#
# pnmindex - build a visual index of a bunch of PNM images
#
# Copyright (C) 1991 by Jef Poskanzer.
#
# Permission to use, copy, modify, and distribute this software and its
# documentation for any purpose and without fee is hereby granted, provided
# that the above copyright notice appear in all copies and that both that
# copyright notice and this permission notice appear in supporting
# documentation.  This software is provided "as is" without express or
# implied warranty.

size=100        # make the images about this big
across=6        # show this many images per row
colors=256      # quantize results to this many colors
back="-white"   # default background color
doquant=true    # quantize or not
title=""        # default title (none)

usage ()
{
  echo "usage: $0 [-size N] [-across N] [-colors N] [-black] pnmfile ..."
  exit 1
}

while :; do
    case "$1" in

    -s*)
        if [ $# -lt 2 ]; then usage; fi
        size="$2"
        shift
        shift
    ;;

    -a*)
        if [ $# -lt 2 ]; then usage; fi
        across="$2"
        shift
        shift
    ;;

    -t*)
        if [ $# -lt 2 ]; then usage; fi
        title="$2"
        shift
        shift
    ;;

    -c*)
        if [ $# -lt 2 ]; then usage; fi
        colors="$2"
        shift
        shift
    ;;

    -b*)
        back="-black"
        shift
    ;;

    -w*)
        back="-white"
        shift
    ;;

    -noq*)
        doquant=false
        shift
    ;;

    -q*)
        doquant=true
        shift
    ;;

    -*)
        usage
    ;;

    *)
        break
    ;;
    esac
done

if [ $# -eq 0 ]; then
    usage
fi

tempdir="${TMPDIR-/tmp}/pnmindex.$$"
mkdir -m 0700 $tempdir || \
  { echo "Could not create temporary file. Exiting."; exit 1;}
trap 'rm -rf $tempdir' 0 1 3 15

tmpfile=$tempdir/pi.tmp
maxformat=PBM

rowfiles=()
imagefiles=()
row=1
col=1

if [ "$title"x != ""x ] ; then
#    rowfile=`tempfile -p pirow -m 600`
    rowfile=$tempdir/pi.${row}
    pbmtext "$title" > $rowfile
    rowfiles=(${rowfiles[*]} $rowfile )
    row=$(($row + 1))
fi

for i in "$@"; do

    description=(`pnmfile $i`)

    format=${description[1]}
    width=${description[3]}
    height=${description[5]}

    if [ $? -ne 0 ]; then
        echo pnmfile returned an error
        exit $?
    fi

    if [ $width -le $size ] && \
       [ $height -le $size ]; then
        cat $i > $tmpfile
    else
        case $format in

        PBM) 
            pamscale -quiet -xysize $size $size $i | pgmtopbm > $tmpfile
        ;;

        PGM)
            pamscale -quiet -xysize $size $size $i > $tmpfile
            if [ $maxformat = PBM ]; then
                maxformat=PGM
            fi
        ;;

        *) 
            if [ "$doquant" = "true" ] ; then
                pamscale -quiet -xysize $size $size $i | \
                pnmquant -quiet $colors > $tmpfile
            else
                pamscale -quiet -xysize $size $size $i > $tmpfile
            fi
            maxformat=PPM
        ;;
        esac
    fi

    imagefile=$tempdir/pi.${row}.${col}
    rm -f $imagefile
    if [ "$back" = "-white" ]; then
        pbmtext "$i" | \
          pamcat -extendplane $back -topbottom $tmpfile - > $imagefile
    else
        pbmtext "$i" | \
          pnminvert | \
          pamcat -extendplane $back -topbottom $tmpfile - > $imagefile
    fi
    imagefiles=( ${imagefiles[*]} $imagefile )

    if [ $col -ge $across ]; then
        rowfile=$tempdir/pi.${row}
        rm -f $rowfile

        if [ $maxformat != PPM -o "$doquant" = "false" ]; then
            pamcat -extendplane $back -leftright -jbottom ${imagefiles[*]} \
              > $rowfile
        else
            pamcat -extendplane $back -leftright -jbottom ${imagefiles[*]} | \
              pnmquant -quiet $colors \
              > $rowfile
        fi

        rm -f ${imagefiles[*]}
        unset imagefiles
        imagefiles=()
        rowfiles=( ${rowfiles[*]} $rowfile )
        col=1
        row=$(($row + 1))
    else
        col=$(($col + 1))
    fi
done

# All the full rows have been put in row files.  
# Now put the final partial row in its row file.

if [ ${#imagefiles[*]} -gt 0 ]; then
    rowfile=$tempdir/pi.${row}
    rm -f $rowfile
    if [ $maxformat != PPM -o "$doquant" = "false" ]; then
        pamcat -extendplane $back -leftright -jbottom ${imagefiles[*]} \
          > $rowfile
    else
        pamcat -extendplane $back -leftright -jbottom ${imagefiles[*]} | \
          pnmquant -quiet $colors > \
          $rowfile
    fi
    rm -f ${imagefiles[*]}
    rowfiles=( ${rowfiles[*]} $rowfile )
fi

if [ ${#rowfiles[*]} -eq 1 ]; then
    pnmtopnm $rowfiles
else
    if [ $maxformat != PPM -o "$doquant" = "false" ]; then
        pamcat -extendplane $back -topbottom ${rowfiles[*]} |\
          pnmtopnm
    else
        pamcat -extendplane $back -topbottom ${rowfiles[*]} | \
          pnmquant -quiet $colors | \
          pnmtopnm
    fi
fi
rm -f ${rowfiles[*]}

exit 0

