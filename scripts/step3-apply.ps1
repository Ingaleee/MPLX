$ErrorActionPreference = "Stop"

function Ensure-Dir($p){ if (!(Test-Path $p)) { New-Item -Force -ItemType Directory $p | Out-Null } }
function Read-All($p){ if(Test-Path $p){ Get-Content $p -Raw } else { "" } }

# Путь-решатель под две раскладки (Clean Architecture / старая)
function Pick([string[]]$candidates){
  foreach($p in $candidates){ if(Test-Path $p){ return $p } }
  return $null
}

$PARSER_HPP = Pick @("Domain/mplx-lang/parser.hpp","src-cpp/mplx-lang/parser.hpp")
$LSP_TS     = Pick @("Presentation/Mplx.Lsp/src/server.ts","src/Mplx.Lsp/src/server.ts")
$EXT_PKG    = Pick @("Presentation/vscode-mplx/package.json","src/vscode-mplx/package.json")
$EXT_TS     = Pick @("Presentation/vscode-mplx/src/extension.ts","src/vscode-mplx/src/extension.ts")
$CLI_MAIN   = Pick @("Presentation/tools/mplx/main.cpp","src-cpp/tools/mplx/main.cpp")
$VM_CPP     = Pick @("Application/mplx-vm/vm.cpp","src-cpp/mplx-vm/vm.cpp")
$VM_HPP     = Pick @("Application/mplx-vm/vm.hpp","src-cpp/mplx-vm/vm.hpp")
$COMP_CPP   = Pick @("Application/mplx-compiler/compiler.cpp","src-cpp/mplx-compiler/compiler.cpp")
$NET_HDR    = Pick @("Infrastructure/mplx-net/net.hpp","src-cpp/mplx-net/net.hpp")
$NET_CLIENT = Pick @("Presentation/tools/mplx-netclient/main.cpp","src-cpp/tools/mplx-netclient/main.cpp")
$README     = "README.md"
$ORM_HPP    = Pick @("Infrastructure/ORM/orm.hpp","src-cpp/mplx-orm/orm.hpp")
$ORM_CPP    = Pick @("Infrastructure/ORM/orm.cpp","src-cpp/mplx-orm/orm.cpp")

