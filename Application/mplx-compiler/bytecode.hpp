#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <algorithm>

namespace mplx {

enum Op : uint8_t {
  OP_PUSH_CONST,
  OP_LOAD_LOCAL,
  OP_STORE_LOCAL,
  OP_ADD, OP_SUB, OP_MUL, OP_DIV,
  OP_NEG,
  OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE,
  OP_JMP,
  OP_JMP_IF_FALSE,
  OP_CALL,
  OP_RET,
  OP_POP,
  OP_HALT
};

struct FuncMeta { std::string name; uint32_t entry{0}; uint8_t arity{0}; uint16_t locals{0}; };

struct Bytecode {
  std::vector<uint8_t> code;
  std::vector<long long> consts;
  std::vector<FuncMeta> functions;
};

inline std::string dump_bytecode_json(const Bytecode& bc){
  std::string out = "{";
  out += "\"functions\":[";
  for (size_t i=0;i<bc.functions.size();++i){
    const auto& f = bc.functions[i];
    if(i) out += ",";
    out += "{\"name\":\"" + f.name + "\",\"entry\":" + std::to_string(f.entry) + ",\"arity\":" + std::to_string(f.arity) + ",\"locals\":" + std::to_string(f.locals) + "}";
  }
  out += "],\"consts\":[";
  for (size_t i=0;i<bc.consts.size();++i){ if(i) out+=","; out += std::to_string(bc.consts[i]); }
  out += "],\"code\":[";
  for (size_t i=0;i<bc.code.size();++i){ if(i) out+=","; out += std::to_string((unsigned)bc.code[i]); }
  out += "]}";
  return out;
}

inline std::string dump_cfg_json(const Bytecode& bc){
  // Very simple CFG: basic blocks split by leaders; edges by terminator opcodes
  struct Block { uint32_t start{0}; uint32_t end{0}; };
  std::vector<Block> blocks;
  std::vector<uint32_t> leaders;
  auto add_leader = [&](uint32_t ip){
    for (auto v: leaders) if (v==ip) return; leaders.push_back(ip);
  };
  // function entries are leaders
  for (const auto& f : bc.functions) add_leader(f.entry);
  // scan code to find jump targets and fallthroughs
  uint32_t ip = 0;
  while (ip < bc.code.size()){
    Op op = (Op)bc.code[ip++];
    auto read32 = [&](uint32_t &ptr){ uint32_t v = (uint32_t)bc.code[ptr] | ((uint32_t)bc.code[ptr+1]<<8) | ((uint32_t)bc.code[ptr+2]<<16) | ((uint32_t)bc.code[ptr+3]<<24); ptr+=4; return v; };
    if (op == OP_JMP){ uint32_t dst = read32(ip); add_leader(dst); add_leader(ip); }
    else if (op == OP_JMP_IF_FALSE){ uint32_t dst = read32(ip); add_leader(dst); add_leader(ip); }
    else if (op == OP_CALL){ (void)read32(ip); }
    else if (op == OP_PUSH_CONST || op == OP_LOAD_LOCAL || op == OP_STORE_LOCAL){ (void)read32(ip); }
  }
  // sort leaders and form blocks
  std::sort(leaders.begin(), leaders.end());
  leaders.erase(std::unique(leaders.begin(), leaders.end()), leaders.end());
  for (size_t i=0;i<leaders.size();++i){
    uint32_t s = leaders[i];
    uint32_t e = (i+1<leaders.size()? leaders[i+1] : (uint32_t)bc.code.size());
    if (s<e) blocks.push_back(Block{ s, e });
  }
  auto find_block = [&](uint32_t ip0)->int{ for (size_t i=0;i<blocks.size();++i){ if (ip0>=blocks[i].start && ip0<blocks[i].end) return (int)i; } return -1; };
  // edges
  struct Edge { int from{-1}; int to{-1}; };
  std::vector<Edge> edges;
  for (size_t bi=0; bi<blocks.size(); ++bi){
    uint32_t ptr = blocks[bi].end;
    // rewind one instruction to read terminator; approximate by scanning from start
    uint32_t p = blocks[bi].start;
    uint32_t last = p;
    while (p < blocks[bi].end){
      last = p;
      Op op = (Op)bc.code[p++];
      auto read32b = [&](uint32_t &q){ uint32_t v = (uint32_t)bc.code[q] | ((uint32_t)bc.code[q+1]<<8) | ((uint32_t)bc.code[q+2]<<16) | ((uint32_t)bc.code[q+3]<<24); q+=4; return v; };
      if (op == OP_JMP || op == OP_CALL || op == OP_PUSH_CONST || op == OP_LOAD_LOCAL || op == OP_STORE_LOCAL || op == OP_JMP_IF_FALSE){
        if (op==OP_JMP || op==OP_JMP_IF_FALSE || op==OP_CALL || op==OP_PUSH_CONST || op==OP_LOAD_LOCAL || op==OP_STORE_LOCAL) (void)read32b(p);
      }
    }
    Op term = (Op)bc.code[last];
    uint32_t q = last+1;
    if (term == OP_JMP){ uint32_t dst = (uint32_t)bc.code[q] | ((uint32_t)bc.code[q+1]<<8) | ((uint32_t)bc.code[q+2]<<16) | ((uint32_t)bc.code[q+3]<<24); int tb = find_block(dst); if (tb>=0) edges.push_back({(int)bi, tb}); }
    else if (term == OP_JMP_IF_FALSE){ uint32_t dst = (uint32_t)bc.code[q] | ((uint32_t)bc.code[q+1]<<8) | ((uint32_t)bc.code[q+2]<<16) | ((uint32_t)bc.code[q+3]<<24); int tb = find_block(dst); if (tb>=0) edges.push_back({(int)bi, tb}); int fb = find_block(blocks[bi].end); if (fb>=0) edges.push_back({(int)bi, fb}); }
    else if (term == OP_RET || term == OP_HALT){ /* no edges */ }
    else { int fb = find_block(blocks[bi].end); if (fb>=0) edges.push_back({(int)bi, fb}); }
  }
  // serialize
  std::string out = "{";
  out += "\"blocks\":[";
  for (size_t i=0;i<blocks.size();++i){ if(i) out+=","; out += "{\\\"id\\\":" + std::to_string(i) + ",\\\"start\\\":" + std::to_string(blocks[i].start) + ",\\\"end\\\":" + std::to_string(blocks[i].end) + "}"; }
  out += "],\"edges\":[";
  for (size_t i=0;i<edges.size();++i){ if(i) out+=","; out += "{\\\"from\\\":" + std::to_string(edges[i].from) + ",\\\"to\\\":" + std::to_string(edges[i].to) + "}"; }
  out += "]}";
  return out;
}

} 