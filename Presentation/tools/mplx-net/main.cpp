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

  std::cout << "MPLX net tool - " << mode << " " << host << ":" << port << std::endl;
  std::cout << "Note: Networking functionality requires asio library" << std::endl;
  return 0;
}