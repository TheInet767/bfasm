// codegen.h
// Brainfuck Assembler (bfasm) – code generator header
// Translates the AST produced by the parser into a Brainfuck output string.

#ifndef CODEGEN_H
#define CODEGEN_H

#include "parser.h"

// Generate Brainfuck code from the given AST.
// The result is printed to stdout.
void generate_bf(const AST *ast);

#endif
