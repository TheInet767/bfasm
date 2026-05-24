// parser.c
// Brainfuck Assembler (bfasm) – parser implementation
//
// Converts .bfasm source into an AST. Supports macros defined with
// MACRO/ENDM, INCLUDE directive with AS/KEEP, and @-notation for
// explicit library selection.
//
// === Current Limitations & Future Improvements ===
// [LIMIT]   Static arrays – variables, instructions, macros are capped.
// [LIMIT]   VAR duplicates are silently ignored; a warning might help.
// [LIMIT]   INCLUDE does not support nested subdirectories.
// [LIMIT]   Macro arguments must be variable names (no numeric literals).
// [LIMIT]   Macros must be defined before use (single-pass parser).
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

// ---- forward declarations ----
static int parse_line(char *trimmed, AST *ast, int line_num);
static void parse_macros_from_file(const char *filename, const char *alias,
                                   AST *ast, char **keep_names, int keep_count);

// ---- macro support ----

// Find macro by name and optional alias. If alias is NULL or empty,
// returns the first macro with matching name (in order of inclusion).
static Macro* find_macro(AST *ast, const char *name, const char *alias) {
    for (int i = 0; i < ast->macro_count; i++) {
        if (strcmp(ast->macros[i].name, name) == 0) {
            if (alias == NULL || alias[0] == '\0') {
                // no alias requested – return first match
                return &ast->macros[i];
            } else if (strcmp(ast->macros[i].lib_alias, alias) == 0) {
                return &ast->macros[i];
            }
        }
    }
    return NULL;
}

// Substitute macro parameters with call arguments in a single line.
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

