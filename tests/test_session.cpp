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

void test_join_sends_the_key_and_part_sends_the_reason()
{
  const auto bare = partyline::split_channel_tail("#secret");
  CHECK(bare.channel == "#secret");
  CHECK(bare.rest.empty());
  const auto keyed = partyline::split_channel_tail("  #secret   hunter2");
  CHECK(keyed.channel == "#secret");
  CHECK(keyed.rest == "hunter2");
  const auto reason = partyline::split_channel_tail("#secret going home");
  CHECK(reason.channel == "#secret");
  CHECK(reason.rest == "going home");
  const auto none = partyline::split_channel_tail("   ");
  CHECK(none.channel.empty());
  CHECK(none.rest.empty());

  fake_irc::Server srv;
  partyline::IrcSession s;
  s.start("127.0.0.1", srv.port(), false, false, "bob", "Bob");
  CHECK(fake_irc::pump_until([&]() { return srv.lines().size() >= 2; }));

  s.join("#secret", "hunter2");
  s.join("#a\r\nQUIT :join", "k\r\nQUIT :key");
  s.part("#secret", "going home");
  s.part("#a\nQUIT :part", "bye\r\nQUIT :x");
  CHECK(fake_irc::pump_until([&]() { return has_line(srv, "PART #a :bye  QUIT :x"); }));
  CHECK(has_line(srv, "JOIN #secret hunter2"));
  CHECK(has_line(srv, "JOIN #a k"));
  CHECK(has_line(srv, "PART #secret :going home"));
  CHECK(!any_line_starts(srv, "QUIT"));
  if (suite_test::failures)
    dump(srv);
  s.stop();
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

void test_action_reaches_the_buffer_and_version_is_exact()
{
  fake_irc::Server srv;
  partyline::IrcSession s;
  std::vector<Said> actions;
  std::vector<Said> msgs;
  s.signal_action.connect(
      [&](const Glib::ustring& t, const Glib::ustring& n, const Glib::ustring& x) {
        actions.push_back({t.raw(), n.raw(), x.raw()});
      });
  s.signal_privmsg.connect(
      [&](const Glib::ustring& t, const Glib::ustring& n, const Glib::ustring& x) {
        msgs.push_back({t.raw(), n.raw(), x.raw()});
      });
  s.start("127.0.0.1", srv.port(), false, false, "bob", "Bob");
  CHECK(fake_irc::pump_until([&]() { return srv.lines().size() >= 2; }));
  srv.send(
      ":al!a@h PRIVMSG #chan :\x01"
      "ACTION waves\x01");
  srv.send(
      ":al!a@h PRIVMSG bob :\x01"
      "ACTION nods\x01");
  srv.send(":al!a@h PRIVMSG bob :\x01VERSIONX\x01");
  srv.send(":al!a@h PRIVMSG bob :\x01PING 123\x01");
  srv.send(":cy!c@h PRIVMSG bob :\x01VERSION\x01");
  CHECK(fake_irc::pump_until([&]() { return actions.size() >= 2; }));
  CHECK(fake_irc::pump_until([&]() { return any_line_starts(srv, "NOTICE cy "); }));
  fake_irc::pump_until([]() { return false; }, 100);
  CHECK(actions.size() == 2);
  if (actions.size() == 2) {
    CHECK(actions[0].target == "#chan");
    CHECK(actions[0].nick == "al");
    CHECK(actions[0].text == "waves");
    CHECK(actions[1].target == "bob");
    CHECK(actions[1].text == "nods");
  }
  CHECK(msgs.empty());
  /* Only the real VERSION query is answered. */
  CHECK(!any_line_starts(srv, "NOTICE al "));
  if (suite_test::failures)
    dump(srv);
  s.stop();
}

size_t count_starts(fake_irc::Server& srv, const std::string& prefix)
{
  size_t n = 0;
  for (const auto& l : srv.lines())
    if (l.compare(0, prefix.size(), prefix) == 0)
      ++n;
  return n;
}

void test_rejected_nick_is_retried_until_registered()
{
  fake_irc::Server srv;
  partyline::IrcSession s;
  bool registered = false;
  s.signal_registered.connect([&]() { registered = true; });
  s.start("127.0.0.1", srv.port(), false, false, "bob", "Bob");
  CHECK(fake_irc::pump_until([&]() { return srv.lines().size() >= 2; }));
  srv.send(":srv 433 * bob :Nickname is already in use");
  CHECK(fake_irc::pump_until([&]() { return has_line(srv, "NICK bob_"); }));
  srv.send(":srv 432 * bob_ :Erroneous nickname");
  CHECK(fake_irc::pump_until([&]() { return has_line(srv, "NICK bob__"); }));
  srv.send(":srv 001 bob__ :Welcome");
  CHECK(fake_irc::pump_until([&]() { return registered; }));
  CHECK(s.nick() == "bob__");

  /* After registration a rejected /nick keeps the current nick. */
  const size_t before = count_starts(srv, "NICK ");
  s.change_nick("al");
  CHECK(fake_irc::pump_until([&]() { return has_line(srv, "NICK al"); }));
  srv.send(":srv 433 bob__ al :Nickname is already in use");
  fake_irc::pump_until([]() { return false; }, 150);
  CHECK(count_starts(srv, "NICK ") == before + 1);
  CHECK(s.nick() == "bob__");
  if (suite_test::failures)
    dump(srv);
  s.stop();
}

void test_nick_retry_gives_up()
{
  fake_irc::Server srv;
  partyline::IrcSession s;
  s.start("127.0.0.1", srv.port(), false, false, "bob", "Bob");
  CHECK(fake_irc::pump_until([&]() { return srv.lines().size() >= 2; }));
  for (int i = 0; i < 20; ++i) {
    const size_t sent = count_starts(srv, "NICK ");
    srv.send(":srv 433 * x :Nickname is already in use");
    if (!fake_irc::pump_until(
            [&]() {
              return count_starts(srv, "NICK ") > sent || has_line(srv, "QUIT :Nickname rejected");
            },
            500))
      break;
    if (has_line(srv, "QUIT :Nickname rejected"))
      break;
  }
  CHECK(has_line(srv, "QUIT :Nickname rejected"));
  CHECK(count_starts(srv, "NICK ") <= 10);
  s.stop();
}

struct Kicked {
  std::string channel, nick, by, reason;
  bool me;
};

void test_kick_is_its_own_event_with_reason()
{
  fake_irc::Server srv;
  partyline::IrcSession s;
  std::vector<Kicked> kicks;
  int parts = 0;
  s.signal_kick.connect([&](const Glib::ustring& c, const Glib::ustring& n, const Glib::ustring& by,
                            const Glib::ustring& r, bool me) {
    kicks.push_back({c.raw(), n.raw(), by.raw(), r.raw(), me});
  });
  s.signal_part.connect([&](const Glib::ustring&, const Glib::ustring&, bool) { ++parts; });
  s.start("127.0.0.1", srv.port(), false, false, "bob", "Bob");
  CHECK(fake_irc::pump_until([&]() { return srv.lines().size() >= 2; }));
  srv.send(":srv 001 bob :Welcome");
  srv.send(":op!o@h KICK #chan al :spamming");
  srv.send(":op!o@h KICK #chan bob");
  srv.send(":al!a@h PART #other");
  CHECK(fake_irc::pump_until([&]() { return kicks.size() >= 2 && parts >= 1; }));
  CHECK(parts == 1);
  CHECK(kicks.size() == 2);
  if (kicks.size() == 2) {
    CHECK(kicks[0].channel == "#chan");
    CHECK(kicks[0].nick == "al");
    CHECK(kicks[0].by == "op");
    CHECK(kicks[0].reason == "spamming");
    CHECK(!kicks[0].me);
    CHECK(kicks[1].nick == "bob");
    CHECK(kicks[1].reason.empty());
    CHECK(kicks[1].me);
  }
  s.stop();
}

}  // namespace

int main()
{
  Gio::init();
  test_line_breaks_never_start_a_second_command();
  test_join_sends_the_key_and_part_sends_the_reason();
  test_long_privmsg_is_split_on_utf8_boundaries();
  test_privmsg_without_a_socket_reports_nothing_sent();
  test_lag_ping_while_pongs_arrive();
  test_channel_notice_reaches_the_channel();
  test_action_reaches_the_buffer_and_version_is_exact();
  test_rejected_nick_is_retried_until_registered();
  test_nick_retry_gives_up();
  test_kick_is_its_own_event_with_reason();
  return suite_test::done("session");
}
