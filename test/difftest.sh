#!/bin/bash
# Differential testing: compile random programs (from gen_random.py) with
# mucc and with gcc, run both, and compare what they print. The programs
# have no undefined behavior, so any difference is a bug.
#
#   test/difftest.sh [count] [first-seed]      e.g. test/difftest.sh 500
#   test/difftest.sh --check-ub [count]        check the generator itself
#
# --check-ub runs gcc's build under its sanitizers instead. They trap on
# undefined behavior at run time, so this proves the generator only makes
# valid programs.
#
# Programs that fail are kept in difftest-failures/SEED.c.
cd "$(dirname "$0")/.."

check_ub=false
if [ "$1" = --check-ub ]; then
  check_ub=true
  shift
fi
count=${1:-200}
seed=${2:-1}

tmp=$(mktemp -d /tmp/mucc-difftest-XXXXXX)
trap 'rm -rf $tmp' EXIT
mkdir -p difftest-failures

fail() {
  cp $tmp/t.c difftest-failures/$s.c
  echo "seed $s: $1 (saved difftest-failures/$s.c)"
  failures=$((failures + 1))
}

failures=0
for ((s = seed; s < seed + count; s++)); do
  python3 test/gen_random.py $s > $tmp/t.c

  if $check_ub; then
    gcc -w -O0 -fsanitize=undefined,float-cast-overflow,address \
      -fno-sanitize-recover=all -o $tmp/san $tmp/t.c || { fail "gcc can't compile it"; continue; }
    timeout 10 $tmp/san > /dev/null 2> $tmp/err || { fail "undefined behavior: $(grep -m1 'runtime error' $tmp/err)"; continue; }
    continue
  fi

  gcc -w -O0 -o $tmp/gcc $tmp/t.c || { fail "gcc can't compile it (generator bug)"; continue; }
  ./mucc -o $tmp/mucc $tmp/t.c 2> $tmp/err || { fail "mucc can't compile it: $(head -1 $tmp/err)"; continue; }

  timeout 10 $tmp/gcc > $tmp/gcc.out; gcc_rc=$?
  timeout 10 $tmp/mucc > $tmp/mucc.out; mucc_rc=$?

  if [ $gcc_rc != $mucc_rc ]; then
    fail "exit code: gcc $gcc_rc, mucc $mucc_rc"
  elif ! cmp -s $tmp/gcc.out $tmp/mucc.out; then
    fail "output differs at line $(cmp $tmp/gcc.out $tmp/mucc.out | grep -o 'line [0-9]*' | cut -d' ' -f2)"
  fi
done

echo "difftest: $count programs (seeds $seed-$((seed + count - 1))), $failures failed"
[ $failures = 0 ]
