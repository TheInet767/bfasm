// parser.c
// Brainfuck Assembler (bfasm) – parser implementation
//
// Converts .bfasm source into an AST.
// The parser works line by line and tracks declared variables
// so that later codegen can map variable names to tape cell indices.
//
// === Current Limitations & Future Improvements ===
// [LIMIT]   Static arrays – variables and instructions are capped
//           by MAX_VARS / MAX_INSTRUCTIONS. Dynamic allocation
//           (via malloc/realloc) would remove the hard limit.
// [LIMIT]   VAR duplicates are silently ignored; a warning might
//           be helpful for debugging.
// [TODO]    Macro support (MACRO/ENDM) is not implemented yet.
// [TODO]    Nested loops (LOOP/END) are not recognized.
// [TODO]    MOV, ZERO, INPUT, GOTO instructions are missing.
// [TODO]    Better error recovery – currently stops at first error.
// [TODO]    Source location tracking (line/column) for better
//           diagnostics.
// [TODO]    Unit-testable interface – allow parsing from a string
//           buffer, not just a file.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "parser.h"

// ---- internal helpers ----

// Remove trailing newline / carriage return
static void trim_newline(char *line) {
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
        line[--len] = '\0';
    }
}

// Strip leading and trailing whitespace (simple version).
// Modifies the string in place and returns a pointer to the
// first non-space character.
static char* trim(char *str) {
    while (*str == ' ' || *str == '\t') str++;
    char *end = str + strlen(str) - 1;
    while (end > str && (*end == ' ' || *end == '\t')) end--;
    *(end+1) = '\0';
    return str;
}

// ---- main parser ----

AST parse_file(const char *filename) {
    AST ast;
    ast.vars.count = 0;
    ast.inst_count = 0;

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Error: cannot open file '%s'\n", filename);
        ast.inst_count = -1;          // signal error
        return ast;
    }

    char line[256];                  // one source line
    int line_num = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        trim_newline(line);

        // Strip comments: everything after ';' is ignored.
        char *comment = strchr(line, ';');
        if (comment) *comment = '\0';

        char *trimmed = trim(line);
        if (strlen(trimmed) == 0) continue;  // skip blank lines

        // --- VAR declaration ---
        // Syntax: VAR name
        // Variables are assigned tape indices in order of
        // appearance.  The index is simply the position in
        // ast.vars.names[].
        if (strncmp(trimmed, "VAR ", 4) == 0) {
            char *name = trim(trimmed + 4);
            if (strlen(name) == 0) {
                fprintf(stderr, "Error line %d: VAR without name\n", line_num);
                ast.inst_count = -1;
                break;
            }

            // Check for duplicate (silently ignore for now).
            int found = 0;
            for (int i = 0; i < ast.vars.count; i++) {
                if (strcmp(ast.vars.names[i], name) == 0) {
                    found = 1;
                    break;
                }
            }

            if (!found) {
                if (ast.vars.count >= MAX_VARS) {
                    fprintf(stderr, "Error line %d: too many variables (max %d)\n",
                            line_num, MAX_VARS);
                    ast.inst_count = -1;
                    break;
                }
                strncpy(ast.vars.names[ast.vars.count], name, MAX_NAME-1);
                ast.vars.names[ast.vars.count][MAX_NAME-1] = '\0';
                ast.vars.count++;
            }
            // else duplicate – silently ignored (see LIMIT section)
            continue;
        }

        // --- Instructions ---
        Instruction inst;
        memset(&inst, 0, sizeof(inst));   // clear all fields

        // INC var
        if (strncmp(trimmed, "INC ", 4) == 0) {
            inst.type = INST_INC;
            char *name = trim(trimmed + 4);
            if (strlen(name) == 0) {
                fprintf(stderr, "Error line %d: INC without variable\n", line_num);
                ast.inst_count = -1;
                break;
            }
            strncpy(inst.var_name, name, MAX_NAME-1);
            inst.var_name[MAX_NAME-1] = '\0';
        }
        // DEC var
        else if (strncmp(trimmed, "DEC ", 4) == 0) {
            inst.type = INST_DEC;
            char *name = trim(trimmed + 4);
            if (strlen(name) == 0) {
                fprintf(stderr, "Error line %d: DEC without variable\n", line_num);
                ast.inst_count = -1;
                break;
            }
            strncpy(inst.var_name, name, MAX_NAME-1);
            inst.var_name[MAX_NAME-1] = '\0';
        }
        // OUTPUT
        else if (strcmp(trimmed, "OUTPUT") == 0) {
            inst.type = INST_OUTPUT;
            // no variable needed
        }
        else {
            fprintf(stderr, "Error line %d: unknown instruction '%s'\n",
                    line_num, trimmed);
            ast.inst_count = -1;
            break;
        }

        // Add to instruction array
        if (ast.inst_count >= MAX_INSTRUCTIONS) {
            fprintf(stderr, "Error line %d: too many instructions (max %d)\n",
                    line_num, MAX_INSTRUCTIONS);
            ast.inst_count = -1;
            break;
        }
        ast.instructions[ast.inst_count++] = inst;
    }

    fclose(fp);

    // If we broke out of the loop with an error, inst_count is -1.
    return ast;
}
