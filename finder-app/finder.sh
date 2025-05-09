#!/bin/bash

filesdir=$1
searchstr=$2

if [ $# -eq 2 ];  then
    if [ -d $filesdir ]; then
        numfiles=$(find $filesdir -type f | wc -l)
        linematches=$(grep -r "$searchstr" $filesdir | wc -l)
        echo "The number of files are $numfiles and the number of matching lines are $linematches \r\n"
        exit 0
    else
        echo "Not a directory"
        exit 1
    fi
    exit 0
else
    echo "Invalid number of arguments"
    exit 1
fi
