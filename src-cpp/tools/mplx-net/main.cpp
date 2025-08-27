#include "../../mplx-net/net.hpp"
#include <asio.hpp>
#include <iostream>

int main(int argc, char** argv){
  if (argc < 4){
    std::cerr << "Usage: mplx-net --serve <host> <port>\n"
                 "       mplx-net --send <host> <port> <payload>\n";
    return 2;
  }
  std::string mode = argv[1];
  std::string host = argv[2];
  uint16_t port = static_cast<uint16_t>(std::stoi(argv[3]));

  try{
    if (mode == std::string("--serve")){
      asio::io_context io;
      mplx::net::EchoServer srv(io, host, port);
      srv.Start();
      std::cout << "MPLX echo server on " << host << ":" << port << std::endl;
      io.run();
      return 0;
    } else if (mode == std::string("--send")){
      if (argc < 5){ std::cerr << "need <payload> for --send\n"; return 2; }
      std::string payload = argv[4];
      asio::io_context io;
      asio::ip::tcp::socket sock(io);
      asio::ip::tcp::endpoint ep(asio::ip::make_address(host), port);
      sock.connect(ep);
      mplx::net::Frame f; f.msgType = 0; f.payload.assign(payload.begin(), payload.end());
      asio::error_code ec;
      mplx::net::write_frame(sock, f, ec);
      if (ec){ std::cerr << "write error: " << ec.message() << "\n"; return 1; }
      mplx::net::Frame r;
      if (!mplx::net::read_frame(sock, r, ec) || ec){ std::cerr << "read error: " << ec.message() << "\n"; return 1; }
      std::string out(r.payload.begin(), r.payload.end());
      std::cout << out << std::endl;
      return 0;
    } else {
      std::cerr << "Unknown mode: " << mode << "\n";
      return 2;
    }
  } catch (const std::exception& ex){
    std::cerr << "Error: " << ex.what() << "\n";
    return 1;
  }
}