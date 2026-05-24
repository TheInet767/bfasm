// main.c
// Brainfuck Assembler (bfasm) – entry point
//
// Usage: ./bfasm <file.bfasm>
// Reads the .bfasm source, parses it into an AST, and prints the
// resulting Brainfuck code to stdout.
//
// === Current Limitations & Future Improvements ===
// [TODO]    Support -o flag to write output to a file.
// [TODO]    Support -t flag to target different BF dialects (bf/bf++).
// [TODO]    Better error reporting with source file and line number.

#include <stdio.h>
#include "parser.h"
#include "codegen.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file.bfasm>\n", argv[0]);
        return 1;
    }

    AST ast = parse_file(argv[1]);
    if (ast.inst_count < 0) {
        fprintf(stderr, "Compilation aborted due to errors.\n");
        return 1;
    }

    generate_bf(&ast);
    return 0;
}