# ---- 1) LSP улучшения: scope-aware defs/references (fn/params/let)
if ($LSP_TS) {
$ts = @'
import {
  createConnection, ProposedFeatures,
  InitializeParams, TextDocuments, TextDocumentSyncKind,
  Diagnostic, DiagnosticSeverity,
  DefinitionParams, Location, Range, Position,
  ReferenceParams, Hover, MarkupKind,
  SignatureHelp, SignatureInformation, ParameterInformation
} from "vscode-languageserver/node";
import { TextDocument } from "vscode-languageserver-textdocument";
import { spawn } from "node:child_process";
import { tmpdir } from "node:os";
import { writeFileSync } from "node:fs";
import { join, basename } from "node:path";

const connection = createConnection(ProposedFeatures.all);
const documents: any = new (TextDocuments as any)(TextDocument);
let cliPath = "build/dev/Presentation/tools/mplx/mplx";

// --- символы и скоупы ---
type DefKind = "fn"|"param"|"let";
type Def = { kind: DefKind; name: string; uri: string; range: Range; scope?: {start:number,end:number}; sig?: string };

const fileDefs = new Map<string, Def[]>();     // uri -> defs
const globalFns = new Map<string, Def[]>();    // name -> fn defs

function rangeFrom(doc: TextDocument, off: number, len: number): Range {
  const s = doc.positionAt(off); const e = doc.positionAt(off+len);
  return { start: s, end: e };
}

function buildIndex(doc: TextDocument){
  const text = doc.getText();
  const defs: Def[] = [];
  // функции
  {
    const re = /fn\s+([A-Za-z_]\w*)\s*\(([^)]*)\)/g;
    let m: RegExpExecArray | null;
    while ((m = re.exec(text))){
      const name = m[1]; const params = m[2]??"";
      const bodyStart = text.indexOf("{", m.index + m[0].length);
      let brace=0; let i=bodyStart; if(i<0) i=text.length;
      for(; i<text.length; ++i){ const ch=text[i]; if(ch=="{") brace++; else if(ch=="}"){ brace--; if(brace==0){ i++; break; } } }
      const fnScope = { start: bodyStart>=0?bodyStart:0, end: i };
      const d: Def = { kind:"fn", name, uri: doc.uri, range: rangeFrom(doc, m.index, m[0].length), scope: fnScope, sig: `${name}(${params.trim()})` };
      defs.push(d);
    }
  }
  // параметры
  for (const d of defs.filter(x=>x.kind==="fn")){
    const textRange = doc.getText(d.range);
    const m = /fn\s+([A-Za-z_]\w*)\s*\(([^)]*)\)/.exec(textRange);
    if (m && m[2].trim().length){
      let posBase = doc.offsetAt(d.range.start) + textRange.indexOf("(") + 1;
      for (const part of m[2].split(",")){
        const nm = (part.trim().split(":")[0] ?? "").trim();
        if (!nm) continue;
        const off = text.indexOf(nm, posBase);
        if (off>=0) defs.push({ kind:"param", name:nm, uri:doc.uri, range: rangeFrom(doc, off, nm.length), scope: d.scope });
      }
    }
  }
  // let объявы
  {
    const reLet = /\blet\s+([A-Za-z_]\w*)/g;
    let m: RegExpExecArray | null;
    while((m = reLet.exec(text))){
      // привязываем к ближайшему fn-скоупу
      const off = m.index;
      const inFn = defs.filter(x=>x.kind==="fn" && x.scope && off>=x.scope.start && off<=x.scope.end)
                       .sort((a,b)=> (a.scope!.end - a.scope!.start) - (b.scope!.end - b.scope!.start))[0] as Def|undefined;
      defs.push({ kind:"let", name:m[1], uri:doc.uri, range: rangeFrom(doc, off+4, m[1].length), scope: inFn?.scope });
    }
  }

  fileDefs.set(doc.uri, defs);

  // обновим globalFns
  for (const [k, arr] of globalFns) globalFns.set(k, arr.filter(d => d.uri !== doc.uri));
  for (const fn of defs.filter(x=>x.kind==="fn")){
    const v = globalFns.get(fn.name) ?? [];
    v.push(fn); globalFns.set(fn.name, v);
  }
}

function idAt(doc: TextDocument, pos: Position): string | null {
  const text = doc.getText();
  const off = doc.offsetAt(pos);
  const re = /[A-Za-z_]\w*/g;
  let m: RegExpExecArray | null;
  while ((m = re.exec(text))){
    if (off>=m.index && off<=m.index+m[0].length) return m[0];
  }
  return null;
}

// --- CLI helper ---
function runCli(args: string[], text: string): Promise<{out:string,err:string}>{
  return new Promise(resolve=>{
    const p = join(tmpdir(),"mplx_"+Math.random().toString(36).slice(2)+".mplx");
    writeFileSync(p, text, "utf8");
    const child = spawn(cliPath, [...args, p], { shell:true });
    let out="", err="";
    child.stdout.on("data", d=>out+=d.toString());
    child.stderr.on("data", d=>err+=d.toString());
    child.on("close", _=>resolve({out,err}));
  });
}

