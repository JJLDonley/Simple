# Simple::Lang (Specification)

This document is authoritative for Simple language syntax and semantics.

## Core Principles
- Strict typing everywhere.
- Explicit mutability everywhere.
- Deterministic validation errors.
- No implicit fallback for unsupported constructs.

## Program Entry + Top-Level Execution
- Top-level statements execute in source order (script-style behavior).
- Top-level declarations define globals and do not execute.
- If a `main :: i32 () { ... }` procedure is present, it is used as the entry point
  and top-level statements are not executed.
  - This also applies to `main : i32 () { ... }`.

## Lexical Tokens
- Identifiers: ASCII letters or `_` followed by ASCII letters, digits, or `_`.
- Comments:
  - Line: `// ...` to end of line.
  - Block: `/* ... */` (not nested).
- Keywords:
  - `while`, `for`, `break`, `skip`, `return`, `if`, `else`, `default`
  - `fn`, `self`
  - `artifact`, `Artifact`, `enum`, `Enum`, `module`, `Module`
  - `import`, `extern`, `as`
  - `true`, `false`
- Operators and punctuation:
  - `:`, `::`, `=`, `,`, `.`, `..`, `;`
  - `+ - * / %`
  - `+= -= *= /= %=`
  - `& | ^ << >>`
  - `&= |= ^= <<= >>=`
  - `== != < <= > >=`
  - `&& || !`
  - `++ --`
  - `|>` (chain)
  - `@` (cast)

## Mutability
`:` means mutable, `::` means immutable.

Mutability applies to:
- variables
- parameters
- procedure return types
- artifact fields
- module members

Examples:

```simple
x : i32 = 10
name :: string = "Simple"
count : i32 = 0
ready :: bool = false
title : string = "ok"
```

## Types

### Primitive Types
- Signed integers: `i8 i16 i32 i64 i128`
- Unsigned integers: `u8 u16 u32 u64 u128`
- Floating point: `f32 f64`
- `bool`, `char`, `string`

### Composite Types
- Pointers: `T*`
- Arrays (static): `T{N}` or unsized `T{}`
- Lists (dynamic): `T[]`

Example parameter types:
- `bullets : Bullet{}`
- `active : Bullet[]`

Address-of is an operator, not a type: `&variable`.

### Procedure Types
Procedure types are first-class values.

Syntax:

```simple
type:
fn RetType (params...)
fn<T, U, V> RetType (params...)

procedure value declaration:
name (:|::) fn RetType (params...) = (param1, param2, ...) { block }
```

Note:
- The parameter list on the right-hand side omits types; types are taken from the `fn` type.

Type examples:

```simple
fn i32 (a : i32, b : i32)
fn void ()
fn bool (value : bool)
fn f64 (x : f64, y : f64)
fn<T> T (value : T)
```

Procedure value declaration examples:

```simple
sum : fn i32 (a : i32, b : i32) = (a, b) { return a + b }
is_even :: fn bool (v : i32) = (v) { return v % 2 == 0 }
next_id : fn i64 () = () { return 1 }
mix : fn f64 (a : f64, b : f64) = (a, b) { return a + b }
echo : fn string (s : string) = (s) { return s }
```

### Generics (Monomorphization)
Generics are monomorphized at compile time (no runtime type parameters).

Syntax:
- `<T, U, V>` for one or more type parameters.

Example:

```simple
Map<K, V> :: Artifact { }
```

More generic examples:

```simple
Pair<A, B> :: Artifact { }
Result<T, E> :: Artifact { }
Option<T> :: Enum { None = 0, Some = 1 }
List<T> :: Artifact { }
Dict<K, V> :: Artifact { }
```

## Declarations

### Variables
Syntax:

```simple
ident (:|::) Type = expr
```

Examples:

```simple
count : i32 = 42
label :: string = "ready"
total : i64 = 0
pi :: f64 = 3.1415926
flag : bool = true
```

Global variables:
- Variable declarations at the top level are global.

### Procedures
Declaration syntax:

```simple
procedure_name<T, U, V> (:|::) RetType (params...) { block }
```

Example:

