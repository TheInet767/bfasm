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
    ast_free(&ast);
    return 0;
}
