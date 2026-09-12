/* SPDX-License-Identifier: Unlicense */

#include "irc_session.hpp"
#include "config.hpp"

namespace partyline {
namespace {

void trim_cr(std::string& s)
{
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n'))
    s.pop_back();
}

std::string prefix_nick(const std::string& line)
{
  if (line.empty() || line[0] != ':')
    return {};
  const auto bang = line.find('!');
  const auto sp = line.find(' ');
  auto end = bang;
  if (end == std::string::npos || (sp != std::string::npos && bang > sp))
    end = sp;
  if (end == std::string::npos)
    return {};
  return line.substr(1, end - 1);
}

std::string ping_payload(const std::string& line)
{
  auto s = line;
  if (!s.empty() && s[0] == ':') {
    const auto sp = s.find(' ');
    s = sp == std::string::npos ? std::string() : s.substr(sp + 1);
  }
  if (s.compare(0, 5, "PING ") == 0)
    s = s.substr(5);
  return s;
}

bool is_ctcp_version(const std::string& line, const std::string& cmd)
{
  if (cmd != "PRIVMSG")
    return false;
  const auto ctcp = line.find('\x01');
  if (ctcp == std::string::npos)
    return false;
  return line.compare(ctcp, 8, "\x01VERSION") == 0;
}

}  // namespace

IrcSession::IrcSession()
{
  dispatcher_.connect(sigc::mem_fun(*this, &IrcSession::on_dispatch));
}

IrcSession::~IrcSession()
{
  stop();
}

void IrcSession::start(std::string host, guint16 port, bool tls, std::string nick,
                       std::string realname)
{
  stop();
  host_ = std::move(host);
  port_ = port == 0 ? (tls ? 6697 : 6667) : port;
  tls_ = tls;
  nick_ = std::move(nick);
  realname_ = realname.empty() ? nick_ : std::move(realname);
  cancellable_ = Gio::Cancellable::create();
  running_.store(true);
  thread_ = std::thread(&IrcSession::thread_main, this);
}

void IrcSession::stop()
{
  if (cancellable_)
    cancellable_->cancel();
  {
    std::lock_guard<std::mutex> lock(out_mu_);
    if (out_) {
      try {
        const std::string quit = "QUIT :Partyline\r\n";
        out_->write(quit.data(), quit.size());
      } catch (...) {
      }
      try {
        out_->close();
      } catch (...) {
      }
      out_.reset();
    }
  }
  if (thread_.joinable())
    thread_.join();
  running_.store(false);
  cancellable_.reset();
}

void IrcSession::enqueue(Event ev)
{
  {
    std::lock_guard<std::mutex> lock(q_mu_);
    q_.push(std::move(ev));
  }
  dispatcher_.emit();
}

void IrcSession::on_dispatch()
{
  for (;;) {
    Event ev;
    {
      std::lock_guard<std::mutex> lock(q_mu_);
      if (q_.empty())
        return;
      ev = std::move(q_.front());
      q_.pop();
    }
    switch (ev.type) {
      case Event::Line:
        signal_line.emit(ev.text);
        break;
      case Event::Registered:
        signal_registered.emit();
        break;
      case Event::Finished:
        running_.store(false);
        signal_finished.emit(ev.text);
        break;
    }
  }
}

bool IrcSession::write_line(const std::string& line)
{
  std::lock_guard<std::mutex> lock(out_mu_);
  if (!out_)
    return false;
  try {
    std::string wire = line;
    if (wire.size() < 2 || wire.substr(wire.size() - 2) != "\r\n")
      wire += "\r\n";
    gsize n = 0;
    out_->write_all(wire.data(), wire.size(), n, cancellable_);
    return n == wire.size();
  } catch (...) {
    return false;
  }
}

std::string IrcSession::command_of(const std::string& line)
{
  std::string s = line;
  if (!s.empty() && s[0] == ':') {
    const auto sp = s.find(' ');
    if (sp == std::string::npos)
      return {};
    s = s.substr(sp + 1);
  }
  const auto sp = s.find(' ');
  return sp == std::string::npos ? s : s.substr(0, sp);
}

void IrcSession::thread_main()
{
  std::string finish = "Disconnected.";
  try {
    auto client = Gio::SocketClient::create();
    client->set_tls(tls_);
    client->set_timeout(30);
    enqueue({Event::Line, std::string(tls_ ? "Connecting (TLS) to " : "Connecting to ") + host_ +
                              ":" + std::to_string(port_) + " as " + nick_ + "…"});

    auto conn = client->connect_to_host(host_, port_, cancellable_);
    /* Connect may use a 30s timeout. Idle IRC is quiet; do not time out reads. */
    if (auto sock = conn->get_socket())
      sock->set_timeout(0);
    {
      std::lock_guard<std::mutex> lock(out_mu_);
      out_ = conn->get_output_stream();
    }

    if (!write_line("NICK " + nick_) ||
        !write_line("USER " + nick_ + " 0 * :" + realname_)) {
      finish = "Could not send NICK/USER.";
    } else {
      auto in = Gio::DataInputStream::create(conn->get_input_stream());
      in->set_newline_type(Gio::DATA_STREAM_NEWLINE_TYPE_ANY);
      std::string line;
      while (in->read_line(line, cancellable_)) {
        trim_cr(line);
        if (line.empty())
          continue;

        const std::string cmd = command_of(line);
        if (cmd == "PING")
          write_line("PONG " + ping_payload(line));
        else if (is_ctcp_version(line, cmd)) {
          const std::string from = prefix_nick(line);
          if (!from.empty())
            write_line("NOTICE " + from + " :\x01VERSION Partyline " VERSION "\x01");
        }

        enqueue({Event::Line, line});

        if (cmd == "001")
          enqueue({Event::Registered, {}});
        if (cmd == "ERROR")
          finish = line;
      }
    }
  } catch (const Glib::Error& e) {
    if (!(cancellable_ && cancellable_->is_cancelled()))
      finish = e.what();
  } catch (const std::exception& e) {
    finish = e.what();
  }

  {
    std::lock_guard<std::mutex> lock(out_mu_);
    out_.reset();
  }
  running_.store(false);
  enqueue({Event::Finished, finish});
}

}  // namespace partyline
