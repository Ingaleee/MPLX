$ErrorActionPreference = "Stop"

# 1) Обновим vcpkg зависимости (asio + sqlite3 для будущего ORM)
(Get-Content vcpkg.json) -join "`n" | ForEach-Object {
} | Out-Null
@'
{
  "name": "mplx",
  "version-string": "0.1.1",
  "dependencies": [
    "fmt",
    "nlohmann-json",
    "gtest",
    "asio",
    "sqlite3"
  ]
}
'@ | Out-File -Encoding utf8 vcpkg.json -NoNewline

# 2) Добавим подпроекты в root CMake
$root = Get-Content CMakeLists.txt -Raw
if ($root -notmatch "src-cpp/mplx-net") {
  $root = $root + @'

add_subdirectory(src-cpp/mplx-net)
add_subdirectory(src-cpp/tools/mplx-net)
add_subdirectory(src-cpp/mplx-pkg)
'@
  Set-Content -Encoding utf8 CMakeLists.txt $root
}

# 3) Создадим папки
$dirs = @(
 "src-cpp/mplx-net",
 "src-cpp/tools/mplx-net",
 "src-cpp/mplx-pkg"
)
$dirs | ForEach-Object { New-Item -Force -ItemType Directory $_ | Out-Null }

# 4) ФИКС: неправильная обработка '!=' в компиляторе
$comp = Get-Content src-cpp/mplx-compiler/compiler.cpp -Raw
$comp = $comp -replace 'else if\(op!="=" && op=="!="\)', 'else if(op=="!=")'
Set-Content -Encoding utf8 src-cpp/mplx-compiler/compiler.cpp $comp

# 5) Сетевой модуль: бинарный протокол varint + asio TCP эхо
@'
add_library(mplx-net net.cpp)
find_package(asio CONFIG REQUIRED)
target_include_directories(mplx-net PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mplx-net PUBLIC asio::asio)
'@ | Out-File -Encoding utf8 src-cpp/mplx-net/CMakeLists.txt -NoNewline

@'
#pragma once
#include <asio.hpp>
#include <cstdint>
#include <vector>
#include <string>
#include <functional>

namespace mplx::net {

// varint (LEB128-like for demo): 7 bits per byte, MSB=continue
inline void write_varint(uint64_t v, std::vector<uint8_t>& out){
  while (true){
    uint8_t b = v & 0x7F;
    v >>= 7;
    if (v) { out.push_back(b | 0x80); }
    else { out.push_back(b); break; }
  }
}

inline bool read_varint(asio::ip::tcp::socket& sock, uint64_t& out, asio::error_code& ec){
  out = 0; int shift = 0;
  for (int i=0;i<10;++i){
    uint8_t b;
    size_t n = asio::read(sock, asio::buffer(&b,1), ec);
    if (ec || n!=1) return false;
    out |= uint64_t(b & 0x7F) << shift;
    if ((b & 0x80) == 0) return true;
    shift += 7;
  }
  ec = asio::error::operation_aborted; // too long
  return false;
}

// frame: varint length | u8 msgType | payload[length-1]
struct Frame { uint8_t msgType{0}; std::vector<uint8_t> payload; };

inline void write_frame(asio::ip::tcp::socket& sock, const Frame& f, asio::error_code& ec){
  std::vector<uint8_t> buf;
  std::vector<uint8_t> body; body.reserve(1 + f.payload.size());
  body.push_back(f.msgType);
  body.insert(body.end(), f.payload.begin(), f.payload.end());
  write_varint(body.size(), buf);
  buf.insert(buf.end(), body.begin(), body.end());
  asio::write(sock, asio::buffer(buf), ec);
}

inline bool read_frame(asio::ip::tcp::socket& sock, Frame& f, asio::error_code& ec){
  uint64_t len;
  if (!read_varint(sock, len, ec)) return false;
  if (len == 0) { ec = asio::error::operation_aborted; return false; }
  std::vector<uint8_t> body(len);
  size_t n = asio::read(sock, asio::buffer(body), ec);
  if (ec || n != len) return false;
  f.msgType = body[0];
  f.payload.assign(body.begin()+1, body.end());
  return true;
}

// Simple echo server; msgType ignored, payload echoed back with msgType=1
class EchoServer {
public:
  EchoServer(asio::io_context& io, const std::string& host, uint16_t port)
    : acceptor_(io, asio::ip::tcp::endpoint(asio::ip::make_address(host), port)) {}

