/* SPDX-License-Identifier: Unlicense */

#include "irc_session.hpp"
#include "config.hpp"

#include <glib.h>

namespace partyline {
namespace {

void trim_cr(std::string& s)
{
  while (!s.empty() && (s.back() == '\r' || s.back() == '\n'))
    s.pop_back();
}

struct Parsed {
  std::string nick;
  std::string cmd;
  std::vector<std::string> params;
};

Parsed parse_irc(const std::string& line)
{
  Parsed p;
  size_t i = 0;
  if (!line.empty() && line[0] == ':') {
    const auto sp = line.find(' ');
    const std::string prefix = line.substr(1, sp == std::string::npos ? std::string::npos : sp - 1);
    const auto bang = prefix.find('!');
    p.nick = bang == std::string::npos ? prefix : prefix.substr(0, bang);
    i = sp == std::string::npos ? line.size() : sp + 1;
    while (i < line.size() && line[i] == ' ')
      ++i;
  }
  const auto sp = line.find(' ', i);
  p.cmd = line.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
  i = sp == std::string::npos ? line.size() : sp + 1;
  while (i < line.size()) {
    while (i < line.size() && line[i] == ' ')
      ++i;
    if (i >= line.size())
      break;
    if (line[i] == ':') {
      p.params.push_back(line.substr(i + 1));
      break;
    }
    const auto nsp = line.find(' ', i);
    p.params.push_back(line.substr(i, nsp == std::string::npos ? std::string::npos : nsp - i));
    i = nsp == std::string::npos ? line.size() : nsp + 1;
  }
  return p;
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

bool is_ctcp_version(const std::string& text)
{
  return text.compare(0, 8, "\x01VERSION") == 0;
}

void split_nicks(const std::string& names, std::vector<std::string>& out)
{
  size_t i = 0;
  while (i < names.size()) {
    while (i < names.size() && names[i] == ' ')
      ++i;
    if (i >= names.size())
      break;
    auto nsp = names.find(' ', i);
    if (nsp == std::string::npos)
      nsp = names.size();
    std::string n = names.substr(i, nsp - i);
    if (!n.empty())
      out.push_back(std::move(n));
    i = nsp;
  }
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

std::string IrcSession::nick() const
{
  std::lock_guard<std::mutex> lock(nick_mu_);
  return nick_;
}

void IrcSession::start(std::string host, guint16 port, bool tls, std::string nick,
                       std::string realname)
{
  stop();
  host_ = std::move(host);
  port_ = port == 0 ? (tls ? 6697 : 6667) : port;
  tls_ = tls;
  {
    std::lock_guard<std::mutex> lock(nick_mu_);
    nick_ = std::move(nick);
    realname_ = realname.empty() ? nick_ : std::move(realname);
  }
  names_acc_.clear();
  names_chan_.clear();
  cancellable_ = Gio::Cancellable::create();
  running_.store(true);
  thread_ = std::thread(&IrcSession::thread_main, this);
}

void IrcSession::stop()
{
  /* Do not close() the TLS stream from this thread — GTlsConnection
   * shutdown can sit on the SocketClient I/O timeout (~30s) after the
   * window is already gone. Cancel, drop the TCP socket, join. */
  if (cancellable_)
    cancellable_->cancel();
  {
    std::lock_guard<std::mutex> lock(out_mu_);
    if (sock_) {
      try {
        sock_->set_timeout(1);
      } catch (...) {
      }
      if (out_) {
        try {
          const std::string quit = "QUIT :Partyline\r\n";
          gsize n = 0;
          out_->write_all(quit.data(), quit.size(), n);
        } catch (...) {
        }
      }
      try {
        sock_->shutdown(true, true);
      } catch (...) {
      }
      try {
        sock_->close();
      } catch (...) {
      }
      sock_.reset();
    }
    out_.reset();
  }
  if (thread_.joinable())
    thread_.join();
  running_.store(false);
  cancellable_.reset();
}

void IrcSession::join(const std::string& channel)
{
  if (!channel.empty())
    write_line("JOIN " + channel);
}

void IrcSession::part(const std::string& channel)
{
  if (!channel.empty())
    write_line("PART " + channel);
}

void IrcSession::privmsg(const std::string& target, const std::string& text)
{
  if (target.empty() || text.empty())
    return;
  std::string body = text;
  if (body.size() > 400)
    body.resize(400);
  write_line("PRIVMSG " + target + " :" + body);
}

void IrcSession::quote(const std::string& raw)
{
  if (!raw.empty())
    write_line(raw);
}

void IrcSession::change_nick(const std::string& nick)
{
  if (!nick.empty())
    write_line("NICK " + nick);
}

void IrcSession::send_lag_ping()
{
  lag_sent_us_ = g_get_monotonic_time();
  lag_token_ = std::to_string(lag_sent_us_);
  write_line("PING :" + lag_token_);
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
      case Event::Privmsg:
        signal_privmsg.emit(ev.channel, ev.nick, ev.text);
        break;
      case Event::Join:
        signal_join.emit(ev.channel, ev.nick, ev.me);
        break;
      case Event::Part:
        signal_part.emit(ev.channel, ev.nick, ev.me);
        break;
      case Event::QuitNick:
        signal_quit_nick.emit(ev.nick);
        break;
      case Event::Names: {
        std::vector<Glib::ustring> nicks;
        nicks.reserve(ev.nicks.size());
        for (const auto& n : ev.nicks)
          nicks.emplace_back(n);
        signal_names.emit(ev.channel, nicks);
        break;
      }
      case Event::NickChange:
        signal_nick.emit(ev.nick, ev.text, ev.me);
        break;
      case Event::Lag:
        signal_lag.emit();
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

bool IrcSession::is_me(const std::string& nick) const
{
  std::lock_guard<std::mutex> lock(nick_mu_);
  return g_ascii_strcasecmp(nick.c_str(), nick_.c_str()) == 0;
}

void IrcSession::handle_line(const std::string& line)
{
  const Parsed p = parse_irc(line);
  const std::string& cmd = p.cmd;

  if (cmd == "PING") {
    write_line("PONG " + ping_payload(line));
    enqueue({Event::Line, line, {}, {}, false, {}});
    return;
  }

  if (cmd == "PRIVMSG" && p.params.size() >= 2) {
    const std::string& target = p.params[0];
    const std::string& text = p.params[1];
    if (is_ctcp_version(text)) {
      if (!p.nick.empty())
        write_line(std::string("NOTICE ") + p.nick + " :\x01VERSION Partyline " VERSION "\x01");
      enqueue({Event::Line, line, {}, {}, false, {}});
      return;
    }
    if (!text.empty() && text[0] == '\x01') {
      enqueue({Event::Line, line, {}, {}, false, {}});
      return;
    }
    enqueue({Event::Line, line, {}, {}, false, {}});
    Event ev;
    ev.type = Event::Privmsg;
    ev.channel = target;
    ev.nick = p.nick;
    ev.text = text;
    enqueue(std::move(ev));
    return;
  }

  if (cmd == "JOIN" && !p.params.empty()) {
    Event ev;
    ev.type = Event::Join;
    ev.channel = p.params[0];
    ev.nick = p.nick;
    ev.me = is_me(p.nick);
    enqueue({Event::Line, line, {}, {}, false, {}});
    enqueue(std::move(ev));
    return;
  }

  if ((cmd == "PART" || cmd == "KICK") && !p.params.empty()) {
    Event ev;
    ev.type = Event::Part;
    ev.channel = p.params[0];
    ev.nick = (cmd == "KICK" && p.params.size() >= 2) ? p.params[1] : p.nick;
    ev.me = is_me(ev.nick);
    enqueue({Event::Line, line, {}, {}, false, {}});
    enqueue(std::move(ev));
    return;
  }

  if (cmd == "QUIT") {
    enqueue({Event::Line, line, {}, {}, false, {}});
    Event ev;
    ev.type = Event::QuitNick;
    ev.nick = p.nick;
    enqueue(std::move(ev));
    return;
  }

  if (cmd == "NICK" && !p.params.empty()) {
    Event ev;
    ev.type = Event::NickChange;
    ev.nick = p.nick;
    ev.text = p.params[0];
    ev.me = is_me(p.nick);
    if (ev.me) {
      std::lock_guard<std::mutex> lock(nick_mu_);
      nick_ = ev.text;
    }
    enqueue({Event::Line, line, {}, {}, false, {}});
    enqueue(std::move(ev));
    return;
  }

  if (cmd == "PONG") {
    const std::string token = p.params.empty() ? std::string() : p.params.back();
    if (!lag_token_.empty() && token == lag_token_) {
      const gint64 now = g_get_monotonic_time();
      lag_ms_.store(static_cast<int>((now - lag_sent_us_) / 1000));
      lag_token_.clear();
      enqueue({Event::Lag, {}, {}, {}, false, {}});
      return;
    }
  }

  if (cmd == "353" && p.params.size() >= 3) {
    const std::string chan = p.params[p.params.size() - 2];
    const std::string names = p.params.back();
    if (g_ascii_strcasecmp(chan.c_str(), names_chan_.c_str()) != 0) {
      names_chan_ = chan;
      names_acc_.clear();
    }
    split_nicks(names, names_acc_);
    enqueue({Event::Line, line, {}, {}, false, {}});
    return;
  }

  if (cmd == "366" && p.params.size() >= 2) {
    Event ev;
    ev.type = Event::Names;
    ev.channel = p.params[1];
    ev.nicks = names_acc_;
    names_acc_.clear();
    names_chan_.clear();
    enqueue({Event::Line, line, {}, {}, false, {}});
    enqueue(std::move(ev));
    return;
  }

  if (cmd == "001") {
    if (!p.params.empty()) {
      std::lock_guard<std::mutex> lock(nick_mu_);
      nick_ = p.params[0];
    }
    enqueue({Event::Line, line, {}, {}, false, {}});
    enqueue({Event::Registered, {}, {}, {}, false, {}});
    return;
  }

  enqueue({Event::Line, line, {}, {}, false, {}});
  if (cmd == "ERROR") {
    Event ev;
    ev.type = Event::Finished;
    ev.text = line;
    /* ERROR still goes through the read loop; finish message is remembered by caller. */
  }
}

void IrcSession::thread_main()
{
  std::string finish = "Disconnected.";
  try {
    auto client = Gio::SocketClient::create();
    client->set_tls(tls_);
    enqueue({Event::Line,
             std::string(tls_ ? "Connecting (TLS) to " : "Connecting to ") + host_ + ":" +
                 std::to_string(port_) + " as " + nick() + "…",
             {},
             {},
             false,
             {}});

    auto conn = client->connect_to_host(host_, port_, cancellable_);
    {
      std::lock_guard<std::mutex> lock(out_mu_);
      sock_ = conn->get_socket();
      if (sock_)
        sock_->set_timeout(0);
      out_ = conn->get_output_stream();
    }

    const std::string n = nick();
    if (!write_line("NICK " + n) || !write_line("USER " + n + " 0 * :" + realname_)) {
      finish = "Could not send NICK/USER.";
    } else {
      auto in = Gio::DataInputStream::create(conn->get_input_stream());
      in->set_newline_type(Gio::DATA_STREAM_NEWLINE_TYPE_ANY);
      std::string line;
      while (in->read_line(line, cancellable_)) {
        trim_cr(line);
        if (line.empty())
          continue;
        const Parsed p = parse_irc(line);
        if (p.cmd == "ERROR")
          finish = line;
        handle_line(line);
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
    sock_.reset();
  }
  running_.store(false);
  enqueue({Event::Finished, finish, {}, {}, false, {}});
}

}  // namespace partyline
