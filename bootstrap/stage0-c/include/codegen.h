#ifndef BASALT_CODEGEN_H
#define BASALT_CODEGEN_H

#include "ast.h"
#include "basalt.h"

/* Generate x86-64 GAS assembly for the program.
   Returns malloc'd string with the full .s content. */
char *codegen(Node *prog, Options *opt);

/* Write assembly to file and optionally assemble+link */
int emit_and_link(Node *prog, Options *opt, const char *input_path);

#endif
