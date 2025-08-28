$ErrorActionPreference = "Stop"

function Ensure-Dir($p){ if (!(Test-Path $p)) { New-Item -Force -ItemType Directory $p | Out-Null } }

# Определяем пути c учётом новой (Clean Architecture) и старой структуры
$CLI_PATHS = @(
  "Presentation/tools/mplx/main.cpp",
  "src-cpp/tools/mplx/main.cpp"
)
$LSP_SERVER_TS = @(
  "Presentation/Mplx.Lsp/src/server.ts",
  "src/Mplx.Lsp/src/server.ts"
)
$EXT_PKG_JSON = @(
  "Presentation/vscode-mplx/package.json",
  "src/vscode-mplx/package.json"
)
$EXT_TS = @(
  "Presentation/vscode-mplx/src/extension.ts",
  "src/vscode-mplx/src/extension.ts"
)
$EXT_TSCONFIG = @(
  "Presentation/vscode-mplx/tsconfig.json",
  "src/vscode-mplx/tsconfig.json"
)
$NET_HEADER = @(
  "Infrastructure/Net/net.hpp",
  "src-cpp/mplx-net/net.hpp"
)
$NET_TOOL_MAIN = @(
  "Presentation/tools/mplx-net/main.cpp",
  "src-cpp/tools/mplx-net/main.cpp"
)
$NET_CLIENT_MAIN = @(
  "Presentation/tools/mplx-netclient/main.cpp",
  "src-cpp/tools/mplx-netclient/main.cpp"
)
$ROOT_CMAKE = "CMakeLists.txt"

# --- 0) CMakePresets: dev/release (+ vcpkg toolchain при наличии)
if (Test-Path "CMakePresets.json") {
  $presets = Get-Content CMakePresets.json -Raw | ConvertFrom-Json
} else {
  $presets = [pscustomobject]@{
    version = 5
    configurePresets = @()
    buildPresets = @()
  }
}
function Add-Preset {
  param($name, $bin, $cfg, $presetsRef)
  $exists = $false
  foreach($p in $presetsRef.configurePresets){ if($p.name -eq $name){ $exists = $true } }
  if(-not $exists){
    $cv = @{ CMAKE_BUILD_TYPE = $cfg }
    if ($env:VCPKG_ROOT) { $cv.CMAKE_TOOLCHAIN_FILE = "$($env:VCPKG_ROOT)/scripts/buildsystems/vcpkg.cmake" }
    $presetsRef.configurePresets += [pscustomobject]@{
      name=$name; generator="Ninja"; binaryDir=$bin; cacheVariables = $cv
    }
    $presetsRef.buildPresets += [pscustomobject]@{ name=$name; configurePreset=$name }
  }
}
Add-Preset -name "dev" -bin "build/dev" -cfg "RelWithDebInfo" -presetsRef $presets
Add-Preset -name "release" -bin "build/release" -cfg "Release" -presetsRef $presets
$presets | ConvertTo-Json -Depth 8 | Out-File -Encoding utf8 CMakePresets.json

# --- 0.1) Root CMake: убедимся, что подпроекты добавлены (идемпотентно)
$root = if (Test-Path $ROOT_CMAKE) { Get-Content $ROOT_CMAKE -Raw } else { "" }
function Ensure-Subdir($text, $pattern, $snippet){
  if ($text -notmatch [regex]::Escape($pattern)) { return $text + "`n" + $snippet + "`n" } else { return $text }
}
if ($root.Length -gt 0) {
  $root = Ensure-Subdir $root "Presentation/tools/mplx" 'add_subdirectory(Presentation/tools/mplx)'
  $root = Ensure-Subdir $root "Presentation/tools/mplx-net" 'add_subdirectory(Presentation/tools/mplx-net)'
  $root = Ensure-Subdir $root "Presentation/tools/mplx-netclient" 'add_subdirectory(Presentation/tools/mplx-netclient)'
  $root = Ensure-Subdir $root "Infrastructure/Net" 'add_subdirectory(Infrastructure/Net)'
  $root = Ensure-Subdir $root "benchmarks" 'add_subdirectory(benchmarks)'
  $root = Ensure-Subdir $root "tests-cpp" 'add_subdirectory(tests-cpp)'
  Set-Content -Encoding utf8 $ROOT_CMAKE $root
}

