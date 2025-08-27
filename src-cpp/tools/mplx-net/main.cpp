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