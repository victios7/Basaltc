#include "basalt.h"
#include "codegen.h"

static void options_init(Options *o) {
    memset(o, 0, sizeof(*o));
    o->target     = TARGET_ELF64;
    o->opt_level  = 1;
    o->emit_asm   = false;
    o->emit_obj   = false;
    o->verbose    = false;
    o->dump_ast   = false;
    o->dump_ir    = false;
    o->output     = NULL;
    o->inputs     = NULL;
    o->ninputs    = 0;
}

void options_usage(const char *prog) {
    fprintf(stderr,
        "Basalt compiler (Stage 0) — bootstrap\n"
        "Usage: %s [options] file.bd ...\n\n"
        "Targets:\n"
        "  -l              ELF64 Linux (default)\n"
        "  -W              PE64 Windows (native, no mingw)\n\n"
        "Optimization:\n"
        "  -O0 -O1 -O2 -O3  Optimization level (default -O1)\n\n"
        "Output:\n"
        "  -o <file>       Output file name\n"
        "  -S              Emit assembly only\n"
        "  -c              Emit object file only\n\n"
        "Debug:\n"
        "  -v              Verbose\n"
        "  --dump-ast      Dump AST after parse\n"
        "  -h, --help      This help\n",
        prog);
}

void options_parse(Options *opt, int argc, char **argv) {
    options_init(opt);

    for (int i = 1; i < argc; i++) {
        char *a = argv[i];
        if (a[0] != '-') {
            opt->inputs = xrealloc(opt->inputs, (opt->ninputs + 1) * sizeof(char *));
            opt->inputs[opt->ninputs++] = a;
            continue;
        }

        if (!strcmp(a, "-l")) {
            opt->target = TARGET_ELF64;
        } else if (!strcmp(a, "-W")) {
            opt->target = TARGET_PE64;
        } else if (!strcmp(a, "-O0")) {
            opt->opt_level = 0;
        } else if (!strcmp(a, "-O1")) {
            opt->opt_level = 1;
        } else if (!strcmp(a, "-O2")) {
            opt->opt_level = 2;
        } else if (!strcmp(a, "-O3")) {
            opt->opt_level = 3;
        } else if (!strcmp(a, "-S")) {
            opt->emit_asm = true;
        } else if (!strcmp(a, "-c")) {
            opt->emit_obj = true;
        } else if (!strcmp(a, "-v")) {
            opt->verbose = true;
        } else if (!strcmp(a, "--dump-ast")) {
            opt->dump_ast = true;
        } else if (!strcmp(a, "--dump-ir")) {
            opt->dump_ir = true;
        } else if (!strcmp(a, "-o")) {
            if (i + 1 >= argc) die("-o needs an argument");
            opt->output = argv[++i];
        } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
            options_usage(argv[0]);
            exit(0);
        } else {
            die("unknown option '%s'", a);
        }
    }

    if (opt->ninputs == 0) {
        options_usage(argv[0]);
        exit(1);
    }
}

int main(int argc, char **argv) {
    Options opt;
    options_parse(&opt, argc, argv);

    type_init();

    for (int i = 0; i < opt.ninputs; i++) {
        if (opt.verbose)
            fprintf(stderr, "compiling %s ...\n", opt.inputs[i]);

        Source *src = source_load(opt.inputs[i]);

        int ntoks = 0;
        Token *toks = lex(src, &ntoks);

        if (opt.verbose)
            fprintf(stderr, "  %d tokens\n", ntoks);

        Node *prog = parse(toks, ntoks, src);

        if (opt.dump_ast) {
            fprintf(stderr, "=== AST ===\n");
            ast_print(prog, 0);
        }

        sema(prog, src);

        if (opt.verbose)
            fprintf(stderr, "  semantic analysis OK\n");

        emit_and_link(prog, &opt, opt.inputs[i]);

        free(toks);
        source_free(src);
    }

    return 0;
}
