#include <asio.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include "../../mplx-net/net.hpp"

static std::vector<uint8_t> parse_hex(const std::string& s){
  std::vector<uint8_t> out; std::string tok; std::istringstream iss(s);
  while (std::getline(iss, tok, '-')) {
    unsigned int v=0; std::stringstream ss; ss << std::hex << tok; ss >> v; out.push_back((uint8_t)v);
  }
  return out;
}

int main(int argc, char** argv){
  if (argc < 5){
    std::cerr << "Usage: mplx-netclient --send <host> <port> <hexpayload>\n";
    return 2;
  }
  std::string mode = argv[1], host=argv[2]; uint16_t port = (uint16_t)std::stoi(argv[3]);
  if (mode != std::string("--send")) { std::cerr << "Unknown mode\n"; return 2; }
  std::string hex = argv[4];

  try{
    asio::io_context io;
    asio::ip::tcp::socket sock(io);
    sock.connect({asio::ip::make_address(host), port});
    mplx::net::Frame f; f.msgType=0; f.payload = parse_hex(hex);
    asio::error_code ec;
    mplx::net::write_frame(sock, f, ec);
    if (ec) { std::cerr << "write error: " << ec.message() << "\n"; return 1; }
    mplx::net::Frame r;
    if (!mplx::net::read_frame(sock, r, ec)) { std::cerr << "read error: " << ec.message() << "\n"; return 1; }
    std::cout << "reply type=" << (int)r.msgType << " size=" << r.payload.size() << "\n";
    return 0;
  } catch (const std::exception& ex){
    std::cerr << "client error: " << ex.what() << "\n"; return 1;
  }
}


