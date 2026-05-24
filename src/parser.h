// parser.h
// Brainfuck Assembler (bfasm) – parser module header
// Converts .bfasm source into an abstract syntax tree (AST)
// Supports macros, INCLUDE with AS/KEEP, and @ alias for macro calls.

#ifndef PARSER_H
#define PARSER_H

// ---------- Limits ----------
#define MAX_VARS         256   // max number of variables
#define MAX_NAME         64    // max length of a variable name
#define MAX_INSTRUCTIONS 1024  // max number of instructions

#define MAX_MACROS        64
#define MAX_MACRO_PARAMS   8
#define MAX_MACRO_LINES   64
#define MAX_MACRO_NAME    64

// ---------- Instruction types ----------
typedef enum {
    INST_INC,        // '+' (on current cell or named var)
    INST_DEC,        // '-' (on current cell or named var)
    INST_ZERO,       // '[-]' (on current cell or named var)
    INST_INPUT,      // ','
    INST_OUTPUT,     // '.'
    INST_LOOP_START, // '['
    INST_LOOP_END,   // ']'
    INST_MOV,        // copy src to dst (destroys src)
    INST_GOTO,       // move head to named var
    INST_RIGHT,      // move head right by n cells
    INST_LEFT        // move head left by n cells
} InstType;

// ---------- Single instruction ----------
typedef struct {
    InstType type;              // what to do
    char var_name[MAX_NAME];    // variable name (for var-based operations)
    int  operand;               // numeric operand (for RIGHT/LEFT/INC n/DEC n)
    char src_var[MAX_NAME];     // source variable (for MOV)
    char dst_var[MAX_NAME];     // destination variable (for MOV)
} Instruction;

// ---------- Macro definition ----------
typedef struct {
    char name[MAX_MACRO_NAME];
    char lib_alias[MAX_NAME];   // library alias (e.g., "stdlib"), empty if none
    char params[MAX_MACRO_PARAMS][MAX_NAME];
    int param_count;
    char body[MAX_MACRO_LINES][256]; // macro body lines
    int body_line_count;
} Macro;

// ---------- Variable table ----------
typedef struct {
    char names[MAX_VARS][MAX_NAME]; // variable names
    int count;                      // number of declared variables
    int org_offset;                 // base offset set by ORG (default 0)
} VarTable;

// ---------- Abstract Syntax Tree ----------
typedef struct {
    VarTable vars;                              // variable table
    Instruction instructions[MAX_INSTRUCTIONS]; // parsed instructions
    int inst_count;                             // number of instructions
    Macro macros[MAX_MACROS];                   // defined macros
    int macro_count;                            // number of macros
} AST;

// ---------- Parser function ----------
AST parse_file(const char *filename);

#endif
