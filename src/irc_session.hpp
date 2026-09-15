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
#include <vector>

namespace partyline {

class IrcSession {
 public:
  IrcSession();
  ~IrcSession();

  IrcSession(const IrcSession&) = delete;
  IrcSession& operator=(const IrcSession&) = delete;

  void start(std::string host, guint16 port, bool tls, bool tls_verify, std::string nick,
             std::string realname);
  void stop();
  bool running() const
  {
    return running_.load();
  }
  std::string nick() const;

  void join(const std::string& channel);
  void part(const std::string& channel);
  void privmsg(const std::string& target, const std::string& text);
  void quote(const std::string& raw);
  void change_nick(const std::string& nick);
  void send_lag_ping();
  bool tls() const
  {
    return tls_;
  }
  int lag_ms() const
  {
    return lag_ms_.load();
  }

  sigc::signal<void, Glib::ustring> signal_line;
  sigc::signal<void> signal_registered;
  sigc::signal<void, Glib::ustring> signal_finished;
  sigc::signal<void, Glib::ustring, Glib::ustring, Glib::ustring> signal_privmsg;
  sigc::signal<void, Glib::ustring, Glib::ustring, bool> signal_join;
  sigc::signal<void, Glib::ustring, Glib::ustring, bool> signal_part;
  sigc::signal<void, Glib::ustring> signal_quit_nick;
  sigc::signal<void, Glib::ustring, std::vector<Glib::ustring>> signal_names;
  sigc::signal<void, Glib::ustring, Glib::ustring, bool> signal_nick;
  sigc::signal<void> signal_lag;

 private:
  struct Event {
    enum Type {
      Line,
      Registered,
      Finished,
      Privmsg,
      Join,
      Part,
      QuitNick,
      Names,
      NickChange,
      Lag
    } type = Line;
    std::string text;
    std::string channel;
    std::string nick;
    bool me = false;
    std::vector<std::string> nicks;
  };

  void thread_main();
  void enqueue(Event ev);
  void on_dispatch();
  bool write_line(const std::string& line);
  void handle_line(const std::string& line);
  bool is_me(const std::string& nick) const;

  std::string host_;
  guint16 port_ = 6697;
  bool tls_ = true;
  bool tls_verify_ = true;
  std::string nick_;
  std::string realname_;
  mutable std::mutex nick_mu_;

  std::atomic<bool> running_{false};
  Glib::RefPtr<Gio::Cancellable> cancellable_;
  Glib::RefPtr<Gio::OutputStream> out_;
  Glib::RefPtr<Gio::Socket> sock_;
  std::mutex out_mu_;
  std::thread thread_;

  std::mutex q_mu_;
  std::queue<Event> q_;
  Glib::Dispatcher dispatcher_;

  std::string names_chan_;
  std::vector<std::string> names_acc_;
  std::atomic<int> lag_ms_{-1};
  std::string lag_token_;
  gint64 lag_sent_us_ = 0;
};

}  // namespace partyline
