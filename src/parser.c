// parser.c
// Brainfuck Assembler (bfasm) – parser implementation
//
// Converts .bfasm source into an AST. Supports macros defined with
// MACRO/ENDM, INCLUDE directive with AS/KEEP, and @-notation for
// explicit library selection.
//
// === Current Limitations & Future Improvements ===
// [TODO]    Nested macro calls (recursive expansion) works but depth not limited.
// [TODO]    INCLUDE for files inside standard library paths.
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

// ---- dynamic memory helpers ----

static int add_variable(AST *ast, const char *name) {
    VarTable *vt = &ast->vars;
    for (int i = 0; i < vt->count; i++) {
        if (strcmp(vt->names[i], name) == 0) return 0;
    }
    if (vt->count >= vt->capacity) {
        int new_cap = vt->capacity * 2;
        char (*new_names)[MAX_NAME] = realloc(vt->names, new_cap * sizeof(char[MAX_NAME]));
        if (!new_names) {
            fprintf(stderr, "Error: out of memory for variables\n");
            return -1;
        }
        vt->names = new_names;
        vt->capacity = new_cap;
    }
    strncpy(vt->names[vt->count], name, MAX_NAME-1);
    vt->names[vt->count][MAX_NAME-1] = '\0';
    vt->count++;
    return 0;
}

static int add_instruction(AST *ast, const Instruction *inst) {
    if (ast->inst_count >= ast->inst_capacity) {
        int new_cap = ast->inst_capacity * 2;
        Instruction *new_inst = realloc(ast->instructions, new_cap * sizeof(Instruction));
        if (!new_inst) {
            fprintf(stderr, "Error: out of memory for instructions\n");
            return -1;
        }
        ast->instructions = new_inst;
        ast->inst_capacity = new_cap;
    }
    ast->instructions[ast->inst_count++] = *inst;
    return 0;
}

static int add_macro(AST *ast, const Macro *macro) {
    if (ast->macro_count >= ast->macro_capacity) {
        int new_cap = ast->macro_capacity * 2;
        Macro *new_macros = realloc(ast->macros, new_cap * sizeof(Macro));
        if (!new_macros) {
            fprintf(stderr, "Error: out of memory for macros\n");
            return -1;
        }
        ast->macros = new_macros;
        ast->macro_capacity = new_cap;
    }
    ast->macros[ast->macro_count++] = *macro;
    return 0;
}

void ast_free(AST *ast) {
    free(ast->vars.names);
    free(ast->instructions);
    free(ast->macros);
    memset(ast, 0, sizeof(*ast));
}

// ---- forward declarations ----
static int parse_line(char *trimmed, AST *ast, int line_num);
static void parse_macros_from_file(const char *filename, const char *alias,
                                   AST *ast, char **keep_names, int keep_count);
static int process_expanded_line(char *trimmed, AST *ast, int line_num);

// ---- macro support ----

static Macro* find_macro(AST *ast, const char *name, const char *alias) {
    for (int i = 0; i < ast->macro_count; i++) {
        if (strcmp(ast->macros[i].name, name) == 0) {
            if (alias == NULL || alias[0] == '\0') {
                return &ast->macros[i];
            } else if (strcmp(ast->macros[i].lib_alias, alias) == 0) {
                return &ast->macros[i];
            }
        }
    }
    return NULL;
}

static char* substitute_line(const char *line, const Macro *m, char *args[]) {
    static char result[256];
    result[0] = '\0';
    const char *p = line;
    while (*p) {
        if (*p == ' ' || *p == '\t') {
            int len = strlen(result);
            result[len] = *p;
            result[len+1] = '\0';
            p++;
            continue;
        }
        if (isalpha(*p) || *p == '_') {
            char token[MAX_NAME];
            int i = 0;
            while (isalnum(*p) || *p == '_') {
                if (i < MAX_NAME-1) token[i++] = *p;
                p++;
            }
            token[i] = '\0';
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
            int len = strlen(result);
            result[len] = *p;
            result[len+1] = '\0';
            p++;
        }
    }
    return result;
}

// ---- INCLUDE and macro loading ----

