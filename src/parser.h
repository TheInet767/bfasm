// parser.h
// Brainfuck Assembler (bfasm) – parser module header
// Converts .bfasm source into an abstract syntax tree (AST)

#ifndef PARSER_H
#define PARSER_H

// ---------- Limits ----------
#define MAX_VARS         256   // max number of variables
#define MAX_NAME         64    // max length of a variable name
#define MAX_INSTRUCTIONS 1024  // max number of instructions

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
} AST;

// ---------- Parser function ----------
// Reads a .bfasm file and builds the AST.
// On success returns an AST; on error sets inst_count = -1.
AST parse_file(const char *filename);

#endif// parser.h
// Brainfuck Assembler (bfasm) – parser module header
// Converts .bfasm source into an abstract syntax tree (AST)

#ifndef PARSER_H
#define PARSER_H

// ---------- Limits ----------
#define MAX_VARS         256   // max number of variables
#define MAX_NAME         64    // max length of a variable name
#define MAX_INSTRUCTIONS 1024  // max number of instructions

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
} AST;

// ---------- Parser function ----------
// Reads a .bfasm file and builds the AST.
// On success returns an AST; on error sets inst_count = -1.
AST parse_file(const char *filename);

#endif
