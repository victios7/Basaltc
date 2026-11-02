#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export PATH="$ROOT/bin:$PATH"
cp -f bin/basalt_rt.o /tmp/basalt_rt.o
cp -f bin/basm /tmp/basm 2>/dev/null || true
chmod +x bin/* 2>/dev/null || true

run_bc() {
  if bin/basaltc "$@" 2>/dev/null; then return 0; fi
  /lib64/ld-linux-x86-64.so.2 bin/basaltc "$@"
}
run_basm() {
  if bin/basm "$@" 2>/dev/null; then return 0; fi
  /lib64/ld-linux-x86-64.so.2 bin/basm "$@"
}

echo "== hello =="
run_bc examples/hello.bd -O2 -S -o /tmp/t_hello
run_basm /tmp/t_hello.basm -e -o /tmp/t_hello -r bin/basalt_rt.o
test "$(/tmp/t_hello)" = "42"
echo PASS hello

echo "== sum_cf =="
if [ -f examples/sum_cf.bd ]; then
  run_bc examples/sum_cf.bd -O5 -S -o /tmp/t_sum
  run_basm /tmp/t_sum.basm -e -o /tmp/t_sum -r bin/basalt_rt.o
  echo "sum_cf => $(/tmp/t_sum)"
fi

echo "ALL OK"