```simple
add : i32 (a : i32, b : i32) {
  return a + b
}
sub : i32 (a : i32, b : i32) {
  return a - b
}
mul : i64 (a : i64, b : i64) {
  return a * b
}
is_zero :: bool (v : i32) {
  return v == 0
}
max : i32 (a : i32, b : i32) {
  if (a > b) { return a }
  return b
}
```

Procedure returning a procedure:

```simple
return_add_proc<T> : fn<K> K (a: K, b: K) () {
  return add<T>
}
```

More typical form:

```simple
make_adder : fn i32 (a: i32, b: i32) () {
  add : i32 (a : i32, b : i32) {
    return a + b
  }

  return add
}
```

### Parameters
Syntax:

```simple
paramName (:|::) Type
```

Examples:

```simple
count : i32
name :: string
dt : f32
ok :: bool
buffer : i32[]
```

### Procedure Types As Parameters
Use `fn` types directly for procedure parameters.

Syntax:
```simple
paramName : fn RetType (params...)
paramName :: fn RetType (params...)
```

Example:

```simple
run : void (cb : fn void ()) {
  cb()
}
map : void (f : fn i32 (x : i32), value : i32) {
  f(value)
}
apply :: void (op :: fn bool (a : bool, b : bool), a : bool, b : bool) {
  op(a, b)
}
make : void (factory : fn i64 (), out : i64) {
  out = factory()
}
on_tick : void (cb : fn void (dt : f32), dt : f32) {
  cb(dt)
}
```

## Call-Site Literal Coercion (Contextual Typing)
When a type is known from context (parameter type, return type, variable type),
literal expressions are coerced to the expected type.

Applies to:
- Numeric literals (`10`, `3.14`) -> target numeric type.
- String literals (`"text"`) -> `string` or compatible string type.
- Char literals (`'a'`) -> `char` or compatible char type.
- List literals (`[...]`) -> target `T[]`.
- Array literals (`{...}`) -> target `T{N}` or `T{}`.
- Artifact literals (`{ ... }`) -> target artifact type.

List/array literal resolution:
- If there is no contextual type, `[...]` is a list literal.
- If the contextual type is `T{N}` or `T{}`, `{...}` is an array literal.
- For `T{N}`, element count must match `N`.

Default numeric literal types (when no context exists):
- Integer literals default to `i32`.
- Float literals default to `f64`.

Examples:

```simple
extern ffi.DrawCircle: void (centerX: i32, centerY: i32, radius: f32, color: Color)
Color :: Artifact { r : u8 g : u8 b : u8 a : u8 }
DrawCircle(10, 10, 10, { 255, 0, 0, 255 })
```

```simple
returnVecs : Vec3[] () {
  return [{ 100, 100, 100 }]
}
```

```simple
print : void (text : string) { }
putc : void (ch : char) { }
take_list : void (vals : i32[]) { }
take_array : void (vals : i32{3}) { }
take_color : void (c : Color) { }

print("Simple")
putc('A')
take_list([1, 2, 3])
take_array({1, 2, 3})
take_color({ 255, 0, 0, 255 })
```

## Casting
Primitive casts use `@T(expr)` where `T` is a primitive type.
`@string(expr)` is the only supported string conversion and accepts numeric or `bool` values.

Example:

```simple
x : i32 = @i32(3.14)
```

Examples:

```simple
a : f64 = @f64(10)
b : i32 = @i32(2.5)
c : u32 = @u32(42)
d : f32 = @f32(1)
e : char = @char(65)
f : string = @string(123)
g : string = @string(false)
```

## Format Expressions
String format expressions use `{}` placeholders and evaluate to a `string`.

Syntax:
- `"format {}", a, b, ...`

Rules:
- The format string must be a string literal.
- Placeholder count must match the number of values.
- Values must be numeric, `bool`, or `string`.
- Format expressions are valid in variable declarations:
  - `name (:|::) string = "{}", value...`

Example:

```simple
name : string = "Sam"
score : i32 = 100
line : string = "name={} score={}", name, score
```

Variable declaration examples:

```simple
s1 : string = "hp={}", hp
s2 :: string = "x={} y={}", x, y
s3 : string = "alive={}", alive
s4 : string = "name={} score={}", name, score
s5 :: string = "{}", value
```

## Pointers
Pointer syntax follows C style and respects mutability.