// Parse macros from a file and add them to AST with the given library alias.
// If keep_names is not NULL and keep_count > 0, only those macros are added.
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
            // We have a macro definition. Parse its name and check KEEP.
            char *def = trim(trimmed + 6);
            char *lparen = strchr(def, '(');
            if (!lparen) {
                fprintf(stderr, "Error in included file '%s': macro missing '('.\n", filename);
                ast->inst_count = -1; break;
            }
            *lparen = '\0';
            char *macro_name = trim(def);

            // KEEP filter: if keep_count > 0 and name not in list, skip this macro
            if (keep_count > 0) {
                int found = 0;
                for (int i = 0; i < keep_count; i++) {
                    if (strcmp(macro_name, keep_names[i]) == 0) { found = 1; break; }
                }
                if (!found) {
                    // skip the whole macro body
                    
                    while (fgets(line, sizeof(line), fp)) {
                        trim_newline(line);
                        char *c = strchr(line, ';'); if (c) *c = '\0';
                        char *t = trim(line);
                        if (strlen(t) == 0) continue;
                        if (strncmp(t, "ENDM", 4) == 0 && strlen(t) == 4) {
                            break;
                        }
                    }
                    continue;
                }
            }

            // Add this macro
            if (ast->macro_count >= MAX_MACROS) {
                fprintf(stderr, "Error: too many macros (max %d)\n", MAX_MACROS);
                ast->inst_count = -1; break;
            }

            Macro *m = &ast->macros[ast->macro_count];
            strncpy(m->name, macro_name, MAX_MACRO_NAME-1);
            m->name[MAX_MACRO_NAME-1] = '\0';
            strncpy(m->lib_alias, alias ? alias : "", MAX_NAME-1);
            m->lib_alias[MAX_NAME-1] = '\0';
            m->param_count = 0;
            m->body_line_count = 0;

            char *params_str = lparen + 1;
            char *rparen = strchr(params_str, ')');
            if (!rparen) {
                fprintf(stderr, "Error in macro definition in '%s': missing ')'\n", filename);
                ast->inst_count = -1; break;
            }
            *rparen = '\0';
            char *token = strtok(params_str, ",");
            while (token) {
                if (m->param_count >= MAX_MACRO_PARAMS) {
                    fprintf(stderr, "Error: too many macro parameters\n");
                    ast->inst_count = -1; break;
                }
                char *p = trim(token);
                if (strlen(p) == 0) {
                    fprintf(stderr, "Error: empty macro parameter\n");
                    ast->inst_count = -1; break;
                }
                strncpy(m->params[m->param_count], p, MAX_NAME-1);
                m->params[m->param_count][MAX_NAME-1] = '\0';
                m->param_count++;
                token = strtok(NULL, ",");
            }
            if (ast->inst_count == -1) break;

            // read body
            while (fgets(line, sizeof(line), fp)) {
                trim_newline(line);
                char *c = strchr(line, ';'); if (c) *c = '\0';
                char *tline = trim(line);
                if (strlen(tline) == 0) continue;
                if (strcmp(tline, "ENDM") == 0) break;
                if (m->body_line_count >= MAX_MACRO_LINES) {
                    fprintf(stderr, "Error: macro body too long\n");
                    ast->inst_count = -1; break;
                }
                strncpy(m->body[m->body_line_count], tline, 255);
                m->body[m->body_line_count][255] = '\0';
                m->body_line_count++;
            }
            if (ast->inst_count == -1) break;
            ast->macro_count++;
        } else {
            // All non-macro lines in included files are silently ignored
            // (they should be macro definitions only)
        }
    }

    fclose(fp);
    // If KEEP was specified, verify all requested macros were found.
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

        // --- INCLUDE directive ---
        if (strncmp(trimmed, "INCLUDE ", 8) == 0) {
            char *rest = trim(trimmed + 8);
            // Extract filename from quotes
            if (*rest != '"') {
                fprintf(stderr, "Error line %d: INCLUDE filename must be in double quotes\n", line_num);
                ast.inst_count = -1; break;
            }
            rest++; // skip opening quote
            char *closing_quote = strchr(rest, '"');
            if (!closing_quote) {
                fprintf(stderr, "Error line %d: missing closing quote for INCLUDE filename\n", line_num);
                ast.inst_count = -1; break;
            }
            *closing_quote = '\0';
            char *include_filename = rest;
            rest = closing_quote + 1;

            // Parse optional AS alias and KEEP list
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
                    // We need to store keep names in persistent storage
                    // because rest points into 'line' which will be overwritten.
                    // Use a static buffer for keep names.
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
                        // Copy the name into our static buffer
                        strncpy(keep_name_buf[keep_count], name, MAX_NAME-1);
                        keep_name_buf[keep_count][MAX_NAME-1] = '\0';
                        keep_macros[keep_count] = keep_name_buf[keep_count];
                        keep_count++;
                        tok = strtok(NULL, ",");
                    }
                    if (ast.inst_count == -1) break;
                    // KEEP is the last component, skip the rest
                    break;
                } else {
                    fprintf(stderr, "Error line %d: unexpected token after INCLUDE filename\n", line_num);
                    ast.inst_count = -1; break;
                }
                if (ast.inst_count == -1) break;
            }
            if (ast.inst_count == -1) break;

            // If no alias explicitly set, derive from filename (basename without extension)
            if (alias[0] == '\0') {
                const char *base = strrchr(include_filename, '/');
                if (base) base++; else base = include_filename;
                const char *dot = strrchr(base, '.');
                int len = dot ? (dot - base) : (int)strlen(base);
                if (len >= MAX_NAME) len = MAX_NAME-1;
                strncpy(alias, base, len);
                alias[len] = '\0';
            }

            // Load macros from included file
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
            // Inline macro definition is still supported without alias.
            // We'll reuse the logic from parse_macros_from_file but inline.
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
            m->lib_alias[0] = '\0';  // no alias for inline macros
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

            while (fgets(line, sizeof(line), fp)) {
                line_num++;
                trim_newline(line);
                char *c = strchr(line, ';'); if (c) *c = '\0';
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

        // --- Check for macro call (including @-notation) ---
        char first_token[MAX_NAME] = {0};
        sscanf(trimmed, "%63s", first_token);
        int first_token_len = strlen(first_token);   // запоминаем исходную длину
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

        Macro *macro = find_macro(&ast, call_name, at_sign ? call_alias : NULL);
        if (macro) {
          
            char args_str[256];
            const char *args_start = trimmed + first_token_len;
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

            for (int li = 0; li < macro->body_line_count; li++) {
                char *expanded = substitute_line(macro->body[li], macro, arg_tokens);
                //fprintf(stderr, "DEBUG expanded: '%s'\n", expanded);
                if (parse_line(expanded, &ast, line_num) != 0) {
                    ast.inst_count = -1; break;
                
                fprintf(stderr, "DEBUG expanded: '%s'\n", expanded);}
            }
            if (ast.inst_count == -1) break;
            continue;
        } else if (at_sign) {
            fprintf(stderr, "Error line %d: macro '%s@%s' not found\n", line_num, call_name, call_alias);
            ast.inst_count = -1; break;
        }
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
                // без кавычек — берём все символы до конца строки
                strncpy(raw, start, RAWBF_MAX-1);
                raw[RAWBF_MAX-1] = '\0';
                len = strlen(raw);
                // удаляем возможные пробелы? Нет, пробелы в BF игнорируются интерпретатором, но нам нужна компактность.
                // Удалим все пробелы и табуляции для чистоты.
                int j = 0;
                for (int i = 0; raw[i]; i++) {
                    if (raw[i] != ' ' && raw[i] != '\t') raw[j++] = raw[i];
                }
                raw[j] = '\0';
                len = j;
            }
            // валидация символов
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

            if (ast.inst_count >= MAX_INSTRUCTIONS) {
                fprintf(stderr, "Error line %d: too many instructions\n", line_num);
                ast.inst_count = -1; break;
            }
            ast.instructions[ast.inst_count++] = inst;
            continue;
        }
        // --- INCLUDEBF directive ---
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
                // игнорируем пробелы и переводы строк? Или оставляем? Лучше оставить, т.к. BF-файлы могут содержать пробелы.
                // Но валидируем.
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

            if (ast.inst_count >= MAX_INSTRUCTIONS) {
                fprintf(stderr, "Error line %d: too many instructions\n", line_num);
                ast.inst_count = -1; break;
            }
            ast.instructions[ast.inst_count++] = inst;
            continue;
        }
        
        // --- BEGINBF ... END block ---
        if (strcmp(trimmed, "BEGINBF") == 0) {
            char raw[RAWBF_MAX] = {0};
            int len = 0;
            while (fgets(line, sizeof(line), fp)) {
                line_num++;
                trim_newline(line);
                // удаляем пробелы/табуляции
                int j = 0;
                for (int i = 0; line[i]; i++) {
                    if (line[i] != ' ' && line[i] != '\t')
                        line[j++] = line[i];
                }
                line[j] = '\0';
                if (strcmp(line, "END") == 0) break;

                // валидация символов
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

            if (ast.inst_count >= MAX_INSTRUCTIONS) {
                fprintf(stderr, "Error line %d: too many instructions\n", line_num);
                ast.inst_count = -1; break;
            }
            ast.instructions[ast.inst_count++] = inst;
            continue;
        }
        
        // --- Ordinary instruction ---
        if (parse_line(trimmed, &ast, line_num) != 0) {
            ast.inst_count = -1; break;
        }
    }

    fclose(fp);
    return ast;
}

// ---- parse_line unchanged (same as before, just ensure it's present) ----
// (Include the entire parse_line function from previous version here)
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
