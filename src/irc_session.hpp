/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <giomm.h>
#include <glibmm.h>
#include <sigc++/sigc++.h>

#include <atomic>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace partyline {

class IrcSession {
 public:
  IrcSession();
  ~IrcSession();

  IrcSession(const IrcSession&) = delete;
  IrcSession& operator=(const IrcSession&) = delete;

  void start(std::string host, guint16 port, bool tls, std::string nick,
             std::string realname);
  void stop();
  bool running() const { return running_.load(); }

  sigc::signal<void, Glib::ustring> signal_line;
  sigc::signal<void> signal_registered;
  sigc::signal<void, Glib::ustring> signal_finished;

 private:
  struct Event {
    enum Type { Line, Registered, Finished } type = Line;
    std::string text;
  };

  void thread_main();
  void enqueue(Event ev);
  void on_dispatch();
  bool write_line(const std::string& line);
  static std::string command_of(const std::string& line);

  std::string host_;
  guint16 port_ = 6697;
  bool tls_ = true;
  std::string nick_;
  std::string realname_;

  std::atomic<bool> running_{false};
  Glib::RefPtr<Gio::Cancellable> cancellable_;
  Glib::RefPtr<Gio::OutputStream> out_;
  std::mutex out_mu_;
  std::thread thread_;

  std::mutex q_mu_;
  std::queue<Event> q_;
  Glib::Dispatcher dispatcher_;
};

}  // namespace partyline
