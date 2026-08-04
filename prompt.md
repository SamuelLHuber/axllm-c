# Add a C ABI facade for the generated C++ Ax package

- Branch: `cpp-c-abi-facade`
- Status: Draft
- Owner(s): TBD
- Created: 2026-08-04
- Last Updated: 2026-08-04
- Links: TBD

This ExecPlan is a living document. Keep `Progress`, `Surprises & Discoveries`, `Decision Log`, and `Outcomes & Retrospective` current as research, implementation, validation, and review proceed.
When the next milestone is clear, continue to it and update the spec instead of asking for generic next steps.

## Purpose / Big Picture

Expose the generated Ax C++ package through a small, stable C ABI so Zig and other C-FFI languages can use Ax without binding directly to C++ templates, namespaces, exceptions, STL types, or compiler-specific C++ ABI details.

The first useful result is a generated C facade shipped with `packages/cpp` that lets a Zig program call Ax through `@cImport`, parse a signature, generate a JSON schema, and free all returned resources safely. The implementation remains the existing generated C++ backend; this is not a full native C backend.

This matters because Zig can consume C cleanly, while direct Zig-to-C++ interop would be brittle and not meaningfully faster for Ax workloads. A C ABI facade gives near-native call overhead while keeping AxIR semantics Core-owned and reusing the existing C++ conformance surface.

Success means:

- `packages/cpp` includes generated `axllm/axllm_c.h` and `axllm/axllm_c.cpp` exposing a minimal C ABI over the existing C++ API.
- CMake builds and installs the C header/source as part of `axllm::axllm` without breaking existing C++ consumers.
- A small C or Zig-style smoke can call the C ABI to produce a schema JSON containing an `answer` property for `question:string -> answer:string`.
- The initial facade stays intentionally small: signatures/schema and coarse JSON helpers first; no direct full C object model, no direct C++ bindings, and no full `packages/c` target.

## Progress

- [x] (2026-08-04 00:00Z) Research current packaging and generated C++ package shape.
- [x] (2026-08-04 00:00Z) Confirm existing generated package targets are Python, Java, C++, Go, and Rust.
- [x] (2026-08-04 00:00Z) Confirm C++ package currently generates `axllm/axllm.hpp`, `axllm/axllm.cpp`, `axllm/mcp.hpp`, `axllm/mcp.cpp`, `CMakeLists.txt`, examples, conformance, API/capability manifests, and README.
- [ ] (2026-08-04 00:00Z) Add generated C facade templates and wire them into C++ package generation.
- [ ] (2026-08-04 00:00Z) Add CMake install/build integration for the C facade.
- [ ] (2026-08-04 00:00Z) Add a minimal C facade example or smoke test.
- [ ] (2026-08-04 00:00Z) Regenerate packages and run validation.
- [ ] (2026-08-04 00:00Z) Update docs/README/API notes so Zig/C FFI consumers can discover the facade.

## Surprises & Discoveries

- Observation: The repo already has generated native packages under `packages/python`, `packages/java`, `packages/cpp`, `packages/go`, and `packages/rust`.
  Evidence: `scripts/generate-axir-packages.mjs` uses `const targets = ['python', 'java', 'cpp', 'go', 'rust'];`.

- Observation: The C++ package is generated from AxIR, not hand-authored as the source of truth.
  Evidence: `tools/axir/internal/axir/codegen.go` has `EmitCpp(...)`, which writes `CMakeLists.txt`, `axllm/axllm.hpp`, `axllm/axllm.cpp`, examples, manifests, README, and API docs.

- Observation: Existing C++ public API already has suitable primitives for the first C ABI milestone.
  Evidence: `packages/cpp/axllm/axllm.hpp` exposes `axllm::s(...)`, `axllm::to_json_schema(...)`, `axllm::Core::get(...)`, `axllm::Core::json_stringify(...)`, and `axllm::Core::json_parse(...)`.

- Observation: C++ CMake install currently only installs `*.hpp` headers under `axllm/`.
  Evidence: `packages/cpp/CMakeLists.txt` has `install(DIRECTORY axllm/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/axllm FILES_MATCHING PATTERN "*.hpp")`.

