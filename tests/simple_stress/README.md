# Simple language stress programs

These are executable, end-to-end language programs rather than isolated syntax
fixtures. Each program compiles through CAST, AST, RAST, TAST, SIR, SBC, and the
VM, then validates a known result.

| Program | Primary coverage |
|---|---|
| `gabriel_tak.simple` | Gabriel-style deep recursion and call frames |
| `vector_matrix.simple` | artifacts, 64-bit fields, fixed arrays, floating point |
| `binary_trees.simple` | recursive allocation, artifact lists, GC pressure |
| `spectral_norm.simple` | nested loops, list mutation, `f64` arithmetic |
| `nbody.simple` | arrays of structures, field mutation, numerical stability |
| `base64_lcg.simple` | bit operations, large lists, deterministic generation |
| `knapsack.simple` | dynamic programming and reverse boundary traversal |
| `self_lexer.simple` | file I/O, string indexing, state machines, EOF tracking |
| `generic_composition.simple` | nested specialization, inferred calls, generic lists and methods |
| `module_generic_composition.simple` | namespace-owned generic specialization across collections and artifacts |
| `generic_methods.simple` | inferred/explicit generic methods, generic receivers, indexed receivers |
| `generic_chains.simple` | temporary generic receivers, namespace factories, chained method calls |
| `tagged_layouts.simple` | canonical Option/Result layouts nested through generic artifacts and lists |

Workloads are deliberately bounded so they remain suitable for normal CI while
still reaching runtime paths that small fixtures miss. Increase loop counts and
tree depths locally for profiling; correctness must not depend on timing.