- `&variable` yields the address of `variable`.
- `T*` is a pointer type.
- `variable->member` accesses a member through a pointer (C-style).

Mutability rule:
- Pointers to immutable data cannot be used to mutate the pointed-to value.

Examples:

```simple
Node :: Artifact { value : i32 next : Node* }
value : i32 = 1
head : Node = { 0, &head }
ptr : i32* = &value
node_ptr : Node* = &head
```

## Arrays and Lists

### Arrays (Static)
Syntax:

```simple
T{N} = {N elements}
```

Rules:
- The literal element count must match the declared static size `N`.
- Mismatched element counts are compile errors.

Examples:

```simple
nums : i32{3} = {1, 2, 3}
flags : bool{2} = {true, false}
letters : char{4} = {'a', 'b', 'c', 'd'}
coords : f32{2} = {0.0, 1.0}
ids : i64{1} = {99}
```

### Lists (Dynamic)
Syntax:

```simple
T[] = []
```

List methods:
`list.len()`, `list.push(value)`, `list.pop()`, `list.insert(index, value)`,
`list.remove(index)`, `list.clear()`.

Examples:

```simple
items : i32[] = []
items.push(1)
names : string[] = ["a", "b"]
flags : bool[] = [true, false]
points : f32[] = [1.0, 2.0, 3.0]
```

## Artifacts
Artifacts are user-defined types with fields and methods.

Declaration:

```simple
TypeName<T, U, V> :: Artifact { ... }
```

Fields:
```simple
field : Type
field :: Type
```

Field examples:
```simple
hp : i32
name :: string
pos : Vector2
alive : bool
speed :: f32
```

Methods:
```simple
update :: void (param : Type) { block }
update : void (param : Type) { block }
```

Method examples:
```simple
reset :: void () { }
move : void (dx : f32, dy : f32) { }
damage : void (amount : i32) { }
is_dead :: bool () { return false }
alive :: bool () { return true }
```

Inside artifact methods, access members via `self`.

Example:

```simple
Player :: Artifact {
  hp : i32

  damage :: void (amount : i32) {
    self.hp -= amount
  }
}
```

More artifact examples:

```simple
Point :: Artifact { x : f32 y : f32 }
Color :: Artifact { r : u8 g : u8 b : u8 a : u8 }
Rect :: Artifact { x : f32 y : f32 w : f32 h : f32 }
Timer :: Artifact { t : f64 running : bool }
Name :: Artifact { first : string last : string }
```

### Artifact Initialization
Positional and named initialization are supported.

```simple
p1 : Player = { 100 }
p2 : Player = { .hp = 100 }
```

Initialization examples:

```simple
p3 : Point = { 1.0, 2.0 }
p4 : Color = { .r = 255, .g = 0, .b = 0, .a = 255 }
p5 : Rect = { 0.0, 0.0, 10.0, 20.0 }
p6 : Timer = { .t = 0.0, .running = false }
p7 : Name = { "Ada", "Lovelace" }
```

Rules:
- Fields may define default values.
- If a field has no default, it must be provided in initialization.
- Missing required fields are compile errors.

Default syntax is the same as variables:
```simple
field : Type = expr
field :: Type = expr
```

### Method Call Style
```simple
p : Player = { .hp = 100 }
p.damage(10)
```

Method call examples:

```simple
p : Player = { .hp = 100 }
p.damage(10)
p.reset()
p.alive()
p.is_dead()
p.damage(1)
```

## Modules
Modules are global namespaces (not reproducible types).

Declaration:

```simple
ModuleName :: Module { ... }
```

Members:
```simple
field : Type
field :: Type
update :: void (params...) { block }
update : void (params...) { block }
```

Defaults:
- Module fields may define default values.
- If a module field has no default, it must be initialized explicitly.

Default syntax is the same as variables:
```simple
field : Type = expr
field :: Type = expr
```

Example:

```simple
Config :: Module {
  MAX_PLAYERS :: i32 = 16
}

count : i32 = Config.MAX_PLAYERS
```

More module examples:

```simple
Maths :: Module {
  PI :: f64 = 3.1415926
}
Limits :: Module {
  Max :: i32 = 100
  Min :: i32 = 0
}
Flags :: Module {
  Debug :: bool = true
}
Version :: Module {
  Major :: i32 = 1
  Minor :: i32 = 0
}
Window :: Module {
  Width :: i32 = 1280
  Height :: i32 = 720
}
```

