#!/bin/sh
# Prints a message showing the number of lines and the matching grep pattern
# for the inputs
# Accepts the following runtime arguments: 
# 1. Path to a directory on the filesystem, filesdir
# 2. Text string which will be searched within the files, searchstr

num_args_required=2

if [ $# -lt $num_args_required ]
then
  echo "Insufficient inputs for finder.sh"
  echo "Required: 2"
  echo "Received: $#"
  echo "USAGE finder.sh filesdir searchstr"
  echo "  filesdir - path to a directory on the filesystem"
  echo "  searchstr - text string which will be searched within the files"
  exit 1
fi

filesdir=$1
searchstr=$2

if [ ! -d $filesdir ]
then
  echo $filesdir is not a directory
  echo exiting
  exit 1
fi

# echo Searching in $filesdir for $searchstr

num_files=$( find $filesdir -type f | wc -l )
num_matches=$( grep "$searchstr" -r $filesdir/* | wc -l)
# echo Found $num_files files in directory $filesdir
echo The number of files are $num_files and the number of matching lines are \
$num_matches
