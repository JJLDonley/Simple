# Simple Programming Language
## Implementation Document

**Version:** 1.0  
**Target Platform:** .NET Common Language Runtime (CLR)  
**File Extension:** `.simple`

---

## Table of Contents

1. [Purpose](#purpose)
2. [Scope](#scope)
3. [Implementation Phases](#implementation-phases)
4. [Compiler Pipeline Checklists](#compiler-pipeline-checklists)
5. [Language Feature Checklists](#language-feature-checklists)
6. [Phase Milestone Checklists](#phase-milestone-checklists)
7. [Non-Goals](#non-goals)

---

## Purpose

This document defines the detailed implementation plan and verification checklists for the Simple programming language. It follows the language specification in `Simple_Programming_Language_Document.md` and is intended to guide implementation and validation of each compiler phase and language feature.

## Scope

This plan covers:
- The compiler pipeline (lexer → parser → AST → semantic analysis → codegen → assembly emission)
- Language features as specified (types, statements, expressions, artifacts, modules, enums, generics, standard library)
- CLI tooling and diagnostics

## Implementation Phases

### Phase 1: Minimal Compiler (MVP)

**Features:**
- Variables (mutable and immutable)
- Primitive types (i32, f64, bool, string)
- Binary expressions (arithmetic, comparison)
- Simple if statements
- Procedure definitions and calls
- Print statement (built-in)

**Deliverable:** Hello World program compiles and runs.

### Phase 2: Control Flow

**Features:**
- If-else chains (`|>`)
- While loops
- For loops (traditional C-style)
- Break and skip
- Return statements

**Deliverable:** FizzBuzz program compiles and runs.

### Phase 3: Artifacts and Methods

**Features:**
- Artifact definitions
- Artifact instantiation
- Member access
- Method calls

**Deliverable:** Point/Rectangle example compiles and runs.

### Phase 4: Advanced Features

**Features:**
- Modules
- Enums
- Arrays and lists
- First-class procedures
- Generics

**Deliverable:** Full language support examples compile and run.

### Phase 5: Optimization and Tooling

**Features:**
- Error recovery in parser
- Better error messages
- Basic optimizations
- Standard library
- Language server protocol (LSP)
- Debugger support

---

## Compiler Pipeline Checklists

### 1) Lexer (Tokenization)
- [ ] Recognize all keywords: `while`, `for`, `break`, `skip`, `return`, `default`, `Fn`, `self`, `Artifact`, `Enum`, `Module`, `Union`
- [ ] Recognize literals: integers (decimal/hex/binary), floats, strings, characters, booleans
- [ ] Recognize operators and punctuators (including `::`, `|>`, `<`, `>`, `[]`, `()`, `{}`)
- [ ] Emit distinct tokens for `:` vs `::`
- [ ] Handle single-line and multi-line comments
- [ ] Track line/column for diagnostics
- [ ] Emit EOF token

### 2) Parser (Recursive Descent + Precedence Climbing)
- [ ] Parse program structure: `declaration*`
- [ ] Parse declarations: variables, procedures, artifacts, modules, enums
- [ ] Parse parameter lists with mutability (`:` and `::`)
- [ ] Parse types (primitive, arrays, lists, procedure types, user-defined)
- [ ] Parse generic parameters and generic arguments (`<T>`, `<T, U>`, `Type<...>`)
- [ ] Parse statements: assignment, if, if-else chain, while, for, return, break, skip, block
- [ ] Parse expressions with precedence and associativity as specified
- [ ] Parse artifact literals and array literals (positional + named fields)
- [ ] Validate grammar error recovery (Phase 5)

### 3) AST Construction
- [ ] Implement AST nodes for all declarations, statements, and expressions
- [ ] Implement type nodes including `GenericInstance` and `TypeParameter`
- [ ] Preserve source spans for diagnostics
- [ ] Normalize operator precedence in AST (no ambiguity after parse)

### 4) Semantic Analysis
- [ ] Build nested scopes and symbol table
- [ ] Enforce explicit type declarations
- [ ] Resolve identifiers and qualify enum values
- [ ] Enforce mutability rules (`:` vs `::`)
- [ ] Enforce type checking for expressions and assignments
- [ ] Validate procedure rules (return types, return paths)
- [ ] Validate artifact rules (member initialization, `self` access)
- [ ] Validate generic rules (type parameter scope, instantiation, inference)
- [ ] Validate array sizes as compile-time constants
- [ ] Validate list and array indexing types

### 5) Code Generation (CIL)
- [ ] Emit CLR types for primitives, arrays, lists, artifacts, modules, enums
- [ ] Emit delegate types for `Fn` procedures
- [ ] Emit generic artifacts and generic procedures as CLR generics
- [ ] Emit fields for global variables
- [ ] Emit methods for procedures and artifact methods
- [ ] Emit control flow: if, if-else chain, while, for, break, skip
- [ ] Emit expression evaluation (binary, unary, call, index, member)
- [ ] Emit artifact initialization (positional and named)
- [ ] Emit array and list literals
- [ ] Emit return statements and default returns for `void`

### 6) Assembly Emission
- [ ] Create assembly/module builders
- [ ] Define entry point (`main : i32 ()`)
- [ ] Write assembly to disk as `.exe` or `.dll`

### 7) Diagnostics and Errors
- [ ] Uniform error format (`error[E0001]: ...`)
- [ ] Report line/column and highlight ranges
- [ ] Distinguish syntax vs semantic errors

### 8) CLI Commands
- [ ] `simple build` emits `.exe` or `.dll`
- [ ] `simple run` compiles + executes
- [ ] `simple check` validates syntax only

---

## Language Feature Checklists

### Variables and Mutability
- [ ] Mutable variable declarations (`:`)
- [ ] Immutable variable declarations (`::`)
- [ ] Zero-initialization of unassigned variables

### Types
- [ ] Primitive types (`i8..i128`, `u8..u128`, `f32`, `f64`, `bool`, `char`, `string`)
- [ ] Fixed-size arrays (`T[N]`, `T[N][M]`, `T[N][M][P]`)
- [ ] Dynamic lists (`T[]`, `T[][]`, `T[][][]`)
- [ ] Procedure types (`(params) : return`, `(params) :: return`)
- [ ] User-defined types: artifacts, modules, enums
- [ ] Generics: type parameters, type arguments, generic instantiation

### Expressions
- [ ] Literals (int, float, string, char, bool)
- [ ] Identifiers and member access
- [ ] Unary operators (prefix/postfix)
- [ ] Binary operators (arithmetic, comparison, logical, bitwise)
- [ ] Calls and argument lists
- [ ] Index expressions
- [ ] Artifact literals with positional and named fields
- [ ] Array literals

### Statements
- [ ] Variable declarations
- [ ] Assignment statements and compound assignments
- [ ] If statements
- [ ] If-else chains (`|>`)
- [ ] While loops
- [ ] For loops
- [ ] Return statements
- [ ] Break and skip
- [ ] Blocks and expression statements

### Procedures and First-Class `Fn`
- [ ] Procedure declarations with mutable/immutable return types
- [ ] Parameter mutability (`:` and `::`)
- [ ] First-class procedure values (`Fn`) with correct type checking
- [ ] Lambda expressions

### Artifacts
- [ ] Artifact declarations (including generics)
- [ ] Member declarations with mutability
- [ ] Methods with `self` access
- [ ] Instantiation with brace syntax

### Modules
- [ ] Module declarations
- [ ] Static member access

### Enums
- [ ] Enum declarations (with optional explicit values)
- [ ] Strict scoping (`Type.Member`)

### Generics
- [ ] Generic artifacts
- [ ] Generic procedures
- [ ] Type argument inference at call sites
- [ ] Invariance rules

### Standard Library
- [ ] Built-ins: `print`, `println`
- [ ] Conversions: `str(i32)`, `str(f64)`, `str(bool)`, `i32(string)`, `f64(string)`
- [ ] `len<T>` for lists and `len` for strings
- [ ] Standard modules: `IO`, `Math`, `String`

---

## Phase Milestone Checklists

### Phase 1: Minimal Compiler (MVP)
- [ ] Lexer supports primitives, literals, operators for MVP
- [ ] Parser supports variable declarations, simple expressions, procedures
- [ ] Semantic checks for explicit typing and returns
- [ ] Codegen for variables, arithmetic, procedure calls, `print`
- [ ] Hello World program compiles and runs

### Phase 2: Control Flow
- [ ] Parser supports `|>`, `while`, `for`, `break`, `skip`
- [ ] Codegen for branching and looping
- [ ] FizzBuzz program compiles and runs

### Phase 3: Artifacts and Methods
- [ ] Artifact declarations and members
- [ ] Artifact instantiation and member access
- [ ] Method calls with `self`
- [ ] Point/Rectangle example compiles and runs

### Phase 4: Advanced Features
- [ ] Modules and enums
- [ ] Arrays and lists
- [ ] First-class procedures (`Fn`)
- [ ] Generics for artifacts and procedures
- [ ] Full language support examples compile and run

### Phase 5: Optimization and Tooling
- [ ] Error recovery in parser
- [ ] Improved diagnostics
- [ ] Optimization passes (AST and CIL)
- [ ] Standard library modules wired to CLR
- [ ] CLI tools stable
- [ ] Optional LSP and debugger hooks

---

## Non-Goals

- [ ] Implementing unions (reserved)
- [ ] Implementing pointers/unsafe system
- [ ] Pattern matching
- [ ] Modules/packages/import system

---

**End of Implementation Document**
