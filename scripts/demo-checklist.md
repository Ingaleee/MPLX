## MPLX Demo Checklist

### Build
- cmake --preset dev
- cmake --build --preset dev
- ctest --test-dir build/dev -C RelWithDebInfo --output-on-failure

### LSP & VS Code
- cd Presentation/Mplx.Lsp; npm i; npm run build; cd ../..
- cd Presentation/vscode-mplx; npm i; npm run build; cd ../..

### CLI
- mplx --check examples/hello.mplx
- mplx --bench examples/hello.mplx --mode compile-run --runs 20 --json

### Net
- build/dev/Presentation/tools/mplx-net/mplx-net.exe --serve 127.0.0.1 5000
- build/dev/Presentation/tools/mplx-netclient/mplx-netclient.exe --ping 127.0.0.1 5000
- build/dev/Presentation/tools/mplx-netclient/mplx-netclient.exe --health 127.0.0.1 5000

### .NET
- dotnet build Presentation/Mplx.DotNet.Sample
- dotnet run --project Presentation/Mplx.DotNet.Sample

### IDE Flow
- Open examples/hello.mplx
- Trigger diagnostics, go-to-def, references, rename (F2)