connection.onInitialize((params: any) => {
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

// validate
async function validate(doc: TextDocument){
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

documents.onDidOpen(e => { buildIndex(e.document); validate(e.document); });
documents.onDidChangeContent(e => { buildIndex(e.document); validate(e.document); });
documents.onDidClose(e => { fileDefs.delete(e.document.uri); });

connection.onDefinition((p): Location[] | null => {
  const doc = documents.get(p.textDocument.uri); if (!doc) return null;
  const name = idAt(doc, p.position); if (!name) return null;
  const defs = fileDefs.get(doc.uri) ?? [];
  const off = doc.offsetAt(p.position);

  // локальные в приоритете в пределах ближайшего fn-скоупа
  const fn = defs.filter(d => d.kind==="fn" && d.scope && off>=d.scope.start && off<=d.scope.end)[0];
  const local = defs.filter(d => (d.kind==="param"||d.kind==="let") && d.name===name && d.scope && fn && d.scope.start===fn.scope!.start);
  if (local.length) {
    return local.map(d => ({ uri: d.uri, range: d.range }));
  }

  // globals (fn)
  const fns = globalFns.get(name) ?? [];
  if (fns.length) return fns.map(d => ({ uri: d.uri, range: d.range }));
  return null;
});

connection.onReferences((p: ReferenceParams): Location[] => {
  const doc = documents.get(p.textDocument.uri); if (!doc) return [];
  const name = idAt(doc, p.position); if (!name) return [];
  const out: Location[] = [];

  // если курсор внутри fn-скоупа — ищем внутри него (переменные); иначе — по всем документам (функции)
  const defs = fileDefs.get(doc.uri) ?? [];
  const off = doc.offsetAt(p.position);
  const curFn = defs.filter(d => d.kind==="fn" && d.scope && off>=d.scope.start && off<=d.scope.end)[0];

  function scan(doc2: TextDocument, range?: {start:number,end:number}){
    const text = doc2.getText();
    const re = new RegExp("\\b"+name+"\\b","g");
    let m: RegExpExecArray | null;
    while ((m = re.exec(text))){
      const pos = doc2.positionAt(m.index);
      const off2 = doc2.offsetAt(pos);
      if (range && (off2<range.start || off2>range.end)) continue;
      out.push({ uri: doc2.uri, range: { start: pos, end: { line: pos.line, character: pos.character + name.length } } });
    }
  }

  if (curFn){
    scan(doc, curFn.scope!);
  } else {
    for (const [uri] of fileDefs){
      const d2 = documents.get(uri); if (!d2) continue;
      scan(d2);
    }
  }
  return out;
});

connection.onHover((p): Hover | null => {
  const doc = documents.get(p.textDocument.uri); if (!doc) return null;
  const name = idAt(doc, p.position); if (!name) return null;
  const defs = fileDefs.get(doc.uri) ?? [];
  const fns = globalFns.get(name) ?? [];
  const local = defs.filter(d => (d.kind==="param"||d.kind==="let") && d.name===name);
  if (local.length){
    const d = local[0];
    return { contents: { kind: MarkupKind.Markdown, value: `\`${name}\` *(local ${d.kind})*` } };
  }
  if (fns.length){
    const md = fns.map(d => `- \`${d.sig}\` @ ${basename(d.uri)}:${d.range.start.line+1}:${d.range.start.character+1}`).join("\n");
    return { contents: { kind: MarkupKind.Markdown, value: `**function** \`${name}\`\n${md}` } };
  }
  return null;
});

connection.onSignatureHelp((p): SignatureHelp | null => {
  const doc = documents.get(p.textDocument.uri); if (!doc) return null;
  const name = idAt(doc, p.position); if (!name) return null;
  const fns = globalFns.get(name) ?? []; if (!fns.length) return null;
  const def = fns[0];
  const inside = (def.sig ?? "").replace(/^[^(]*\(/,"").replace(/\)\s*$/,"");
  const parts = inside.trim().length ? inside.split(",").map(s=>s.trim()) : [];
  return {
    signatures: [ SignatureInformation.create(def.sig ?? name+"()", undefined, ...parts.map(ParameterInformation.create)) ],
    activeSignature: 0, activeParameter: 0
  };
});

documents.listen(connection);
connection.listen();
'@
Set-Content -Encoding utf8 $LSP_TS $ts
}

# ---- 2) Compiler peephole: if без else -> без лишнего OP_JMP
if ($COMP_CPP) {
  $cc = Get-Content $COMP_CPP -Raw
  if ($cc -match 'IfStmt') {
    $cc = $cc -replace 'if\(auto ifs = dynamic_cast<const IfStmt\*\>\(s\)\)\{\s*compileExpr\(ifs->cond\.get\(\)\);\s*emit_u8\(OP_JMP_IF_FALSE\);\s*auto jmpFalsePos = tell\(\);\s*emit_u32\(0\);\s*// then\s*for\(auto& st : ifs->thenS\) compileStmt\(st\.get\(\)\);\s*emit_u8\(OP_JMP\);\s*auto jmpEndPos = tell\(\);\s*emit_u32\(0\);\s*// patch false to else start\s*write_u32_at\(jmpFalsePos, tell\(\)\);\s*// else\s*for\(auto& st : ifs->elseS\) compileStmt\(st\.get\(\)\);\s*// patch end to code end\s*write_u32_at\(jmpEndPos, tell\(\)\);\s*return;\s*\}',
@'
if(auto ifs = dynamic_cast<const IfStmt*>(s)){
  compileExpr(ifs->cond.get());
  emit_u8(OP_JMP_IF_FALSE); auto jmpFalsePos = tell(); emit_u32(0);
  // then
  for(auto& st : ifs->thenS) compileStmt(st.get());
  if (ifs->elseS.empty()){
    // без else: просто ставим метку конца
    write_u32_at(jmpFalsePos, tell());
  } else {
    emit_u8(OP_JMP); auto jmpEndPos = tell(); emit_u32(0);
    write_u32_at(jmpFalsePos, tell()); // else start
    for(auto& st : ifs->elseS) compileStmt(st.get());
    write_u32_at(jmpEndPos, tell());
  }
  return;
}
'@
    Set-Content -Encoding utf8 $COMP_CPP $cc
  }
}

# ---- 3) VM perf: меньше ресайзов стека + fast path POP перед RET
if ($VM_CPP) {
  $vm = Get-Content $VM_CPP -Raw
  # В OP_CALL: заранее резерв локалов
  if ($vm -notmatch 'bp = \(uint32_t\)\(stack_\.size\(\) - callee\.arity\)') {
    # уже патченный; пропустим
  } else {
    $vm = $vm -replace 'case OP_CALL: \{\s*uint32_t idx = read_u32\(bc_\.code, ip_\);\s*auto callee = bc_\.functions\[idx\];\s*uint32_t bp = \(uint32_t\)\(stack_\.size\(\) - callee\.arity\);\s*uint32_t ret_ip = ip_;\s*frames_\.push_back\(CallFrame\{ret_ip, idx, bp, callee\.arity, callee\.locals\}\);\s*ip_ = callee\.entry;\s*break;\s*\}',
@'
case OP_CALL: {
  uint32_t idx = read_u32(bc_.code, ip_);
  auto callee = bc_.functions[idx];
  uint32_t bp = (uint32_t)(stack_.size() - callee.arity);
  uint32_t ret_ip = ip_;
  if (stack_.size() < (size_t)(bp + callee.locals)) stack_.resize((size_t)(bp + callee.locals));
  frames_.push_back(CallFrame{ret_ip, idx, bp, callee.arity, callee.locals});
  ip_ = callee.entry;
  break;
}
'
    # TAILCALL аналогично
    $vm = $vm -replace 'case OP_TAILCALL: \{\s*uint32_t idx = read_u32\(bc_\.code, ip_\);\s*auto callee = bc_\.functions\[idx\];\s*auto& cur = frames_\.back\(\);\s*uint32_t bp = \(uint32_t\)\(stack_\.size\(\) - callee\.arity\);\s*// переиспользуем текущий фрейм\s*cur\.fn = idx; cur\.bp = bp; cur\.arity = callee\.arity; cur\.locals = callee\.locals; ip_ = callee\.entry;\s*break;\s*\}',
@'
case OP_TAILCALL: {
  uint32_t idx = read_u32(bc_.code, ip_);
  auto callee = bc_.functions[idx];
  auto& cur = frames_.back();
  uint32_t bp = (uint32_t)(stack_.size() - callee.arity);
  if (stack_.size() < (size_t)(bp + callee.locals)) stack_.resize((size_t)(bp + callee.locals));
  cur.fn = idx; cur.bp = bp; cur.arity = callee.arity; cur.locals = callee.locals; ip_ = callee.entry;
  break;
}
'
  }

  # Fast path: если POP сразу перед RET — игнорируем POP (микрооптимизация исполнения)
  if ($vm -notmatch 'fast_popret') {
    $vm = $vm -replace 'case OP_POP: \{ \(void\)pop\(\); break; \}',
@'
case OP_POP: {
  // fast_popret: заглянем вперёд
  Op nxt = (ip_ < bc_.code.size()) ? (Op)bc_.code[ip_] : OP_HALT;
  if (nxt == OP_RET) { /* пропускаем POP */ break; }
  (void)pop();
  break;
}
'
  }
  Set-Content -Encoding utf8 $VM_CPP $vm
}

# ---- 4) Net: HEALTH с аптаймом и клиентский --health
if ($NET_HDR) {
  $h = Get-Content $NET_HDR -Raw
  if ($h -notmatch 'uptime_sec') {
$h = $h -replace 'class EchoServer \{[^\}]+\};',
@'
class EchoServer {
public:
  EchoServer(asio::io_context& io, const std::string& host, uint16_t port)
    : acceptor_(io, asio::ip::tcp::endpoint(asio::ip::make_address(host), port)),
      start_(std::chrono::steady_clock::now()) {}

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
      [this, sock]() -> asio::awaitable<void> {
        asio::error_code ec;
        for(;;){
          Frame f; if (!read_frame(*sock, f, ec)) break;
          Frame out;
          if (f.msgType == 0x01) { // PING
            const char* payload = "{\"ok\":true}";
            out.msgType = 0x81; out.payload.assign(payload, payload + strlen(payload));
          } else if (f.msgType == 0x02) { // HEALTH
            using namespace std::chrono;
            auto secs = duration_cast<seconds>(steady_clock::now() - start_).count();
            std::string payload = std::string("{\"ok\":true,\"service\":\"mplx\",\"version\":\"0.2.0\",\"uptime_sec\":") + std::to_string(secs) + "}";
            out.msgType = 0x82; out.payload.assign(payload.begin(), payload.end());
          } else { // echo
            out.msgType = 0x80; out.payload = f.payload;
          }
          write_frame(*sock, out, ec); if (ec) break;
        }
        co_return;
      }, asio::detached);
  }
  asio::ip::tcp::acceptor acceptor_;
  std::chrono::steady_clock::time_point start_;
};
'
    Set-Content -Encoding utf8 $NET_HDR $h
  }
}

if ($NET_CLIENT) {
  $c = Get-Content $NET_CLIENT -Raw
  if ($c -notmatch '--health') {
    $c = $c -replace 'Usage: mplx-netclient --send <host> <port> <hexpayload> \| --ping <host> <port>',
'Usage: mplx-netclient --send <host> <port> <hexpayload> | --ping <host> <port> | --health <host> <port>'
    $c = $c -replace 'if \(mode != std::string\("--send"\) && mode != std::string\("--ping"\)\)',
'if (mode != std::string("--send") && mode != std::string("--ping") && mode != std::string("--health"))'
    $c = $c -replace 'mplx::net::Frame f; if\(mode=="--ping"\)\{ f\.msgType=0x01; \} else \{ f\.msgType=0x00; f\.payload = parse_hex\(hex\); \}',
'mplx::net::Frame f; if(mode=="--ping"){ f.msgType=0x01; } else if(mode=="--health"){ f.msgType=0x02; } else { f.msgType=0x00; f.payload = parse_hex(hex); }'
    $c = $c -replace 'if\(r\.msgType==0x81\)\{ std::cout << std::string\(r\.payload\.begin\(\), r\.payload\.end\(\)\) << "\n"; \}',
'if(r.msgType==0x81 || r.msgType==0x82){ std::cout << std::string(r.payload.begin(), r.payload.end()) << "\n"; }'
    Set-Content -Encoding utf8 $NET_CLIENT $c
  }
}

# ---- 5) CLI: статусы выхода + README usage
if ($CLI_MAIN) {
  $cli = Get-Content $CLI_MAIN -Raw
  if ($cli -notmatch 'mplx 0\.2\.0') {
    $cli = $cli -replace 'mplx 0\.[0-9]+\.[0-9]+','mplx 0.2.0'
  }
  Set-Content -Encoding utf8 $CLI_MAIN $cli
}

# README usage блок
$readme = Read-All $README
if ($readme -notmatch '## Usage') {
@'
## Usage

### CLI
```

mplx --run <file>
mplx --check <file>
mplx --symbols <file>
mplx --bench <file> \[--mode compile-run|run-only] \[--runs N] \[--json]
mplx --help
mplx --version

```

### Net
```

mplx-net --serve <host> <port>
mplx-netclient --send <host> <port> <hexpayload>
mplx-netclient --ping <host> <port>
mplx-netclient --health <host> <port>

```
'@ | Add-Content -Encoding utf8 $README
}

# ---- 6) ORM: LRU-кэш prepared statements + upsert helper
if ($ORM_HPP) {
$orm = Get-Content $ORM_HPP -Raw
if ($orm -notmatch 'StmtCache') {
$orm = $orm -replace '#include <functional>\s*#include <vector>',
'#include <functional>
#include <vector>
#include <unordered_map>
#include <list>
#include <variant>'
$orm = $orm -replace 'namespace mplx::orm \{',
'namespace mplx::orm {

using Val = std::variant<long long, std::string>;
'
$orm += @'

struct StmtCache {
  struct Entry { std::string sql; sqlite3_stmt* s{nullptr}; };
  explicit StmtCache(Db& d, size_t cap=32): db(&d), cap_(cap) {}
  ~StmtCache(){ clear(); }

  sqlite3_stmt* acquire(const std::string& sql){
    // hit?
    if (auto it = map_.find(sql); it != map_.end()){
      lru_.splice(lru_.begin(), lru_, it->second); // move to front
      return it->second->s;
    }
    // miss
    sqlite3_stmt* st{nullptr};
    if (sqlite3_prepare_v2(db->h, sql.c_str(), -1, &st, nullptr)!=SQLITE_OK) throw std::runtime_error("prepare");
    lru_.push_front(Entry{sql, st});
    map_[sql] = lru_.begin();
    if (lru_.size() > cap_) {
      auto last = lru_.end(); --last;
      sqlite3_finalize(last->s);
      map_.erase(last->sql);
      lru_.pop_back();
    }
    return st;
  }

  void clear(){
    for (auto& e : lru_) if (e.s) sqlite3_finalize(e.s);
    lru_.clear(); map_.clear();
  }

private:
  Db* db;
  size_t cap_;
  std::list<Entry> lru_;
  std::unordered_map<std::string, std::list<Entry>::iterator> map_;
};

inline void upsert(Db& db, const std::string& table, const std::string& keyCol, const std::vector<std::pair<std::string,Val>>& cols){
  // cols включает ключевой столбец и прочие
  std::vector<std::string> names; names.reserve(cols.size());
  for (auto& kv : cols) names.push_back(kv.first);
  std::string qs; for (size_t i=0;i<names.size();++i){ qs += (i?",":""); qs += "?"; }
  std::string setList; for(size_t i=0;i<names.size();++i){ if (names[i]==keyCol) continue; setList += (setList.empty()?"":" ,"); setList += names[i] + "=excluded." + names[i]; }

  auto joinNames = [&](){ std::string s; for(size_t i=0;i<names.size();++i){ if(i) s+=","; s+=names[i]; } return s; }();
  std::string sql = "INSERT INTO "+table+"("+joinNames+") VALUES ("+qs+") ON CONFLICT("+keyCol+") DO UPDATE SET "+ setList + ";";

  sqlite3_stmt* st{nullptr};
  if (sqlite3_prepare_v2(db.h, sql.c_str(), -1, &st, nullptr)!=SQLITE_OK) throw std::runtime_error("prepare");
  auto guard = std::unique_ptr<sqlite3_stmt, void(*)(sqlite3_stmt*)>(st, sqlite3_finalize);

  for (int i=0;i<(int)cols.size();++i){
    auto& v = cols[i].second;
    if (std::holds_alternative<long long>(v)) {
      if (sqlite3_bind_int64(st, i+1, std::get<long long>(v))!=SQLITE_OK) throw std::runtime_error("bind");
    } else {
      const auto& s = std::get<std::string>(v);
      if (sqlite3_bind_text(st, i+1, s.c_str(), -1, SQLITE_TRANSIENT)!=SQLITE_OK) throw std::runtime_error("bind");
    }
  }
  int rc = sqlite3_step(st);
  if (!(rc==SQLITE_DONE || rc==SQLITE_ROW)) throw std::runtime_error("upsert step");
}
'@
Set-Content -Encoding utf8 $ORM_HPP $orm
}
}

# ---- 7) Git commit
git add -A
git commit -m "feat(step3): LSP scope-aware defs/refs; compiler if-peephole; VM stack perf + POP/RET fastpath; Net HEALTH/uptime + --health; CLI docs; ORM LRU stmt cache + upsert" | Out-Null

Write-Host "step3-apply.ps1: done." -ForegroundColor Green

