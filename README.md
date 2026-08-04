# axllm-c

A tiny C ABI facade for Ax's generated C++ package.

`axllm-c` lets Zig, C, and other C-FFI languages call Ax without binding to C++ directly. The implementation is still Ax's generated C++ backend; this repo only adds the boring C boundary and release automation.

## Use it

Download a release asset from:

```text
https://github.com/SamuelLHuber/axllm-c/releases
```

Or use the checked-in generated package under `dist/`.

The C header is:

```text
dist/axllm/axllm_c.h
```

Current C ABI surface is intentionally small:

```c
const char* axllm_c_version(void);

axllm_c_status axllm_c_schema_for_signature(
  const char* signature,
  const char* section,
  char** out_json,
  axllm_c_error** out_error
);

void axllm_c_string_free(char* value);
const char* axllm_c_error_message(const axllm_c_error* error);
void axllm_c_error_free(axllm_c_error* error);
```

Returned strings must be freed with `axllm_c_string_free`. Errors must be freed with `axllm_c_error_free`.

## Zig hello world

```zig
const std = @import("std");

const ax = @cImport({
    @cInclude("axllm/axllm_c.h");
});

pub fn main() !void {
    var json: [*c]u8 = null;
    var err: ?*ax.axllm_c_error = null;

    const status = ax.axllm_c_schema_for_signature(
        "question:string -> answer:string",
        "outputs",
        &json,
        &err,
    );
    defer if (err) |e| ax.axllm_c_error_free(e);

    if (status != ax.AXLLM_C_OK) return error.AxllmError;
    defer ax.axllm_c_string_free(json);

    std.debug.print("{s}\n", .{std.mem.span(json)});
}
```

Run the included smoke:

```bash
./scripts/install-zig.sh # optional when zig is already on PATH
zig run examples/zig/hello.zig -I dist dist/axllm/axllm_c.cpp dist/axllm/axllm.cpp dist/axllm/mcp.cpp -lc++
```

Expected output includes:

```text
hello from Zig + axllm-c: {"type":"object",...
```

## Why not Zig -> C++?

Zig can call C cleanly. C++ interop means name mangling, exceptions, STL types, destructors, compiler ABI differences, and templates. For Ax workloads the extra C wrapper call is noise compared with JSON, HTTP, and LLM latency.

So the shape is:

```text
Zig / C / other FFI
  -> axllm_c.h
  -> tiny C++ wrapper
  -> generated Ax C++ package
```

## Automation

This repo tracks upstream [`ax-llm/ax`](https://github.com/ax-llm/ax).

Daily, GitHub Actions checks the latest Ax release. When a new release appears, it:

1. downloads the upstream release tarball,
2. vendors it into `vendor/ax/`,
3. applies the C ABI overlay from `overlays/ax/`,
4. regenerates Ax packages,
5. copies the generated C++ package to `dist/`,
6. verifies C++, C ABI, license, and Zig smoke tests,
7. commits the update,
8. publishes a GitHub release with a tarball and SHA256.

There is also a weekly Zig check that opens a PR when a new stable Zig release appears. Zig is installed from `ziglang.org` with pinned SHA256 checksums; no third-party Zig setup action is used.

## Local maintenance

```bash
./scripts/update-upstream.sh 23.0.9
./scripts/verify.sh
./scripts/verify-zig-example.sh
```

Release assets are produced with:

```bash
./scripts/package-release.sh 23.0.9
```

## Scope

This is not a full native C backend. It is a small C ABI over the generated C++ package. Add C functions only when a real FFI use case needs them.

## License

Ax is Apache-2.0. This repo preserves upstream license files in `vendor/ax/` and `dist/`. Local automation and overlay code are Apache-2.0 unless stated otherwise.
