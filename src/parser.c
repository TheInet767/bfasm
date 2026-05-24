// parser.c
// Brainfuck Assembler (bfasm) – parser implementation
//
// Converts .bfasm source into an AST. Supports macros defined with
// MACRO/ENDM and expands macro calls by text substitution before
// parsing the resulting instructions.
//
// === Current Limitations & Future Improvements ===
// [LIMIT]   Static arrays – variables, instructions, macros are capped.
// [LIMIT]   VAR duplicates are silently ignored; a warning might help.
// [LIMIT]   Macro arguments must be variable names (no numeric literals).
// [LIMIT]   Macros must be defined before use (single-pass parser).
// [TODO]    Nested macro calls (recursive expansion) works but depth not limited.
// [TODO]    INCLUDE directive for external macro libraries.
// [TODO]    Better error recovery – stops at first error.
// [TODO]    Source location tracking (line/column).

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

// ---- macro support ----

// Find a macro by name in the AST's macro table.
static Macro* find_macro(AST *ast, const char *name) {
    for (int i = 0; i < ast->macro_count; i++) {
        if (strcmp(ast->macros[i].name, name) == 0)
            return &ast->macros[i];
    }
    return NULL;
}

// Substitute macro parameters with call arguments in a single line.
// Returns a pointer to a static buffer containing the expanded line.
static char* substitute_line(const char *line, const Macro *m, char *args[]) {
    static char result[256];
    result[0] = '\0';
    const char *p = line;
    while (*p) {
        // skip whitespace – copy verbatim
        if (*p == ' ' || *p == '\t') {
            int len = strlen(result);
            result[len] = *p;
            result[len+1] = '\0';
            p++;
            continue;
        }
        // identifier or keyword
        if (isalpha(*p) || *p == '_') {
            char token[MAX_NAME];
            int i = 0;
            while (isalnum(*p) || *p == '_') {
                if (i < MAX_NAME-1) token[i++] = *p;
                p++;
            }
            token[i] = '\0';
            // check if token matches a parameter name
            int replaced = 0;
            for (int j = 0; j < m->param_count; j++) {
                if (strcmp(token, m->params[j]) == 0) {
                    strcat(result, args[j]);
                    replaced = 1;
                    break;
                }
            }
            if (!replaced) strcat(result, token);
        } else {
            // copy other characters (numbers, commas, etc.)
            int len = strlen(result);
            result[len] = *p;
            result[len+1] = '\0';
            p++;
        }
    }
    return result;
}

// ---- forward declaration of parse_line ----
static int parse_line(char *trimmed, AST *ast, int line_num);

// ---- main parser ----

AST parse_file(const char *filename) {
    AST ast;
    ast.vars.count = 0;
    ast.vars.org_offset = 0;
    ast.inst_count = 0;
    ast.macro_count = 0;

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

        // --- MACRO definition ---
        if (strncmp(trimmed, "MACRO ", 6) == 0) {
            char *def = trim(trimmed + 6);
            char *lparen = strchr(def, '(');
            if (!lparen) {
                fprintf(stderr, "Error line %d: macro definition missing '('\n", line_num);
                ast.inst_count = -1; break;
            }
            *lparen = '\0';
            char *macro_name = trim(def);
            if (ast.macro_count >= MAX_MACROS) {
                fprintf(stderr, "Error line %d: too many macros (max %d)\n", line_num, MAX_MACROS);
                ast.inst_count = -1; break;
            }

            Macro *m = &ast.macros[ast.macro_count];
            strncpy(m->name, macro_name, MAX_MACRO_NAME-1);
            m->name[MAX_MACRO_NAME-1] = '\0';
            m->param_count = 0;
            m->body_line_count = 0;

            char *params_str = lparen + 1;
            char *rparen = strchr(params_str, ')');
            if (!rparen) {
                fprintf(stderr, "Error line %d: macro definition missing ')'\n", line_num);
                ast.inst_count = -1; break;
            }
            *rparen = '\0';
            char *token = strtok(params_str, ",");
            while (token) {
                if (m->param_count >= MAX_MACRO_PARAMS) {
                    fprintf(stderr, "Error line %d: too many macro parameters\n", line_num);
                    ast.inst_count = -1; break;
                }
                char *p = trim(token);
                if (strlen(p) == 0) {
                    fprintf(stderr, "Error line %d: empty macro parameter\n", line_num);
                    ast.inst_count = -1; break;
                }
                strncpy(m->params[m->param_count], p, MAX_NAME-1);
                m->params[m->param_count][MAX_NAME-1] = '\0';
                m->param_count++;
                token = strtok(NULL, ",");
            }
            if (ast.inst_count == -1) break;

            // read macro body until ENDM
            while (fgets(line, sizeof(line), fp)) {
                line_num++;
                trim_newline(line);
                char *c = strchr(line, ';');
                if (c) *c = '\0';
                char *tline = trim(line);
                if (strlen(tline) == 0) continue;
                if (strcmp(tline, "ENDM") == 0) break;
                if (m->body_line_count >= MAX_MACRO_LINES) {
                    fprintf(stderr, "Error line %d: macro body too long\n", line_num);
                    ast.inst_count = -1; break;
                }
                strncpy(m->body[m->body_line_count], tline, 255);
                m->body[m->body_line_count][255] = '\0';
                m->body_line_count++;
            }
            if (ast.inst_count == -1) break;
            ast.macro_count++;
            continue;
        }

        // --- Check for macro call ---
        char first_token[MAX_NAME] = {0};
        sscanf(trimmed, "%63s", first_token);
        Macro *macro = find_macro(&ast, first_token);
        if (macro) {
            const char *args_start = trimmed + strlen(first_token);
            char args_str[256];
            strncpy(args_str, trim((char*)args_start), 255);
            args_str[255] = '\0';

            char *arg_tokens[MAX_MACRO_PARAMS];
            int arg_count = 0;
            char *tok = strtok(args_str, ",");
            while (tok) {
                if (arg_count >= MAX_MACRO_PARAMS) break;
                arg_tokens[arg_count] = trim(tok);
                arg_count++;
                tok = strtok(NULL, ",");
            }
            if (arg_count != macro->param_count) {
                fprintf(stderr, "Error line %d: macro '%s' expects %d arguments, got %d\n",
                        line_num, macro->name, macro->param_count, arg_count);
                ast.inst_count = -1; break;
            }

            // Expand each line of the macro body
            for (int li = 0; li < macro->body_line_count; li++) {
                char *expanded = substitute_line(macro->body[li], macro, arg_tokens);
                if (parse_line(expanded, &ast, line_num) != 0) {
                    ast.inst_count = -1; break;
                }
            }
            if (ast.inst_count == -1) break;
            continue;
        }

        // --- Ordinary instruction (via parse_line) ---
        if (parse_line(trimmed, &ast, line_num) != 0) {
            ast.inst_count = -1; break;
        }
    }

    fclose(fp);
    return ast;
}

