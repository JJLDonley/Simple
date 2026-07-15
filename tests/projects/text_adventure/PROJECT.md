# Text-Based Adventure conformance project

- **Timeline gate:** lambdas and lexical closures
- **Status:** active
- **Entrypoint:** `main.simple`
- **Expected exit:** `0`

## Feature matrix

- quoted multi-module imports and namespaces;
- enum values in artifact fields, comparisons, and closure arguments;
- nested artifacts and generic lists;
- `T?` item lookup with exhaustive patterns;
- `Result<T,E>` movement outcomes, enum errors, and postfix propagation;
- generic helper specialization;
- returned, nested, sibling-shared, escaping, and collection-stored closures;
- mutable captured command counters and captured artifact state;
- file input through `Standard.FS`;
- loops, branch mutation, indexing, and deterministic final-state validation;
- interpreter execution and explicit LLVM fallback boundaries for captured closures.

## Deterministic scenario

An exhaustive optional pattern first verifies the key lookup. The command fixture
then moves from Atrium to Library, takes the key, moves to Vault,
and attempts a blocked westward move. The project verifies room, movement count,
score, inventory, transcript, error count, and shared closure execution count.

Automated commands:

```bash
svm run --interpreter tests/projects/text_adventure/main.simple
svm run tests/projects/text_adventure/main.simple
```

Both modes must exit `0` from the same source and fixture.
