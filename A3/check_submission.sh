#!/bin/sh

BUILDFILE=/u/csc469h/winter/pub/a3-w2016/build.tgz

# Check Usage
if [ $# -ne 1 ]
then
  echo "Usage: check_submission.sh <submission file>"
  exit
fi

if [ -e tmp_test_submission_dir ]
then
    echo "Expect to build in tmp_test_submission_dir, which already exists."
    echo "Please remove the directory or run the command from another location."
    exit
fi

# Check the submission
mkdir tmp_test_submission_dir

cd tmp_test_submission_dir
tar xzf $BUILDFILE
tar xzf $1
make > make.out >& make.err
errors=`grep "[Ee]rror\|No rule" make.err`
if [ -z "$errors" ]
then
  echo "Your submission compiled without errors."
else
  echo "Your submission will not compile."
  echo "$errors"
fi
cd ..
#rm -rf tmp_test_submission_dir
