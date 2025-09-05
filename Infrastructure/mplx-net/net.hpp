#pragma once
#include <asio.hpp>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace mplx::net {

  // varint (LEB128-like for demo): 7 bits per byte, MSB=continue
  inline void write_varint(uint64_t v, std::vector<uint8_t> &out) {
    while (true) {
      uint8_t b = v & 0x7F;
      v >>= 7;
      if (v) {
        out.push_back(b | 0x80);
      } else {
        out.push_back(b);
        break;
      }
    }
  }

  inline bool read_varint(asio::ip::tcp::socket &sock, uint64_t &out, asio::error_code &ec) {
    out       = 0;
    int shift = 0;
    for (int i = 0; i < 10; ++i) {
      uint8_t b;
      size_t n = asio::read(sock, asio::buffer(&b, 1), ec);
      if (ec || n != 1)
        return false;
      out |= uint64_t(b & 0x7F) << shift;
      if ((b & 0x80) == 0)
        return true;
      shift += 7;
    }
    ec = asio::error::operation_aborted; // too long
    return false;
  }

  // frame: varint length | u8 msgType | payload[length-1]
  struct Frame {
    uint8_t msgType{0};
    std::vector<uint8_t> payload;
  };

  inline void write_frame(asio::ip::tcp::socket &sock, const Frame &f, asio::error_code &ec) {
    std::vector<uint8_t> buf;
    std::vector<uint8_t> body;
    body.reserve(1 + f.payload.size());
    body.push_back(f.msgType);
    body.insert(body.end(), f.payload.begin(), f.payload.end());
    write_varint(body.size(), buf);
    buf.insert(buf.end(), body.begin(), body.end());
    asio::write(sock, asio::buffer(buf), ec);
  }

  inline bool read_frame(asio::ip::tcp::socket &sock, Frame &f, asio::error_code &ec) {
    uint64_t len;
    if (!read_varint(sock, len, ec))
      return false;
    if (len == 0) {
      ec = asio::error::operation_aborted;
      return false;
    }
    std::vector<uint8_t> body(len);
    size_t n = asio::read(sock, asio::buffer(body), ec);
    if (ec || n != len)
      return false;
    f.msgType = body[0];
    f.payload.assign(body.begin() + 1, body.end());
    return true;
  }

  // Message types
  // 0x00 echo, 0x01 PING -> 0x81 {ok:true,service,version}, 0x02 HEALTH -> 0x82 {ok:true,service,version,uptime_sec}

  class EchoServer {
  public:
    EchoServer(asio::io_context &io, const std::string &host, uint16_t port)
        : acceptor_(io, asio::ip::tcp::endpoint(asio::ip::make_address(host), port)),
          start_(std::chrono::steady_clock::now()) {}
    static Frame Ping() {
      Frame f;
      f.msgType = 0x01;
      return f;
    }

    void Start() {
      do_accept();
    }

  private:
    void do_accept() {
      auto sock = std::make_shared<asio::ip::tcp::socket>(acceptor_.get_executor());
      acceptor_.async_accept(*sock, [this, sock](const asio::error_code &ec) {
        if (!ec) {
          do_session(sock);
        }
        do_accept();
      });
    }
    void do_session(std::shared_ptr<asio::ip::tcp::socket> sock) {
      auto self = sock;
      asio::co_spawn(acceptor_.get_executor(), [this, self]() -> asio::awaitable<void> {
        asio::error_code ec;
        try{
          for(;;){
            Frame f;
            if (!read_frame(*self, f, ec)) break;
            Frame out;
            if (f.msgType == 0x01){
              // PING
              std::string payload = "{\"ok\":true,\"service\":\"mplx\",\"version\":\"0.2.0\"}";
              out.msgType = 0x81; out.payload.assign(payload.begin(), payload.end());
            } else if (f.msgType == 0x02){
              // HEALTH
              using namespace std::chrono;
              auto secs = duration_cast<seconds>(steady_clock::now() - start_).count();
              std::string payload = std::string("{\"ok\":true,\"service\":\"mplx\",\"version\":\"0.2.0\",\"uptime_sec\":") + std::to_string(secs) + "}";
              out.msgType = 0x82; out.payload.assign(payload.begin(), payload.end());
            } else {
              // echo
              out.msgType = 0x80; out.payload = f.payload;
            }
            write_frame(*self, out, ec);
            if (ec) break;
          }
        } catch(...) {}
        co_return; }, asio::detached);
    }

    asio::ip::tcp::acceptor acceptor_;
    std::chrono::steady_clock::time_point start_;
  };

} // namespace mplx::net