## Decision Log

- Decision: Implement a C ABI facade over the generated C++ backend, not a full C backend.
  Rationale: Zig needs a stable C ABI, not native C internals. Reusing generated C++ keeps semantics aligned with existing AxIR C++ conformance and avoids duplicating JSON/value/runtime/provider behavior in C.
  Date/Author: 2026-08-04 / agent

- Decision: Start with coarse JSON/string and opaque-handle boundaries.
  Rationale: This is the smallest useful API for Zig and avoids designing a large C object model. JSON conversion overhead is negligible compared with network/LLM latency.
  Date/Author: 2026-08-04 / agent

- Decision: Keep callbacks/tools/streaming out of the first milestone.
  Rationale: Cross-language callbacks and token-level streaming are where C ABI complexity grows. They should be added only after the basic facade proves useful.
  Date/Author: 2026-08-04 / agent

- Decision: Generate the facade as part of the existing C++ target rather than adding a new AxIR `c` target.
  Rationale: The generated output belongs with `packages/cpp` and can share CMake/package metadata. A standalone C target would need separate package semantics, conformance, and more generated code.
  Date/Author: 2026-08-04 / agent

## Outcomes & Retrospective

- Outcome: TBD after implementation.
  Evidence: TBD.
  Remaining: TBD.

## Context and Orientation

Ax is primarily packaged as the npm package `@ax-llm/ax` from `src/ax`, built with `tsup` into ESM, CommonJS, IIFE/global, and `.d.ts` output. The repo also generates non-TypeScript packages from AxIR under `packages/`.

AxIR is the compiler contract that emits generated packages. The current generated package targets are Python, Java, C++, Go, and Rust. The C++ package is generated into `packages/cpp` and exposes a C++17 library target named `axllm::axllm`.

Relevant repo rules:

- `src/ax/index.ts` is generated; do not edit it manually.
- Generated package changes should be made in AxIR generator code/templates, then regenerated with `npm run axir:generate-packages`.
- Use `tools/axir/skills/axir-language-backend/SKILL.md` guidance when changing generated AxIR language backends.
- Keep semantics Core-owned. Do not reimplement provider, agent, flow, optimizer, or prompt semantics in the C facade.
- Keep generated target code idiomatic but thin. The C facade owns ABI shape, memory ownership, error boundaries, and FFI-safe wrappers only.
- If portable TypeScript behavior under `src/ax/ai`, `src/ax/dsp`, `src/ax/agent`, `src/ax/flow`, or `src/ax/mcp` changes, update AxIR/conformance or add backlog. This spec should not need portable TS changes.
- Node.js must be `>=20`; repo is ES modules only.

Current behavior:

- `packages/cpp` can be consumed by C++ projects through CMake and `#include "axllm/axllm.hpp"`.
- The generated C++ API is not a stable C ABI and is not pleasant for Zig to consume directly.
- `packages/cpp/CMakeLists.txt` builds `add_library(axllm axllm/axllm.cpp axllm/mcp.cpp)` and installs `*.hpp` headers only.
- `tools/axir/internal/axir/codegen.go` function `EmitCpp` decides which files are generated for the C++ package.
- `tools/axir/internal/axir/templates/package/cppCMakeLists.cmake` is the source template for generated CMake.
- `tools/axir/internal/axir/templates/cpp/cppHeader.hpp` and `tools/axir/internal/axir/cpp_core_emit.go` contribute the existing C++ header/source.
- `packages/cpp/examples/signature_schema.cpp` demonstrates the smallest useful behavior: parse a signature and produce a schema.

How the relevant pieces fit together:

- AxIR compiles `ir/axcore/root.axir` into target packages.
- The C++ backend emits C++ `Value`, `Core`, provider/client, program, flow, agent, MCP, runtime, and optimizer wrappers.
- The new C facade should sit beside the generated C++ files in `packages/cpp/axllm/` and call into the existing `axllm` namespace internally.
- Zig should only see the C header and link the existing `axllm` library.

Assumptions:

