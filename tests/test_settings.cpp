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

void test_renaming_the_last_server_moves_last_server()
{
  auto s = two_servers();
  std::string last = "libera";
  s.servers[0].name = "Libera Chat";
  partyline::Settings::assign_id(s.servers, 0, last);
  CHECK(s.servers[0].id == "libera_chat");
  CHECK(last == "libera_chat");

  /* Renaming another server does not move it. */
  s.servers[1].name = "OFTC net";
  partyline::Settings::assign_id(s.servers, 1, last);
  CHECK(s.servers[1].id == "oftc_net");
  CHECK(last == "libera_chat");
}

void test_removing_the_last_server_clears_it()
{
  auto s = two_servers();
  std::string last = "libera";
  partyline::Settings::remove_server(s.servers, 0, last);
  CHECK(last.empty());
  CHECK(s.servers.size() == 1);
  CHECK(s.servers[0].id == "oftc");
}

void test_removing_another_server_keeps_last_server()
{
  auto s = two_servers();
  std::string last = "libera";
  partyline::Settings::remove_server(s.servers, 1, last);
  CHECK(last == "libera");
  CHECK(s.servers.size() == 1);
  CHECK(s.servers[0].id == "libera");
}

void test_new_server_does_not_take_an_empty_last_server()
{
  auto s = two_servers();
  std::string last;
  partyline::Server n;
  n.name = "OFTC";
  s.servers.push_back(n);
  partyline::Settings::assign_id(s.servers, 2, last);
  CHECK(s.servers[2].id == "oftc_2");
  CHECK(last.empty());
}

}  // namespace

int main()
{
  test_nick_on_a_server_with_its_own_nick_updates_that_server();
  test_nick_on_a_server_without_one_updates_the_default();
  test_unknown_server_or_empty_nick();
  test_renaming_the_last_server_moves_last_server();
  test_removing_the_last_server_clears_it();
  test_removing_another_server_keeps_last_server();
  test_new_server_does_not_take_an_empty_last_server();
  return suite_test::done("settings");
}