# --- 1) CLI: добавим --help/--version/--bench --json (compile-run|run-only, runs N)
$cliPath = $null
foreach($p in $CLI_PATHS){ if(Test-Path $p){ $cliPath = $p; break } }
if(-not $cliPath){ throw "Не найден CLI main.cpp (ожидал Presentation/tools/mplx/main.cpp или src-cpp/tools/mplx/main.cpp)" }

$cliSrc = Get-Content $cliPath -Raw
if ($cliSrc -notmatch "--bench") {
$cliSrc = @'
#include <chrono>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <nlohmann/json.hpp>

// Приоритет: фасад Application, если есть
#if __has_include("../../../Application/public_api.hpp")
  #include "../../../Application/public_api.hpp"
#else
  // Fallback на прямые include старой структуры
  #include "../../mplx-lang/lexer.hpp"
  #include "../../mplx-lang/parser.hpp"
  #include "../../mplx-compiler/compiler.hpp"
  #include "../../mplx-vm/vm.hpp"
#endif

using json = nlohmann::json;

static std::string read_all(const std::string& path){
  std::ifstream in(path, std::ios::binary);
  if(!in) throw std::runtime_error("cannot open " + path);
  return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

static void print_help(){
  std::cout <<
    "mplx usage:\n"
    "  mplx --run <file>\n"
    "  mplx --check <file>\n"
    "  mplx --symbols <file>\n"
    "  mplx --bench <file> [--mode compile-run|run-only] [--runs N] [--json]\n"
    "  mplx --version\n"
    "  mplx --help\n";
}

int main(int argc, char** argv){
  if (argc < 2) { print_help(); return 2; }
  std::string mode = argv[1];
  if (mode == "--help"){ print_help(); return 0; }
  if (mode == "--version"){ std::cout << "mplx 0.2.0" << std::endl; return 0; }
  if (argc < 3){ print_help(); return 2; }
  std::string path = argv[2];

  std::string src;
  try { src = read_all(path); } catch(const std::exception& ex){ std::cerr << ex.what() << "\n"; return 2; }

  mplx::Lexer lex(src); auto toks = lex.Lex();
  mplx::Parser p(toks); auto mod = p.parse();

  if(mode == "--check"){
    json out; out["diagnostics"] = json::array();
    for (auto& d : p.diagnostics()){
      out["diagnostics"].push_back({ {"message", d.message}, {"line", d.line}, {"col", d.col} });
    }
    std::cout << out.dump() << std::endl; return 0;
  }

  if(mode == "--symbols"){
    json out; out["functions"] = json::array();
    for (auto& f : mod.functions) out["functions"].push_back({ {"name", f.name}, {"arity", (int)f.params.size()} });
    std::cout << out.dump() << std::endl; return 0;
  }

  if(mode == "--bench"){
    std::string benchMode = "compile-run";
    int runs = 20;
    bool jsonOut = false;
    for (int i=3;i<argc;i++){
      std::string a = argv[i];
      if (a == "--mode" && i+1<argc) { benchMode = argv[++i]; }
      else if (a == "--runs" && i+1<argc) { runs = std::max(1, atoi(argv[++i])); }
      else if (a == "--json") { jsonOut = true; }
    }
    std::vector<double> timesMs; timesMs.reserve(runs);
    if (benchMode == "run-only"){
      mplx::Compiler c; auto res = c.compile(mod);
      for(int i=0;i<runs;i++){
        auto t0 = std::chrono::high_resolution_clock::now();
        mplx::VM vm(res.bc); volatile auto rv = vm.run("main"); (void)rv;
        auto t1 = std::chrono::high_resolution_clock::now();
        timesMs.push_back(std::chrono::duration<double, std::milli>(t1-t0).count());
      }
    } else {
      for(int i=0;i<runs;i++){
        auto t0 = std::chrono::high_resolution_clock::now();
        mplx::Compiler c; auto res = c.compile(mod); mplx::VM vm(res.bc); volatile auto rv = vm.run("main"); (void)rv;
        auto t1 = std::chrono::high_resolution_clock::now();
        timesMs.push_back(std::chrono::duration<double, std::milli>(t1-t0).count());
      }
    }
    double sum=0, best=1e100, worst=-1;
    for (auto v: timesMs){ sum+=v; if(v<best) best=v; if(v>worst) worst=v; }
    double avg = sum / timesMs.size();
    if (jsonOut){
      json j; j["mode"]=benchMode; j["runs"]=runs; j["avg_ms"]=avg; j["best_ms"]=best; j["worst_ms"]=worst;
      std::cout << j.dump() << std::endl;
    } else {
      std::cout << "mode="<<benchMode<<" runs="<<runs<<" avg="<<avg<<"ms best="<<best<<"ms worst="<<worst<<"ms"<<std::endl;
    }
    return 0;
  }

  // default: --run
  if (mode != "--run"){ std::cerr << "Unknown mode: "<<mode<<"\n"; print_help(); return 2; }
  mplx::Compiler c; auto res = c.compile(mod);
  try{
    mplx::VM vm(res.bc); auto rv = vm.run("main");
    std::cout << rv << std::endl;
  } catch (const std::exception& ex){ std::cerr << "Runtime error: "<<ex.what()<<"\n"; return 1; }
  return 0;
}
'@
Set-Content -Encoding utf8 $cliPath $cliSrc
}

# --- 2) СЕТЬ: HEALTH/PING. Сервер: msgType 0x01 -> {"ok":true,...} c msgType 0x81; echo -> 0x80
function Patch-NetHeader($hdrPath){
  $h = Get-Content $hdrPath -Raw
  if ($h -notmatch 'service:\"mplx\"') {
$h = $h -replace 'class EchoServer \{[^\}]+\};',
@'
class EchoServer {
public:
  EchoServer(asio::io_context& io, const std::string& host, uint16_t port)
    : acceptor_(io, asio::ip::tcp::endpoint(asio::ip::make_address(host), port)) {}

  void Start(){ do_accept(); }
private:
  void do_accept(){
    auto sock = std::make_shared<asio::ip::tcp::socket>(acceptor_.get_executor());
    acceptor_.async_accept(*sock, [this, sock](const asio::error_code& ec){
      if (!ec) { do_session(sock); }
      do_accept();
    });
  }
  void do_session(std::shared_ptr<asio::ip::tcp::socket> sock){
    asio::co_spawn(acceptor_.get_executor(),
      [sock]() -> asio::awaitable<void> {
        asio::error_code ec;
        for(;;){
          Frame f; if (!read_frame(*sock, f, ec)) break;
          Frame out;
          if (f.msgType == 0x01) { // PING
            const char* payload = "{\"ok\":true,\"service\":\"mplx\",\"version\":\"0.2.0\"}";
            out.msgType = 0x81; out.payload.assign(payload, payload + strlen(payload));
          } else { // echo
            out.msgType = 0x80; out.payload = f.payload;
          }
          write_frame(*sock, out, ec); if (ec) break;
        }
        co_return;
      }, asio::detached);
  }
  asio::ip::tcp::acceptor acceptor_;
};
'@
    Set-Content -Encoding utf8 $hdrPath $h
  }
}
$netHdr = $null
foreach($p in $NET_HEADER){ if(Test-Path $p){ $netHdr=$p; break } }
if($netHdr){ Patch-NetHeader $netHdr } else { Write-Host "Предупреждение: net.hpp не найден — пропускаю HEALTH/PING" -ForegroundColor Yellow }

# Клиент: добавим --ping, и сделаем include устойчивым к новой структуре
function Patch-NetClient($path){
  $src = Get-Content $path -Raw
  if ($src -notmatch "--ping") {
    $incl = @"
#if __has_include("../../../Infrastructure/Net/net.hpp")
#include "../../../Infrastructure/Net/net.hpp"
#else
#include "../../mplx-net/net.hpp"
#endif
"@
    $src = $src -replace '#include "../../mplx-net/net.hpp"', $incl
    $src = $src -replace 'if \(argc < 4\)\{[^\}]+\}',
'if (argc < 4){ std::cerr << "Usage: mplx-netclient --send <host> <port> <hexpayload> | --ping <host> <port>\n"; return 2; }'
    $src = $src -replace 'if \(mode != std::string\("--send"\)\) \{ std::cerr << "Unknown mode',
'if (mode != std::string("--send") && mode != std::string("--ping")) { std::cerr << "Unknown mode'
    $src = $src -replace 'std::string hex = argc>4\? argv\[4\] : "48-69";', 'std::string hex = argc>4? argv[4] : "48-69";'
    $src = $src -replace 'mplx::net::Frame f; f.msgType=0; f.payload = parse_hex\(hex\);',
'mplx::net::Frame f; if(mode=="--ping"){ f.msgType=0x01; } else { f.msgType=0x00; f.payload = parse_hex(hex); }'
    $src = $src -replace 'std::cout << "reply type=" << \(int\)r.msgType << " size=" << r.payload.size\(\) << "\n";',
'std::cout << "reply type=" << (int)r.msgType << " size=" << r.payload.size() << "\n"; if(r.msgType==0x81){ std::cout << std::string(r.payload.begin(), r.payload.end()) << "\n"; }'
    Set-Content -Encoding utf8 $path $src
  }
}
$netClient = $null
foreach($p in $NET_CLIENT_MAIN){ if(Test-Path $p){ $netClient=$p; break } }
if($netClient){ Patch-NetClient $netClient }

# Серверный tools main: поправим include на новый путь (если нужно)
$netTool = $null
foreach($p in $NET_TOOL_MAIN){ if(Test-Path $p){ $netTool=$p; break } }
if($netTool){
  $t = Get-Content $netTool -Raw
  if ($t -match '#include "../../mplx-net/net.hpp"') {
    $incl2 = @"
#if __has_include("../../../Infrastructure/Net/net.hpp")
#include "../../../Infrastructure/Net/net.hpp"
#else
#include "../../mplx-net/net.hpp"
#endif
"@
    $t = $t -replace '#include "../../mplx-net/net.hpp"', $incl2
    Set-Content -Encoding utf8 $netTool $t
  }
}

# --- 3) LSP SERVER: индекс (definition/references/hover/signature) + конфиг cliPath
$lsp = $null
foreach($p in $LSP_SERVER_TS){ if(Test-Path $p){ $lsp=$p; break } }
if($lsp){
@'
import {
  createConnection, ProposedFeatures,
  InitializeParams, TextDocuments, TextDocumentSyncKind,
  Diagnostic, DiagnosticSeverity,
  DefinitionParams, Location,
  ReferenceParams, Hover, MarkupKind,
  SignatureHelp, SignatureInformation, ParameterInformation, Position
} from "vscode-languageserver/node";
import { TextDocument } from "vscode-languageserver-textdocument";
import { spawn } from "node:child_process";
import { tmpdir } from "node:os";
import { writeFileSync } from "node:fs";
import { join, basename } from "node:path";

const connection = createConnection(ProposedFeatures.all);
const documents: TextDocuments<TextDocument> = new TextDocuments(TextDocument);

let cliPath = "build/dev/Presentation/tools/mplx/mplx";

type Def = { name: string; uri: string; line: number; col: number; sig: string };
const index = new Map<string, Def[]>();       // uri -> defs
const nameToDefs = new Map<string, Def[]>();  // name -> defs

function buildIndexForDoc(doc: TextDocument){
  const text = doc.getText();
  const defs: Def[] = [];
  const re = /fn\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(([^)]*)\)/g;
  let m: RegExpExecArray | null;
  while((m = re.exec(text))){
    const name = m[1]; const params = m[2].trim();
    const off = m.index;
    const pos = doc.positionAt(off);
    const sig = `${name}(${params})`;
    defs.push({ name, uri: doc.uri, line: pos.line, col: pos.character, sig });
  }
  index.set(doc.uri, defs);
  for (const [k, arr] of nameToDefs) nameToDefs.set(k, arr.filter(d => d.uri !== doc.uri));
  for (const d of defs){
    const arr = nameToDefs.get(d.name) ?? [];
    arr.push(d); nameToDefs.set(d.name, arr);
  }
}

function wordAt(doc: TextDocument, pos: Position): string | null {
  const text = doc.getText();
  const off = doc.offsetAt(pos);
  const re = /[A-Za-z_][A-Za-z0-9_]*/g;
  let m: RegExpExecArray | null; let found: string | null = null;
  while((m = re.exec(text))){
    const s = m.index, e = s + m[0].length;
    if (off >= s && off <= e) { found = m[0]; break; }
  }
  return found;
}

function runCli(args: string[], text: string): Promise<{out:string, err:string}>{
  return new Promise((resolve) => {
    const p = join(tmpdir(), "mplx_" + Math.random().toString(36).slice(2) + ".mplx");
    writeFileSync(p, text, "utf8");
    const cli = spawn(cliPath, [...args, p], { shell: true });
    let out = ""; let err = "";
    cli.stdout.on("data", d => out += d.toString());
    cli.stderr.on("data", d => err += d.toString());
    cli.on("close", _ => resolve({out, err}));
  });
}

connection.onInitialize((params: InitializeParams) => {
  const cfg = (params.initializationOptions as any) ?? {};
  if (cfg.cliPath) cliPath = cfg.cliPath;
  return {
    capabilities: {
      textDocumentSync: TextDocumentSyncKind.Incremental,
      definitionProvider: true,
      referencesProvider: true,
      hoverProvider: true,
      signatureHelpProvider: { triggerCharacters: ["(", ","] }
    }
  };
});

documents.onDidOpen(e => { buildIndexForDoc(e.document); validateDocument(e.document); });
documents.onDidChangeContent(e => { buildIndexForDoc(e.document); validateDocument(e.document); });
documents.onDidClose(e => {
  index.delete(e.document.uri);
  for (const [k, arr] of nameToDefs) nameToDefs.set(k, arr.filter(d => d.uri !== e.document.uri));
});

async function validateDocument(doc: TextDocument){
  const { out } = await runCli(["--check"], doc.getText());
  const diags: Diagnostic[] = [];
  try{
    const j = JSON.parse(out);
    for (const d of (j.diagnostics ?? [])){
      const line = Math.max(0, (d.line ?? 1) - 1);
      const col  = Math.max(0, (d.col  ?? 1) - 1);
      diags.push({
        message: d.message || "error",
        range: { start: { line, character: col }, end: { line, character: col+1 } },
        severity: DiagnosticSeverity.Error, source: "mplx"
      });
    }
  } catch {}
  connection.sendDiagnostics({ uri: doc.uri, diagnostics: diags });
}

connection.onDefinition((p) => {
  const doc = documents.get(p.textDocument.uri);
  if (!doc) return null;
  const name = wordAt(doc, p.position);
  if (!name) return null;
  const defs = nameToDefs.get(name) ?? [];
  const locs: Location[] = defs.map(d => ({
    uri: d.uri,
    range: { start: { line: d.line, character: d.col }, end: { line: d.line, character: d.col + name.length } }
  }));
  return locs.length ? locs : null;
});

connection.onReferences((p: ReferenceParams) => {
  const out: Location[] = [];
  for (const [uri, defs] of index){
    const d = documents.get(uri);
    if (!d) continue;
    const text = d.getText();
    const name = wordAt(d, p.position) || ""; // best-effort
    if (!name) continue;
    const re = new RegExp("\\b"+name+"\\b","g");
    let m: RegExpExecArray | null;
    while((m = re.exec(text))){
      const pos = d.positionAt(m.index);
      out.push({ uri, range: { start: pos, end: { line: pos.line, character: pos.character + name.length } } });
    }
  }
  return out;
});

connection.onHover((p): Hover | null => {
  const doc = documents.get(p.textDocument.uri);
  if (!doc) return null;
  const name = wordAt(doc, p.position);
  if (!name) return null;
  const defs = nameToDefs.get(name) ?? [];
  if (defs.length === 0) return null;
  const sigs = defs.map(d => `- \`${d.sig}\` @ ${basename(d.uri)}:${d.line+1}:${d.col+1}`).join("\n");
  return { contents: { kind: MarkupKind.Markdown, value: `**${name}**\n${sigs}` } };
});

connection.onSignatureHelp((p): SignatureHelp | null => {
  const doc = documents.get(p.textDocument.uri);
  if (!doc) return null;
  const name = wordAt(doc, p.position); if (!name) return null;
  const defs = nameToDefs.get(name) ?? []; if (defs.length === 0) return null;
  const def = defs[0];
  const inside = def.sig.substring(def.sig.indexOf("(")+1, def.sig.lastIndexOf(")")).trim();
  const parts = inside.length ? inside.split(",").map(s=>s.trim()) : [];
  return {
    signatures: [ SignatureInformation.create(def.sig, undefined, ...parts.map(ParameterInformation.create)) ],
    activeSignature: 0, activeParameter: 0
  };
});

documents.listen(connection);
connection.listen();
'@ | Out-File -Encoding utf8 $lsp
} else {
  Write-Host "Предупреждение: LSP server.ts не найден — пропускаю" -ForegroundColor Yellow
}

