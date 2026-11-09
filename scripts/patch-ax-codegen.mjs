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
  s = insertAfterLine(s,
    'var cppConformance string',
    '\n//go:embed templates/cpp/cppCHeader.h\nvar cppCHeader string\n\n//go:embed templates/cpp/cppCSignatureSchemaExample.cpp\nvar cppCSignatureSchemaExample string\n\n//go:embed templates/cpp/cppCSource.cpp\nvar cppCSource string\n'
  );
  writeFileSync(file, s);
}

function patchCodegen(file) {
  let s = readFileSync(file, 'utf8');
  s = insertAfterLine(s,
    '\t\t"axllm/axllm.cpp":                                       core,',
    '\t\t"axllm/axllm_c.h":                                       renderPackageTemplate(cppCHeader, version),\n\t\t"axllm/axllm_c.cpp":                                     renderPackageTemplate(cppCSource, version),\n'
  );
  s = insertAfterLine(s,
    '\t\t"examples/signature_schema.cpp":                         cppSignatureSchemaExample,',
    '\t\t"examples/c_signature_schema.cpp":                       cppCSignatureSchemaExample,\n'
  );
  writeFileSync(file, s);
}

function patchCmake(file) {
  let s = readFileSync(file, 'utf8');
  s = replaceOnce(s,
    'add_library(axllm axllm/axllm.cpp axllm/mcp.cpp)',
    'add_library(axllm axllm/axllm.cpp axllm/axllm_c.cpp axllm/mcp.cpp)'
  );
  s = insertAfterLine(s, '  foreach(example\n    signature_schema', '    c_signature_schema\n');
  s = replaceOnce(s,
    '  FILES_MATCHING PATTERN "*.hpp"\n)',
    '  FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h"\n)'
  );
  writeFileSync(file, s);
}

// Inserts `text` directly after the line(s) matching `anchor`; tolerant of
// upstream adding or reordering neighbouring entries.
function insertAfterLine(s, anchor, text) {
  if (s.includes(`${anchor}\n${text}`)) return s;
  const from = `${anchor}\n`;
  if (count(s, from) !== 1) throw new Error(`patch anchor not found exactly once: ${anchor}`);
  return s.replace(from, `${from}${text}`);
}

function replaceOnce(s, from, to) {
  if (s.includes(to)) return s;
  if (count(s, from) !== 1) throw new Error(`patch target not found exactly once: ${from}`);
  return s.replace(from, to);
}

function count(s, needle) {
  return s.split(needle).length - 1;
}