static void parse_macros_from_file(const char *filename, const char *alias,
                                   AST *ast, char **keep_names, int keep_count) {
    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Error: cannot open included file '%s'\n", filename);
        ast->inst_count = -1;
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        trim_newline(line);
        char *comment = strchr(line, ';');
        if (comment) *comment = '\0';
        char *trimmed = trim(line);
        if (strlen(trimmed) == 0) continue;

        if (strncmp(trimmed, "MACRO ", 6) == 0) {
            char *def = trim(trimmed + 6);
            char *lparen = strchr(def, '(');
            if (!lparen) {
                fprintf(stderr, "Error in included file '%s': macro missing '('.\n", filename);
                ast->inst_count = -1; break;
            }
            *lparen = '\0';
            char *macro_name = trim(def);

            if (keep_count > 0) {
                int found = 0;
                for (int i = 0; i < keep_count; i++) {
                    if (strcmp(macro_name, keep_names[i]) == 0) { found = 1; break; }
                }
                if (!found) {
                    while (fgets(line, sizeof(line), fp)) {
                        trim_newline(line);
                        char *c = strchr(line, ';'); if (c) *c = '\0';
                        char *t = trim(line);
                        if (strlen(t) == 0) continue;
                        if (strncmp(t, "ENDM", 4) == 0 && strlen(t) == 4) break;
                    }
                    continue;
                }
            }

            Macro m;
            memset(&m, 0, sizeof(m));
            strncpy(m.name, macro_name, MAX_MACRO_NAME-1);
            m.name[MAX_MACRO_NAME-1] = '\0';
            strncpy(m.lib_alias, alias ? alias : "", MAX_NAME-1);
            m.lib_alias[MAX_NAME-1] = '\0';

            char *params_str = lparen + 1;
            char *rparen = strchr(params_str, ')');
            if (!rparen) {
                fprintf(stderr, "Error in macro definition in '%s': missing ')'\n", filename);
                ast->inst_count = -1; break;
            }
            *rparen = '\0';
            char *token = strtok(params_str, ",");
            while (token) {
                if (m.param_count >= MAX_MACRO_PARAMS) {
                    fprintf(stderr, "Error: too many macro parameters\n");
                    ast->inst_count = -1; break;
                }
                char *p = trim(token);
                if (strlen(p) == 0) {
                    fprintf(stderr, "Error: empty macro parameter\n");
                    ast->inst_count = -1; break;
                }
                strncpy(m.params[m.param_count], p, MAX_NAME-1);
                m.params[m.param_count][MAX_NAME-1] = '\0';
                m.param_count++;
                token = strtok(NULL, ",");
            }
            if (ast->inst_count == -1) break;

            while (fgets(line, sizeof(line), fp)) {
                trim_newline(line);
                char *c = strchr(line, ';'); if (c) *c = '\0';
                char *tline = trim(line);
                if (strlen(tline) == 0) continue;
                if (strcmp(tline, "ENDM") == 0) break;
                if (m.body_line_count >= MAX_MACRO_LINES) {
                    fprintf(stderr, "Error: macro body too long\n");
                    ast->inst_count = -1; break;
                }
                strncpy(m.body[m.body_line_count], tline, 255);
                m.body[m.body_line_count][255] = '\0';
                m.body_line_count++;
            }
            if (ast->inst_count == -1) break;

            if (add_macro(ast, &m) != 0) {
                ast->inst_count = -1; break;
            }
        }
    }

    fclose(fp);
    if (keep_count > 0 && ast->inst_count != -1) {
        for (int i = 0; i < keep_count; i++) {
            int found = 0;
            for (int j = 0; j < ast->macro_count; j++) {
                if (strcmp(ast->macros[j].name, keep_names[i]) == 0 &&
                    strcmp(ast->macros[j].lib_alias, alias ? alias : "") == 0) {
                    found = 1; break;
                }
            }
            if (!found) {
                fprintf(stderr, "Error: macro '%s' not found in '%s'\n", keep_names[i], filename);
                ast->inst_count = -1;
            }
        }
    }
}

