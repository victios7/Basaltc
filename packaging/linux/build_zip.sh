#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VER="${VERSION:-$(tr -d ' \n' < "$ROOT/VERSION" 2>/dev/null || echo 0.4.0)}"
OUT="$ROOT/dist"
NAME="basalt-${VER}-linux-x86_64"
ST="$OUT/$NAME"

rm -rf "$ST"
mkdir -p "$ST/bin" "$ST/lib" "$ST/share/examples" "$ST/share/languages" \
  "$ST/share/std" "$ST/share/lib" "$ST/share/docs"

install -m 755 "$ROOT/bin/basaltc" "$ST/bin/"
install -m 755 "$ROOT/bin/basm" "$ST/bin/"
[ -f "$ROOT/bin/pe_tool" ] && install -m 755 "$ROOT/bin/pe_tool" "$ST/bin/"
install -m 644 "$ROOT/bin/basalt_rt.o" "$ST/lib/"
[ -f "$ROOT/bin/basalt_rt_bare.o" ] && install -m 644 "$ROOT/bin/basalt_rt_bare.o" "$ST/lib/"
cp -a "$ROOT/examples/." "$ST/share/examples/" 2>/dev/null || true
cp -a "$ROOT/languages/." "$ST/share/languages/" 2>/dev/null || true
cp -a "$ROOT/std/." "$ST/share/std/" 2>/dev/null || true
cp -a "$ROOT/docs/." "$ST/share/docs/" 2>/dev/null || true
cp -a "$ROOT/lib/." "$ST/share/lib/" 2>/dev/null || true
cp "$ROOT/LICENSE" "$ROOT/VERSION" "$ST/" 2>/dev/null || true
[ -f "$ROOT/README.md" ] && cp "$ROOT/README.md" "$ST/"

cat > "$ST/env.sh" << 'E'
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export PATH="$DIR/bin:$PATH"
export BASALT_HOME="$DIR"
E

mkdir -p "$OUT"
tar -C "$OUT" -czf "$OUT/${NAME}.tar.gz" "$NAME"
( cd "$OUT" && zip -qr "${NAME}.zip" "$NAME" )
echo "OK $OUT/${NAME}.zip"
