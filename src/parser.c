// parser.c
// Brainfuck Assembler (bfasm) – parser implementation
//
// Converts .bfasm source into an AST.  The parser works line by line
// and tracks declared variables so that later codegen can map variable
// names to tape cell indices.
//
// === Current Limitations & Future Improvements ===
// [LIMIT]   Static arrays – variables and instructions are capped
//           by MAX_VARS / MAX_INSTRUCTIONS. Dynamic allocation
//           (via malloc/realloc) would remove the hard limit.
// [LIMIT]   VAR duplicates are silently ignored; a warning might
//           be helpful for debugging.
// [TODO]    Macro support (MACRO/ENDM) is not implemented yet.
// [TODO]    Nested loops are parsed but not validated for correctness
//           (codegen assumes programmer moves head correctly).
// [TODO]    MOV currently hardcoded to destroy src; a non-destructive
//           copy (using tmp cell) could be added.
// [TODO]    Better error recovery – currently stops at first error.
// [TODO]    Source location tracking (line/column) for better diagnostics.
// [TODO]    Unit-testable interface – allow parsing from a string buffer,
//           not just a file.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "parser.h"

// ---- internal helpers ----

static void trim_newline(char *line) {
    size_t len = strlen(line);
    while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) {
        line[--len] = '\0';
    }
}

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
    ast.vars.org_offset = 0;
    ast.inst_count = 0;

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Error: cannot open file '%s'\n", filename);
        ast.inst_count = -1;
        return ast;
    }

    char line[256];
    int line_num = 0;

    while (fgets(line, sizeof(line), fp)) {
        line_num++;
        trim_newline(line);

        // Strip comments
        char *comment = strchr(line, ';');
        if (comment) *comment = '\0';

        char *trimmed = trim(line);
        if (strlen(trimmed) == 0) continue;

        // --- ORG directive ---
        if (strncmp(trimmed, "ORG ", 4) == 0) {
            int offset = 0;
            if (sscanf(trimmed + 4, "%d", &offset) != 1 || offset < 0) {
                fprintf(stderr, "Error line %d: invalid ORG value\n", line_num);
                ast.inst_count = -1; break;
            }
            ast.vars.org_offset = offset;
            continue;
        }

        // --- RESERVE directive ---
        if (strncmp(trimmed, "RESERVE ", 8) == 0) {
            int n = 0;
            if (sscanf(trimmed + 8, "%d", &n) != 1 || n < 0) {
                fprintf(stderr, "Error line %d: invalid RESERVE value\n", line_num);
                ast.inst_count = -1; break;
            }
            ast.vars.count += n;
            continue;
        }

        // --- VAR declaration ---
        if (strncmp(trimmed, "VAR ", 4) == 0) {
            char *name = trim(trimmed + 4);
            if (strlen(name) == 0) {
                fprintf(stderr, "Error line %d: VAR without name\n", line_num);
                ast.inst_count = -1; break;
            }
            int found = 0;
            for (int i = 0; i < ast.vars.count; i++) {
                if (strcmp(ast.vars.names[i], name) == 0) {
                    found = 1; break;
                }
            }
            if (!found) {
                if (ast.vars.count >= MAX_VARS) {
                    fprintf(stderr, "Error line %d: too many variables (max %d)\n",
                            line_num, MAX_VARS);
                    ast.inst_count = -1; break;
                }
                strncpy(ast.vars.names[ast.vars.count], name, MAX_NAME-1);
                ast.vars.names[ast.vars.count][MAX_NAME-1] = '\0';
                ast.vars.count++;
            }
            continue;
        }

        // --- Instructions ---
        Instruction inst;
        memset(&inst, 0, sizeof(inst));
        inst.operand = 1;  // default

        // Helper macro for RIGHT/LEFT numeric argument
        #define PARSE_NUM_OR_DEFAULT(s, default_val) \
            do { \
                char *arg = trim(s); \
                if (*arg != '\0') { \
                    if (sscanf(arg, "%d", &inst.operand) != 1 || inst.operand < 0) { \
                        fprintf(stderr, "Error line %d: invalid number '%s'\n", line_num, arg); \
                        ast.inst_count = -1; break; \
                    } \
                } else { \
                    inst.operand = default_val; \
                } \
            } while(0)

        // --- INC ---
        if (strncmp(trimmed, "INC", 3) == 0) {
            inst.type = INST_INC;
            char *rest = trim(trimmed + 3);
            if (*rest == '\0') {
                inst.operand = 1;
            } else {
                char first_token[MAX_NAME] = {0};
                char second_token[MAX_NAME] = {0};
                int parsed = sscanf(rest, "%63s %63s", first_token, second_token);
                if (parsed == 1) {
                    char *endp;
                    long val = strtol(first_token, &endp, 10);
                    if (*endp == '\0' && val >= 0) {
                        inst.operand = (int)val;
                    } else {
                        strncpy(inst.var_name, first_token, MAX_NAME-1);
                        inst.var_name[MAX_NAME-1] = '\0';
                        inst.operand = 1;
                    }
                } else if (parsed == 2) {
                    strncpy(inst.var_name, first_token, MAX_NAME-1);
                    inst.var_name[MAX_NAME-1] = '\0';
                    char *endp;
                    long val = strtol(second_token, &endp, 10);
                    if (*endp != '\0' || val < 0) {
                        fprintf(stderr, "Error line %d: invalid number '%s' after variable\n", line_num, second_token);
                        ast.inst_count = -1; break;
                    }
                    inst.operand = (int)val;
                }
            }
        }
        // --- DEC ---
        else if (strncmp(trimmed, "DEC", 3) == 0) {
            inst.type = INST_DEC;
            char *rest = trim(trimmed + 3);
            if (*rest == '\0') {
                inst.operand = 1;
            } else {
                char first_token[MAX_NAME] = {0};
                char second_token[MAX_NAME] = {0};
                int parsed = sscanf(rest, "%63s %63s", first_token, second_token);
                if (parsed == 1) {
                    char *endp;
                    long val = strtol(first_token, &endp, 10);
                    if (*endp == '\0' && val >= 0) {
                        inst.operand = (int)val;
                    } else {
                        strncpy(inst.var_name, first_token, MAX_NAME-1);
                        inst.var_name[MAX_NAME-1] = '\0';
                        inst.operand = 1;
                    }
                } else if (parsed == 2) {
                    strncpy(inst.var_name, first_token, MAX_NAME-1);
                    inst.var_name[MAX_NAME-1] = '\0';
                    char *endp;
                    long val = strtol(second_token, &endp, 10);
                    if (*endp != '\0' || val < 0) {
                        fprintf(stderr, "Error line %d: invalid number '%s' after variable\n", line_num, second_token);
                        ast.inst_count = -1; break;
                    }
                    inst.operand = (int)val;
                }
            }
        }
        // --- ZERO ---
        else if (strncmp(trimmed, "ZERO", 4) == 0) {
            inst.type = INST_ZERO;
            char *rest = trim(trimmed + 4);
            if (*rest != '\0') {
                strncpy(inst.var_name, rest, MAX_NAME-1);
                inst.var_name[MAX_NAME-1] = '\0';
            }
        }
        // --- INPUT ---
        else if (strcmp(trimmed, "INPUT") == 0) {
            inst.type = INST_INPUT;
        }
        // --- OUTPUT ---
        else if (strcmp(trimmed, "OUTPUT") == 0) {
            inst.type = INST_OUTPUT;
        }
        // --- LOOP ---
        else if (strncmp(trimmed, "LOOP", 4) == 0) {
            inst.type = INST_LOOP_START;
            char *rest = trim(trimmed + 4);
            if (*rest != '\0') {
                strncpy(inst.var_name, rest, MAX_NAME-1);
                inst.var_name[MAX_NAME-1] = '\0';
            }
        }
        // --- END ---
        else if (strcmp(trimmed, "END") == 0) {
            inst.type = INST_LOOP_END;
        }
        // --- MOV ---
        else if (strncmp(trimmed, "MOV ", 4) == 0) {
            inst.type = INST_MOV;
            char *args = trim(trimmed + 4);
            char *comma = strchr(args, ',');
            if (!comma) {
                fprintf(stderr, "Error line %d: MOV requires two arguments: MOV src, dst\n", line_num);
                ast.inst_count = -1; break;
            }
            *comma = '\0';
            char *src = trim(args);
            char *dst = trim(comma + 1);
            if (strlen(src) == 0 || strlen(dst) == 0) {
                fprintf(stderr, "Error line %d: MOV requires two variable names\n", line_num);
                ast.inst_count = -1; break;
            }
            strncpy(inst.src_var, src, MAX_NAME-1);
            inst.src_var[MAX_NAME-1] = '\0';
            strncpy(inst.dst_var, dst, MAX_NAME-1);
            inst.dst_var[MAX_NAME-1] = '\0';
        }
        // --- GOTO ---
        else if (strncmp(trimmed, "GOTO ", 5) == 0) {
            inst.type = INST_GOTO;
            char *name = trim(trimmed + 5);
            if (strlen(name) == 0) {
                fprintf(stderr, "Error line %d: GOTO requires a variable name\n", line_num);
                ast.inst_count = -1; break;
            }
            strncpy(inst.var_name, name, MAX_NAME-1);
            inst.var_name[MAX_NAME-1] = '\0';
        }
        // --- RIGHT ---
        else if (strncmp(trimmed, "RIGHT", 5) == 0) {
            inst.type = INST_RIGHT;
            char *rest = trim(trimmed + 5);
            PARSE_NUM_OR_DEFAULT(rest, 1);
        }
        // --- LEFT ---
        else if (strncmp(trimmed, "LEFT", 4) == 0) {
            inst.type = INST_LEFT;
            char *rest = trim(trimmed + 4);
            PARSE_NUM_OR_DEFAULT(rest, 1);
        }
        else {
            fprintf(stderr, "Error line %d: unknown instruction '%s'\n", line_num, trimmed);
            ast.inst_count = -1; break;
        }
        #undef PARSE_NUM_OR_DEFAULT

        if (ast.inst_count >= MAX_INSTRUCTIONS) {
            fprintf(stderr, "Error line %d: too many instructions (max %d)\n",
                    line_num, MAX_INSTRUCTIONS);
            ast.inst_count = -1; break;
        }
        ast.instructions[ast.inst_count++] = inst;
    }

    fclose(fp);
    return ast;
}
