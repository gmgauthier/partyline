/* SPDX-License-Identifier: Unlicense */

#include "irc_session.hpp"
#include "check.hpp"
#include "fake_irc.hpp"

#include <giomm.h>

#include <algorithm>
#include <thread>
#include <atomic>
#include <string>
#include <vector>

namespace {

bool has_line(fake_irc::Server& srv, const std::string& want)
{
  const auto l = srv.lines();
  return std::find(l.begin(), l.end(), want) != l.end();
}

bool any_line_starts(fake_irc::Server& srv, const std::string& prefix)
{
  for (const auto& l : srv.lines())
    if (l.compare(0, prefix.size(), prefix) == 0)
      return true;
  return false;
}

void dump(fake_irc::Server& srv)
{
  for (const auto& l : srv.lines())
    std::cerr << "  sent: [" << l << "]\n";
}

void test_line_breaks_never_start_a_second_command()
{
  fake_irc::Server srv;
  partyline::IrcSession s;
  s.start("127.0.0.1", srv.port(), false, false, "bob\r\nQUIT :nick", "Real\nName");
  CHECK(fake_irc::pump_until([&]() { return srv.lines().size() >= 2; }));
  CHECK(has_line(srv, "NICK bob"));
  CHECK(has_line(srv, "USER bob 0 * :Real Name"));

  s.join("#a\r\nQUIT :join");
  s.part("#a\nQUIT :part");
  s.change_nick("x\r\nQUIT :nick2");
  s.whois("y\nQUIT :whois");
  s.list_channels("#*\r\nQUIT :list");
  s.privmsg("#a", "one\r\nQUIT :msg");
  s.privmsg("#a\r\nQUIT :target", "hi");
  s.quote("MODE #a +i\r\nQUIT :quote");
  CHECK(fake_irc::pump_until([&]() { return has_line(srv, "MODE #a +i"); }));

  CHECK(has_line(srv, "JOIN #a"));
  CHECK(has_line(srv, "PART #a"));
  CHECK(has_line(srv, "NICK x"));
  CHECK(has_line(srv, "WHOIS y"));
  CHECK(has_line(srv, "LIST #*"));
  /* A message with a line break is sent as one PRIVMSG per line. */
  CHECK(has_line(srv, "PRIVMSG #a :one"));
  CHECK(has_line(srv, "PRIVMSG #a :QUIT :msg"));
  CHECK(has_line(srv, "PRIVMSG #a :hi"));
  CHECK(!any_line_starts(srv, "QUIT"));
  if (suite_test::failures)
    dump(srv);
  s.stop();
}

bool valid_utf8(const std::string& s)
{
  return g_utf8_validate(s.data(), static_cast<gssize>(s.size()), nullptr);
}

void test_long_privmsg_is_split_on_utf8_boundaries()
{
  fake_irc::Server srv;
  partyline::IrcSession s;
  s.start("127.0.0.1", srv.port(), false, false, "bob", "Bob");
  CHECK(fake_irc::pump_until([&]() { return srv.lines().size() >= 2; }));

  /* 'a' then 600 two-byte characters: byte 400 falls inside a character. */
  std::string text = "a";
  for (int i = 0; i < 600; ++i)
    text += "\xc3\xa9";
  const auto r = s.privmsg("#a", text);
  CHECK(r.complete);
  CHECK(r.sent.size() >= 3);
  std::string joined;
  for (const auto& piece : r.sent) {
    CHECK(piece.size() <= 400);
    CHECK(valid_utf8(piece));
    joined += piece;
  }
  CHECK(joined == text);

  const std::string prefix = "PRIVMSG #a :";
  CHECK(fake_irc::pump_until([&]() {
    size_t n = 0;
    for (const auto& l : srv.lines())
      if (l.compare(0, prefix.size(), prefix) == 0)
        ++n;
    return n >= r.sent.size();
  }));
  std::string got;
  for (const auto& l : srv.lines())
    if (l.compare(0, prefix.size(), prefix) == 0) {
      const std::string body = l.substr(prefix.size());
      CHECK(body.size() <= 400);
      CHECK(valid_utf8(body));
      got += body;
    }
  CHECK(got == text);
  if (suite_test::failures)
    dump(srv);
  s.stop();
}

void test_privmsg_without_a_socket_reports_nothing_sent()
{
  partyline::IrcSession s;
  const auto r = s.privmsg("#a", "hello");
  CHECK(!r.complete);
  CHECK(r.sent.empty());
}

/* The UI thread sends lag pings while the socket thread matches PONGs.
 * Run under -Db_sanitize=thread this reports a data race unless the lag
 * token and send time are locked. */
void test_lag_ping_while_pongs_arrive()
{
  fake_irc::Server srv;
  partyline::IrcSession s;
  s.start("127.0.0.1", srv.port(), false, false, "bob", "Bob");
  CHECK(fake_irc::pump_until([&]() { return srv.lines().size() >= 2; }));

  std::atomic<bool> done{false};
  std::thread ponger([&]() {
    size_t seen = 0;
    while (!done.load()) {
      const auto l = srv.lines();
      for (; seen < l.size(); ++seen)
        if (l[seen].compare(0, 6, "PING :") == 0)
          srv.send(":srv PONG srv :" + l[seen].substr(6));
      g_usleep(200);
    }
  });
  for (int i = 0; i < 300; ++i) {
    s.send_lag_ping();
    g_usleep(500);
  }
  CHECK(fake_irc::pump_until([&]() { return s.lag_ms() >= 0; }));
  done.store(true);
  ponger.join();
  s.stop();
}

struct Said {
  std::string target, nick, text;
};

void test_channel_notice_reaches_the_channel()
{
  fake_irc::Server srv;
  partyline::IrcSession s;
  std::vector<Said> notices;
  s.signal_notice.connect(
      [&](const Glib::ustring& t, const Glib::ustring& n, const Glib::ustring& x) {
        notices.push_back({t.raw(), n.raw(), x.raw()});
      });
  s.start("127.0.0.1", srv.port(), false, false, "bob", "Bob");
  CHECK(fake_irc::pump_until([&]() { return srv.lines().size() >= 2; }));
  srv.send(":al!a@h NOTICE bob :just for you");
  srv.send(":al!a@h NOTICE #chan :hello channel");
  srv.send(":al!a@h NOTICE &local :hi local");
  CHECK(fake_irc::pump_until([&]() { return notices.size() >= 2; }));
  fake_irc::pump_until([]() { return false; }, 100);
  CHECK(notices.size() == 2);
  if (notices.size() == 2) {
    CHECK(notices[0].target == "#chan");
    CHECK(notices[0].nick == "al");
    CHECK(notices[0].text == "hello channel");
    CHECK(notices[1].target == "&local");
  }
  s.stop();
}

}  // namespace

int main()
{
  Gio::init();
  test_line_breaks_never_start_a_second_command();
  test_long_privmsg_is_split_on_utf8_boundaries();
  test_privmsg_without_a_socket_reports_nothing_sent();
  test_lag_ping_while_pongs_arrive();
  test_channel_notice_reaches_the_channel();
  return suite_test::done("session");
}
