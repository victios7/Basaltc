.PHONY: test selftest bootstrap basm blink clean

export PATH := $(CURDIR)/bin:$(PATH)

selftest test:
	@cp -f bin/basalt_rt.o /tmp/basalt_rt.o
	@cp -f bin/basm /tmp/basm 2>/dev/null || true
	./scripts/selftest.sh

basm:
	cp -f bin/basalt_rt.o /tmp/basalt_rt.o
	bin/basaltc compiler/basm.bd -O2 -o bin/basm
	@echo "built bin/basm from compiler/basm.bd"

blink:
	cp -f bin/basalt_rt.o /tmp/basalt_rt.o
	bin/basaltc compiler/blink.bd -O2 -o bin/blink
	@echo "built bin/blink from compiler/blink.bd"

# stage0 unchanged; link step uses runtime so self-hosted basaltc resolves symbols
bootstrap:
	gcc -O2 -o bin/stage0 \
	  bootstrap/stage0-c/src/main.c \
	  bootstrap/stage0-c/src/util.c \
	  bootstrap/stage0-c/src/lexer.c \
	  bootstrap/stage0-c/src/parser.c \
	  bootstrap/stage0-c/src/type.c \
	  bootstrap/stage0-c/src/sema.c \
	  bootstrap/stage0-c/src/codegen_c.c \
	  -Ibootstrap/stage0-c/include
	cp -f bin/basalt_rt.o /tmp/basalt_rt.o
	bin/stage0 compiler/basaltc.bd -o bin/basaltc_tmp || true
	@# Prefer linking generated C with full runtime (production)
	@CFILE=; \
	  for f in basaltc.bd.c bin/basaltc.bd.c /tmp/basaltc.bd.c "$$TMPDIR/basaltc.bd.c"; do \
	    [ -f "$$f" ] && CFILE=$$f && break; \
	  done; \
	  if [ -n "$$CFILE" ]; then \
	    gcc -O1 -pipe -c -o /tmp/basalt_rt_boot.o runtime/runtime.c; \
	    gcc -O1 -pipe -c -o /tmp/basalt_arena_boot.o runtime/arena.c; \
	    gcc -O1 -pipe -c -o /tmp/basalt_ff_boot.o runtime/float_fmt.c; \
	    gcc -O1 -pipe -c -o /tmp/basalt_accel_boot.o runtime/accel.c; \
	    gcc -O1 -pipe -o bin/basaltc "$$CFILE" \
	      /tmp/basalt_rt_boot.o /tmp/basalt_arena_boot.o /tmp/basalt_ff_boot.o /tmp/basalt_accel_boot.o \
	      -ldl -lm; \
	  elif [ -f bin/basaltc_tmp ]; then \
	    mv -f bin/basaltc_tmp bin/basaltc; \
	  fi
	chmod +x bin/basaltc bin/stage0 2>/dev/null || true
	@test -x bin/basaltc
	@echo "refreshed bin/basaltc (self-hosted via stage0 + runtime link)"

clean:
	rm -f examples/*.basm /tmp/basalt_* /tmp/t_* 2>/dev/null || true