- The first Zig use case only needs coarse calls and JSON/string boundaries.
- Consumers can link a C++ library even when their application code is Zig.
- Memory returned by the C facade will be freed only with facade-provided free functions.
- The facade can catch C++ exceptions and convert them into `axllm_c_error` objects.
- If Zig requires richer APIs later, add them incrementally behind the same C ABI style instead of exposing C++ types.

## Plan of Work

### Milestone 1: Add the minimal generated C ABI facade

Scope: Add generated `axllm_c.h` and `axllm_c.cpp` to the C++ package. Keep the API intentionally tiny and useful: version, free string, free error, error message, schema-for-signature, JSON parse/stringify sanity helpers if useful.

Files and interfaces:

- `tools/axir/internal/axir/codegen.go`: add `axllm/axllm_c.h` and `axllm/axllm_c.cpp` to `EmitCpp` output.
- `tools/axir/internal/axir/templates/cpp/cppCHeader.h` or equivalent new template: C ABI declarations.
- `tools/axir/internal/axir/templates/cpp/cppCSource.cpp` or equivalent new template: C++ implementation of the C ABI.
- Generated output: `packages/cpp/axllm/axllm_c.h`.
- Generated output: `packages/cpp/axllm/axllm_c.cpp`.

Work:

Define a C header that is safe from C and Zig:

```c
#ifndef AXLLM_C_H
#define AXLLM_C_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum axllm_c_status {
  AXLLM_C_OK = 0,
  AXLLM_C_ERR = 1
} axllm_c_status;

typedef struct axllm_c_error axllm_c_error;

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

#ifdef __cplusplus
}
#endif

#endif
```

Implementation rules:

- `axllm_c_version()` returns a static string and must not require freeing.
- `axllm_c_schema_for_signature(...)` validates non-null `signature` and `out_json`.
- `section` defaults to `"outputs"` when null or empty.
- Implementation should call existing C++ API: `axllm::s(signature)`, `axllm::Core::get(sig, section)`, `axllm::to_json_schema(...)`, and `axllm::Core::json_stringify(...)` or equivalent.
- Return JSON must be allocated with an allocation strategy paired with `axllm_c_string_free`. Prefer `std::malloc`/`std::free` or `new[]`/matching delete, but keep it local and explicit. `malloc/free` is easiest for C callers.
- Catch `std::exception` and unknown exceptions. Convert to `axllm_c_error`.
- Never let C++ exceptions cross the C ABI.
- Never return pointers to temporary `std::string` storage.
- Keep all structs opaque in the header.

Acceptance:

- Run `npm run axir:generate-packages` from repo root and expect generated `packages/cpp/axllm/axllm_c.h` and `packages/cpp/axllm/axllm_c.cpp`.
- Inspect generated header and confirm it contains `extern "C"`, `axllm_c_schema_for_signature`, `axllm_c_string_free`, and opaque `axllm_c_error`.

### Milestone 2: Build and install the C facade through CMake

Scope: Ensure the facade is compiled into `axllm::axllm` and installed for external consumers.

Files and interfaces:

- `tools/axir/internal/axir/templates/package/cppCMakeLists.cmake`: add `axllm/axllm_c.cpp` to the `axllm` library sources and install `*.h` in addition to `*.hpp`.
- Generated output: `packages/cpp/CMakeLists.txt`.

Work:

Update the generated CMake template:

- Change `add_library(axllm axllm/axllm.cpp axllm/mcp.cpp)` to include `axllm/axllm_c.cpp`.
- Keep the public target name `axllm::axllm` unchanged.
- Keep C++ standard at C++17.
- Update install rule to include both C++ and C headers:

```cmake
install(DIRECTORY axllm/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/axllm
  FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"
)
```

Acceptance:

- Run a CMake configure/build of `packages/cpp` with examples/conformance disabled for speed:

```bash
cmake -S packages/cpp -B /tmp/axllm-cpp-build -DAX_BUILD_EXAMPLES=OFF -DAX_BUILD_CONFORMANCE=OFF
cmake --build /tmp/axllm-cpp-build
```

- Expect the `axllm` library to compile with `axllm_c.cpp` included.

### Milestone 3: Add a C facade smoke example

Scope: Add one tiny generated C example that proves C consumers can use the facade. This also approximates what Zig will do through `@cImport`.

