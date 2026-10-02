/* SPDX-License-Identifier: Unlicense */

#include "irc_session.hpp"
#include "config.hpp"

#include <glib.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>

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

std::string strip_irc_format(const std::string& in)
{
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size();) {
    const unsigned char c = static_cast<unsigned char>(in[i]);
    if (c == 0x02 || c == 0x0F || c == 0x16 || c == 0x1D || c == 0x1F || c == 0x01) {
      ++i;
      continue;
    }
    if (c == 0x03) {
      ++i;
      int n = 0;
      while (n < 2 && i < in.size() && in[i] >= '0' && in[i] <= '9') {
        ++i;
        ++n;
      }
      if (i < in.size() && in[i] == ',') {
        ++i;
        n = 0;
        while (n < 2 && i < in.size() && in[i] >= '0' && in[i] <= '9') {
          ++i;
          ++n;
        }
      }
      continue;
    }
    out.push_back(in[i]);
    ++i;
  }
  return out;
}

/* Human WHOIS lines for Status. Empty if this numeric is not WHOIS. */
std::string format_whois(const std::string& cmd, const std::vector<std::string>& p)
{
  auto at = [&](size_t i) -> const std::string& {
    static const std::string empty;
    return i < p.size() ? p[i] : empty;
  };
  const std::string& nick = at(1);
  if (cmd == "311" && p.size() >= 4) {
    std::string s = "* " + nick + " is " + at(2) + "@" + at(3);
    if (p.size() >= 6 && !p.back().empty())
      s += " (" + p.back() + ")";
    return s;
  }
  if (cmd == "312" && p.size() >= 3)
    return "* " + nick + " using " + at(2) + (p.size() >= 4 ? " (" + p.back() + ")" : "");
  if (cmd == "313")
    return "* " + nick + " is an IRC operator";
  if (cmd == "301" && p.size() >= 2)
    return "* " + nick + " is away" + (p.size() >= 3 ? ": " + p.back() : "");
  if (cmd == "317" && p.size() >= 3)
    return "* " + nick + " idle " + at(2) + "s" + (p.size() >= 5 ? ", signon " + at(3) : "");
  if (cmd == "318")
    return "* " + nick + " End of /WHOIS list.";
  if (cmd == "319" && p.size() >= 3)
    return "* " + nick + " on " + p.back();
  if (cmd == "330" && p.size() >= 3)
    return "* " + nick + " is logged in as " + at(2);
  if (cmd == "338" && p.size() >= 3)
    return "* " + nick + " actually " + at(2);
  if (cmd == "671")
    return "* " + nick + " is using a secure connection";
  if (cmd == "401" && p.size() >= 2)
    return "* " + nick + " No such nick/channel";
  if (cmd == "402" && p.size() >= 2)
    return "* " + nick + " No such server";
  return {};
}

