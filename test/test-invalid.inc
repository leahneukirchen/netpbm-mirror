echo 1>&2
echo "Invalid command-line argument combination(s)." 1>&2
echo "Error message(s) should appear below the line." 1>&2
echo "-----------------------------------------------------------" 1>&2

tmpdir=${tmpdir:-/tmp}
test_out=${tmpdir}/test_out

# define function
invCmd () { $1 > ${test_out} && \
  printf "Unexpected success ${testcnt} " || printf "Expected failure ${testcnt} "
  test -s ${test_out} && echo "unexpected output" || echo "(no output)"
  rm -f ${test_out}
  testcnt=$(($testcnt +1)); }

testcnt=1
