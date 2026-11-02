#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
VER="${VERSION:-$(tr -d ' \n' < "$ROOT/VERSION" 2>/dev/null || echo 0.4.0)}"
ARCH=amd64
PKG="basalt_${VER}_${ARCH}"
OUT="$ROOT/dist"
ST="$OUT/debroot"

rm -rf "$ST"
mkdir -p "$ST/DEBIAN" "$ST/usr/bin" "$ST/usr/lib/basalt" \
  "$ST/usr/share/basalt/examples" "$ST/usr/share/basalt/languages" \
  "$ST/usr/share/basalt/std" "$ST/usr/share/basalt/lib" \
  "$ST/usr/share/doc/basalt" "$ST/usr/share/basalt/docs"

install -m 755 "$ROOT/bin/basaltc" "$ST/usr/bin/basaltc"
install -m 755 "$ROOT/bin/basm" "$ST/usr/bin/basm"
[ -f "$ROOT/bin/pe_tool" ] && install -m 755 "$ROOT/bin/pe_tool" "$ST/usr/bin/pe_tool"
install -m 644 "$ROOT/bin/basalt_rt.o" "$ST/usr/lib/basalt/basalt_rt.o"
[ -f "$ROOT/bin/basalt_rt_bare.o" ] && install -m 644 "$ROOT/bin/basalt_rt_bare.o" "$ST/usr/lib/basalt/basalt_rt_bare.o"
[ -f "$ROOT/bin/basalt_gpu.o" ] && install -m 644 "$ROOT/bin/basalt_gpu.o" "$ST/usr/lib/basalt/basalt_gpu.o"

cp -a "$ROOT/examples/." "$ST/usr/share/basalt/examples/" 2>/dev/null || true
cp -a "$ROOT/languages/." "$ST/usr/share/basalt/languages/" 2>/dev/null || true
cp -a "$ROOT/std/." "$ST/usr/share/basalt/std/" 2>/dev/null || true
cp -a "$ROOT/docs/." "$ST/usr/share/basalt/docs/" 2>/dev/null || true
cp -a "$ROOT/docs/." "$ST/usr/share/doc/basalt/" 2>/dev/null || true
cp -a "$ROOT/lib/." "$ST/usr/share/basalt/lib/" 2>/dev/null || true
[ -f "$ROOT/LICENSE" ] && cp "$ROOT/LICENSE" "$ST/usr/share/doc/basalt/copyright"
[ -f "$ROOT/README.md" ] && cp "$ROOT/README.md" "$ST/usr/share/doc/basalt/README.md"
echo "Basalt $VER" > "$ST/usr/share/doc/basalt/VERSION.txt"

# Wrapper so BASALT_HOME points at package data without hardcoded /tmp
cat > "$ST/usr/bin/basaltc.sh" << 'W'
#!/bin/sh
# Prefer package layout; override with BASALT_HOME
if [ -z "${BASALT_HOME:-}" ]; then
  if [ -d /usr/lib/basalt ]; then
    export BASALT_HOME=/usr
    # lib under /usr/lib/basalt — find_runtime_o uses $HOME/lib; symlink layout:
    export BASALT_HOME=/usr/lib/basalt/..
  fi
fi
# Real binary is basaltc; this wrapper only sets env when invoked as basaltc-env
exec basaltc "$@"
W

# postinst: symlink layout BASALT_HOME=/usr with lib/basalt -> relative
cat > "$ST/DEBIAN/postinst" << 'P'
#!/bin/sh
set -e
# Discoverable layout for find_runtime_o: $BASALT_HOME/lib/basalt_rt.o
# We install runtime at /usr/lib/basalt/basalt_rt.o
# Users: export BASALT_HOME=/usr/lib/basalt && put bin tools in PATH
# Or use defaults: basm and bin/basalt_rt.o on PATH/cwd
exit 0
P
chmod 755 "$ST/DEBIAN/postinst"

SIZE=$(du -sk "$ST" | cut -f1)
cat > "$ST/DEBIAN/control" << C
Package: basalt
Version: $VER
Section: devel
Priority: optional
Architecture: $ARCH
Maintainer: Basalt Contributors
Depends: libc6
Installed-Size: $SIZE
Description: Basalt compiler for x86_64
 Self-hosted systems language toolchain (basaltc, basm, runtime, docs).
 Environment: set BASALT_HOME to the prefix containing bin/ and lib/
 (e.g. export BASALT_HOME=/usr/lib/basalt after adjusting layout, or
 keep tools on PATH and basalt_rt.o in ./bin).
 Documentation is under /usr/share/doc/basalt and /usr/share/basalt/docs.
C

mkdir -p "$OUT"
dpkg-deb --build --root-owner-group "$ST" "$OUT/${PKG}.deb"
echo "OK $OUT/${PKG}.deb"