std::string utf8_clean(std::string s)
{
  s = strip_irc_format(s);
  if (s.empty() || g_utf8_validate(s.data(), static_cast<gssize>(s.size()), nullptr))
    return s;
  GError* err = nullptr;
  gsize out_len = 0;
  gchar* conv = g_convert(s.data(), static_cast<gssize>(s.size()), "UTF-8", "ISO-8859-1", nullptr,
                          &out_len, &err);
  if (conv && !err) {
    std::string out(conv, out_len);
    g_free(conv);
    return out;
  }
  if (err)
    g_error_free(err);
  if (conv)
    g_free(conv);
  gchar* valid = g_utf8_make_valid(s.data(), static_cast<gssize>(s.size()));
  std::string out = valid ? valid : std::string();
  g_free(valid);
  return out;
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

/* A middle parameter (nick, channel, mask): it ends at the first space, CR,
 * LF, or NUL, so nothing after it can become another parameter or command. */
std::string irc_token(const std::string& in)
{
  const auto end = in.find_first_of(std::string(" \r\n\0", 4));
  return end == std::string::npos ? in : in.substr(0, end);
}

/* A trailing parameter (real name): CR, LF, and NUL become spaces. */
std::string irc_text(std::string in)
{
  for (char& c : in)
    if (c == '\r' || c == '\n' || c == '\0')
      c = ' ';
  return in;
}

/* Message text split on line breaks; each non-empty line is its own message. */
std::vector<std::string> irc_lines(const std::string& text)
{
  std::vector<std::string> out;
  size_t i = 0;
  while (i <= text.size()) {
    const auto end = text.find_first_of(std::string("\r\n\0", 3), i);
    const std::string part = text.substr(i, end == std::string::npos ? std::string::npos : end - i);
    if (!part.empty())
      out.push_back(part);
    if (end == std::string::npos)
      break;
    i = end + 1;
  }
  return out;
}

/* Longest PRIVMSG body sent in one line, in bytes. */
constexpr size_t kMaxBody = 400;

/* Splits text into pieces of at most max bytes without cutting a UTF-8
 * sequence (a continuation byte never starts a piece). */
std::vector<std::string> utf8_chunks(const std::string& text, size_t max)
{
  std::vector<std::string> out;
  size_t i = 0;
  while (i < text.size()) {
    size_t end = std::min(text.size(), i + max);
    if (end < text.size()) {
      size_t cut = end;
      while (cut > i && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80)
        --cut;
      if (cut > i)
        end = cut;
    }
    out.push_back(text.substr(i, end - i));
    i = end;
  }
  return out;
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

void IrcSession::start(std::string host, guint16 port, bool tls, bool tls_verify, std::string nick,
                       std::string realname)
{
  stop();
  host_ = std::move(host);
  port_ = port == 0 ? (tls ? 6697 : 6667) : port;
  tls_ = tls;
  tls_verify_ = tls_verify;
  {
    std::lock_guard<std::mutex> lock(nick_mu_);
    nick_ = irc_token(nick);
    realname_ = realname.empty() ? nick_ : irc_text(std::move(realname));
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
  const std::string c = irc_token(channel);
  if (!c.empty())
    write_line("JOIN " + c);
}

void IrcSession::part(const std::string& channel)
{
  const std::string c = irc_token(channel);
  if (!c.empty())
    write_line("PART " + c);
}

IrcSession::SendResult IrcSession::privmsg(const std::string& target, const std::string& text)
{
  SendResult r;
  const std::string t = irc_token(target);
  if (t.empty() || text.empty())
    return r;
  for (const auto& line : irc_lines(text))
    for (const auto& body : utf8_chunks(line, kMaxBody)) {
      if (!write_line("PRIVMSG " + t + " :" + body))
        return r;
      r.sent.push_back(body);
    }
  r.complete = !r.sent.empty();
  return r;
}

void IrcSession::whois(const std::string& nick)
{
  const std::string n = irc_token(nick);
  if (n.empty())
    return;
  write_line("WHOIS " + n);
}

void IrcSession::list_channels(const std::string& mask)
{
  const std::string m = irc_token(mask);
  if (m.empty())
    write_line("LIST");
  else
    write_line("LIST " + m);
}

void IrcSession::quote(const std::string& raw)
{
  if (!raw.empty())
    write_line(raw);
}

void IrcSession::change_nick(const std::string& nick)
{
  const std::string n = irc_token(nick);
  if (!n.empty())
    write_line("NICK " + n);
}

void IrcSession::send_lag_ping()
{
  std::string token;
  {
    std::lock_guard<std::mutex> lock(lag_mu_);
    lag_sent_us_ = g_get_monotonic_time();
    lag_token_ = std::to_string(lag_sent_us_);
    token = lag_token_;
  }
  write_line("PING :" + token);
}

void IrcSession::enqueue(Event ev)
{
  ev.text = utf8_clean(std::move(ev.text));
  ev.channel = utf8_clean(std::move(ev.channel));
  ev.nick = utf8_clean(std::move(ev.nick));
  for (auto& n : ev.nicks)
    n = utf8_clean(std::move(n));
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
      case Event::Notice:
        signal_notice.emit(ev.channel, ev.nick, ev.text);
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
      case Event::ListStart:
        signal_list_start.emit();
        break;
      case Event::ListRow:
        signal_list_row.emit(ev.channel, ev.users, ev.text);
        break;
      case Event::ListEnd:
        signal_list_end.emit();
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
    /* One call is one command: anything after a CR, LF, or NUL is dropped
     * (this also covers /quote). */
    std::string wire = line.substr(0, line.find_first_of(std::string("\r\n\0", 3)));
    if (wire.empty())
      return false;
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
    enqueue({Event::Line, line, {}, {}, false, 0, {}});
    return;
  }

  if (cmd == "PRIVMSG" && p.params.size() >= 2) {
    const std::string& target = p.params[0];
    const std::string& text = p.params[1];
    if (is_ctcp_version(text)) {
      if (!p.nick.empty())
        write_line(std::string("NOTICE ") + p.nick + " :\x01VERSION Partyline " VERSION "\x01");
      enqueue({Event::Line, line, {}, {}, false, 0, {}});
      return;
    }
    if (!text.empty() && text[0] == '\x01') {
      enqueue({Event::Line, line, {}, {}, false, 0, {}});
      return;
    }
    enqueue({Event::Line, line, {}, {}, false, 0, {}});
    Event ev;
    ev.type = Event::Privmsg;
    ev.channel = target;
    ev.nick = p.nick;
    ev.text = text;
    enqueue(std::move(ev));
    return;
  }

  /* A NOTICE to a channel goes to that channel's buffer. A NOTICE to us
   * (server or user) and CTCP replies stay on the status line. */
  if (cmd == "NOTICE" && p.params.size() >= 2 && !p.params[0].empty() &&
      std::string("#&+!").find(p.params[0][0]) != std::string::npos && !p.params[1].empty() &&
      p.params[1][0] != '\x01') {
    enqueue({Event::Line, line, {}, {}, false, 0, {}});
    Event ev;
    ev.type = Event::Notice;
    ev.channel = p.params[0];
    ev.nick = p.nick;
    ev.text = p.params[1];
    enqueue(std::move(ev));
    return;
  }

  if (cmd == "JOIN" && !p.params.empty()) {
    Event ev;
    ev.type = Event::Join;
    ev.channel = p.params[0];
    ev.nick = p.nick;
    ev.me = is_me(p.nick);
    enqueue({Event::Line, line, {}, {}, false, 0, {}});
    enqueue(std::move(ev));
    return;
  }

  if ((cmd == "PART" || cmd == "KICK") && !p.params.empty()) {
    Event ev;
    ev.type = Event::Part;
    ev.channel = p.params[0];
    ev.nick = (cmd == "KICK" && p.params.size() >= 2) ? p.params[1] : p.nick;
    ev.me = is_me(ev.nick);
    enqueue({Event::Line, line, {}, {}, false, 0, {}});
    enqueue(std::move(ev));
    return;
  }

  if (cmd == "QUIT") {
    enqueue({Event::Line, line, {}, {}, false, 0, {}});
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
    enqueue({Event::Line, line, {}, {}, false, 0, {}});
    enqueue(std::move(ev));
    return;
  }

  if (cmd == "PONG") {
    const std::string token = p.params.empty() ? std::string() : p.params.back();
    bool matched = false;
    {
      std::lock_guard<std::mutex> lock(lag_mu_);
      if (!lag_token_.empty() && token == lag_token_) {
        const gint64 now = g_get_monotonic_time();
        lag_ms_.store(static_cast<int>((now - lag_sent_us_) / 1000));
        lag_token_.clear();
        matched = true;
      }
    }
    if (matched) {
      enqueue({Event::Lag, {}, {}, {}, false, 0, {}});
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
    enqueue({Event::Line, line, {}, {}, false, 0, {}});
    return;
  }

  if (cmd == "366" && p.params.size() >= 2) {
    Event ev;
    ev.type = Event::Names;
    ev.channel = p.params[1];
    ev.nicks = names_acc_;
    names_acc_.clear();
    names_chan_.clear();
    enqueue({Event::Line, line, {}, {}, false, 0, {}});
    enqueue(std::move(ev));
    return;
  }

  {
    const std::string who = format_whois(cmd, p.params);
    if (!who.empty()) {
      enqueue({Event::Line, utf8_clean(who), {}, {}, false, 0, {}});
      return;
    }
  }

  if (cmd == "321") {
    enqueue({Event::ListStart, {}, {}, {}, false, 0, {}});
    return;
  }
  if (cmd == "322" && !p.params.empty()) {
    std::string chan;
    std::string count = "0";
    std::string topic;
    const auto chan_pfx = [](char c) { return c == '#' || c == '&' || c == '+' || c == '!'; };
    if (!p.params[0].empty() && chan_pfx(p.params[0][0])) {
      chan = p.params[0];
      if (p.params.size() >= 2)
        count = p.params[1];
      if (p.params.size() >= 3)
        topic = p.params.back();
    } else if (p.params.size() >= 3) {
      chan = p.params[1];
      count = p.params[2];
      if (p.params.size() >= 4)
        topic = p.params.back();
    }
    if (!chan.empty()) {
      Event ev;
      ev.type = Event::ListRow;
      ev.channel = utf8_clean(chan);
      ev.users = std::atoi(count.c_str());
      ev.text = utf8_clean(topic);
      enqueue(std::move(ev));
    }
    return;
  }
  if (cmd == "323") {
    enqueue({Event::ListEnd, {}, {}, {}, false, 0, {}});
    return;
  }

  if (cmd == "001") {
    if (!p.params.empty()) {
      std::lock_guard<std::mutex> lock(nick_mu_);
      nick_ = p.params[0];
    }
    enqueue({Event::Line, line, {}, {}, false, 0, {}});
    enqueue({Event::Registered, {}, {}, {}, false, 0, {}});
    return;
  }

  enqueue({Event::Line, line, {}, {}, false, 0, {}});
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
  std::atomic<bool> timed_out{false};
  try {
    auto client = Gio::SocketClient::create();
    client->set_tls(tls_);
    if (tls_ && !tls_verify_) {
      const auto flags = static_cast<Gio::TlsCertificateFlags>(Gio::TLS_CERTIFICATE_VALIDATE_ALL &
                                                               ~Gio::TLS_CERTIFICATE_BAD_IDENTITY);
      client->set_tls_validation_flags(flags);
    }
    enqueue({Event::Line,
             std::string(tls_ ? "Connecting (TLS) to " : "Connecting to ") + host_ + ":" +
                 std::to_string(port_) + " as " + nick() + "…",
             {},
             {},
             false,
             0,
             {}});

    /* GIO's per-socket timeout tries every A record; a filtered port on a
     * five-address round-robin hangs for minutes. Cancel the whole attempt. */
    std::atomic<bool> connecting{true};
    std::thread watchdog([this, &connecting, &timed_out]() {
      for (int i = 0; i < 20 && connecting.load(); ++i) {
        if (cancellable_ && cancellable_->is_cancelled())
          return;
        std::this_thread::sleep_for(std::chrono::seconds(1));
      }
      if (connecting.load() && cancellable_) {
        timed_out.store(true);
        cancellable_->cancel();
      }
    });
    Glib::RefPtr<Gio::SocketConnection> conn;
    try {
      conn = client->connect_to_host(host_, port_, cancellable_);
    } catch (...) {
      connecting.store(false);
      if (watchdog.joinable())
        watchdog.join();
      throw;
    }
    connecting.store(false);
    if (watchdog.joinable())
      watchdog.join();
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
    if (timed_out) {
      finish =
          "Connection timed out after 20s. Check host, port, and TLS "
          "(Undernet is 6667 with TLS off).";
    } else if (!(cancellable_ && cancellable_->is_cancelled())) {
      finish = e.what();
      const std::string w = e.what();
      if (w.find("TLS certificate") != std::string::npos ||
          w.find("Unacceptable TLS") != std::string::npos)
        finish +=
            " — certificate name may not match this host. Try the network's canonical "
            "name, or uncheck Verify hostname in Servers…";
    }
  } catch (const std::exception& e) {
    finish = e.what();
  }

  {
    std::lock_guard<std::mutex> lock(out_mu_);
    out_.reset();
    sock_.reset();
  }
  running_.store(false);
  enqueue({Event::Finished, finish, {}, {}, false, 0, {}});
}

}  // namespace partyline