  void Start(){
    do_accept();
  }
private:
  void do_accept(){
    auto sock = std::make_shared<asio::ip::tcp::socket>(acceptor_.get_executor());
    acceptor_.async_accept(*sock, [this, sock](const asio::error_code& ec){
      if (!ec) { do_session(sock); }
      do_accept();
    });
  }
  void do_session(std::shared_ptr<asio::ip::tcp::socket> sock){
    auto self = sock;
    asio::co_spawn(acceptor_.get_executor(),
      [self]() -> asio::awaitable<void> {
        asio::error_code ec;
        try{
          for(;;){
            Frame f;
            if (!read_frame(*self, f, ec)) break;
            Frame out{1, f.payload};
            write_frame(*self, out, ec);
            if (ec) break;
          }
        } catch(...) {}
        co_return;
      },
      asio::detached);
  }

  asio::ip::tcp::acceptor acceptor_;
};

} // namespace mplx::net
'@ | Out-File -Encoding utf8 src-cpp/mplx-net/net.hpp -NoNewline

@'
#include "net.hpp"
'@ | Out-File -Encoding utf8 src-cpp/mplx-net/net.cpp -NoNewline

@'
add_executable(mplx-net main.cpp)
find_package(asio CONFIG REQUIRED)
target_link_libraries(mplx-net PRIVATE mplx-net asio::asio)
'@ | Out-File -Encoding utf8 src-cpp/tools/mplx-net/CMakeLists.txt -NoNewline

@'
#include "../../mplx-net/net.hpp"
#include <asio.hpp>
#include <iostream>

int main(int argc, char** argv){
  if (argc < 4){
    std::cerr << "Usage: mplx-net --serve <host> <port>\n";
    return 2;
  }
  std::string mode = argv[1];
  std::string host = argv[2];
  uint16_t port = static_cast<uint16_t>(std::stoi(argv[3]));

  if (mode != std::string("--serve")){
    std::cerr << "Unknown mode: " << mode << "\n";
    return 2;
  }
  try{
    asio::io_context io;
    mplx::net::EchoServer srv(io, host, port);
    srv.Start();
    std::cout << "MPLX echo server on " << host << ":" << port << std::endl;
    io.run();
  } catch (const std::exception& ex){
    std::cerr << "Server error: " << ex.what() << "\n";
    return 1;
  }
  return 0;
}
'@ | Out-File -Encoding utf8 src-cpp/tools/mplx-net/main.cpp -NoNewline

# 6) Пакетник (очень простой): init/add/list/lock
@'
add_executable(mplx-pkg main.cpp)
find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(mplx-pkg PRIVATE nlohmann_json::nlohmann_json)
'@ | Out-File -Encoding utf8 src-cpp/mplx-pkg/CMakeLists.txt -NoNewline

@'
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using json = nlohmann::json;
namespace fs = std::filesystem;

static fs::path manifest() { return "mplx.json"; }
static fs::path lockfile() { return "mplx.lock"; }

static void write_manifest(){
  json j;
  j["name"] = fs::current_path().filename().string();
  j["version"] = "0.1.0";
  j["dependencies"] = json::object();
  std::ofstream(manifest()) << j.dump(2);
  std::cout << "Created mplx.json\n";
}

static void write_lock(const json& m){
  json l;
  l["name"] = m.value("name","app");
  l["resolved"] = m["dependencies"];
  std::ofstream(lockfile()) << l.dump(2);
  std::cout << "Wrote mplx.lock\n";
}

