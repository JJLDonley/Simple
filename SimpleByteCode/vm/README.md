# Simple VM (Prototype)

Build:
```
./build.sh
```

Build (CMake, cross-platform):
```
cmake -S . -B build
cmake --build build --config Release
```

Build (Windows PowerShell):
```
./build.ps1
```

Build (Windows cmd):
```
build.bat
```

Run:
```
./bin/simplevm <module.sbc>
./bin/simplevm <module.sbc> --no-verify
```

Tests:
```
./bin/simplevm_tests
```

Tests (CMake build):
```
./build/bin/simplevm_tests
```

Notes:
- This is a minimal interpreter + loader + verifier skeleton.
- Opcode numeric values are defined in `include/opcode.h` and must match the compiler output.
