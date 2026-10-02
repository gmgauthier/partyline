/* SPDX-License-Identifier: Unlicense */

#include "settings.hpp"
#include "check.hpp"

namespace {

partyline::Settings two_servers()
{
  partyline::Settings s;
  s.nick = "greg";
  partyline::Server a;
  a.id = "libera";
  a.name = "Libera";
  a.host = "irc.libera.chat";
  a.nick = "gmg";
  partyline::Server b;
  b.id = "oftc";
  b.name = "OFTC";
  b.host = "irc.oftc.net";
  s.servers = {a, b};
  return s;
}

void test_nick_on_a_server_with_its_own_nick_updates_that_server()
{
  auto s = two_servers();
  s.remember_nick("libera", "gmg2");
  CHECK(s.nick == "greg");
  CHECK(s.find_id("libera")->nick == "gmg2");
  CHECK(s.find_id("oftc")->nick.empty());
}

void test_nick_on_a_server_without_one_updates_the_default()
{
  auto s = two_servers();
  s.remember_nick("oftc", "gregg");
  CHECK(s.nick == "gregg");
  CHECK(s.find_id("libera")->nick == "gmg");
  CHECK(s.find_id("oftc")->nick.empty());
}

void test_unknown_server_or_empty_nick()
{
  auto s = two_servers();
  s.remember_nick("gone", "x");
  CHECK(s.nick == "x");
  s.remember_nick("libera", "");
  CHECK(s.find_id("libera")->nick == "gmg");
  CHECK(s.nick == "x");
}

}  // namespace

int main()
{
  test_nick_on_a_server_with_its_own_nick_updates_that_server();
  test_nick_on_a_server_without_one_updates_the_default();
  test_unknown_server_or_empty_nick();
  return suite_test::done("settings");
}