## Enums (Scoped)
Enums are scoped and strongly typed.

```simple
Status :: Enum { Idle = 0, Running = 1, Failed = 2 }
s : Status = Status.Running
```

Members are numbered explicitly, starting at `0` by convention.

Enum examples:

```simple
Mode :: Enum { Off = 0, On = 1 }
Dir :: Enum { Left = 0, Right = 1, Up = 2, Down = 3 }
State :: Enum { Start = 0, Play = 1, End = 2 }
Level :: Enum { Low = 0, Medium = 1, High = 2 }
Result :: Enum { Ok = 0, Err = 1 }
```

## Control Flow

### If / Else If / Else
```simple
if (cond) { block }
else if (cond) { block }
else { block }
```

Examples:

```simple
if (x > 0) { y = 1 } else { y = 0 }
if (ready) { start() }
if (a == b) { ok = true } else { ok = false }
if (hp <= 0) { dead = true } else { dead = false }
if (flag) { count += 1 } else if (alt) { count += 2 } else { count += 3 }
```

### Chain Form
```simple
|> (hp <= 0) { state = 0 }
|> (hp < 50) { state = 1 }
|> default { state = 2 }
```

Examples:

```simple
|> (score >= 100) { rank = 3 }
|> (score >= 50) { rank = 2 }
|> default { rank = 1 }

|> (x < 0) { sign = -1 }
|> (x == 0) { sign = 0 }
|> default { sign = 1 }

|> (mode == 0) { speed = 1 }
|> (mode == 1) { speed = 2 }
|> default { speed = 3 }

|> (hp > 0) { alive = true }
|> default { alive = false }

|> (t < 0.0) { t = 0.0 }
|> default { t = t }
```

### While
Parentheses are required.

```simple
while (bool_condition) { block }
```

Examples:

```simple
while (i < 10) { i += 1 }
while (alive) { tick() }
while (count > 0) { count -= 1 }
while (x != y) { x += 1 }
while (sum < 100) { sum += 5 }
```

### For
Normal form:

```simple
for (i; i < number; expr) { block }
```

Rules:
- If the initializer is just `i`, it implies `i : i32 = 0`.
- Otherwise, use an explicit initializer such as `i : i32 = 0` or `i : i32 = 5`.
- Standard C-style `for` loop conventions apply: initializer; condition; step.

Examples:

```simple
for (i; i < 10; i += 1) { sum += i }
for (i : i32 = 0; i < 5; i += 1) { total += i }
for (i : i32 = 5; i >= 0; i -= 1) { countdown = i }
for (i; i < items.len(); i += 1) { items[i] = 0 }
for (i : i32 = 1; i < 10; i += 2) { odds += i }
```

## Switch

### Assigning Switch
If a switch is used in an assignment, all branches must return a value of the
assigned type.

```simple
result : i32 = switch (expr) {
  cond expr => return expr
  cond expr => { block_returning_i32 }
  default => return 0
}
```

Examples:

```simple
state : i32 = switch (mode) {
  mode == 0 => return 0
  mode == 1 => return 1
  default => return 2
}
value : i32 = switch (x) {
  x < 0 => return -1
  x == 0 => return 0
  default => return 1
}
score : i32 = switch (rank) {
  rank == 1 => return 10
  rank == 2 => return 20
  default => return 30
}
flag : i32 = switch (ok) {
  ok == true => return 1
  default => return 0
}
size : i32 = switch (count) {
  count < 5 => return 4
  default => return 8
}
```

### Expression Switch
```simple
switch (expr) {
  cond expr => expr
  cond expr => { block_returning_expr }
  default => expr
}
```

Examples:

```simple
switch (x) {
  x < 0 => -1
  x == 0 => 0
  default => 1
}
switch (mode) {
  mode == 0 => "idle"
  mode == 1 => "run"
  default => "stop"
}
switch (flag) {
  flag == true => 1
  default => 0
}
switch (score) {
  score >= 100 => 3
  score >= 50 => 2
  default => 1
}
switch (t) {
  t < 0.0 => 0.0
  default => t
}
```