# --- 4) VS Code extension: конфиг cliPath + LSP-клиент (vscode-languageclient)
$extPath = $null
foreach($p in $EXT_PKG_JSON){ if(Test-Path $p){ $extPath=$p; break } }
if($extPath){
  $pkg = Get-Content $extPath -Raw | ConvertFrom-Json
  if (-not $pkg.contributes) { $pkg | Add-Member -NotePropertyName contributes -NotePropertyValue (@{}) }
  $pkg.contributes.configuration = @{
    title = "MPLX"
    properties = @{
      "mplx.cliPath" = @{
        type = "string"; default = "build/dev/Presentation/tools/mplx/mplx";
        description = "Path to mplx CLI executable"
      }
    }
  }
  if (-not $pkg.activationEvents) { $pkg.activationEvents = @("onLanguage:mplx") }
  if (-not $pkg.main) { $pkg.main = "dist/extension.js" }
  if (-not $pkg.scripts) { $pkg.scripts = @{} }
  $pkg.scripts.build = "tsc -p ."
  if (-not $pkg.devDependencies) { $pkg.devDependencies = @{} }
  if (-not $pkg.devDependencies."typescript") { $pkg.devDependencies."typescript" = "5.5.4" }
  $pkg.devDependencies."vscode-languageclient" = "9.0.1"
  $pkg.devDependencies."@types/node" = "20.11.30"
  Set-Content -Encoding utf8 $extPath ($pkg | ConvertTo-Json -Depth 8)

  # tsconfig для extension (на случай отсутствия)
  $tscPath = $null
  foreach($p in $EXT_TSCONFIG){ if(Test-Path $p){ $tscPath=$p; break } }
  if(-not $tscPath){ $tscPath = $EXT_TSCONFIG[0]; Ensure-Dir (Split-Path $tscPath) }
@'
{
  "compilerOptions": {
    "module": "commonjs",
    "target": "ES2022",
    "outDir": "dist",
    "rootDir": "src",
    "strict": true,
    "moduleResolution": "Node"
  }
}
'@ | Out-File -Encoding utf8 $tscPath

  # extension.ts — запускаем LSP-сервер и передаём initializationOptions.cliPath
  $extTsPath = $null
  foreach($p in $EXT_TS){ if(Test-Path $p){ $extTsPath=$p; break } }
  if(-not $extTsPath){ $extTsPath = $EXT_TS[0]; Ensure-Dir (Split-Path $extTsPath) }
@'
import * as vscode from "vscode";
import * as path from "path";
import { LanguageClient, LanguageClientOptions, ServerOptions, TransportKind } from "vscode-languageclient/node";

let client: LanguageClient | undefined;

export function activate(context: vscode.ExtensionContext){
  const config = vscode.workspace.getConfiguration("mplx");
  const cliPath = config.get<string>("cliPath", "build/dev/Presentation/tools/mplx/mplx");

  // Ищем server.js в соседнем проекте Presentation/Mplx.Lsp/dist
  let serverModule = path.join(context.asAbsolutePath(".."), "Mplx.Lsp", "dist", "server.js");
  // Фоллбек на старую структуру
  if (!require("fs").existsSync(serverModule)) {
    serverModule = path.join(context.asAbsolutePath("..", ".."), "src", "Mplx.Lsp", "dist", "server.js");
  }

  const serverOptions: ServerOptions = {
    run:   { module: serverModule, transport: TransportKind.ipc, options: { execArgv: [] } },
    debug: { module: serverModule, transport: TransportKind.ipc, options: { execArgv: ["--nolazy","--inspect=6009"] } }
  };

  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: "file", language: "mplx" }],
    initializationOptions: { cliPath }
  };

  client = new LanguageClient("mplx-lsp", "MPLX Language Server", serverOptions, clientOptions);
  context.subscriptions.push(client.start());
}

