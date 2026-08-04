#!/usr/bin/env node
import { readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';

const axRoot = process.argv[2];
if (!axRoot) throw new Error('usage: patch-ax-codegen.mjs <vendor/ax>');

patchEmbeds(path.join(axRoot, 'tools/axir/internal/axir/templates_embed.go'));
patchCodegen(path.join(axRoot, 'tools/axir/internal/axir/codegen.go'));
patchCmake(path.join(axRoot, 'tools/axir/internal/axir/templates/package/cppCMakeLists.cmake'));

function patchEmbeds(file) {
  let s = readFileSync(file, 'utf8');
  s = replaceOnce(s,
    '//go:embed templates/cpp/cppConformance.cpp\nvar cppConformance string\n\n//go:embed templates/cpp/cppHeader.hpp',
    '//go:embed templates/cpp/cppConformance.cpp\nvar cppConformance string\n\n//go:embed templates/cpp/cppCHeader.h\nvar cppCHeader string\n\n//go:embed templates/cpp/cppCSignatureSchemaExample.cpp\nvar cppCSignatureSchemaExample string\n\n//go:embed templates/cpp/cppCSource.cpp\nvar cppCSource string\n\n//go:embed templates/cpp/cppHeader.hpp'
  );
  writeFileSync(file, s);
}

function patchCodegen(file) {
  let s = readFileSync(file, 'utf8');
  s = replaceOnce(s,
    '"axllm/axllm.cpp":                                       core,\n\t\t"axllm/mcp.hpp":',
    '"axllm/axllm.cpp":                                       core,\n\t\t"axllm/axllm_c.h":                                       renderPackageTemplate(cppCHeader, version),\n\t\t"axllm/axllm_c.cpp":                                     renderPackageTemplate(cppCSource, version),\n\t\t"axllm/mcp.hpp":'
  );
  s = replaceOnce(s,
    '"examples/signature_schema.cpp":                         cppSignatureSchemaExample,\n\t\t"examples/axgen_scripted_client_tool.cpp":',
    '"examples/signature_schema.cpp":                         cppSignatureSchemaExample,\n\t\t"examples/c_signature_schema.cpp":                       cppCSignatureSchemaExample,\n\t\t"examples/axgen_scripted_client_tool.cpp":'
  );
  writeFileSync(file, s);
}

function patchCmake(file) {
  let s = readFileSync(file, 'utf8');
  s = replaceOnce(s,
    'add_library(axllm axllm/axllm.cpp axllm/mcp.cpp)',
    'add_library(axllm axllm/axllm.cpp axllm/axllm_c.cpp axllm/mcp.cpp)'
  );
  s = replaceOnce(s,
    '    signature_schema\n    axgen_scripted_client_tool',
    '    signature_schema\n    c_signature_schema\n    axgen_scripted_client_tool'
  );
  s = replaceOnce(s,
    '  FILES_MATCHING PATTERN "*.hpp"\n)',
    '  FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"\n)'
  );
  writeFileSync(file, s);
}

function replaceOnce(s, from, to) {
  if (s.includes(to)) return s;
  if (!s.includes(from)) throw new Error(`patch target not found: ${from}`);
  return s.replace(from, to);
}