// ---- process_expanded_line ----
static int process_expanded_line(char *trimmed, AST *ast, int line_num) {
    char first_token[MAX_NAME] = {0};
    sscanf(trimmed, "%63s", first_token);
    int first_token_len = strlen(first_token);
    char *at_sign = strchr(first_token, '@');
    char call_name[MAX_MACRO_NAME];
    char call_alias[MAX_NAME] = "";
    if (at_sign) {
        *at_sign = '\0';
        strncpy(call_name, first_token, MAX_MACRO_NAME-1);
        call_name[MAX_MACRO_NAME-1] = '\0';
        strncpy(call_alias, at_sign + 1, MAX_NAME-1);
        call_alias[MAX_NAME-1] = '\0';
    } else {
        strncpy(call_name, first_token, MAX_MACRO_NAME-1);
        call_name[MAX_MACRO_NAME-1] = '\0';
    }

    Macro *macro = find_macro(ast, call_name, at_sign ? call_alias : NULL);
    if (macro) {
        const char *args_start = trimmed + first_token_len;
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
            return -1;
        }

        for (int li = 0; li < macro->body_line_count; li++) {
            char *expanded = substitute_line(macro->body[li], macro, arg_tokens);
            if (process_expanded_line(expanded, ast, line_num) != 0) {
                return -1;
            }
        }
        return 0;
    } else if (at_sign) {
        fprintf(stderr, "Error line %d: macro '%s@%s' not found\n", line_num, call_name, call_alias);
        return -1;
    }

    return parse_line(trimmed, ast, line_num);
}

// ---- main parser ----

