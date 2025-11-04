# Repository formatting

Use four spaces per indentation level, UTF-8, LF line endings and a final
newline. Ordinary functions and control-flow blocks occupy multiple lines.
The target line width is 110 columns; literal strings and URLs can exceed it.
C++ uses attached braces; C# uses its conventional Allman braces. Visual Studio
solution files retain their conventional tab indentation.

## Tools

Versions are pinned so local formatting and CI agree:

| Files                                        | Formatter                                              | Configuration             |
| -------------------------------------------- | ------------------------------------------------------ | ------------------------- |
| C/C++                                        | clang-format 19.1.5                                    | `.clang-format`           |
| C#                                           | .NET SDK 10.0.303, `dotnet format whitespace --folder` | `.editorconfig`           |
| TypeScript, JavaScript, JSON, YAML, Markdown | Prettier 3.9.10                                        | `.prettierrc.json`        |
| XML project files                            | Prettier XML plugin 3.4.2                              | `.prettierrc.json`        |
| Shell and Dockerfile                         | Prettier shell plugin 0.20.2                           | `.prettierrc.json`        |
| CMake                                        | cmake-format 0.6.13                                    | `.cmake-format.json`      |
| PowerShell                                   | PSScriptAnalyzer 1.25.0                                | `.powershell-format.json` |
| MPLX examples                                | clang-format layout with a token invariant             | `.clang-format`           |

Node 22+, Python 3 and PowerShell 7 are required for the complete formatting
command. The formatting SDK is independent of the .NET 8 embedding target.
Install development tools from the repository root:

```sh
npm ci --ignore-scripts
python -m pip install -r scripts/format-requirements.txt
```

Save the PowerShell module in the ignored build directory:

```powershell
Save-Module PSScriptAnalyzer -RequiredVersion 1.25.0 -Repository PSGallery -Path build/format-tools/powershell -Force
```

Alternatively, keep the Python packages inside the repository too:

```sh
python -m pip install --target build/format-tools/python -r scripts/format-requirements.txt
```

The runner discovers the locally saved Python tools and PowerShell module.
`CLANG_FORMAT`, `PYTHON` and `POWERSHELL` can select executable paths.

## Commands and scope

```sh
npm run format
npm run format:check
```

The runner uses Git's tracked and non-ignored new files, so it covers every
source layer, optional historical modules, tests, examples, scripts and docs.
Deleted files are excluded. Complete source templates in historical generator
scripts are formatted without executing those scripts; regex search patterns
and ordinary replacement strings keep their literal content. PowerShell token comparisons and MPLX
token comparisons guard against accidental syntax changes.

Binary artifacts, dependency directories, generated `bin`, `obj`, `dist`,
extension server bundles and build outputs are excluded. Rebuild generated
TypeScript output using the corresponding package's `npm run build` command.
Fuzzer corpus files retain their exact bytes: their sizes, malformed input and
checksums are part of the tests. JSON lockfiles are included in formatting.

The dedicated formatting CI job runs the same full check. EditorConfig also
provides indentation and newline defaults in supporting editors; language
formatters apply the remaining rules.
