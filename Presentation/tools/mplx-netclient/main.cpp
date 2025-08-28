#include <asio.hpp>
#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include "../../../Infrastructure/mplx-net/net.hpp"

static std::vector<uint8_t> parse_hex(const std::string& s){
	std::vector<uint8_t> out; std::string tok; std::istringstream iss(s);
	while (std::getline(iss, tok, '-')) {
		unsigned int v=0; std::stringstream ss; ss << std::hex << tok; ss >> v; out.push_back((uint8_t)v);
	}
	return out;
}

int main(int argc, char** argv){
	if (argc < 4){
		std::cerr << "Usage: mplx-netclient --ping <host> <port>\n"
					  "       mplx-netclient --health <host> <port>\n"
					  "       mplx-netclient --send <host> <port> <hexpayload>\n";
		return 2;
	}
	std::string mode = argv[1], host=argv[2]; uint16_t port = (uint16_t)std::stoi(argv[3]);
	std::string hex = argc>4? argv[4] : "";

	try{
		asio::io_context io;
		asio::ip::tcp::socket sock(io);
		sock.connect({asio::ip::make_address(host), port});
		mplx::net::Frame f;
		if (mode == std::string("--ping")) { f = mplx::net::EchoServer::Ping(); }
		else if (mode == std::string("--health")) { f.msgType=0x02; }
		else if (mode == std::string("--send")) { f.msgType=0; f.payload = parse_hex(hex); }
		else { std::cerr << "Unknown mode\n"; return 2; }
		asio::error_code ec;
		mplx::net::write_frame(sock, f, ec);
		if (ec) { std::cerr << "write error: " << ec.message() << "\n"; return 1; }
		mplx::net::Frame r;
		if (!mplx::net::read_frame(sock, r, ec)) { std::cerr << "read error: " << ec.message() << "\n"; return 1; }
		std::cout << "reply type=" << (int)r.msgType << " size=" << r.payload.size() << "\n";
		if (r.msgType==0x81 || r.msgType==0x82){ std::cout << std::string(r.payload.begin(), r.payload.end()) << "\n"; }
		return 0;
	} catch (const std::exception& ex){
		std::cerr << "client error: " << ex.what() << "\n"; return 1;
	}
}