int main(int argc, char** argv){
  if (argc < 2){
    std::cerr << "mplx-pkg <init|add|list|restore> [args]\n";
    return 2;
  }
  std::string cmd = argv[1];
  if (cmd == "init"){
    if (fs::exists(manifest())) { std::cerr << "mplx.json already exists\n"; return 1; }
    write_manifest(); return 0;
  }
  if (!fs::exists(manifest())){ std::cerr << "no mplx.json, run 'mplx-pkg init'\n"; return 1; }
  std::ifstream in(manifest());
  json m = json::parse(in, nullptr, true, true);

  if (cmd == "add"){
    if (argc < 3){ std::cerr << "mplx-pkg add <name>@<version>\n"; return 2; }
    std::string spec = argv[2];
    auto at = spec.find('@');
    std::string name = (at==std::string::npos)? spec : spec.substr(0,at);
    std::string ver  = (at==std::string::npos)? "*"  : spec.substr(at+1);
    m["dependencies"][name] = ver;
    std::ofstream(manifest()) << m.dump(2);
    std::cout << "Added " << name << "@" << ver << " to mplx.json\n";
    write_lock(m);
    return 0;
  } else if (cmd == "list"){
    for (auto& [k,v] : m["dependencies"].items()){
      std::cout << k << " = " << v << "\n";
    }
    return 0;
  } else if (cmd == "restore"){
    write_lock(m);
    return 0;
  } else {
    std::cerr << "Unknown cmd: " << cmd << "\n";
    return 2;
  }
}
'@ | Out-File -Encoding utf8 src-cpp/mplx-pkg/main.cpp -NoNewline

# 7) Улучшаем LSP: реально вызывает CLI `mplx --check` и маппит diagnostics
@'
import {
  createConnection, ProposedFeatures,
  InitializeParams, TextDocuments, TextDocumentSyncKind,
  Diagnostic, DiagnosticSeverity
} from "vscode-languageserver/node.js";
import { TextDocument } from "vscode-languageserver-textdocument";
import { spawn } from "node:child_process";
import { tmpdir } from "node:os";
import { writeFileSync } from "node:fs";
import { join } from "node:path";

const connection = createConnection(ProposedFeatures.all);
const documents: TextDocuments<TextDocument> = new TextDocuments(TextDocument);

connection.onInitialize((_params: InitializeParams) => {
  return { capabilities: { textDocumentSync: TextDocumentSyncKind.Incremental } };
});

documents.onDidChangeContent(change => {
  const uri = change.document.uri;
  const text = change.document.getText();

  // write temp file
  const p = join(tmpdir(), "mplx_" + Math.random().toString(36).slice(2) + ".mplx");
  writeFileSync(p, text, "utf8");

  const cli = spawn("build/dev/src-cpp/tools/mplx/mplx", ["--check", p], { shell: true });
  let out = ""; let err = "";
  cli.stdout.on("data", d => out += d.toString());
  cli.stderr.on("data", d => err += d.toString());
  cli.on("close", (_code) => {
    let diags: Diagnostic[] = [];
    try{
      const j = JSON.parse(out);
      for (const msg of (j.diagnostics ?? [])){
        // у нас сейчас только текст, без точных позиций — кладём в (0,0)
        diags.push({
          message: msg.toString(),
          range: { start: { line: 0, character: 0 }, end: { line: 0, character: 1 } },
          severity: DiagnosticSeverity.Error,
          source: "mplx"
        });
      }
    } catch {
      if (err.trim().length){
        diags.push({
          message: err.trim(),
          range: { start: { line: 0, character: 0 }, end: { line: 0, character: 1 } },
          severity: DiagnosticSeverity.Error,
          source: "mplx"
        });
      }
    }
    connection.sendDiagnostics({ uri, diagnostics: diags });
  });
});

documents.listen(connection);
connection.listen();
'@ | Out-File -Encoding utf8 src/Mplx.Lsp/src/server.ts -NoNewline

# 8) README апдейт (короткий how-to)
@'
# MPLX – v0.1.1

- C++20 core: lexer/parser -> bytecode compiler -> stack VM
- Tools: `mplx` (CLI), `mplx-net` (binary framed echo server), `mplx-pkg` (simple package manifest/lock)
- LSP/VSCode: TS-based server & extension

## Build
cmake --preset dev
cmake --build --preset dev
ctest --test-dir build/dev -C RelWithDebInfo --output-on-failure
'@ | Out-File -Encoding utf8 README.md -NoNewline

# 9) git commit
git add -A
git commit -m "feat(net,pkg,lsp): add asio echo server, simple package manager, hook LSP to CLI; fix compiler '!='"


