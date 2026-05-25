// parser.h
// Brainfuck Assembler (bfasm) – parser module header
// Converts .bfasm source into an abstract syntax tree (AST)
// Supports macros, INCLUDE with AS/KEEP, and @ alias for macro calls.

#ifndef PARSER_H
#define PARSER_H

// ---------- Initial capacities (will grow as needed) ----------
#define INIT_VARS_CAPACITY    16
#define INIT_INSTR_CAPACITY   64
#define INIT_MACROS_CAPACITY   8

#define MAX_NAME              64
#define MAX_MACRO_PARAMS      16
#define MAX_MACRO_LINES       64
#define MAX_MACRO_NAME        64

// ---------- Instruction types ----------
typedef enum {
    INST_INC,
    INST_DEC,
    INST_ZERO,
    INST_INPUT,
    INST_OUTPUT,
    INST_LOOP_START,
    INST_LOOP_END,
    INST_MOV,
    INST_GOTO,
    INST_RIGHT,
    INST_LEFT,
    INST_RAWBF,
    INST_MOVEBY,       // MOVEBY var – move right by var cells, destroys var
    INST_MOVEBY_LEFT,   // MOVEBY_LEFT var – move left by var cells, destroys var
    INST_CMP_GE
} InstType;

#define RAWBF_MAX 1024

// ---------- Single instruction ----------
typedef struct {
    InstType type;
    char var_name[MAX_NAME];   // result
    int  operand;
    char src_var[MAX_NAME];    // a
    char dst_var[MAX_NAME];    // b
    char tmp1[MAX_NAME];       // t1
    char tmp2[MAX_NAME];       // t2
    char raw_bf[RAWBF_MAX];
} Instruction;
// ---------- Macro definition ----------
typedef struct {
    char name[MAX_MACRO_NAME];
    char lib_alias[MAX_NAME];
    char params[MAX_MACRO_PARAMS][MAX_NAME];
    int param_count;
    char body[MAX_MACRO_LINES][256];
    int body_line_count;
} Macro;

// ---------- Variable table (dynamic) ----------
typedef struct {
    char (*names)[MAX_NAME];   // pointer to array of strings
    int count;
    int capacity;
    int org_offset;
} VarTable;

// ---------- Abstract Syntax Tree ----------
typedef struct {
    VarTable vars;
    Instruction *instructions;  // dynamic array
    int inst_count;
    int inst_capacity;
    Macro *macros;              // dynamic array
    int macro_count;
    int macro_capacity;
} AST;

// ---------- Parser function ----------
AST parse_file(const char *filename);
void ast_free(AST *ast);

#endif
