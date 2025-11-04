import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import * as prettier from 'prettier';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
process.chdir(root);
const check = process.argv.includes('--check');
const webOnly = process.argv.includes('--web');
let differences = 0;

function run(command, args, options = {}) {
    const result = spawnSync(command, args, {
        cwd: root,
        encoding: 'utf8',
        maxBuffer: 16 * 1024 * 1024,
        ...options,
    });
    if (result.error) {
        throw new Error(`Cannot run ${command}: ${result.error.message}`);
    }
    if (result.status !== 0) {
        process.stderr.write(result.stdout ?? '');
        process.stderr.write(result.stderr ?? '');
        throw new Error(`${command} exited with ${result.status}`);
    }
    return result.stdout;
}

function update(file, formatted) {
    const original = fs.readFileSync(file, 'utf8');
    if (original === formatted) {
        return;
    }
    ++differences;
    if (check) {
        process.stderr.write(`Formatting differs: ${file}\n`);
    } else {
        fs.writeFileSync(file, formatted);
    }
}

// Git supplies tracked files plus new, non-ignored source files. Existence checks also
// exclude staged/uncommitted deletions without resurrecting removed files.
const files = [
    ...new Set(
        run('git', [
            '-c',
            `safe.directory=${root}`,
            'ls-files',
            '--cached',
            '--others',
            '--exclude-standard',
            '-z',
        ])
            .split('\0')
            .filter(Boolean),
    ),
]
    .filter(
        (file) =>
            fs.existsSync(file) &&
            fs.statSync(file).isFile() &&
            !/(^|\/)(build|out|node_modules|bin|obj|dist)(\/|$)/u.test(file) &&
            !file.startsWith('Presentation/vscode-mplx/server/') &&
            !file.startsWith('Presentation/fuzz/corpus/'),
    )
    .sort();

const cpp = files.filter((file) => /\.(c|cc|cpp|h|hpp)$/u.test(file));
const csharp = files.filter((file) => file.endsWith('.cs'));
const cmake = files.filter((file) => path.basename(file) === 'CMakeLists.txt' || file.endsWith('.cmake'));
const powershell = files.filter((file) => /\.(ps1|psd1)$/u.test(file));
const mplx = files.filter((file) => file.endsWith('.mplx'));
const web = files.filter(
    (file) =>
        /\.(ts|js|mjs|cjs|json|ya?ml|md|csproj|xml|sh)$/u.test(file) ||
        ['NuGet.Config', '.clang-format', 'Dockerfile'].includes(path.basename(file)),
);
const plain = files.filter(
    (file) =>
        [
            '.editorconfig',
            '.gitignore',
            '.gitattributes',
            '.prettierignore',
            '.vscodeignore',
            'format-requirements.txt',
        ].includes(path.basename(file)) || file.endsWith('.sln'),
);

