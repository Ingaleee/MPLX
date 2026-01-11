import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const manifest = JSON.parse(fs.readFileSync(path.join(root, 'conformance/semantics.json'), 'utf8'));
const destination = path.join(root, 'conformance/generated_cases.hpp');
const ids = new Set();
const rules = new Set(manifest.rules.map((rule) => rule.id));
if (
    manifest.schema_version !== 1 ||
    manifest.semantics_version !== 1 ||
    rules.size !== manifest.rules.length
) {
    throw new Error('Invalid conformance manifest version or duplicate rule');
}
const covered = new Set();
const cpp = (value) => JSON.stringify(value).replace(/\\u([0-9a-f]{4})/gi, '\\u$1');
const integer = (value) => {
    if (typeof value !== 'string' || !/^-?(0|[1-9][0-9]*)$/.test(value)) {
        throw new Error(`Noncanonical integer: ${value}`);
    }
    const number = BigInt(value);
    if (number < -(1n << 63n) || number >= 1n << 63n) {
        throw new Error('Conformance integer outside i64');
    }
    return number === -(1n << 63n) ? '(-9223372036854775807LL - 1)' : `${value}LL`;
};
let output = `// Generated from semantics.json by scripts/conformance.mjs. Do not edit.
#pragma once

#include <string>
#include <vector>

namespace mplx::conformance {
    constexpr unsigned semantics_version = 1;
    struct Case {
        std::string id, rule, source, entry;
        std::vector<long long> arguments;
        std::string kind;
        long long value;
        std::string function, diagnostic;
        unsigned fault_start, fault_end;
    };
    inline const std::vector<Case> cases{
`;
for (const item of manifest.cases) {
    if (!/^[a-z0-9-]+$/.test(item.id) || ids.has(item.id) || !rules.has(item.rule)) {
        throw new Error(`Invalid conformance identity: ${item.id}`);
    }
    ids.add(item.id);
    covered.add(item.rule);
    if (
        !['value', 'arithmetic', 'diagnostic', 'invalid-entry'].includes(item.expected.kind) ||
        typeof item.source !== 'string' ||
        !Array.isArray(item.arguments) ||
        item.arguments.length > 255
    ) {
        throw new Error(`Invalid conformance case: ${item.id}`);
    }
    const expected = item.expected;
    let start = 0,
        end = 0;
    if (expected.kind === 'arithmetic') {
        const character = item.source.indexOf(expected.fault_expression);
        if (
            character < 0 ||
            !expected.fault_expression ||
            !expected.function ||
            item.source.indexOf(expected.fault_expression, character + 1) >= 0
        ) {
            throw new Error(`Fault expression must occur exactly once: ${item.id}`);
        }
        start = Buffer.byteLength(item.source.slice(0, character));
        end = start + Buffer.byteLength(expected.fault_expression);
    }
    if (expected.kind === 'diagnostic' && !/^MPLX[0-9]{4}$/.test(expected.code)) {
        throw new Error(`Missing diagnostic code: ${item.id}`);
    }
    const lines = item.source
        .split(/(?<=\n)/)
        .map((line) => `            ${cpp(line)}`)
        .join('\n');
    output += `        {
            ${cpp(item.id)}, ${cpp(item.rule)},
${lines},
            ${cpp(item.entry ?? 'main')}, {${item.arguments.map(integer).join(', ')}},
            ${cpp(expected.kind)}, ${integer(expected.value ?? '0')},
            ${cpp(expected.function ?? '')}, ${cpp(expected.code ?? '')}, ${start}, ${end},
        },
`;
}
for (const rule of manifest.rules) {
    if (!covered.has(rule.id) || typeof rule.normative !== 'string' || !rule.normative) {
        throw new Error(`Uncovered or empty semantic rule: ${rule.id}`);
    }
}
output += '    };\n} // namespace mplx::conformance\n';
if (process.argv.includes('--check')) {
    // Compare tokens: repository clang-format owns whitespace in the generated C++.
    const normalize = (value) => value.match(/"(?:\\.|[^"\\])*"|[^\s"]+/g).join('');
    if (normalize(fs.readFileSync(destination, 'utf8')) !== normalize(output)) {
        throw new Error('Generated conformance cases are stale; run node scripts/conformance.mjs');
    }
} else {
    fs.writeFileSync(destination, output);
}
process.stdout.write(
    `Conformance semantics v1: ${manifest.rules.length} rules, ${manifest.cases.length} cases\n`,
);
