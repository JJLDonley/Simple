# Simple::Lang

**Scope**  
Language frontend(s) that target SIR.

**How It Works**  
Language compilers lower source -> SIR text (or direct emitter API) and rely on the VM for
verification and execution.

**What Works (Current)**  
- Not implemented (intentional).

**Implementation Plan**  
1) Build Simple language front-end targeting SIR.  
2) Layer standard library using ABI/FFI.  

**Future Plan**  
- Full language toolchain and packaging.  

---

## Language Specification (Legacy Reference)

### Simple Language Spec

# Simple Programming Language
## Grammar and Design Specification

**Version:** 1.0  
**Target Platform:** Simple VM (portable C++ runtime)  
**File Extension:** `.simple`

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Design Philosophy](#design-philosophy)
3. [Lexical Structure](#lexical-structure)
4. [Type System](#type-system)
5. [Grammar Specification](#grammar-specification)
6. [Semantic Rules](#semantic-rules)
7. [VM Mapping](#vm-mapping)
8. [Compiler Architecture](#compiler-architecture)

---

## Executive Summary

Simple is a statically-typed, general-purpose programming language that compiles to Simple bytecode executed by the Simple VM. The language emphasizes:

- **Universal mutability system** using `:` (mutable) and `::` (immutable)
- **Minimal syntax** with no unnecessary keywords
- **Strict typing** with explicit type declarations
- **Composition over inheritance** through artifacts and modules
- **First-class procedures** with clear syntax

---

## Design Philosophy

### Core Principles

1. **Consistency**: The `:` vs `::` pattern applies universally to all declarations
2. **Explicitness**: All types must be explicitly declared
3. **Simplicity**: Minimal keywords and clean syntax
4. **Safety**: Immutability by default encourages safer code

### Mutability System

The fundamental principle: **`:` = mutable, `::` = immutable**

This applies to:
- Variables
- artifact members
- Procedure parameters
- Procedure return types
- First-class procedure bindings

---

## Lexical Structure

### Keywords

```
while, for, break, skip, return, default
fn, self, artifact, enum, module
```

### Operators

**Arithmetic:** `+`, `-`, `*`, `/`, `%`, `++`, `--`  
**Bitwise:** `&`, `|`, `^`, `<<`, `>>`  
**Comparison:** `==`, `!=`, `<`, `>`, `<=`, `>=`  
**Logical:** `&&`, `||`, `!`  
**Assignment:** `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`  
**Declaration:** `:`, `::`  
**Access:** `.`, `[]`  
**Conditional Chain:** `|>`  
**Pointer:** `*`, `&`, `@`, `^` (future)  
**Generics:** `<`, `>` (type parameter and type argument lists)

### Literals

```
Integers:  123, -456, 0, 0xFF, 0b1010
Floats:    3.14, -0.5, 1.0e-10
Strings:   "hello world"
Characters: 'a', '\n', '\t'
Booleans:  true, false
Arrays:    [1, 2, 3], [[1, 2], [3, 4]]
```

### Zero-Initialization

All variables that are declared but not explicitly initialized receive zero values:

```
x : i32        // 0
y : i64        // 0
f : f32        // 0.0
d : f64        // 0.0
b : bool       // false
s : string     // ""
c : char       // '\0'
```

artifacts are initialized with all members set to their zero values:

```
Point :: artifact {
    x : f32
    y : f32
}

p : Point = { }  // x = 0.0, y = 0.0
```

### Comments

```
// Single-line comment
/* Multi-line
   comment */
```

### Identifiers

```
identifier ::= [a-zA-Z_][a-zA-Z0-9_]*
```

---

## Type System

### Primitive Types

#### Integer Types (Signed)
- `i8` - 8-bit signed integer
- `i16` - 16-bit signed integer
- `i32` - 32-bit signed integer
- `i64` - 64-bit signed integer
- `i128` - 128-bit signed integer

#### Integer Types (Unsigned)
- `u8` - 8-bit unsigned integer
- `u16` - 16-bit unsigned integer
- `u32` - 32-bit unsigned integer
- `u64` - 64-bit unsigned integer
- `u128` - 128-bit unsigned integer

#### Floating Point Types
- `f32` - 32-bit floating point (IEEE 754 single precision)
- `f64` - 64-bit floating point (IEEE 754 double precision)

#### Other Primitive Types
- `bool` - Boolean type (`true` or `false`)
- `char` - Unicode character (UTF-16 code unit)
- `string` - UTF-16 encoded string

### Composite Types

#### Arrays (Fixed Size)
```
T[N]
T[N][M]
T[N][M][P]
```
- `T` is the element type
- `N`, `M`, `P` are compile-time constant sizes
- Arrays are fixed-size and rectangular (all dimensions must match)
- Example: `i32[10]`, `f32[3][4]`, `bool[5][5][5]`

#### Lists (Dynamic Size)
```
T[]
T[][]
T[][][]
```
- `T` is the element type
- Dynamically sized collections
- Multi-dimensional lists can be jagged (rows can have different lengths)
- Example: `i32[]`, `string[][]`, `f64[][][]`

#### Procedure Types
```
(param_types) : return_type
(param_types) :: return_type
```
- Example: `(i32, i32) : i32` - procedure taking two i32s, returning mutable i32
- Example: `(string) :: bool` - procedure taking string, returning immutable bool

#### Generics (Type Parameters)
Generic types and procedures accept type parameters declared with `<...>` and instantiated with type arguments:

```
Box<T>
Result<T, E>
```

- Type parameters are identifiers like `T`, `U`, `Key`, `Value`
- Type arguments are concrete types: `Box<i32>`, `Map<string, f64>`
- Generic procedures and artifacts may reference their type parameters in member types, parameter types, and return types

**Examples:**
```
Box<T> :: artifact {
    value : T
}

identity<T> : T (value : T) {
    return value
}

boxed : Box<string> = { "hello" }
v : i32 = identity<i32>(10)
```

### User-Defined Types

#### artifacts
Class-like composite types with members and procedures. artifacts are essentially classes and are declared with the `:: artifact` keyword:

```
Point :: artifact {
    x : f32
    y : f32
}
```

#### enums
Strictly scoped enumeration types declared with `:: enum`:

```
Color :: enum {
    Red,
    Green,
    Blue
}
```

#### modules
Static collections of procedures and constants. modules are declared with `:: module` and all members are static:

```
Math :: module {
    PI :: f64 = 3.14159265359

    abs :: f64 (x : f64) {
        |> x < 0.0 { return -x }
        |> default { return x }
    }
}
```

---

## Grammar Specification

### Program Structure

```ebnf
program ::= declaration*

declaration ::= variable_declaration
              | procedure_declaration
              | artifact_declaration
              | module_declaration
              | enum_declaration
```

### Variables

#### Mutable Variable
```ebnf
variable_declaration ::= identifier ":" type "=" expression
```

**Example:**
```
count : i32 = 0
name : string = "Alice"
values : i32[] = [1, 2, 3]
```

#### Immutable Variable
```ebnf
variable_declaration ::= identifier "::" type "=" expression
```

**Example:**
```
MAX_SIZE :: i32 = 100
PI :: f64 = 3.14159
config :: Config = Config()
```

### Procedures

#### Procedure Definition
```ebnf
procedure_declaration ::= identifier generic_parameters? ":" type parameter_list block
                        | identifier generic_parameters? "::" type parameter_list block

parameter_list ::= "(" ")"
                 | "(" parameter ("," parameter)* ")"

parameter ::= identifier ":" type
            | identifier "::" type

generic_parameters ::= "<" identifier ("," identifier)* ">"
```

**Syntax:**
- `name : return_type (params) { block }` - returns mutable value
- `name :: return_type (params) { block }` - returns immutable value

**Examples:**
```
add : i32 (a : i32, b : i32) {
    return a + b
}

get_config :: Config () {
    return Config()
}

process : void (data : Point, config :: Settings) {
    data.x = 10
}

identity<T> : T (value : T) {
    return value
}
```

#### First-Class Procedures
```ebnf
fn_variable ::= identifier ":" "fn" ":" type "=" lambda
              | identifier ":" "fn" "::" type "=" lambda
              | identifier "::" "fn" ":" type "=" lambda
              | identifier "::" "fn" "::" type "=" lambda

lambda ::= "(" parameter_list ")" block
```

**Examples:**
```
// Mutable function variable, returns mutable i32
multiply : fn : i32 = (a : i32, b : i32) {
    return a * b
}

// Immutable function variable, returns immutable string
formatter :: fn :: string = (value : i32) {
    return "Value: " + str(value)
}

// Can reassign multiply
multiply = (a : i32, b : i32) {
    return a * b * 2
}
```

### artifacts

```ebnf
artifact_declaration ::= identifier generic_parameters? "::" "artifact" "{" artifact_body "}"

artifact_body ::= (member_declaration | method_declaration)*

member_declaration ::= identifier ":" type "=" expression
                     | identifier "::" type "=" expression

method_declaration ::= identifier ":" type parameter_list block
                     | identifier "::" type parameter_list block
```

**Example:**
```
Point :: artifact {
    x : f32
    y : f32
    label : string
    
    distance :: f64 () {
        return sqrt(self.x * self.x + self.y * self.y)
    }
    
    move : void (dx : f32, dy : f32) {
        self.x = self.x + dx
        self.y = self.y + dy
    }
}
```

**Instantiation:**

artifacts are initialized using brace syntax with member assignments:

```
// Ordered member values (positional)
p : Point = { 10.0, 20.0, "Origin" }
p2 :: Point = { 5.0, 5.0, "Corner" }

// Named member assignment
p3 : Point = {
    .x = 10.0,
    .y = 20.0,
    .label = "Origin"
}

// Can mix positional and named (positional must come first)
p4 : Point = { 10.0, .y = 20.0, .label = "Mixed" }
```

**Member Access:**
Within methods, use `self` to access members and other methods:
```
self.x = 10.0
value : f32 = self.y
self.other_method()
```

### modules

```ebnf
module_declaration ::= identifier "::" "module" "{" (procedure_declaration | variable_declaration)* "}"
```

modules are artifacts whose members are static. The `:: module` declaration makes the module itself immutable while its members follow their own mutability annotations. modules operate under static semantics and cannot be instantiated.

**Example:**
```
Math :: module {
    PI :: f64 = 3.14159265359

    abs :: f64 (x : f64) {
        |> x < 0.0 { return -x }
        |> default { return x }
    }
    
    max :: i32 (a : i32, b : i32) {
        |> a > b { return a }
        |> default { return b }
    }
}
```

**Usage:**
```
result : f64 = Math.abs(-5.5)
largest : i32 = Math.max(10, 20)
pi_value : f64 = Math.PI
```

### enums

```ebnf
enum_declaration ::= identifier "::" "enum" "{" enum_body "}"

enum_body ::= enum_member ("," enum_member)* ","?

enum_member ::= identifier
              | identifier "=" integer_literal
```

**Example:**
```
Status :: enum {
    Pending = 1,
    Active = 2,
    Completed = 3,
    Cancelled = 4
}

Color :: enum {
    Red,
    Green,
    Blue
}
```

**Usage (Strict Scoping):**
```
current_status : Status = Status.Active
primary : Color = Color.Red

// ERROR: Implicit enum values not allowed
status : Status = Active  // WRONG
```

### Statements

```ebnf
statement ::= variable_declaration
            | assignment_statement
            | if_statement
            | if_else_chain
            | while_statement
            | for_statement
            | return_statement
            | break_statement
            | skip_statement
            | expression_statement
            | block

assignment_statement ::= identifier "=" expression
                       | identifier "+=" expression
                       | identifier "-=" expression
                       | identifier "*=" expression
                       | identifier "/=" expression
                       | identifier "%=" expression
                       | identifier "&=" expression
                       | identifier "|=" expression
                       | identifier "^=" expression
                       | identifier "<<=" expression
                       | identifier ">>=" expression

if_statement ::= expression block

if_else_chain ::= ("|>" expression block)+

while_statement ::= "while" expression block

for_statement ::= "for" "(" (variable_declaration | assignment_statement)? ";" expression? ";" (assignment_statement | expression)? ")" block

return_statement ::= "return" expression?

break_statement ::= "break"

skip_statement ::= "skip"

expression_statement ::= expression

block ::= "{" statement* "}"
```

### Control Flow

#### Simple If
```
condition { block }
```

**Example:**
```
x > 10 {
    print("Greater than 10")
}
```

#### If-Else Chain
```
|> condition { block }
|> condition { block }
|> default { block }
```

**Example:**
```
|> score >= 90 { grade = "A" }
|> score >= 80 { grade = "B" }
|> score >= 70 { grade = "C" }
|> default { grade = "F" }
```

#### While Loop
```
while condition {
    // body
    break    // exit loop
    skip     // continue to next iteration
}
```

**Example:**
```
i : i32 = 0
while i < 10 {
    |> i == 5 { 
        i = i + 1
        skip 
    }
    print(i)
    i = i + 1
}
```

#### For Loop
```
for (init; condition; update) {
    // body
}
```

**Syntax:**
- `init` - variable declaration or assignment (optional)
- `condition` - boolean expression (optional, defaults to true)
- `update` - assignment or expression (optional)

**Examples:**
```
// Traditional counting loop
for (i : i32 = 0; i < 10; i++) {
    print(i)
}

// Count by 2s
for (i : i32 = 0; i < 10; i += 2) {
    print(i)  // 0, 2, 4, 6, 8
}

// Count backwards
for (i : i32 = 10; i >= 0; i--) {
    print(i)  // 10, 9, 8, ..., 0
}

// Multiple operations in update
for (i : i32 = 0; i < 100; i = i + 1) {
    |> i % 2 == 0 { skip }
    print(i)  // prints odd numbers only
}

// Infinite loop (all parts optional)
for (;;) {
    // runs forever
    break  // until break
}

// Declaration outside, no update
count : i32 = 0
for (; count < 10;) {
    print(count)
    count += 1
}
```

### Expressions

```ebnf
expression ::= literal
             | identifier
             | identifier generic_arguments
             | binary_expression
             | unary_expression
             | call_expression
             | index_expression
             | member_expression
             | artifact_literal
             | array_literal
             | "(" expression ")"

binary_expression ::= expression binary_operator expression

unary_expression ::= unary_operator expression
                   | expression "++"
                   | expression "--"

call_expression ::= expression "(" argument_list? ")"

argument_list ::= expression ("," expression)*

generic_arguments ::= "<" type ("," type)* ">"

index_expression ::= expression "[" expression "]"

member_expression ::= expression "." identifier

artifact_literal ::= "{" artifact_init_list? "}"

artifact_init_list ::= artifact_init_item ("," artifact_init_item)*

artifact_init_item ::= expression
                     | "." identifier "=" expression

array_literal ::= "[" array_element_list? "]"

array_element_list ::= expression ("," expression)*
```

### Operator Precedence

From highest to lowest:

1. `++`, `--` (postfix), `[]`, `.` (member access)
2. `!`, `-` (unary), `++`, `--` (prefix)
3. `*`, `/`, `%`
4. `+`, `-`
5. `<<`, `>>`
6. `<`, `>`, `<=`, `>=`
7. `==`, `!=`
8. `&` (bitwise AND)
9. `^` (bitwise XOR)
10. `|` (bitwise OR)
11. `&&` (logical AND)
12. `||` (logical OR)
13. `|>` (conditional chain - statement level)
14. `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=` (assignment)

### Array and List Operations

#### Declaration
```
// Fixed-size arrays
arr : i32[5] = [1, 2, 3, 4, 5]
matrix : i32[3][3] = [
    [1, 2, 3],
    [4, 5, 6],
    [7, 8, 9]
]

// Dynamic lists
list : string[] = ["hello", "world"]
jagged : i32[][] = [
    [1, 2],
    [3, 4, 5, 6],
    [7]
]

// Empty array
empty : bool[10]              // All false (zero-initialized)

// Immutable
constants :: f64[3] = [3.14, 2.71, 1.41]
```

#### Indexing
```
// 1D
first : i32 = arr[0]          // First element
last : i32 = arr[5]           // Last element

// 2D
value : i32 = matrix[2][3]    // Row 2, Column 3
matrix[0][0] = 100            // Modify first element

// Jagged lists **not implemented**
jagged[0][1] = 99             // Second element of first row
len : i32 = len(jagged[2])    // Length of second row (6)
```

---

## Semantic Rules

### Type Checking

1. **All variables must have explicit type declarations**
   ```
   x : i32 = 10       // OK
   y = 10             // ERROR: missing type
   ```

2. **No implicit type conversions**
   ```
   a : i32 = 10
   b : f32 = a        // ERROR: cannot assign i32 to f32
   b : f32 = f32(a)   // OK: explicit conversion
   ```

3. **Binary operations require matching types**
   ```
   x : i32 = 10
   y : f64 = 3.14
   z = x + y          // ERROR: type mismatch
   z = f64(x) + y     // OK
   ```

4. **Generic type arguments must be concrete**
   ```
   Box<T> :: artifact { value : T }
   b : Box<T> = { }   // ERROR: T is not in scope
   b : Box<i32> = { 1 }  // OK
   ```

### Mutability Rules

1. **Immutable variables cannot be reassigned**
   ```
   x :: i32 = 10
   x = 20             // ERROR: cannot reassign immutable variable
   ```

2. **Immutable artifact members cannot be modified**
   ```
   Point :: artifact {
       x :: f32
   }
   p : Point = { 10.0 }
   p.x = 20.0         // ERROR: x is immutable
   ```

3. **Immutable parameters cannot be modified**
   ```
   process : void (value :: i32) {
       value = value + 1    // ERROR: value is immutable
   }
   ```

4. **Return type mutability affects returned values**
   ```
   get_point :: Point () {
       return { 1.0, 2.0 }
   }
   
   p : Point = get_point()
   // Compiler may enforce immutability constraints based on return type
   ```

### Scoping Rules

1. **Block scope**: Variables declared in a block are only visible within that block
2. **Shadowing allowed**: Inner scope variables can shadow outer scope variables
3. **enum scoping**: enum values must always be qualified with their type name
   ```
   Status.Active      // OK
   Active             // ERROR: unqualified enum value
   ```

### Procedure Rules

1. **All procedures must have explicit return types**
   ```
   foo () { }         // ERROR: missing return type
   foo : void () { }  // OK
   ```

2. **All parameters must have explicit types**
   ```
   add (a, b) { }           // ERROR: missing types
   add : i32 (a : i32, b : i32) { }  // OK
   ```

3. **Procedures returning non-void must have return statement**
   ```
   get_value : i32 () {
       // ERROR: missing return
   }
   
   get_value : i32 () {
       return 42          // OK
   }
   ```

4. **Generic procedures are instantiated at use sites**
   ```
   identity<T> : T (value : T) { return value }
   x : i32 = identity<i32>(10)    // OK
   y : i32 = identity(10)         // OK if type can be inferred
   z : i32 = identity()           // ERROR: cannot infer T
   ```

### artifact Rules

1. **Members are zero-initialized if not given a value**
   ```
   Point :: artifact {
       x : f32           // Automatically 0.0
       y : f32 = 10.0    // Explicitly 10.0
   }
   
   p : Point = { }  // x = 0.0, y = 10.0
   ```

2. **Instantiation uses brace syntax with positional or named values**
   ```
   Point :: artifact {
       x : f32
       y : f32
   }
   
   // Positional
   p : Point = { 10.0, 20.0 }  // x=10.0, y=20.0
   
   // Named
   p2 : Point = { .x = 10.0, .y = 20.0 }
   
   // Mixed (positional must come first)
   p3 : Point = { 10.0, .y = 20.0 }
   ```

3. **Within methods, use `self` to access members and call other methods**
   ```
   Point :: artifact {
       x : f32
       
       get_x :: f32 () {
           return self.x      // OK
           return x           // ERROR: undefined identifier
       }
       
       move : void (dx : f32) {
           self.x = self.x + dx  // Access member
           self.normalize()       // Call other method
       }
   }
   ```

### Generic Rules

1. **Type parameters are scoped to their declaration**
   ```
   Box<T> :: artifact { value : T }
   use_box : void () {
       x : T = 1       // ERROR: T not in scope
   }
   ```

2. **Generic types must be fully instantiated in variable declarations**
   ```
   b : Box = { }       // ERROR: missing type arguments
   b : Box<i32> = { 1 }  // OK
   ```

3. **Generic procedure calls may infer type arguments**
   ```
   identity<T> : T (value : T) { return value }
   a : i32 = identity(10)          // OK, T inferred as i32
   b : string = identity("hi")     // OK, T inferred as string
   ```

4. **First-class `fn` values are always fully concrete**
   ```
   identity<T> : T (value : T) { return value }
   f : fn : i32 = (v : i32) { return identity<i32>(v) }  // OK
   ```

5. **Generics are invariant**
   ```
   Box<T> :: artifact { value : T }
   b1 : Box<string> = { "hi" }
   b2 : Box<object> = b1    // ERROR: invariant type parameters
   ```

---

## VM Mapping

Simple compiles to Simple bytecode executed by the Simple VM, implemented in portable C++.

### Value Representation

- Integer and floating types are stored in fixed-width VM slots matching the Simple type.
- `bool` is stored as `u8` (0 or 1).
- `char` is a UTF-16 code unit (`u16`).
- `string` is a heap object containing UTF-16 data.
- Arrays, lists, artifacts, modules, and closures are heap objects referenced by a VM ref value.

### Heap Object Layout (Conceptual)

- Header: `type_id`, `size`, GC/refcount metadata
- Payload: type-specific data (fields, elements, captures)

### Globals and Locals

- Globals live in a global slot table (indexed by the bytecode).
- Locals live in a fixed slot array per call frame.

### Procedures and Calls

- Each procedure compiles to a bytecode function with a signature and local count.
- Call frames store return address, locals, and evaluation stack base.

### Mutability

Mutability rules are enforced by the compiler. The VM treats storage as writable.

### First-Class Procedures

Closures are heap objects containing a `function_id` and captured environment values.

### Generics

Generics are monomorphized at compile time (specialized bytecode per concrete type).

### Bytecode Module Layout

- Header: magic, version, endianness
- Sections: types, constants, globals, functions, code, debug info

### Instruction Set

The full opcode list and encoding rules live in `Simple_VM_Opcode_Spec.md`.

### Execution Model

- Execution can run in the interpreter or via a tiered JIT.
- The VM uses a tracing GC; roots are globals, call frames, locals, and the operand stack.

---

## Compiler Architecture

### Pipeline Overview

```
Source Code (.simple)
    ↓
┌─────────────┐
│   Lexer     │ -> Tokens
└─────────────┘
    ↓
┌─────────────┐
│   Parser    │ -> Abstract Syntax Tree (AST)
└─────────────┘
    ↓
┌─────────────┐
│   Semantic  │ -> Annotated AST
│   Analyzer  │   (Type checking, scope resolution)
└─────────────┘
    ↓
┌─────────────┐
│  Bytecode   │ -> Simple bytecode
│  Emitter    │
└─────────────┘
    ↓
┌─────────────┐
│  VM Image   │ -> .sbc module
│  Packager   │
└─────────────┘
```

### Phase 1: Lexical Analysis

**Input:** Source code string  
**Output:** Stream of tokens

**Responsibilities:**
- Tokenize source code
- Recognize keywords, identifiers, operators, literals
- Track line/column positions for error reporting
- Handle comments (discard)

**Token Types:**
```
IDENTIFIER, KEYWORD, OPERATOR, LITERAL, 
COLON, DOUBLE_COLON, LBRACE, RBRACE, 
LPAREN, RPAREN, LBRACKET, RBRACKET,
COMMA, SEMICOLON, PIPE_ARROW, EOF
```

### Phase 2: Syntax Analysis (Parsing)

**Input:** Token stream  
**Output:** Abstract Syntax Tree (AST)

**Responsibilities:**
- Build AST from tokens
- Enforce grammar rules
- Report syntax errors
- Handle operator precedence

**AST Node Types:**
```
Program
Declaration (Variable, Procedure, artifact, module, enum)
Statement (Assignment, If, IfElseChain, While, Return, Break, Skip)
Expression (Binary, Unary, Call, Index, Member, Literal, Identifier)
Type (Primitive, Array, List, Procedure, UserDefined, GenericInstance, TypeParameter)
```

**Parser Strategy:** Recursive descent parser with operator precedence climbing

### Phase 3: Semantic Analysis

**Input:** AST  
**Output:** Annotated AST with type information

**Responsibilities:**
- Build symbol table
- Resolve identifiers
- Type checking
- Mutability checking
- Flow analysis (return paths, unreachable code)
- Validate enum scoping

**Symbol Table:**
```
Scope {
    parent: Scope?
    symbols: Map<string, Symbol>
}

Symbol {
    name: string
    type: Type
    mutable: bool
    kind: Variable | Procedure | artifact | module | enum | TypeParameter
}
```

### Phase 4: Bytecode Generation

**Input:** Annotated AST  
**Output:** Simple bytecode

**Responsibilities:**
- Emit typed VM instructions
- Build constant pool and function metadata
- Emit local/global slot maps
- Compute stack usage per function
- Emit optional debug line info

**Generation Strategy:**
- Emit opcodes defined in `Simple_VM_Opcode_Spec.md`
- Use stack-based evaluation with explicit local/global loads

**VM Stack Code Generation:**

For expression: `a + b * c`

1. Parse to AST: `Add(a, Mul(b, c))`
2. Post-order traversal:
   - Load `b`
   - Load `c`
   - Multiply
   - Load `a`
   - Add

**Bytecode (conceptual):**
```
load_local 1   // b
load_local 2   // c
mul_i32
load_local 0   // a
add_i32
```

### Phase 5: Bytecode Packaging

**Input:** Simple bytecode  
**Output:** `.sbc` module

**Responsibilities:**
- Write module to disk
- Record entry point (main function id)
- Persist type, constant, and function tables
- Emit optional debug sections

**Entry Point:**

Simple requires a top-level procedure to serve as entry point. By convention, `main : i32 () { }`:

```
main : i32 () {
    println("Hello, World!")
    return 0
}
```

### Error Handling

**Error Types:**
1. **Lexical Errors:** Invalid characters, unterminated strings
2. **Syntax Errors:** Unexpected tokens, missing delimiters
3. **Semantic Errors:** Type mismatches, undefined identifiers, mutability violations
4. **Code Generation Errors:** Invalid bytecode sequences

**Error Reporting Format:**
```
error[E0001]: type mismatch
  --> file.simple:10:5
   |
10 |     x : i32 = 3.14
   |               ^^^^ expected i32, found f64
```

### Optimization Opportunities

**Phase 1 - AST Level:**
- Constant folding
- Dead code elimination
- Common subexpression elimination

**Phase 2 - Bytecode Level:**
- Peephole optimization
- Stack effect simplification
- Tail call optimization

**Phase 3 - VM Runtime:**
- Optional JIT/AOT compilation
- Garbage collection / refcount tuning
- Native code generation hooks

---

## Implementation Plan (Lang Module → SIR Text)

This compiler targets **SIR text** (not bytecode). The VM compiles SIR → SBC and executes. This plan is ordered by dependency and reflects current progress.

### Phase 1: Frontend Foundations

**Goal:** A reliable lexer/parser/AST that covers the core grammar.

**Status:** IN PROGRESS  
**Done:** core keywords/tokens, numeric/string/char literals, comments, if/else/|>, artifact/enum/module decls, fn literals, postfix ++/--, dot member access, enum dot qualification.  
**Next:** finalize remaining tokens/operators, lock full type syntax, finish error recovery.

### Phase 2: Semantic Validation

**Goal:** A consistent, typed AST with correct scoping/returns.

**Status:** IN PROGRESS  
**Done:** duplicate top-level/member/param/local checks; break/skip-in-loop; return rules (void/non-void); enum dot qualification; disallow `::` in expression access.  
**Next:** full type checking, mutability rules, array/list indexing types, artifact init checks, procedure type checking, generics rules.

### Phase 3: SIR Text Emission

**Goal:** Emit SIR text for all supported constructs and run through VM SIR pipeline.

**Status:** IN PROGRESS  
**Done:** SIR emitter/IR builder infrastructure and IR tests.  
**Next:** language → SIR mapping for expressions, control flow, artifacts, enums, arrays/lists, fn values.

### Phase 4: End-to-End Tests

**Goal:** Language tests compile to SIR, then SIR → SBC → VM run.

**Status:** TODO  
**Next:** golden language fixtures, performance fixtures, runtime behavior tests.

### Phase 5: Tooling + UX

**Goal:** CLI workflow and diagnostics suitable for v0.1.

**Status:** TODO  
**Next:** consistent error format, source spans, `simple build/run/check` wired to SIR.

---

## Detailed Plan (Dependency Ordered)

### 1) Lexer
- [x] Keywords: `while`, `for`, `break`, `skip`, `return`, `if`, `else`, `default`, `fn`, `self`, `artifact`, `enum`, `module`, `true`, `false`
- [x] Literals: integer, float, string, char
- [x] Operators/punctuators (including `:`, `::`, `|>`, `[]`, `()`, `{}`)
- [x] Single-line and multi-line comments
- [x] Line/column tracking
- [x] Final pass over remaining operators/edge cases (error recovery, invalid tokens)

### 2) Parser
- [x] Declarations: variables, procedures, artifacts, modules, enums
- [x] Statements: if/else, `|>`, while/for, return, break/skip, blocks
- [x] Expressions: precedence, calls, member access (`.` only), fn literals, artifact literals
- [x] Parameter lists with mutability
- [x] Type grammar locked for all type literals (`i8..i128`, `u8..u128`, `f32`, `f64`, `bool`, `char`, `string`)
- [x] Array/list type syntax and constraints fully parsed
- [x] Generics parsing (type params/args) if kept
- [ ] Parser error recovery pass

### 3) AST
- [x] Nodes for current declarations/statements/expressions
- [x] Source spans on nodes used in diagnostics
- [ ] Complete type node coverage for all type literals and container types

### 4) Semantic Validation
- [x] Scope building + identifier resolution
- [x] Duplicate checks (top-level, member, param, local)
- [x] `break`/`skip` only inside loops
- [x] Return checks (void/non-void, all paths for if/else/|>)
- [x] Enum dot qualification enforced (`Enum.Value`)
- [x] Mutability rules (`:` vs `::`)
- [ ] Full type checking for expressions/assignments
- [ ] Artifact initialization rules + `self` access validation
- [x] Array/list indexing type checks
- [x] Procedure type checking + call argument validation
- [ ] Generics rules (if retained)

### 5) SIR Text Emission
- [x] SIR emitter/IR builder APIs exist
- [x] SIR text compiler exists (SIR → SBC)
- [ ] Language → SIR mapping for expressions
- [ ] Control flow emission (if/|>/loops/break/skip)
- [ ] Artifact/enum emission + member access
- [ ] Arrays/lists + literals
- [ ] Fn values and calls

### 6) Tooling + CLI
- [ ] `simple build` emits `.sir`
- [ ] `simple run` uses VM to compile SIR → SBC and execute
- [ ] `simple check` syntax/semantic checks only
- [ ] Diagnostic format + source span reporting

### 7) End-to-End Tests
- [ ] Language fixtures → SIR → SBC → run
- [ ] Negative tests for parser/semantic errors
- [ ] Performance fixtures via SIR runner

## Standard Library

### Built-in Procedures

```
print : void (value : string)
println : void (value : string)

str : string (value : i32)
str : string (value : f64)
str : string (value : bool)

i32 : i32 (value : string)
f64 : f64 (value : string)

len<T> : i32 (arr : T[])       // Length of 1D list
len<T> : i32 (arr : T[][])     // Length of outer dimension (number of rows)
len : i32 (str : string)    // Length of string
```

### Standard modules

```
IO :: module {
    read_file :: string (path : string)
    write_file : void (path : string, content : string)
}

Math :: module {
    abs :: f64 (x : f64)
    sqrt :: f64 (x : f64)
    pow :: f64 (base : f64, exp : f64)
    sin :: f64 (x : f64)
    cos :: f64 (x : f64)
}

String :: module {
    concat :: string (a : string, b : string)
    substring :: string (s : string, start : i32, length : i32)
    to_upper :: string (s : string)
    to_lower :: string (s : string)
}
```

---

## Example Programs

### Hello World

```
main : i32 () {
    println("Hello, World!")
    return 0
}
```

### FizzBuzz

```
main : i32 () {
    for (i : i32 = 1; i <= 100; i++) {
        |> i % 15 == 0 { println("FizzBuzz") }
        |> i % 3 == 0 { println("Fizz") }
        |> i % 5 == 0 { println("Buzz") }
        |> default { println(str(i)) }
    }
    return 0
}
```

### artifact Example

```
Point :: artifact {
    x : f64
    y : f64
    
    distance :: f64 () {
        return Math.sqrt(self.x * self.x + self.y * self.y)
    }
    
    move : void (dx : f64, dy : f64) {
        self.x = self.x + dx
        self.y = self.y + dy
    }
}

main : i32 () {
    p : Point = { 3.0, 4.0 }
    println("Distance: " + str(p.distance()))
    
    p.move(1.0, 1.0)
    println("New position: (" + str(p.x) + ", " + str(p.y) + ")")
    
    return 0
}
```

### First-Class Procedures

```
apply : i32 (operation : fn : i32, a : i32, b : i32) {
    return operation(a, b)
}

main : i32 () {
    add : fn : i32 = (x : i32, y : i32) { return x + y }
    multiply : fn : i32 = (x : i32, y : i32) { return x * y }
    
    sum : i32 = apply(add, 5, 3)
    product : i32 = apply(multiply, 5, 3)
    
    println("Sum: " + str(sum))
    println("Product: " + str(product))
    
    return 0
}
```

### Generics

```
swap<T> : void (a : T, b : T) {
    temp : T = a
    a = b
    b = temp
}

Box<T> :: artifact {
    value : T
    
    get :: T () {
        return self.value
    }
}

main : i32 () {
    i : i32 = 1
    j : i32 = 2
    swap<i32>(i, j)

    box : Box<string> = { "hello" }
    println(box.get())
    return 0
}
```

### module Example

```
Math :: module {
    PI :: f64 = 3.14159265359
    E :: f64 = 2.71828182846
    
    abs :: f64 (x : f64) {
        |> x < 0.0 { return -x }
        |> default { return x }
    }
    
    max :: i32 (a : i32, b : i32) {
        |> a > b { return a }
        |> default { return b }
    }
}

main : i32 () {
    println("PI = " + str(Math.PI))
    println("abs(-5.5) = " + str(Math.abs(-5.5)))
    println("max(10, 20) = " + str(Math.max(10, 20)))
    return 0
}
```

---

## Future Considerations

### Pattern Matching

```
|> value is Point(x, y) { println("Point at " + str(x) + ", " + str(y)) }
|> value is Status.Active { println("Active status") }
|> default { println("Unknown") }
```

### modules/Packages

```
import Math
import IO.File

// Or
using Math
using IO.File
```

---

## Appendix

### Grammar Summary (EBNF)

```ebnf
program ::= declaration*

declaration ::= variable_declaration
              | procedure_declaration  
              | artifact_declaration
              | module_declaration
              | enum_declaration

variable_declaration ::= identifier (":" | "::") type "=" expression

procedure_declaration ::= identifier generic_parameters? (":" | "::") type parameter_list block

parameter_list ::= "(" (parameter ("," parameter)*)? ")"
parameter ::= identifier (":" | "::") type

artifact_declaration ::= identifier generic_parameters? "::" "artifact" "{" (member_declaration | method_declaration)* "}"
member_declaration ::= identifier (":" | "::") type "=" expression
method_declaration ::= identifier (":" | "::") type parameter_list block

module_declaration ::= identifier "::" "module" "{" (procedure_declaration | variable_declaration)* "}"

enum_declaration ::= identifier "::" "enum" "{" enum_member ("," enum_member)* ","? "}"
enum_member ::= identifier ("=" integer_literal)?

statement ::= variable_declaration
            | identifier "=" expression
            | expression block
            | ("|>" expression block)+
            | "while" expression block
            | "for" "(" (variable_declaration | identifier "=" expression)? ";" expression? ";" (assignment_statement | expression)? ")" block
            | "return" expression?
            | "break"
            | "skip"
            | expression
            | block

block ::= "{" statement* "}"

expression ::= literal
             | identifier
             | identifier generic_arguments
             | expression binary_op expression
             | unary_op expression
             | expression "(" (expression ("," expression)*)? ")"
             | expression "[" expression "]"
             | expression "." identifier
             | "{" (artifact_init_item ("," artifact_init_item)*)? "}"
             | "[" (expression ("," expression)*)? "]"
             | "(" expression ")"

artifact_init_item ::= expression
                     | "." identifier "=" expression

type ::= primitive_type
       | identifier generic_arguments?
       | type "[" integer_literal "]"
       | type "[" "]"
       | "(" (type ("," type)*)? ")" (":" | "::") type

generic_parameters ::= "<" identifier ("," identifier)* ">"
generic_arguments ::= "<" type ("," type)* ">"
```

### Reserved Words

```
while, for, break, skip, return, default, fn, self,
artifact, enum, module,
true, false
```

### File Extension

`.simple`

### Compiler Invocation

```bash
# Compile to bytecode module
simple build program.simple -o program.sbc

# Compile to library module
simple build library.simple --lib -o library.sbc

# Run directly (compile + execute)
simple run program.simple

# Check syntax only
simple check program.simple
```

---

**End of Specification**

**Version:** 1.0  
**Last Updated:** 2026-01-29


---

### Simple Implementation Notes

# Simple Programming Language
## Implementation Document

**Version:** 1.0  
**Target Platform:** Simple VM (portable C++ runtime)  
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
- The compiler pipeline (lexer -> parser -> AST -> semantic analysis -> bytecode emission -> bytecode packaging)
- Language features as specified (types, statements, expressions, artifacts, modules, enums, generics, standard library)
- CLI tooling and diagnostics

## Implementation Phases

### Phase 0: VM Core + Bytecode Spec

**Features:**
- Define bytecode format and module sections
- Define opcode list (see `Simple_VM_Opcode_Spec.md`)
- Define metadata tables (types, fields, methods, signatures)
- Define verifier rules (IL-style)
- Implement C++ VM fetch/decode/execute loop
- Implement call frames, locals, globals, and value stack
- Implement heap objects (string, array, list, artifact, closure)
- Implement tracing GC (mark-sweep or generational)
- Implement tiered JIT (tier 0 quick + tier 1 optimizing)
- Implement entry point loading and execution

**Deliverable:** Minimal VM runs a hand-written bytecode "hello"

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

### 0) VM Runtime (C++)
- [ ] Define bytecode header, versioning, and section layout
- [ ] Define opcode enum and operand decoding rules
- [ ] Define metadata tables and indexing rules
- [ ] Implement verifier (stack/type safety)
- [ ] Implement fetch/decode/execute loop
- [ ] Implement call frames, locals, globals, and value stack
- [ ] Implement core opcodes: const/load/store/arithmetic/compare/branch/call/ret
- [ ] Implement heap objects: string, array, list, artifact, closure
- [ ] Implement tracing GC (mark-sweep or generational)
- [ ] Implement tiered JIT (tier 0 quick, tier 1 optimizing)
- [ ] Implement debug hooks (line info, traps)

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

### 5) Bytecode Generation
- [ ] Emit VM instructions for primitives, arrays, lists, artifacts, modules, enums
- [ ] Emit closures for `Fn` procedures
- [ ] Monomorphize generics into concrete bytecode
- [ ] Emit globals and locals with stable slot indexes
- [ ] Emit control flow: if, if-else chain, while, for, break, skip
- [ ] Emit expression evaluation (binary, unary, call, index, member)
- [ ] Emit artifact initialization (positional and named)
- [ ] Emit array and list literals
- [ ] Emit return statements and default returns for `void`

### 6) Bytecode Packaging
- [ ] Build module header and section tables
- [ ] Define entry point (`main : i32 ()`)
- [ ] Write module to disk as `.sbc`

### 7) Diagnostics and Errors
- [ ] Uniform error format (`error[E0001]: ...`)
- [ ] Report line/column and highlight ranges
- [ ] Distinguish syntax vs semantic errors

### 8) CLI Commands
- [ ] `simple build` emits `.sbc`
- [ ] `simple run` compiles + executes on the VM
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
- [ ] Optimization passes (AST and bytecode)
- [ ] Standard library modules wired to VM runtime
- [ ] CLI tools stable
- [ ] Optional LSP and debugger hooks

---

## Non-Goals

- [ ] Implementing pointers/unsafe system
- [ ] Pattern matching
- [ ] Modules/packages/import system

---

**End of Implementation Document**


---