export function deactivate(){
  if (client) return client.stop();
}
'@ | Out-File -Encoding utf8 $extTsPath
}

# --- 5) Локальный CI-скрипт
Ensure-Dir "scripts"
@'
param(
  [string]$Preset = "dev"
)

$ErrorActionPreference = "Stop"
Write-Host "== Configure ($Preset) ==" -ForegroundColor Cyan
cmake --preset $Preset

Write-Host "== Build ==" -ForegroundColor Cyan
cmake --build --preset $Preset

Write-Host "== CTest ==" -ForegroundColor Cyan
ctest --test-dir ("build/"+$Preset) -C RelWithDebInfo --output-on-failure

Write-Host "== LSP build ==" -ForegroundColor Cyan
if (Test-Path "Presentation/Mplx.Lsp/package.json") {
  Push-Location Presentation/Mplx.Lsp
  npm i
  npm run build
  Pop-Location
}

if (Test-Path "Presentation/vscode-mplx/package.json") {
  Push-Location Presentation/vscode-mplx
  npm i
  npm run build
  Pop-Location
}

Write-Host "== Bench (--bench --json) ==" -ForegroundColor Cyan
$cli = Join-Path "build/$Preset" "Presentation/tools/mplx/mplx.exe"
if (Test-Path $cli) {
  & $cli --bench examples/hello.mplx --mode compile-run --runs 10 --json
} else {
  Write-Host "CLI not found at $cli (skip bench)" -ForegroundColor Yellow
}
'@ | Out-File -Encoding utf8 scripts/local-ci.ps1 -NoNewline

# --- 6) Git commit
git add -A
git commit -m "feat(step2): CLI --bench/--json; LSP xref+hover+signature with cliPath; Net PING/HEALTH; presets; VSCode client; local CI script" | Out-Null

Write-Host "step2-apply.ps1: готово." -ForegroundColor Green
