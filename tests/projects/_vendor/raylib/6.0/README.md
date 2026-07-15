# raylib 6.0 test binaries

Official runtime libraries cached for graphics conformance projects. These are
test assets, not public Simple runtime dependencies.

| Target | Runtime |
|---|---|
| Linux x86-64 | `linux-x64/libraylib.so.6.0.0` |
| macOS universal (x86-64/arm64) | `macos-universal/libraylib.6.0.0.dylib` |
| Windows x86-64 (MSVC) | `windows-x64/raylib.dll` |

Source release: <https://github.com/raysan5/raylib/releases/tag/6.0>

## Download archive hashes

```text
b64ba618a19e7da9e9c0e09bb398ecfd477a77d2d7231901bafc8739d27c08d2  raylib-6.0_linux_amd64.tar.gz
6ae5947fbd36aee4c280e3a2b3e1893316c433e292bda6e94e0f2b037498ad70  raylib-6.0_macos.tar.gz
c93c7dc74576e00e3ee57fa2bd5fd109fbfc5aca87e12046dd7ec2c2268b3f78  raylib-6.0_win64_msvc16.zip
```

## Extracted runtime hashes

```text
1041653dd5c1cb8c67494fa398a296520d3ffec20cf1bf71b1eaa4b96ac61aee  linux-x64/libraylib.so.6.0.0
7e08b3372babd415f36f23a6852cb5bfce8b491c4f6c18756718142f274f5c66  macos-universal/libraylib.6.0.0.dylib
c62606798c3f736b479db7721aed884102060541b743fd81be3e687ac6de3e67  windows-x64/raylib.dll
```

The license is retained in `LICENSE`. Graphics tests must select exactly one
platform path and must not copy these binaries into production packages.