AST parse_file(const char *filename) {
    AST ast;
    memset(&ast, 0, sizeof(ast));

    ast.vars.capacity = INIT_VARS_CAPACITY;
    ast.vars.names = malloc(ast.vars.capacity * sizeof(char[MAX_NAME]));
    if (!ast.vars.names) {
        fprintf(stderr, "Error: out of memory\n");
        ast.inst_count = -1;
        return ast;
    }

    ast.inst_capacity = INIT_INSTR_CAPACITY;
    ast.instructions = malloc(ast.inst_capacity * sizeof(Instruction));
    if (!ast.instructions) {
        fprintf(stderr, "Error: out of memory\n");
        free(ast.vars.names);
        ast.inst_count = -1;
        return ast;
    }

    ast.macro_capacity = INIT_MACROS_CAPACITY;
    ast.macros = malloc(ast.macro_capacity * sizeof(Macro));
    if (!ast.macros) {
        fprintf(stderr, "Error: out of memory\n");
        free(ast.vars.names);
        free(ast.instructions);
        ast.inst_count = -1;
        return ast;
    }

    FILE *fp = fopen(filename, "r");
    if (!fp) {
        fprintf(stderr, "Error: cannot open file '%s'\n", filename);
        ast_free(&ast);
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

        // --- INCLUDE directive ---
        if (strncmp(trimmed, "INCLUDE ", 8) == 0) {
            char *rest = trim(trimmed + 8);
            if (*rest != '"') {
                fprintf(stderr, "Error line %d: INCLUDE filename must be in double quotes\n", line_num);
                ast.inst_count = -1; break;
            }
            rest++;
            char *closing_quote = strchr(rest, '"');
            if (!closing_quote) {
                fprintf(stderr, "Error line %d: missing closing quote for INCLUDE filename\n", line_num);
                ast.inst_count = -1; break;
            }
            *closing_quote = '\0';
            char *include_filename = rest;
            rest = closing_quote + 1;

            char alias[MAX_NAME] = "";
            char *keep_macros[32];
            int keep_count = 0;

            while (*rest) {
                rest = trim(rest);
                if (strncmp(rest, "AS ", 3) == 0) {
                    rest += 3;
                    char *end = rest;
                    while (*end && *end != ' ' && *end != '\t') end++;
                    if (end == rest) {
                        fprintf(stderr, "Error line %d: AS requires an alias\n", line_num);
                        ast.inst_count = -1; break;
                    }
                    int len = end - rest;
                    if (len >= MAX_NAME) len = MAX_NAME-1;
                    strncpy(alias, rest, len);
                    alias[len] = '\0';
                    rest = end;
                } else if (strncmp(rest, "KEEP ", 5) == 0) {
                    rest += 5;
                    static char keep_name_buf[32][MAX_NAME];
                    keep_count = 0;
                    char *tok = strtok(rest, ",");
                    while (tok) {
                        char *name = trim(tok);
                        if (strlen(name) == 0) {
                            fprintf(stderr, "Error line %d: empty KEEP argument\n", line_num);
                            ast.inst_count = -1; break;
                        }
                        if (keep_count >= 32) {
                            fprintf(stderr, "Error line %d: too many KEEP arguments\n", line_num);
                            ast.inst_count = -1; break;
                        }
                        strncpy(keep_name_buf[keep_count], name, MAX_NAME-1);
                        keep_name_buf[keep_count][MAX_NAME-1] = '\0';
                        keep_macros[keep_count] = keep_name_buf[keep_count];
                        keep_count++;
                        tok = strtok(NULL, ",");
                    }
                    if (ast.inst_count == -1) break;
                    break;
                } else {
                    fprintf(stderr, "Error line %d: unexpected token after INCLUDE filename\n", line_num);
                    ast.inst_count = -1; break;
                }
                if (ast.inst_count == -1) break;
            }
            if (ast.inst_count == -1) break;

            if (alias[0] == '\0') {
                const char *base = strrchr(include_filename, '/');
                if (base) base++; else base = include_filename;
                const char *dot = strrchr(base, '.');
                int len = dot ? (dot - base) : (int)strlen(base);
                if (len >= MAX_NAME) len = MAX_NAME-1;
                strncpy(alias, base, len);
                alias[len] = '\0';
            }

            parse_macros_from_file(include_filename, alias, &ast,
                                   keep_count > 0 ? keep_macros : NULL, keep_count);
            if (ast.inst_count == -1) break;
            continue;
        }

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
            for (int i = 0; i < n; i++) {
                if (add_variable(&ast, "") != 0) {
                    ast.inst_count = -1; break;
                }
            }
            if (ast.inst_count == -1) break;
            continue;
        }

        // --- VAR declaration ---
        if (strncmp(trimmed, "VAR ", 4) == 0) {
            char *name = trim(trimmed + 4);
            if (strlen(name) == 0) {
                fprintf(stderr, "Error line %d: VAR without name\n", line_num);
                ast.inst_count = -1; break;
            }
            if (add_variable(&ast, name) != 0) {
                ast.inst_count = -1; break;
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

            Macro m;
            memset(&m, 0, sizeof(m));
            strncpy(m.name, macro_name, MAX_MACRO_NAME-1);
            m.name[MAX_MACRO_NAME-1] = '\0';
            m.lib_alias[0] = '\0';

            char *params_str = lparen + 1;
            char *rparen = strchr(params_str, ')');
            if (!rparen) {
                fprintf(stderr, "Error line %d: macro definition missing ')'\n", line_num);
                ast.inst_count = -1; break;
            }
            *rparen = '\0';
            char *token = strtok(params_str, ",");
            while (token) {
                if (m.param_count >= MAX_MACRO_PARAMS) {
                    fprintf(stderr, "Error line %d: too many macro parameters\n", line_num);
                    ast.inst_count = -1; break;
                }
                char *p = trim(token);
                if (strlen(p) == 0) {
                    fprintf(stderr, "Error line %d: empty macro parameter\n", line_num);
                    ast.inst_count = -1; break;
                }
                strncpy(m.params[m.param_count], p, MAX_NAME-1);
                m.params[m.param_count][MAX_NAME-1] = '\0';
                m.param_count++;
                token = strtok(NULL, ",");
            }
            if (ast.inst_count == -1) break;

            while (fgets(line, sizeof(line), fp)) {
                line_num++;
                trim_newline(line);
                char *c = strchr(line, ';'); if (c) *c = '\0';
                char *tline = trim(line);
                if (strlen(tline) == 0) continue;
                if (strcmp(tline, "ENDM") == 0) break;
                if (m.body_line_count >= MAX_MACRO_LINES) {
                    fprintf(stderr, "Error line %d: macro body too long\n", line_num);
                    ast.inst_count = -1; break;
                }
                strncpy(m.body[m.body_line_count], tline, 255);
                m.body[m.body_line_count][255] = '\0';
                m.body_line_count++;
            }
            if (ast.inst_count == -1) break;

            if (add_macro(&ast, &m) != 0) {
                ast.inst_count = -1; break;
            }
            continue;
        }

        // --- Raw BF and other directives ---
        if (strncmp(trimmed, "BF ", 3) == 0) {
            char *start = trim(trimmed + 3);
            char raw[RAWBF_MAX] = {0};
            int len = 0;
            if (*start == '"') {
                start++;
                char *end = strchr(start, '"');
                if (!end) {
                    fprintf(stderr, "Error line %d: missing closing quote for BF\n", line_num);
                    ast.inst_count = -1; break;
                }
                len = end - start;
                if (len >= RAWBF_MAX) {
                    fprintf(stderr, "Error line %d: raw BF string too long\n", line_num);
                    ast.inst_count = -1; break;
                }
                strncpy(raw, start, len);
                raw[len] = '\0';
            } else {
                strncpy(raw, start, RAWBF_MAX-1);
                raw[RAWBF_MAX-1] = '\0';
                len = strlen(raw);
                int j = 0;
                for (int i = 0; raw[i]; i++) {
                    if (raw[i] != ' ' && raw[i] != '\t') raw[j++] = raw[i];
                }
                raw[j] = '\0';
                len = j;
            }
            int ok = 1;
            for (int i = 0; i < len; i++) {
                if (!strchr("><+-.,[]", raw[i])) {
                    fprintf(stderr, "Error line %d: invalid character '%c' in raw BF\n", line_num, raw[i]);
                    ok = 0; break;
                }
            }
            if (!ok) { ast.inst_count = -1; break; }

            Instruction inst;
            memset(&inst, 0, sizeof(inst));
            inst.type = INST_RAWBF;
            strncpy(inst.raw_bf, raw, RAWBF_MAX-1);
            inst.raw_bf[RAWBF_MAX-1] = '\0';
            if (add_instruction(&ast, &inst) != 0) {
                ast.inst_count = -1; break;
            }
            continue;
        }

        if (strncmp(trimmed, "INCLUDEBF ", 10) == 0) {
            char *rest = trim(trimmed + 10);
            if (*rest != '"') {
                fprintf(stderr, "Error line %d: INCLUDEBF filename must be in double quotes\n", line_num);
                ast.inst_count = -1; break;
            }
            rest++;
            char *closing = strchr(rest, '"');
            if (!closing) {
                fprintf(stderr, "Error line %d: missing closing quote for INCLUDEBF filename\n", line_num);
                ast.inst_count = -1; break;
            }
            *closing = '\0';
            char *filename = rest;

            FILE *bf_fp = fopen(filename, "r");
            if (!bf_fp) {
                fprintf(stderr, "Error line %d: cannot open BF file '%s'\n", line_num, filename);
                ast.inst_count = -1; break;
            }
            char raw[RAWBF_MAX] = {0};
            int len = 0;
            int ch;
            while ((ch = fgetc(bf_fp)) != EOF && len < RAWBF_MAX-1) {
                if (!strchr("><+-.,[]", ch)) {
                    fprintf(stderr, "Error line %d: invalid character '%c' in BF file '%s'\n", line_num, ch, filename);
                    fclose(bf_fp);
                    ast.inst_count = -1; break;
                }
                raw[len++] = (char)ch;
            }
            fclose(bf_fp);
            if (ast.inst_count == -1) break;
            raw[len] = '\0';

            Instruction inst;
            memset(&inst, 0, sizeof(inst));
            inst.type = INST_RAWBF;
            strncpy(inst.raw_bf, raw, RAWBF_MAX-1);
            inst.raw_bf[RAWBF_MAX-1] = '\0';
            if (add_instruction(&ast, &inst) != 0) {
                ast.inst_count = -1; break;
            }
            continue;
        }

        if (strcmp(trimmed, "BEGINBF") == 0) {
            char raw[RAWBF_MAX] = {0};
            int len = 0;
            while (fgets(line, sizeof(line), fp)) {
                line_num++;
                trim_newline(line);
                int j = 0;
                for (int i = 0; line[i]; i++) {
                    if (line[i] != ' ' && line[i] != '\t')
                        line[j++] = line[i];
                }
                line[j] = '\0';
                if (strcmp(line, "END") == 0) break;

                for (int i = 0; line[i]; i++) {
                    if (!strchr("><+-.,[]", line[i])) {
                        fprintf(stderr, "Error line %d: invalid character '%c' in BEGINBF block\n", line_num, line[i]);
                        ast.inst_count = -1; break;
                    }
                }
                if (ast.inst_count == -1) break;

                int add_len = strlen(line);
                if (len + add_len >= RAWBF_MAX) {
                    fprintf(stderr, "Error line %d: raw BF block too long\n", line_num);
                    ast.inst_count = -1; break;
                }
                strncpy(raw + len, line, add_len);
                len += add_len;
            }
            if (ast.inst_count == -1) break;

            Instruction inst;
            memset(&inst, 0, sizeof(inst));
            inst.type = INST_RAWBF;
            strncpy(inst.raw_bf, raw, RAWBF_MAX-1);
            inst.raw_bf[RAWBF_MAX-1] = '\0';
            if (add_instruction(&ast, &inst) != 0) {
                ast.inst_count = -1; break;
            }
            continue;
        }

        // --- Ordinary instruction (or macro call) ---
        if (process_expanded_line(trimmed, &ast, line_num) != 0) {
            ast.inst_count = -1; break;
        }
    }

    fclose(fp);
    return ast;
}

