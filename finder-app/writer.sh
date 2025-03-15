#!/bin/bash
# Creates a new file with a name and path (first input) with content 
# (second input)
# Accepts the following arguments:
# 1. writefile: full path to a file (including filename) on the filesystem
# 2. writestr: text string to write to the file


num_args_required=2

if [ $# -lt $num_args_required ]
then
  echo Insufficient inputs for writer.sh
  echo Required: 2
  echo Received: $#
  echo USAGE writer.sh writefile writestr
  echo "  writefile - full path to a file, including filename"
  echo "  writestr - text string to write to file"
  exit 1
fi

writefile=$1
writestr=$2
writefile_dir=$( dirname $writefile)

if [ ! -e $writefile ]
then
  # file does not exist, make the directory
  mkdir -p "$writefile_dir"
fi

echo "$writestr" > $writefile
