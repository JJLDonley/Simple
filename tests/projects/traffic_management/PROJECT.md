# Traffic Management conformance project

- **Timeline gate:** `Promise<T>`, `async`, prefix `await`, and cancellation
- **Status:** active
- **Entrypoint:** `main.simple`
- **Expected exit:** `0`

The deterministic controller models three intersections over two signal cycles.
It combines quoted modules, enum state transitions, artifacts, lists of typed
promises, Result propagation after `await`, callback closures, sibling-shared
mutable capture, file input, loops, and aggregate final-state checks.

The simulation creates every cycle's pending operations before awaiting them.
Interpreter and default-JIT execution use the same source; LLVM rejects async
Promise opcodes before execution and the VM interpreter scheduler remains the
semantic authority.

```bash
svm run --interpreter tests/projects/traffic_management/main.simple
svm run tests/projects/traffic_management/main.simple
```
