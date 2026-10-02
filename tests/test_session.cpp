/* SPDX-License-Identifier: Unlicense */

#include "irc_session.hpp"
#include "check.hpp"
#include "fake_irc.hpp"

#include <giomm.h>

#include <algorithm>
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

}  // namespace

int main()
{
  Gio::init();
  test_line_breaks_never_start_a_second_command();
  return suite_test::done("session");
}
