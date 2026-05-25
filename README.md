[![License: GPL v2](https://img.shields.io/badge/License-GPL_v2-blue.svg)](LICENSE)

# BFASM – Brainfuck Macro Assembler

BFASM is a macro system that compiles to pure Brainfuck, making
Brainfuck programs easier to read, write and debug.

It was inspired by projects that implement Brainfuck natively in
hardware – notably **BrainfuckPC** (relay computer) and **DekatronPC**
(vacuum‑tube computer) – where every BF instruction is a real
processor opcode.

This is a hobby project, built for fun and learning.

## Philosophy

- **Human‑friendly names** – use `VAR x` and `INC x` instead of counting `>` and `<`.
- **No hidden overhead** – the generated BF looks like it was written by hand.
- **Explicit head management** – the programmer controls the tape head position.
- **Macros, not functions** – macros are expanded at compile time, with zero runtime cost.
- **Libraries with namespaces** – `INCLUDE` with optional `AS` and `KEEP`,
  conflict resolution via `@`.

## Current Status (v0.2.1)

- C99 compiler (`bfasm`) and minimal BF interpreter (`bfrun`) for testing.
- Supported instructions: `VAR`, `ORG`, `RESERVE`, `INC`, `DEC`, `ZERO`,
  `INPUT`, `OUTPUT`, `LOOP`, `END`, `MOV`, `GOTO`, `RIGHT`, `LEFT`.
- Macro system with parameters (`MACRO`/`ENDM`).
- Module system: `INCLUDE`, `INCLUDE … AS`, `INCLUDE … KEEP`,
  name‑resolution with `@`.
- Raw Brainfuck insertion: `BF`, `BEGINBF`…`END`, `INCLUDEBF`.
- Core standard library: `ADD`, `SUB`, `MOV_SAFE`, `MUL`, `NEG`, `CMP`.

## Language Reference

### Basic Instructions

| Instruction | Description | Generated BF |
|-------------|-------------|--------------|
| `VAR name` | Declare a variable, assign it the next free cell | (none, metadata) |
| `ORG n` | Set base cell index for subsequent `VAR` | (none) |
| `RESERVE n` | Reserve `n` unnamed cells (advance the cell counter) | (none) |
| `INC` | Increment current cell by 1 | `+` |
| `INC var` | Move to `var`, then increment by 1 | `>`/`<` (move) `+` |
| `INC n` | Increment current cell by `n` | `+` repeated `n` times |
| `INC var n` | Move to `var`, increment by `n` | move, then `+`×`n` |
| `DEC` / `DEC var` / `DEC n` / `DEC var n` | Decrement (same patterns as `INC`) | `-` instead of `+` |
| `ZERO` | Set current cell to 0 | `[-]` |
| `ZERO var` | Move to `var`, then set to 0 | move + `[-]` |
| `INPUT` | Read one character into current cell | `,` |
| `OUTPUT` | Output current cell as a character | `.` |
| `LOOP` | Begin loop (while current cell ≠ 0) | `[` |
| `LOOP var` | Move to `var`, then begin loop | move + `[` |
| `END` | End loop (head must be on the same cell as `LOOP`!) | `]` |
| `MOV src, dst` | Copy `src` to `dst`, **destroying** `src` | `[->+<]` idiom (with proper moves) |
| `GOTO var` | Move head to `var` without modifying anything | `>`/`<` |
| `RIGHT` / `RIGHT n` | Move head right 1 or `n` cells | `>` / `>`×`n` |
| `LEFT` / `LEFT n` | Move head left 1 or `n` cells | `<` / `<`×`n` |

**Note:** After `LOOP`, the head must be on the same cell when `END` is reached. The programmer is responsible for this (use `GOTO` inside the loop if needed).

## Macros

Macros are defined with `MACRO` and expanded inline at compile time.

```asm
MACRO name(param1, param2, ...)
    ; body using the parameters
ENDM



Macro arguments are variable names (not numeric literals).

Macros can call other macros (including those from included libraries).

Use @ to specify which library to take the macro from when names conflict.

; Call macro
ADD a, b, tmp
MUL@math result, a, b, t1, t2, t3
Module System (INCLUDE)
Libraries are loaded with INCLUDE. Macros from the included file become available.


INCLUDE "stdlib.bfasm"                  ; all macros, alias = "stdlib"
INCLUDE "math.bfasm" AS math            ; all macros with alias "math"
INCLUDE "math.bfasm" KEEP MUL, DIV      ; import only MUL and DIV
INCLUDE "math.bfasm" AS m KEEP ADD      ; import ADD with alias "m"
AS – explicit library alias (used with @).

KEEP – import only the listed macros. Dependencies are not automatically resolved – the programmer must ensure they are available.

Duplicate includes of the same file are ignored.

Raw Brainfuck Insertion
Drop to pure Brainfuck anywhere in the source. After raw BF, the head position is reset to 0 – use GOTO to sync with named variables.


BF "+++>><<"          ; single line
BEGINBF
+++
[>++<-]
END                  ; multi‑line block
INCLUDEBF "file.bf"  ; load raw BF from a file
All characters except ><+-.,[] are rejected.

Standard Library (stdlib.bfasm)
The following macros are provided in lib/stdlib.bfasm:

Macro	Description	Temporaries
ADD dst, src, tmp	dst += src, src preserved	1 (tmp)
SUB dst, src, tmp	dst -= src, src preserved	1 (tmp)
MOV_SAFE src, dst, tmp	Copy src to dst, src preserved	1 (tmp)
MUL result, a, b, t1, t2, t3	result = a * b, both operands preserved	3 (t1,t2,t3)
NEG result, tmp	result = 1 if result was 0, else 0	1 (tmp)
CMP result, a, b, t1, t2	result = 1 if a == b, else 0	2 (t1,t2)
Quick Example


; hello.bfasm
VAR a
INC a 72
GOTO a
OUTPUT


Compile and run:
./bfasm hello.bfasm | ./tools/bfrun /dev/stdin


## Building & Testing

make          # build bfasm and bfrun
make test     # run all tests
make clean

##  Roadmap
DIV, MOD, MIN, MAX (require CMP_GE).

Stack library (PUSH, POP, PEEK).

Lazy loading of modules with indexing (v0.3.0).

Meta‑compiled libraries (.bfml) and link manifests (.bflink) (v0.4.0).

High‑level language BFL that compiles to BFASM (v1.0.0).

## LICENSE
GPL-2.0 (see LICENSE file).


