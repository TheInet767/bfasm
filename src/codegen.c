// codegen2.c
// Brainfuck Assembler (bfasm) – table‑driven code generator
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

// ---------- генераторы для каждой инструкции ----------

static void gen_inc(const Instruction *inst, const AST *ast, int *head_pos) {
    if (inst->var_name[0]) {
        int idx = var_index(&ast->vars, inst->var_name);
        if (idx < 0) return;
        move_head(head_pos, idx);
    }
    for (int j = 0; j < inst->operand; j++) putchar('+');
}

static void gen_dec(const Instruction *inst, const AST *ast, int *head_pos) {
    if (inst->var_name[0]) {
        int idx = var_index(&ast->vars, inst->var_name);
        if (idx < 0) return;
        move_head(head_pos, idx);
    }
    for (int j = 0; j < inst->operand; j++) putchar('-');
}

static void gen_zero(const Instruction *inst, const AST *ast, int *head_pos) {
    if (inst->var_name[0]) {
        int idx = var_index(&ast->vars, inst->var_name);
        if (idx < 0) return;
        move_head(head_pos, idx);
    }
    printf("[-]");
}

static void gen_input(const Instruction *inst, const AST *ast, int *head_pos) {
    (void)inst; (void)ast;
    putchar(',');
}

static void gen_output(const Instruction *inst, const AST *ast, int *head_pos) {
    (void)inst; (void)ast;
    putchar('.');
}

static void gen_loop_start(const Instruction *inst, const AST *ast, int *head_pos) {
    if (inst->var_name[0]) {
        int idx = var_index(&ast->vars, inst->var_name);
        if (idx < 0) return;
        move_head(head_pos, idx);
    }
    putchar('[');
}

static void gen_loop_end(const Instruction *inst, const AST *ast, int *head_pos) {
    (void)inst; (void)ast;
    putchar(']');
}

static void gen_mov(const Instruction *inst, const AST *ast, int *head_pos) {
    int src = var_index(&ast->vars, inst->src_var);
    int dst = var_index(&ast->vars, inst->dst_var);
    if (src < 0 || dst < 0) return;
    move_head(head_pos, src);
    putchar('['); putchar('-');
    move_head(head_pos, dst);
    putchar('+');
    move_head(head_pos, src);
    putchar(']');
}

static void gen_goto(const Instruction *inst, const AST *ast, int *head_pos) {
    if (inst->var_name[0]) {
        int idx = var_index(&ast->vars, inst->var_name);
        if (idx < 0) return;
        move_head(head_pos, idx);
    }
}

static void gen_right(const Instruction *inst, const AST *ast, int *head_pos) {
    (void)ast;
    *head_pos += inst->operand;
    for (int j = 0; j < inst->operand; j++) putchar('>');
}

static void gen_left(const Instruction *inst, const AST *ast, int *head_pos) {
    (void)ast;
    *head_pos -= inst->operand;
    for (int j = 0; j < inst->operand; j++) putchar('<');
}

static void gen_rawbf(const Instruction *inst, const AST *ast, int *head_pos) {
    (void)ast;
    printf("%s", inst->raw_bf);
    *head_pos = 0;
}

static void gen_moveby(const Instruction *inst, const AST *ast, int *head_pos) {
    if (inst->var_name[0]) {
        int idx = var_index(&ast->vars, inst->var_name);
        if (idx < 0) return;
        move_head(head_pos, idx);
    }
    printf("[>]");
    *head_pos = 0;
}

static void gen_moveby_left(const Instruction *inst, const AST *ast, int *head_pos) {
    if (inst->var_name[0]) {
        int idx = var_index(&ast->vars, inst->var_name);
        if (idx < 0) return;
        move_head(head_pos, idx);
    }
    printf("[<]");
    *head_pos = 0;
}

static void gen_cmp_ge(const Instruction *inst, const AST *ast, int *head_pos) {
    int res = var_index(&ast->vars, inst->var_name);
    int idx_a = var_index(&ast->vars, inst->src_var);
    int idx_b = var_index(&ast->vars, inst->dst_var);
    int idx_t1 = var_index(&ast->vars, inst->tmp1);
    if (res < 0 || idx_a < 0 || idx_b < 0 || idx_t1 < 0) return;

    move_head(head_pos, res);  printf("[-]+");
    move_head(head_pos, idx_a); printf("[-");
    move_head(head_pos, idx_b); printf("[-");
    move_head(head_pos, idx_t1); printf("+");
    move_head(head_pos, idx_b); printf("]");
    move_head(head_pos, idx_t1); printf("[-");
    move_head(head_pos, res); printf("[-]");
    move_head(head_pos, idx_t1); printf("]");
    move_head(head_pos, idx_a); printf("]");
    move_head(head_pos, idx_b); printf("[-]");
    move_head(head_pos, res);
}

// ---------- таблица генераторов ----------
typedef void (*GenFunc)(const Instruction*, const AST*, int*);

typedef struct {
    InstType type;
    GenFunc  func;
} GenEntry;

static const GenEntry gen_table[] = {
    { INST_INC,         gen_inc          },
    { INST_DEC,         gen_dec          },
    { INST_ZERO,        gen_zero         },
    { INST_INPUT,       gen_input        },
    { INST_OUTPUT,      gen_output       },
    { INST_LOOP_START,  gen_loop_start   },
    { INST_LOOP_END,    gen_loop_end     },
    { INST_MOV,         gen_mov          },
    { INST_GOTO,        gen_goto         },
    { INST_RIGHT,       gen_right        },
    { INST_LEFT,        gen_left         },
    { INST_RAWBF,       gen_rawbf        },
    { INST_MOVEBY,      gen_moveby       },
    { INST_MOVEBY_LEFT, gen_moveby_left  },
    { INST_CMP_GE,      gen_cmp_ge       },
};
static const int gen_table_size = sizeof(gen_table) / sizeof(gen_table[0]);

// ---------- главная функция ----------
int generate_bf(const AST *ast) {
    int head_pos = 0;
    for (int i = 0; i < ast->inst_count; i++) {
        const Instruction *inst = &ast->instructions[i];
        GenFunc func = NULL;
        for (int j = 0; j < gen_table_size; j++) {
            if (gen_table[j].type == inst->type) {
                func = gen_table[j].func;
                break;
            }
        }
        if (func) {
            func(inst, ast, &head_pos);
        } else {
            fprintf(stderr, "Internal error: no generator for instruction type %d\n", inst->type);
            return 1;
        }
    }
    putchar('\n');
    return 0;

}