Files and interfaces:

- New template: `tools/axir/internal/axir/templates/cpp/cppCSignatureSchemaExample.c` or `.cpp` if CMake C language support should be avoided.
- Generated output: `packages/cpp/examples/c_signature_schema.c` or `packages/cpp/examples/c_signature_schema.cpp`.
- `tools/axir/internal/axir/templates/package/cppCMakeLists.cmake`: add executable when `AX_BUILD_EXAMPLES` is enabled.

Work:

Prefer a real `.c` example if CMake is updated to `LANGUAGES C CXX`. If that is too much churn, use a `.cpp` file that includes only the C header and calls only the C ABI; name it clearly as a C ABI smoke.

Example behavior:

```c
#include "axllm/axllm_c.h"
#include <stdio.h>
#include <string.h>

int main(void) {
  char* json = 0;
  axllm_c_error* err = 0;
  axllm_c_status status = axllm_c_schema_for_signature(
    "question:string -> answer:string",
    "outputs",
    &json,
    &err
  );
  if (status != AXLLM_C_OK) {
    fprintf(stderr, "%s\n", axllm_c_error_message(err));
    axllm_c_error_free(err);
    return 1;
  }
  int ok = strstr(json, "answer") != 0;
  axllm_c_string_free(json);
  if (!ok) return 1;
  puts("cpp-c-abi-signature-schema-ok");
  return 0;
}
```

Acceptance:

- Run CMake build with examples enabled and expect the C ABI example to build.
- Run the example and expect `cpp-c-abi-signature-schema-ok`.

### Milestone 4: Document Zig/C FFI consumption

Scope: Make the feature discoverable without presenting it as a full C backend.

Files and interfaces:

- `tools/axir/internal/axir/codegen.go` or README template source for package README text.
- Generated output: `packages/cpp/README.md`.
- Generated output: `packages/cpp/API.md`, if the API manifest/docs generator should mention the C facade.
- Optional canonical docs: `docs/COMPILER.md` if generated package distribution docs list package shapes.

Work:

Add a short README section under C++ package shape or a new `C ABI facade` heading:

- Explain that `axllm/axllm_c.h` exposes a stable C ABI implemented by the generated C++ package.
- Show C usage and a minimal Zig `@cImport` snippet.
- State memory ownership: strings from Ax must be freed with `axllm_c_string_free`; errors with `axllm_c_error_free`; `axllm_c_version` is static.
- State scope: first facade is coarse JSON/signature/schema, not a full native C backend.

Minimal Zig snippet:

```zig
const ax = @cImport({
    @cInclude("axllm/axllm_c.h");
});
```

Acceptance:

- Generated README mentions `axllm_c.h`, Zig `@cImport`, and memory ownership.
- Docs do not claim a separate `packages/c` target or full C SDK.

### Milestone 5: Validate generated output and repo gates

Scope: Prove the generated package remains fresh and existing C++ functionality still works.

Files and interfaces:

- Generated package files under `packages/cpp`.
- Relevant generator tests under `tools/axir/internal/axir/` if audits require generated markers.

Work:

Run the smallest meaningful checks first, then broader gates:

```bash
npm run axir:generate-packages
cmake -S packages/cpp -B /tmp/axllm-cpp-build -DAX_BUILD_EXAMPLES=ON -DAX_BUILD_CONFORMANCE=OFF
cmake --build /tmp/axllm-cpp-build
/tmp/axllm-cpp-build/c_signature_schema
npm run axir:backlog:validate
npm run test:axir
npm run axir:check-packages
```

If `npm run test:axir` is too slow during iteration, run focused C++ verify first through the repo's AxIR command, then finish with full required gates before review.

Acceptance:

- Generated package check passes or generated diff is intentionally committed.
- C++ package builds.
- C ABI example runs and prints success.
- Existing C++ signature schema example still runs.
- No placeholder-only public API is introduced.

## Interfaces and Dependencies

Local interfaces:

- `axllm_c_status`:
  - Inputs: none.
  - Outputs: `AXLLM_C_OK` for success, `AXLLM_C_ERR` for failure.
  - Failures: none; enum is returned by fallible C ABI functions.

