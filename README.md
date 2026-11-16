# axllm-c

A C ABI facade for Ax's generated C++ package.

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

The C ABI covers Ax's inference, program, automation and option surface:

| Area | Entry points |
| --- | --- |
| AI clients | `axllm_c_ai_new` (built-in providers via `ai(provider, options)`), `axllm_c_ai_from_callback` (host-implemented client), balancer/router, chat, streaming chat, embed, transcribe, speak, features, models, metrics, options, cost, last-used model |
| Programs | `axllm_c_gen_new`, `axllm_c_agent_new`, `axllm_c_flow_new`; forward, streaming forward, cancellation, run control; examples/demos, asserts, field transforms/processors, stop functions, hooks, instructions, traces, usage, state |
| Agents | signature/actor configuration, tool modules, child agents, state export/restore, discovery, recall, callables |
| Flows | execute/derive/map/branch/while/feedback/extended nodes/parallel/returns, raw steps, Mermaid, plan |
| Tools | `axllm_c_tool_new` backed by a C callback |
| Optimization | `optimize`, GEPA and bootstrap optimizers, apply/evaluate artifacts, playbooks |
| MCP | HTTP, stdio and scripted transports, tools/prompts/resources/tasks, attaching MCP tools to programs |
| Helpers | signature parsing, JSON schema, output validation, prompt rendering, stream folding, global usage/log/cache/formatter hooks |

Conventions (see the comments at the top of `axllm_c.h`):

- Every Ax `Value` crosses the boundary as a UTF-8 JSON string. Options are JSON objects; `NULL` means defaults.
- Fallible calls return `AXLLM_C_OK` / `AXLLM_C_ERR` and set `axllm_c_error**`.
- Returned strings must be freed with `axllm_c_string_free`, errors with `axllm_c_error_free`, handles with their matching `*_free`.
- Callbacks are plain C function pointers with a `void* user` argument. JSON callbacks return `0` on success and write a string allocated with `axllm_c_string_dup`; streaming callbacks return nonzero to continue.
- Built-in providers make HTTP calls through libcurl; build with `-DAXLLM_ENABLE_CURL=1` and link `-lcurl` (the CMake build does this when libcurl is found).

`dist/examples/c_abi_surface.cpp` (written in C, also compiled as C11 by `verify.sh`) exercises the surface end to end with a scripted client.

Not exposed yet: the event runtime, realtime audio sockets and code runtimes.

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

Real inference from Zig (`examples/zig/inference.zig`) against any OpenAI-compatible endpoint:

```bash
OPENAI_API_KEY=... zig run examples/zig/inference.zig -I dist -DAXLLM_ENABLE_CURL=1 \
  dist/axllm/axllm_c.cpp dist/axllm/axllm.cpp dist/axllm/mcp.cpp -lc++ -lcurl
```

`AXLLM_C_BASE_URL` and `AXLLM_C_MODEL` override the endpoint and model; `scripts/verify-zig-example.sh` runs it against a local mock (`scripts/mock-openai.py`).

### Email extraction sample (live)

`examples/zig/email_extraction.zig` is an Ax port of the [DSPy email extraction tutorial](https://dspy.ai/tutorials/email_extraction/): four typed Ax programs (classify → extract entities → summarize → actions) composed from Zig, run against a real model:

```bash
OPENAI_API_KEY=... zig run examples/zig/email_extraction.zig -I dist -DAXLLM_ENABLE_CURL=1 \
  dist/axllm/axllm_c.cpp dist/axllm/axllm.cpp dist/axllm/mcp.cpp -lc++ -lcurl
```

Expected hello output includes:

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

This is not a native C backend. It is a C ABI over the generated C++ package; overlays in `overlays/ax/` are re-applied on every upstream sync.

## License

Ax is Apache-2.0. This repo preserves upstream license files in `vendor/ax/` and `dist/`. Local automation and overlay code are Apache-2.0 unless stated otherwise.