// ---- parse_line: parse a single instruction line ----
static int parse_line(char *trimmed, AST *ast, int line_num) {
    if (ast->inst_count >= MAX_INSTRUCTIONS) {
        fprintf(stderr, "Error line %d: too many instructions (max %d)\n",
                line_num, MAX_INSTRUCTIONS);
        return -1;
    }

    Instruction inst;
    memset(&inst, 0, sizeof(inst));
    inst.operand = 1;

    #define PARSE_NUM_OR_DEFAULT(s, default_val) \
        do { \
            char *arg = trim(s); \
            if (*arg != '\0') { \
                if (sscanf(arg, "%d", &inst.operand) != 1 || inst.operand < 0) { \
                    fprintf(stderr, "Error line %d: invalid number '%s'\n", line_num, arg); \
                    return -1; \
                } \
            } else { \
                inst.operand = default_val; \
            } \
        } while(0)

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
                    return -1;
                }
                inst.operand = (int)val;
            }
        }
    }
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
                    return -1;
                }
                inst.operand = (int)val;
            }
        }
    }
    else if (strncmp(trimmed, "ZERO", 4) == 0) {
        inst.type = INST_ZERO;
        char *rest = trim(trimmed + 4);
        if (*rest != '\0') {
            strncpy(inst.var_name, rest, MAX_NAME-1);
            inst.var_name[MAX_NAME-1] = '\0';
        }
    }
    else if (strcmp(trimmed, "INPUT") == 0) {
        inst.type = INST_INPUT;
    }
    else if (strcmp(trimmed, "OUTPUT") == 0) {
        inst.type = INST_OUTPUT;
    }
    else if (strncmp(trimmed, "LOOP", 4) == 0) {
        inst.type = INST_LOOP_START;
        char *rest = trim(trimmed + 4);
        if (*rest != '\0') {
            strncpy(inst.var_name, rest, MAX_NAME-1);
            inst.var_name[MAX_NAME-1] = '\0';
        }
    }
    else if (strcmp(trimmed, "END") == 0) {
        inst.type = INST_LOOP_END;
    }
    else if (strncmp(trimmed, "MOV ", 4) == 0) {
        inst.type = INST_MOV;
        char *args = trim(trimmed + 4);
        char *comma = strchr(args, ',');
        if (!comma) {
            fprintf(stderr, "Error line %d: MOV requires two arguments: MOV src, dst\n", line_num);
            return -1;
        }
        *comma = '\0';
        char *src = trim(args);
        char *dst = trim(comma + 1);
        if (strlen(src) == 0 || strlen(dst) == 0) {
            fprintf(stderr, "Error line %d: MOV requires two variable names\n", line_num);
            return -1;
        }
        strncpy(inst.src_var, src, MAX_NAME-1);
        inst.src_var[MAX_NAME-1] = '\0';
        strncpy(inst.dst_var, dst, MAX_NAME-1);
        inst.dst_var[MAX_NAME-1] = '\0';
    }
    else if (strncmp(trimmed, "GOTO ", 5) == 0) {
        inst.type = INST_GOTO;
        char *name = trim(trimmed + 5);
        if (strlen(name) == 0) {
            fprintf(stderr, "Error line %d: GOTO requires a variable name\n", line_num);
            return -1;
        }
        strncpy(inst.var_name, name, MAX_NAME-1);
        inst.var_name[MAX_NAME-1] = '\0';
    }
    else if (strncmp(trimmed, "RIGHT", 5) == 0) {
        inst.type = INST_RIGHT;
        char *rest = trim(trimmed + 5);
        PARSE_NUM_OR_DEFAULT(rest, 1);
    }
    else if (strncmp(trimmed, "LEFT", 4) == 0) {
        inst.type = INST_LEFT;
        char *rest = trim(trimmed + 4);
        PARSE_NUM_OR_DEFAULT(rest, 1);
    }
    else {
        fprintf(stderr, "Error line %d: unknown instruction '%s'\n", line_num, trimmed);
        return -1;
    }
    #undef PARSE_NUM_OR_DEFAULT

    ast->instructions[ast->inst_count++] = inst;
    return 0;
}
