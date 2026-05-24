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
    INST_INC,    // '+' in Brainfuck
    INST_DEC,    // '-' in Brainfuck
    INST_OUTPUT  // '.' in Brainfuck
} InstType;

// ---------- Single instruction ----------
typedef struct {
    InstType type;              // what to do
    char var_name[MAX_NAME];    // variable name (for INC/DEC)
} Instruction;

// ---------- Variable table ----------
// Maps variable names to cell indices (order of declaration)
typedef struct {
    char names[MAX_VARS][MAX_NAME]; // variable names
    int count;                      // number of declared variables
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