- `axllm_c_error`:
  - Inputs: opaque pointer returned through `axllm_c_error**`.
  - Outputs: message through `axllm_c_error_message`.
  - Failures: `axllm_c_error_message(NULL)` should return a static fallback like `""` or `"unknown axllm error"`; document exact behavior.

- `axllm_c_version(void)`:
  - Inputs: none.
  - Outputs: static null-terminated version string matching generated package version.
  - Failures: none.

- `axllm_c_schema_for_signature(const char* signature, const char* section, char** out_json, axllm_c_error** out_error)`:
  - Inputs: non-null signature string, optional section string, non-null `out_json` pointer, optional `out_error` pointer.
  - Outputs: on success, `*out_json` points to allocated null-terminated JSON. Caller frees with `axllm_c_string_free`.
  - Failures: invalid args, signature parse errors, C++ exceptions. On failure, returns `AXLLM_C_ERR`; if `out_error` is non-null, sets `*out_error` to an allocated error.

- `axllm_c_string_free(char*)`:
  - Inputs: pointer returned by the C facade, or null.
  - Outputs: none.
  - Failures: none for null. Passing non-facade memory is undefined behavior.

External dependencies:

- C++ standard library:
  - Version/source checked: existing C++ package requires C++17.
  - Expected behavior: internal implementation can use `std::string`, exceptions, and existing Ax C++ API.
  - Failure handling: no C++ exceptions cross C ABI.

- CMake:
  - Version/source checked: `packages/cpp/CMakeLists.txt` currently requires CMake 3.16.
  - Expected behavior: build `axllm_c.cpp` into `axllm::axllm`, install `.h` and `.hpp` headers.
  - Failure handling: configure/build failures block acceptance.

- Zig:
  - Version/source checked: no Zig dependency is required for repo validation unless a Zig smoke is explicitly added.
  - Expected behavior: downstream Zig can `@cImport` the generated C header.
  - Failure handling: document C usage as the validated contract; add Zig smoke later only if the repo agrees to require Zig in CI.

## Concrete Steps

From the branch checkout's repository root (`/Users/samuel/git/playground/ax`):

```bash
rg "func EmitCpp" tools/axir/internal/axir/codegen.go
rg "cppCMakeLists" tools/axir/internal/axir
rg "signature_schema" packages/cpp tools/axir/internal/axir/templates
```

Add C facade templates and wire them into `EmitCpp`.

Regenerate packages:

```bash
npm run axir:generate-packages
```

Build C++ package:

```bash
cmake -S packages/cpp -B /tmp/axllm-cpp-build -DAX_BUILD_EXAMPLES=ON -DAX_BUILD_CONFORMANCE=OFF
cmake --build /tmp/axllm-cpp-build
```

Run examples:

```bash
/tmp/axllm-cpp-build/signature_schema
/tmp/axllm-cpp-build/c_signature_schema
```

Run broader validation:

```bash
npm run axir:backlog:validate
npm run axir:check-packages
npm run test:axir
```

Expected evidence:

```text
cpp-signature-schema-ok
cpp-c-abi-signature-schema-ok
```

## Validation and Acceptance

Automated validation:

- `npm run axir:generate-packages`: expect generated `packages/cpp` includes the C facade files and no unintended target churn outside expected generated output.
- `cmake -S packages/cpp -B /tmp/axllm-cpp-build -DAX_BUILD_EXAMPLES=ON -DAX_BUILD_CONFORMANCE=OFF`: expect configure success.
- `cmake --build /tmp/axllm-cpp-build`: expect build success.
- `/tmp/axllm-cpp-build/c_signature_schema`: expect `cpp-c-abi-signature-schema-ok`.
- `/tmp/axllm-cpp-build/signature_schema`: expect `cpp-signature-schema-ok`.
- `npm run axir:check-packages`: expect generated packages are fresh.
- `npm run test:axir`: expect AxIR verification still passes.

Manual or runtime validation:

- Inspect `packages/cpp/axllm/axllm_c.h` and confirm it contains only C-compatible types and functions.
- Confirm no C++ namespace, class, template, `std::`, reference, or exception type appears in the public C header.
- Confirm README memory ownership docs match implementation.

