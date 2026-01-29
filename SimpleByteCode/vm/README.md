# Simple VM (Prototype)

Build:
```
./build.sh
```

Run:
```
./bin/simplevm <module.sbc>
```

Tests:
```
./bin/simplevm_tests
```

Notes:
- This is a minimal interpreter + loader + verifier skeleton.
- Opcode numeric values are defined in `include/opcode.h` and must match the compiler output.
