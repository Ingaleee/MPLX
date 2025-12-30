# MPLX Language for VS Code

Compiler-backed diagnostics, hover, definitions, references, scope-aware
completion, capture-safe rename, signature help, document/workspace symbols and
semantic tokens for .mplx files.

Build the C++ compiler with CMake, then:

```sh
cd Presentation/Mplx.Lsp
npm ci
npm run build
npm test -- ../../build/jit/bin/mplx
cd ../vscode-mplx
npm ci
npm run build
npx @vscode/vsce package --skip-license
```

On Windows use mplx.exe. Install the resulting VSIX in VS Code.
Open the repository folder and set mplx.cliPath to build/jit/bin/mplx.exe
(or your compiler path). An empty setting checks dev, dev-msvc, jit and release
builds. The command MPLX: Restart Language Server restarts analysis.

The extension bundles the TypeScript server and its Node dependencies.
The native compiler is built separately; workspace analysis requires MPLX 0.6.
The first workspace folder defines the module root. Imports support foreign
definitions, references, signatures and compiler-validated cross-file rename.
Workspace symbols include closed source files. Open text overrides disk;
the extension watches .mplx create/change/delete events to refresh dependencies.
Namespace aliases and filenames cannot yet be renamed through LSP.
