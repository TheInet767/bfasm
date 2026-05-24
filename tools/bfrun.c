// tools/bfrun.c
// Minimal Brainfuck interpreter (8-bit cells, 30000 cells tape).
// Reads BF code from file given as argument, input from stdin,
// output to stdout.
//
// Compile: cc -O2 -o bfrun tools/bfrun.c

#include <stdio.h>
#include <stdlib.h>

#define TAPE_SIZE 30000

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <file.bf>\n", argv[0]);
        return 1;
    }
    FILE *fp = fopen(argv[1], "r");
    if (!fp) {
        perror(argv[1]);
        return 1;
    }
    // Read entire file into a buffer
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    rewind(fp);
    char *code = malloc(sz + 1);
    fread(code, 1, sz, fp);
    code[sz] = '\0';
    fclose(fp);

    unsigned char tape[TAPE_SIZE] = {0};
    int dp = 0;               // data pointer
    long ip = 0;              // instruction pointer

    while (ip < sz) {
        switch (code[ip]) {
            case '>': dp++; if (dp >= TAPE_SIZE) dp = 0; break;
            case '<': dp--; if (dp < 0) dp = TAPE_SIZE - 1; break;
            case '+': tape[dp]++; break;
            case '-': tape[dp]--; break;
            case '.': putchar(tape[dp]); break;
            case ',': tape[dp] = getchar(); if (tape[dp] == EOF) tape[dp] = 0; break;
            case '[':
                if (tape[dp] == 0) {
                    int depth = 1;
                    while (depth > 0) {
                        ip++;
                        if (ip >= sz) { fprintf(stderr, "Unmatched '['\n"); return 1; }
                        if (code[ip] == '[') depth++;
                        if (code[ip] == ']') depth--;
                    }
                }
                break;
            case ']':
                if (tape[dp] != 0) {
                    int depth = 1;
                    while (depth > 0) {
                        ip--;
                        if (ip < 0) { fprintf(stderr, "Unmatched ']'\n"); return 1; }
                        if (code[ip] == ']') depth++;
                        if (code[ip] == '[') depth--;
                    }
                }
                break;
        }
        ip++;
    }
    free(code);
    return 0;
}
