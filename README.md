# axllm-c

C ABI facade packaging for the generated Ax C++ package.

This repository tracks upstream [`ax-llm/ax`](https://github.com/ax-llm/ax) releases and keeps a generated C ABI layer on top of `packages/cpp` so C-FFI languages such as Zig can consume Ax without binding to C++ directly.

## Shape

- Upstream source is vendored under `vendor/ax/` by automation.
- C ABI generator patches live under `overlays/ax/`.
- Generated/released C++ package output lives under `dist/` after automation runs.
- The C ABI header is `dist/axllm/axllm_c.h`.

The implementation remains the generated C++ Ax backend. This is not a separate native C backend.

## Why

Zig can call C cleanly:

```zig
const ax = @cImport({
    @cInclude("axllm/axllm_c.h");
});
```

Run the tiny Zig smoke:

```bash
./scripts/install-zig.sh # optional when zig is already on PATH
zig run examples/zig/hello.zig -I dist dist/axllm/axllm_c.cpp dist/axllm/axllm.cpp dist/axllm/mcp.cpp -lc++
```

It calls `axllm_c_schema_for_signature("question:string -> answer:string", ...)` and prints the generated schema.

Direct Zig to C++ buys almost no speed for LLM workloads and adds C++ ABI/toolchain pain. The C ABI keeps the boundary boring.

## Updating

The daily GitHub Action checks the latest upstream Ax release. If it differs from `.ax-upstream-version`, it:

1. downloads the upstream release tarball,
2. vendors it into `vendor/ax/`,
3. applies `overlays/ax/`,
4. runs Ax package generation,
5. copies `vendor/ax/packages/cpp` to `dist/`,
6. builds a CMake smoke,
7. commits and pushes the update.

Manual run:

```bash
./scripts/update-upstream.sh 23.0.9
./scripts/verify.sh
```

## License

Ax is Apache-2.0. This repository preserves upstream license files in the vendored source and generated distribution. Local automation/overlay code is also Apache-2.0 unless stated otherwise.