// ---- parse_line: include MOVEBY, MOVEBY_LEFT and CMP_GE ----
static int parse_line(char *trimmed, AST *ast, int line_num) {
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
    // --- MOVEBY var ---
    else if (strncmp(trimmed, "MOVEBY ", 7) == 0) {
        inst.type = INST_MOVEBY;
        char *name = trim(trimmed + 7);
        if (strlen(name) == 0) {
            fprintf(stderr, "Error line %d: MOVEBY requires a variable\n", line_num);
            return -1;
        }
        strncpy(inst.var_name, name, MAX_NAME-1);
        inst.var_name[MAX_NAME-1] = '\0';
    }
    // --- MOVEBY_LEFT var ---
    else if (strncmp(trimmed, "MOVEBY_LEFT ", 12) == 0) {
        inst.type = INST_MOVEBY_LEFT;
        char *name = trim(trimmed + 12);
        if (strlen(name) == 0) {
            fprintf(stderr, "Error line %d: MOVEBY_LEFT requires a variable\n", line_num);
            return -1;
        }
        strncpy(inst.var_name, name, MAX_NAME-1);
        inst.var_name[MAX_NAME-1] = '\0';
    }
    // --- CMP_GE result, a, b, t1, t2 ---
    else if (strncmp(trimmed, "CMP_GE ", 7) == 0) {
    inst.type = INST_CMP_GE;
    char *args = trim(trimmed + 7);

    // Извлекаем первый аргумент (result)
    char *comma = strchr(args, ',');
    if (!comma) { fprintf(stderr, "Error line %d: CMP_GE expects at least 5 arguments\n", line_num); return -1; }
    *comma = '\0';
    char *result = trim(args);
    args = trim(comma + 1);

    // a
    comma = strchr(args, ',');
    if (!comma) { fprintf(stderr, "Error line %d: CMP_GE expects at least 5 arguments\n", line_num); return -1; }
    *comma = '\0';
    char *a = trim(args);
    args = trim(comma + 1);

    // b
    comma = strchr(args, ',');
    if (!comma) { fprintf(stderr, "Error line %d: CMP_GE expects at least 5 arguments\n", line_num); return -1; }
    *comma = '\0';
    char *b = trim(args);
    args = trim(comma + 1);

    // t1
    comma = strchr(args, ',');
    if (!comma) { fprintf(stderr, "Error line %d: CMP_GE expects at least 5 arguments\n", line_num); return -1; }
    *comma = '\0';
    char *t1 = trim(args);
    args = trim(comma + 1);

    // t2 (может содержать запятую, если есть шестой аргумент)
    char *t2 = args;
    comma = strchr(t2, ',');   // если шестой аргумент есть – обрежем
    if (comma) *comma = '\0';
    t2 = trim(t2);
    // шестой аргумент (если был) просто игнорируем

    if (strlen(result)==0 || strlen(a)==0 || strlen(b)==0 || strlen(t1)==0 || strlen(t2)==0) {
        fprintf(stderr, "Error line %d: CMP_GE requires first 5 arguments to be non-empty\n", line_num);
        return -1;
    }
    strncpy(inst.var_name, result, MAX_NAME-1);
    strncpy(inst.src_var, a, MAX_NAME-1);
    strncpy(inst.dst_var, b, MAX_NAME-1);
    strncpy(inst.tmp1, t1, MAX_NAME-1);
    strncpy(inst.tmp2, t2, MAX_NAME-1);
}
    else {
        fprintf(stderr, "Error line %d: unknown instruction '%s'\n", line_num, trimmed);
        return -1;
    }
    #undef PARSE_NUM_OR_DEFAULT

    return add_instruction(ast, &inst);
}