Regression checks:

- Existing C++ consumers still include `axllm/axllm.hpp` and link `axllm::axllm` unchanged.
- Existing C++ examples still compile.
- Existing optional CMake behavior for CURL, OpenSSL, Boost.Process, realtime, QuickJS remains unchanged except for compiling the additional C facade source.

## Idempotence and Recovery

- Re-running `npm run axir:generate-packages` is safe because generated package output is deterministic.
- Re-running CMake with the same `/tmp/axllm-cpp-build` directory is safe for normal iteration; if configure state becomes stale, delete `/tmp/axllm-cpp-build` and rerun.
- If generated package output changes unexpectedly across all languages, stop and inspect generator/template changes before committing.
- If the C facade build fails because CMake language support changed for a `.c` example, either enable `LANGUAGES C CXX` deliberately or switch the smoke to `.cpp` while still using only the C ABI.
- Backout plan: remove `axllm_c.h/.cpp` from `EmitCpp`, revert CMake template changes, regenerate packages, and rerun package freshness checks.

## Rollout and Operations

- Feature flags/config/env vars: none.
- Migration/backfill steps: none.
- Monitoring/alerts/logs: none; this is package surface area.
- PR/branch workflow: commit generator/template changes and regenerated `packages/cpp` output together. Do not hand-edit generated package files without changing generator templates.
- Release: the C facade ships with the next generated C++ package release. README must state this is a C ABI facade, not a standalone C package.

## Risks and Open Questions

- Risk: Public C ABI grows into a second full API surface that is hard to maintain.
  Mitigation: Start with a tiny coarse API and add functions only when there is a real consumer need.

- Risk: Memory ownership bugs across C/Zig boundary.
  Mitigation: Use explicit facade alloc/free functions, test success and error paths, and document ownership in the header and README.

- Risk: C++ exceptions leak through `extern "C"`.
  Mitigation: Wrap every fallible facade function in `try/catch (...)` and convert to `axllm_c_error`.

- Risk: JSON string boundary becomes expensive for large or high-frequency calls.
  Mitigation: Accept for v1. Add opaque handles or streaming/pull APIs later only if profiling proves a bottleneck.

- Risk: CMake `.c` example forces C language enablement and causes unexpected build churn.
  Mitigation: Prefer `.cpp` smoke using only C ABI if enabling C language causes issues. The public header remains C-compatible either way.

- Open question: Should the first API include only `axllm_c_schema_for_signature`, or also generic `axllm_c_json_parse_pretty`/`axllm_c_json_stable_stringify` helpers?
  Owner/next step: Implement only schema first unless a concrete Zig smoke needs extra helpers.

- Open question: Should `axllm_c_error` expose category/type/status/code/retryable from `AxError`?
  Owner/next step: Start with message only. Add structured error accessors in a later milestone if consumers need them.

- Open question: Should a Zig smoke be committed under `src/examples/zig`?
  Owner/next step: No for v1 unless the repo is ready to add Zig as a validated example language. Document Zig snippet in README instead.

## Artifacts and Notes

Current C++ signature schema example:

```cpp
#include "axllm/axllm.hpp"
#include <iostream>

int main() {
  axllm::Value sig = axllm::s("question:string -> answer:string");
  axllm::Value schema = axllm::to_json_schema(axllm::Core::get(sig, "outputs"));
  if (!axllm::Core::truthy(axllm::Core::get(axllm::Core::get(schema, "properties"), "answer"))) return 1;
  std::cout << "cpp-signature-schema-ok\n";
}
```

Generated C++ file list source:

```go
func EmitCpp(model AxRuntimeModel, outDir string) error {
    files := map[string]string{
        "CMakeLists.txt": renderPackageTemplate(cppCMakeLists, version),
        "axllm/axllm.hpp": header,
        "axllm/axllm.cpp": core,
        "axllm/mcp.hpp": cppMCPHeader,
        "axllm/mcp.cpp": cppMCPSource,
        ...
    }
}
```

## Revision Notes

- 2026-08-04: Initial spec for generated C ABI facade over the existing generated C++ Ax package, aimed at Zig/C FFI consumers.
