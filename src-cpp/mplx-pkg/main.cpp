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