## Keywords
`break`, `skip`, and `return` are supported to exit early.

## Operator Precedence and Associativity
From highest to lowest precedence:
1. Postfix: `expr++`, `expr--`, call `()`, index `[]`, member `.`
2. Prefix: `!`, `-`, `++`, `--`, cast `@T(...)`
3. Multiplicative: `*`, `/`, `%`
4. Additive: `+`, `-`
5. Shift: `<<`, `>>`
6. Relational: `<`, `<=`, `>`, `>=`
7. Equality: `==`, `!=`
8. Bitwise AND: `&`
9. Bitwise XOR: `^`
10. Bitwise OR: `|`
11. Logical AND: `&&`
12. Logical OR: `||`
13. Assignment (right-associative): `=`, `+=`, `-=`, `*=`, `/=`, `%=`, `&=`, `|=`, `^=`, `<<=`, `>>=`

All binary operators are left-associative unless otherwise specified.

Return rules:
- `return` with a value is required when returning a value.
- `return` is not allowed to return a value from `void` procedures.

## Imports

### Reserved Library Imports
Reserved library imports are compiler/runtime-defined.
See `Docs/StdLib.md`.

Examples:

```simple
import System.io
import System.dl as DL
import System.time
import FS
import Log
```

### File/Path Imports
Imports are resolved relative to the current working directory.

Resolution rules:
- Build a list of available files in the CWD.
- Import names are matched within that directory only.
- `"name"` auto-appends `.simple` and resolves to `name.simple`.
- If both `name` and `name.simple` exist, the import is a conflict and is an error.
- If the resolved file is not in the CWD, the import fails.
Direct path imports are allowed and bypass the CWD name lookup (e.g., `./path/name.simple`).

Examples:

```simple
import "name"
import "name.simple"
import "./path/name.simple"
import "./assets/ui.simple"
import "../shared/math.simple"
```

## Extern + DLL Interop
`extern` declarations define typed signatures used by `DL` dynamic loading.

ABI shapes:
- Scalars: numeric, bool, char.
- Pointers: `T*`.
- Enums.
- Artifacts by value (struct layout).

Constraints:
- Recursive artifact structs are rejected for `extern` ABI.
- Artifacts in `extern` signatures are marshaled by value using their field layout.
- If an artifact contains nested artifact fields, ABI marshalling automatically flattens nested fields at the `extern` boundary.
- Flattening is boundary-only:
  - user-visible artifact definitions stay unchanged in language semantics.
  - generated ABI shape is used only for extern call/return marshalling.
- Flattening is recursive and preserves source field order.
- Use pointers for recursive or self-referential structures.
- Artifact methods are ignored for ABI layout; only fields are used.

Examples:

```simple
extern ffi.SetWindowIcon: void (image: i64)
extern ffi.SetWindowTitle: void (title: string)
extern ffi.SetWindowPosition: void (x: i32, y: i32)
```

```simple
lib:: i64 = dl.open("./blobs/libraylib.so", ffi)
```

Artifact by value:
```simple
Color :: Artifact {
  r : u8
  g : u8
  b : u8
  a : u8
}
extern ffi.simple_color_sum: i32 (color: Color)
```

Nested artifact flattening at extern boundary:
```simple
Texture :: Artifact {
  id : u32
  width : i32
  height : i32
}

RenderTexture :: Artifact {
  id : u32
  texture : Texture
}

extern ffi.use_rt: void (rt: RenderTexture)
```

Pointer for recursive structures:
```simple
Node :: Artifact {
  value : i32
  next : Node*
}
extern ffi.walk: i32 (head: Node*)
```

Enum in extern signature:
```simple
Mode :: Enum { Off = 0, On = 1 }
extern ffi.set_mode: void (mode: Mode)
```


## Diagnostics Contract
- Type mismatches are compile errors.
- Invalid mutability writes are compile errors.
- Unsupported syntax/constructs fail explicitly.

## Source Ownership
- Lexer: `Lang/src/lang_lexer.cpp`
- Parser: `Lang/src/lang_parser.cpp`
- Validator: `Lang/src/lang_validate.cpp`
- SIR emission: `Lang/src/lang_sir.cpp`

