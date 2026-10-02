/* SPDX-License-Identifier: Unlicense */

#pragma once

/* A one-client IRC server on 127.0.0.1 for headless IrcSession tests. It
 * records every line the client sends and can push lines to the client. */

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <glib.h>

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fake_irc {

class Server {
 public:
  Server()
  {
    listen_fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    ::setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ::bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    ::listen(listen_fd_, 1);
    socklen_t len = sizeof(addr);
    ::getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len);
    port_ = ntohs(addr.sin_port);
    thread_ = std::thread([this]() { run(); });
  }

  ~Server()
  {
    stop_.store(true);
    ::shutdown(listen_fd_, SHUT_RDWR);
    ::close(listen_fd_);
    {
      std::lock_guard<std::mutex> lock(mu_);
      if (client_fd_ >= 0)
        ::shutdown(client_fd_, SHUT_RDWR);
    }
    if (thread_.joinable())
      thread_.join();
    if (client_fd_ >= 0)
      ::close(client_fd_);
  }

  unsigned short port() const
  {
    return port_;
  }

  /* Raw bytes the client wrote, split on CRLF exactly as a server would. */
  std::vector<std::string> lines()
  {
    std::lock_guard<std::mutex> lock(mu_);
    return lines_;
  }

  void send(const std::string& line)
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (client_fd_ < 0)
      return;
    const std::string wire = line + "\r\n";
    size_t done = 0;
    while (done < wire.size()) {
      const ssize_t n = ::write(client_fd_, wire.data() + done, wire.size() - done);
      if (n <= 0)
        return;
      done += static_cast<size_t>(n);
    }
  }

  bool connected()
  {
    std::lock_guard<std::mutex> lock(mu_);
    return client_fd_ >= 0;
  }

 private:
  void run()
  {
    const int fd = ::accept(listen_fd_, nullptr, nullptr);
    if (fd < 0)
      return;
    {
      std::lock_guard<std::mutex> lock(mu_);
      client_fd_ = fd;
    }
    std::string buf;
    char tmp[4096];
    while (!stop_.load()) {
      const ssize_t n = ::read(fd, tmp, sizeof(tmp));
      if (n <= 0)
        break;
      buf.append(tmp, static_cast<size_t>(n));
      size_t pos;
      while ((pos = buf.find("\r\n")) != std::string::npos) {
        std::lock_guard<std::mutex> lock(mu_);
        lines_.push_back(buf.substr(0, pos));
        buf.erase(0, pos + 2);
      }
    }
  }

  int listen_fd_ = -1;
  int client_fd_ = -1;
  unsigned short port_ = 0;
  std::atomic<bool> stop_{false};
  std::mutex mu_;
  std::vector<std::string> lines_;
  std::thread thread_;
};

/* Run the default main context until pred() holds or ~3 s pass. */
inline bool pump_until(const std::function<bool()>& pred, int ms = 3000)
{
  for (int waited = 0; waited < ms; waited += 5) {
    while (g_main_context_iteration(nullptr, false)) {
    }
    if (pred())
      return true;
    g_usleep(5000);
  }
  while (g_main_context_iteration(nullptr, false)) {
  }
  return pred();
}

}  // namespace fake_irc
