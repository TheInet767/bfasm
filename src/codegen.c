// codegen.c
// Brainfuck Assembler (bfasm) – code generator implementation
//
// Walks the AST and emits pure Brainfuck instructions while tracking
// the virtual head position so that variable accesses generate minimal
// '>' / '<' sequences.
//
// === Current Limitations & Future Improvements ===
// [TODO]    Optimize consecutive head movements (e.g., > followed by <).
// [TODO]    Output to file or string buffer instead of stdout.
// [TODO]    Support for BF++ target (extended instructions).

#include <stdio.h>
#include <string.h>
#include "codegen.h"

static int var_index(const VarTable *vt, const char *name) {
    for (int i = 0; i < vt->count; i++) {
        if (strcmp(vt->names[i], name) == 0) return i + vt->org_offset;
    }
    return -1;
}

static void move_head(int *pos, int target) {
    int delta = target - *pos;
    if (delta > 0) {
        for (int i = 0; i < delta; i++) putchar('>');
    } else if (delta < 0) {
        for (int i = 0; i < -delta; i++) putchar('<');
    }
    *pos = target;
}

void generate_bf(const AST *ast) {
    int head_pos = 0;

    for (int i = 0; i < ast->inst_count; i++) {
        Instruction inst = ast->instructions[i];

        switch (inst.type) {
            case INST_INC:
                if (inst.var_name[0] != '\0') {
                    int idx = var_index(&ast->vars, inst.var_name);
                    if (idx < 0) { fprintf(stderr, "Internal error: undefined variable '%s'\n", inst.var_name); return; }
                    move_head(&head_pos, idx);
                }
                for (int j = 0; j < inst.operand; j++) putchar('+');
                break;

            case INST_DEC:
                if (inst.var_name[0] != '\0') {
                    int idx = var_index(&ast->vars, inst.var_name);
                    if (idx < 0) { fprintf(stderr, "Internal error: undefined variable '%s'\n", inst.var_name); return; }
                    move_head(&head_pos, idx);
                }
                for (int j = 0; j < inst.operand; j++) putchar('-');
                break;

            case INST_ZERO:
                if (inst.var_name[0] != '\0') {
                    int idx = var_index(&ast->vars, inst.var_name);
                    if (idx < 0) { fprintf(stderr, "Internal error: undefined variable '%s'\n", inst.var_name); return; }
                    move_head(&head_pos, idx);
                }
                printf("[-]");
                break;

            case INST_INPUT:
                putchar(',');
                break;

            case INST_OUTPUT:
                putchar('.');
                break;

            case INST_LOOP_START:
                if (inst.var_name[0] != '\0') {
                    int idx = var_index(&ast->vars, inst.var_name);
                    if (idx < 0) { fprintf(stderr, "Internal error: undefined variable '%s'\n", inst.var_name); return; }
                    move_head(&head_pos, idx);
                }
                putchar('[');
                break;

            case INST_LOOP_END:
                putchar(']');
                break;

            case INST_MOV:
                {
                    int src_idx = var_index(&ast->vars, inst.src_var);
                    int dst_idx = var_index(&ast->vars, inst.dst_var);
                    if (src_idx < 0 || dst_idx < 0) {
                        fprintf(stderr, "Internal error: undefined variable in MOV '%s', '%s'\n",
                                inst.src_var, inst.dst_var);
                        return;
                    }
                    move_head(&head_pos, src_idx);
                    putchar('[');
                    putchar('-');
                    move_head(&head_pos, dst_idx);
                    putchar('+');
                    move_head(&head_pos, src_idx);
                    putchar(']');
                }
                break;

            case INST_GOTO:
                if (inst.var_name[0] != '\0') {
                    int idx = var_index(&ast->vars, inst.var_name);
                    if (idx < 0) { fprintf(stderr, "Internal error: undefined variable '%s'\n", inst.var_name); return; }
                    move_head(&head_pos, idx);
                }
                break;

            case INST_RIGHT:
                head_pos += inst.operand;
                for (int j = 0; j < inst.operand; j++) putchar('>');
                break;

            case INST_LEFT:
                head_pos -= inst.operand;
                for (int j = 0; j < inst.operand; j++) putchar('<');
                break;

            case INST_RAWBF:
                printf("%s", inst.raw_bf);
                head_pos = 0;
                break;

            case INST_MOVEBY:
                if (inst.var_name[0] != '\0') {
                    int idx = var_index(&ast->vars, inst.var_name);
                    if (idx < 0) { fprintf(stderr, "Internal error: undefined variable '%s'\n", inst.var_name); return; }
                    move_head(&head_pos, idx);
                }
                printf("[>]");
                head_pos = 0;
                break;

            case INST_MOVEBY_LEFT:
                if (inst.var_name[0] != '\0') {
                    int idx = var_index(&ast->vars, inst.var_name);
                    if (idx < 0) { fprintf(stderr, "Internal error: undefined variable '%s'\n", inst.var_name); return; }
                    move_head(&head_pos, idx);
                }
                printf("[<]");
                head_pos = 0;
                break;

            default:
                fprintf(stderr, "Internal error: unknown instruction type %d\n", inst.type);
                return;
        }
    }

    putchar('\n');
}
