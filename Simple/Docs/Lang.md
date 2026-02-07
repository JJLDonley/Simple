# Simple Language

**Note:** This document consolidates the Simple language specification. Implementation plans live in `Simple/Docs/Implementation.md`.

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
fn, self, artifact, enum, module, union
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
- `char` - Byte character (`u8`)
- `string` - UTF-16 encoded string

### Pointer Types

- `*T` - typed pointer to `T`
- `*void` - opaque/raw pointer
- Pointers are primarily intended for FFI interop boundaries (`extern`/`Core.DL`)
- Current lowering maps pointer ABI values to 64-bit integer slots in VM call paths

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

#### unions (Reserved)
Strictly scoped tagged unions declared with `:: union`. The keyword is reserved, and implementation is still deferred.

---

## Grammar Specification

### Program Structure

```ebnf
program ::= declaration*

declaration ::= variable_declaration
              | procedure_declaration
              | import_declaration
              | extern_declaration
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

### Imports and Externs

```ebnf
import_declaration ::= "import" string_literal ("as" identifier)?

extern_declaration ::= "extern" identifier "." identifier (":" | "::") type parameter_list
                     | "extern" identifier (":" | "::") type parameter_list
```

`extern` is declaration-only.
- No block body is allowed.
- No `=` initializer form is allowed.

**Valid:**
```
extern ffi.simple_add_i32 : i32 (a : i32, b : i32)
```

**Invalid:**
```
extern ffi.simple_add_i32 : i32 = (a : i32, b : i32)
extern ffi.simple_add_i32 : i32 (a : i32, b : i32) { return 0 }
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

Semicolons are optional at statement boundaries. A newline or `}` terminates a statement.
Use `;` to place multiple statements on the same line.

### Control Flow

#### Simple If
```
condition { block }
```

**Example:**
```
x > 10 {
    IO.print("Greater than 10")
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
    IO.print(i)
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
    IO.print(i)
}

// Count by 2s
for (i : i32 = 0; i < 10; i += 2) {
    IO.print(i)  // 0, 2, 4, 6, 8
}

// Count backwards
for (i : i32 = 10; i >= 0; i--) {
    IO.print(i)  // 10, 9, 8, ..., 0
}

// Multiple operations in update
for (i : i32 = 0; i < 100; i = i + 1) {
    |> i % 2 == 0 { skip }
    IO.print(i)  // prints odd numbers only
}

// Infinite loop (all parts optional)
for (;;) {
    // runs forever
    break  // until break
}

// Declaration outside, no update
count : i32 = 0
for (; count < 10;) {
    IO.print(count)
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
   b : f32 = @f32(a)  // OK: explicit conversion
   ```

3. **Binary operations require matching types**
   ```
   x : i32 = 10
   y : f64 = 3.14
   z = x + y          // ERROR: type mismatch
   z = @f64(x) + y    // OK
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
3. **enum and union scoping**: enum values (and union values once implemented) must always be qualified with their type name
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

### Dynamic Library Manifest (Core.DL)

`Core.DL.open` supports a typed symbol manifest via an extern module:

```
import "Core.DL" as DL
extern ffi.simple_add_i32 : i32 (a : i32, b : i32)
extern ffi.simple_hello : string ()

main : i32 () {
    lib : i64 = DL.Open("Simple/Tests/ffi/libsimpleffi.so", ffi)
    sum : i32 = lib.simple_add_i32(64, 64)
    msg : string = lib.simple_hello()
    DL.Close(lib)
    return 0
}
```

- The second `DL.Open` argument must be an extern module identifier.
- Calls are type-checked from extern signatures.
- Dynamic VM dispatch supports extern ABI types directly (no artifact flattening in signatures):
  - parameters: `i8/i16/i32/i64/u8/u16/u32/u64/f32/f64/bool/char/string/*T`, enums, and artifacts
  - returns: `void` plus the same scalar/pointer/enum/artifact set
  - arity: up to 254 extern parameters per dynamic symbol (plus internal function-pointer slot)
  - artifact parameters/returns are marshalled by-value via runtime ABI struct marshalling

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
- `char` is a byte (`u8`).
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
│   Lexer     │ → Tokens
└─────────────┘
    ↓
┌─────────────┐
│   Parser    │ → Abstract Syntax Tree (AST)
└─────────────┘
    ↓
┌─────────────┐
│   Semantic  │ → Annotated AST
│   Analyzer  │   (Type checking, scope resolution)
└─────────────┘
    ↓
┌─────────────┐
│  Bytecode   │ → Simple bytecode
│  Emitter    │
└─────────────┘
    ↓
┌─────────────┐
│  VM Image   │ → .sbc module
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
    IO.println("Hello, World!")
    return 0
}
```

If `main` omits an explicit return, the compiler inserts `return 0`.

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

## Appendix

### Grammar Summary (EBNF)

```ebnf
program ::= declaration*

declaration ::= variable_declaration
              | import_declaration
              | extern_declaration
              | procedure_declaration  
              | artifact_declaration
              | module_declaration
              | enum_declaration

import_declaration ::= "import" string_literal ("as" identifier)?
extern_declaration ::= "extern" identifier ("." identifier)? (":" | "::") type parameter_list

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
       | "*" type
       | type "[" integer_literal "]"
       | type "[" "]"
       | "(" (type ("," type)*)? ")" (":" | "::") type

generic_parameters ::= "<" identifier ("," identifier)* ">"
generic_arguments ::= "<" type ("," type)* ">"
```

### Reserved Words

```
while, for, break, skip, return, default, fn, self,
artifact, enum, module, union,
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