## Grammar (EBNF)
```ebnf
program        = { decl | stmt } ;

decl           = import_decl
               | extern_decl
               | artifact_decl
               | module_decl
               | enum_decl
               | proc_decl
               | var_decl ;

import_decl    = "import" (string | path) [ "as" ident ] ;
extern_decl    = "extern" ident [ "." ident ] ":" type "(" [ params ] ")" ;

artifact_decl  = ident [ generics ] "::" "Artifact" "{" { artifact_member } "}" ;
module_decl    = ident "::" "Module" "{" { module_member } "}" ;
enum_decl      = ident "::" "Enum" "{" { enum_member [ "," ] } "}" ;

proc_decl      = ident [ generics ] (":" | "::") type "(" [ params ] ")" block ;
var_decl       = ident (":" | "::") type [ "=" expr ] ;

artifact_member = (field_decl | method_decl) ;
module_member   = (field_decl | method_decl) ;
field_decl      = ident (":" | "::") type [ "=" expr ] ;
method_decl     = ident (":" | "::") type "(" [ params ] ")" block ;

params         = param { "," param } ;
param          = ident (":" | "::") type ;

type           = proc_type | base_type ;
proc_type      = "fn" [ generics ] type "(" [ params ] ")" ;
base_type      = ident { "*" } [ type_dims ] ;
type_dims      = { list_dim | array_dim } ;
list_dim       = "[" "]" ;
array_dim      = "{" [ integer ] "}" ;

generics       = "<" ident { "," ident } ">" ;

stmt           = return_stmt
               | if_chain
               | if_stmt
               | while_stmt
               | for_stmt
               | break_stmt
               | skip_stmt
               | var_decl
               | assign_stmt
               | expr_stmt ;

return_stmt    = "return" [ expr ] ;
if_chain       = "|>" "(" expr ")" block { "|>" "(" expr ")" block } [ "|>" "default" block ] ;
if_stmt        = "if" "(" expr ")" block { "else" "if" "(" expr ")" block } [ "else" block ] ;
while_stmt     = "while" "(" expr ")" block ;
for_stmt       = "for" "(" for_init ";" expr ";" expr ")" block ;
for_init       = ident
               | ident (":" | "::") type "=" expr
               | expr ;
break_stmt     = "break" ;
skip_stmt      = "skip" ;
assign_stmt    = expr assign_op expr ;
expr_stmt      = expr ;

block          = "{" stmt { stmt } "}" ;

expr           = assignment ;
assignment     = logic_or [ assign_op assignment ] ;
logic_or       = logic_and { "||" logic_and } ;
logic_and      = bit_or { "&&" bit_or } ;
bit_or         = bit_xor { "|" bit_xor } ;
bit_xor        = bit_and { "^" bit_and } ;
bit_and        = equality { "&" equality } ;
equality       = relation { ("==" | "!=") relation } ;
relation       = shift { ("<" | "<=" | ">" | ">=") shift } ;
shift          = add { ("<<" | ">>") add } ;
add            = mul { ("+" | "-") mul } ;
mul            = unary { ("*" | "/" | "%") unary } ;
unary          = ( "!" | "-" | "++" | "--" | "@" type ) unary | postfix ;
postfix        = primary { call | index | member | post_incdec } ;
call           = "(" [ args ] ")" ;
index          = "[" expr "]" ;
member         = "." ident | "->" ident ;
post_incdec    = "++" | "--" ;

primary        = literal
               | ident
               | "self"
               | "(" expr ")"
               | list_literal
               | array_literal
               | artifact_literal ;

list_literal   = "[" [ expr { "," expr } ] "]" ;
array_literal  = "{" [ expr { "," expr } ] "}" ;
/* list_literal is only valid when the contextual type is T[].
   array_literal is only valid when the contextual type is T{N} or T{}.
   for T{N}, element count must match N. */
artifact_literal = "{" [ artifact_fields ] "}" ;
artifact_fields  = artifact_named | artifact_positional ;
artifact_named   = artifact_named_field { "," artifact_named_field } ;
artifact_named_field = ("." ident "=" expr) | (ident ":" expr) ;
artifact_positional = expr { "," expr } ;

args           = expr { "," expr } ;

literal        = integer | float | string | char | "true" | "false" ;
assign_op      = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "^=" | "<<=" | ">>=" ;
```
