#include <iostream>
#include <sstream>
#include <string>
#include <vector>

static std::vector<uint8_t> parse_hex(const std::string &s) {
  std::vector<uint8_t> out;
  std::string tok;
  std::istringstream iss(s);
  while (std::getline(iss, tok, '-')) {
    unsigned int v = 0;
    std::stringstream ss;
    ss << std::hex << tok;
    ss >> v;
    out.push_back((uint8_t)v);
  }
  return out;
}

int main(int argc, char **argv) {
  if (argc < 4) {
    std::cerr << "Usage: mplx-netclient --ping <host> <port>\n"
                 "       mplx-netclient --health <host> <port>\n"
                 "       mplx-netclient --send <host> <port> <hexpayload>\n";
    return 2;
  }
  std::string mode = argv[1], host = argv[2];
  uint16_t port   = (uint16_t)std::stoi(argv[3]);
  std::string hex = argc > 4 ? argv[4] : "";

  std::cout << "MPLX netclient - " << mode << " " << host << ":" << port << std::endl;
  if (mode == std::string("--send")) {
    auto payload = parse_hex(hex);
    std::cout << "Payload size: " << payload.size() << " bytes" << std::endl;
  }
  std::cout << "Note: Networking functionality requires asio library" << std::endl;
  return 0;
}
