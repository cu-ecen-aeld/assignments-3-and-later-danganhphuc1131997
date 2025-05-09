#!/bin/sh
if [ $# -eq 2 ]
then
    writefile=$1
    writestr=$2
    dirpath="$(dirname "$writefile")"
    mkdir -p $dirpath
    echo "$writestr" > "$writefile"
    exit 0
else
    echo "Wrong argument number"
    exit 1
fi