if (!webOnly) {
    const localClang = path.join(
        root,
        'build/format-tools/python/clang_format/data/bin',
        process.platform === 'win32' ? 'clang-format.exe' : 'clang-format',
    );
    const clang = process.env.CLANG_FORMAT || (fs.existsSync(localClang) ? localClang : 'clang-format');
    const version = run(clang, ['--version']).trim();
    if (!/^clang-format version 19\.1\.5\b/u.test(version)) {
        throw new Error(`Expected clang-format 19.1.5; found ${version}`);
    }
    const pythonPath = path.join(root, 'build/format-tools/python');
    const pythonEnvironment = { ...process.env };
    if (fs.existsSync(pythonPath)) {
        pythonEnvironment.PYTHONPATH = [pythonPath, process.env.PYTHONPATH]
            .filter(Boolean)
            .join(path.delimiter);
    }
    if (cpp.length > 0) {
        run(clang, ['--style=file', ...(check ? ['--dry-run', '--Werror'] : ['-i']), ...cpp]);
    }
    // Historical bootstrap scripts contain complete source templates. Format those
    // templates as source too, while preserving regex search patterns and ordinary strings.
    for (const file of powershell) {
        const original = fs.readFileSync(file, 'utf8');
        const matches = [...original.matchAll(/(@(['"])\r?\n)([\s\S]*?)(^\2@)/gmu)];
        let formatted = original;
        for (const match of matches.reverse()) {
            const payload = match[3];
            let replacement;
            if (/^\s*(?:#(?:include|pragma|if)\b|class\b|struct\b|case OP_|if\(auto)/u.test(payload)) {
                replacement = run(clang, ['--style=file', '--assume-filename=template.cpp'], {
                    input: payload,
                });
            } else if (/^\s*(?:fn|export fn)\b/u.test(payload)) {
                replacement = run(clang, ['--style=file', '--assume-filename=template.cpp'], {
                    input: payload,
                });
            } else if (
                /^\s*(?:cmake_minimum_required|add_library|add_executable|add_subdirectory)\(/u.test(payload)
            ) {
                replacement = run(
                    process.env.PYTHON || 'python',
                    ['-m', 'cmakelang.format', '--config-files', '.cmake-format.json', '--', '-'],
                    { input: payload, env: pythonEnvironment },
                );
            } else if (/^\s*using System\b/u.test(payload)) {
                replacement = run(
                    clang,
                    [
                        '--style={BasedOnStyle: InheritParentConfig, BreakBeforeBraces: Allman, NamespaceIndentation: None}',
                        '--assume-filename=template.cs',
                    ],
                    { input: payload },
                );
            } else {
                const parser = /^\s*import\b/u.test(payload)
                    ? 'typescript'
                    : /^\s*\{\s*"/u.test(payload)
                      ? 'json'
                      : /^\s*<Project\b/u.test(payload)
                        ? 'xml'
                        : undefined;
                if (parser) {
                    replacement = await prettier.format(payload, {
                        ...(await prettier.resolveConfig(file)),
                        parser,
                        xmlWhitespaceSensitivity: 'ignore',
                    });
                }
            }
            if (replacement !== undefined) {
                const start = match.index + match[1].length;
                formatted = formatted.slice(0, start) + replacement + formatted.slice(start + payload.length);
            }
        }
        update(file, formatted);
    }
    for (const file of mplx) {
        const original = fs.readFileSync(file, 'utf8');
        // MPLX deliberately uses C-style braces and operators. Clang changes only
        // layout here; a token invariant guards this use outside its C++ grammar.
        let formatted = run(clang, ['--style=file', `--assume-filename=${file}.cpp`], { input: original });
        // Type annotations use `name: type`; clang treats these as C++ bit fields.
        formatted = formatted.replace(
            /"(?:\\.|[^"\\])*"|\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|\s+:(?!:)/gu,
            (token) => (/^\s+:/u.test(token) ? ':' : token),
        );
        const tokens = (source) =>
            source
                .match(
                    /"(?:\\.|[^"\\])*"|\/\/[^\r\n]*|\/\*[\s\S]*?\*\/|[A-Za-z_]\w*|\d+|::|->|==|!=|<=|>=|&&|\|\||[^\s]/gu,
                )
                ?.filter((token) => !token.startsWith('//') && !token.startsWith('/*')) ?? [];
        if (JSON.stringify(tokens(original)) !== JSON.stringify(tokens(formatted))) {
            throw new Error(`Formatter changed MPLX tokens: ${file}`);
        }
        update(file, formatted);
    }
    if (cmake.length > 0) {
        run(
            process.env.PYTHON || 'python',
            [
                '-m',
                'cmakelang.format',
                '--config-files',
                '.cmake-format.json',
                ...(check ? ['--check'] : ['--in-place']),
                ...cmake,
            ],
            { env: pythonEnvironment },
        );
    }
    if (csharp.length > 0) {
        run('dotnet', [
            'format',
            'whitespace',
            '.',
            '--folder',
            ...(check ? ['--verify-no-changes'] : []),
            '--include',
            ...csharp,
        ]);
    }
    if (powershell.length > 0) {
        fs.mkdirSync('build', { recursive: true });
        const manifest = path.join(root, 'build/format-powershell-files.json');
        fs.writeFileSync(manifest, JSON.stringify(powershell));
        run(process.env.POWERSHELL || 'pwsh', [
            '-NoProfile',
            '-File',
            'scripts/format-powershell.ps1',
            '-ManifestPath',
            manifest,
            ...(check ? ['-Check'] : []),
        ]);
    }
}

for (const file of web) {
    const original = fs.readFileSync(file, 'utf8');
    const configuration = await prettier.resolveConfig(file);
    update(file, await prettier.format(original, { ...configuration, filepath: file }));
}
if (!webOnly) {
    for (const file of [...cpp, ...csharp, ...cmake, ...powershell, ...plain]) {
        const original = fs.readFileSync(file, 'utf8');
        let formatted = original.replace(/\r\n/gu, '\n');
        // Language formatters preserve whitespace inside literals. Only plain
        // configuration files can safely trim every line without tokenization.
        if (plain.includes(file)) {
            formatted = formatted.replace(/[\t ]+$/gmu, '');
        }
        update(file, formatted.trimEnd() + '\n');
    }
}

console.log(
    `${check ? 'Checked' : 'Formatted'} ${webOnly ? web.length : cpp.length + csharp.length + cmake.length + powershell.length + mplx.length + web.length + plain.length} files` +
        ` (${cpp.length} C/C++, ${csharp.length} C#, ${cmake.length} CMake, ${powershell.length} PowerShell, ${mplx.length} MPLX, ${web.length} web/config/docs).`,
);
if (check && differences > 0) {
    process.exitCode = 1;
}
