/* SPDX-License-Identifier: Unlicense */

#include "log.hpp"
#include "check.hpp"

#include <cstdlib>
#include <filesystem>
#include <glibmm.h>
#include <string>
#include <unistd.h>

namespace {

std::string data_dir()
{
  return "/tmp/partyline-test-" + std::to_string(static_cast<long long>(::getpid()));
}

__attribute__((constructor)) void pin_data_dir()
{
  ::setenv("XDG_DATA_HOME", data_dir().c_str(), 1);
}

}  // namespace

int main()
{
  const std::string root = data_dir();
  CHECK(Glib::get_user_data_dir() == root);

  partyline::ChatLog log;
  CHECK(log.tail_channel("#lcos", 10).empty());
  log.write_channel("#lcos", "before-host");
  CHECK(log.tail_channel("#lcos", 10).empty());

  log.set_host("IRC.Example");
  log.write_channel("#LCOS", "hello channel");
  log.write_channel("../../tmp-escape", "secret");
  log.write_query("Nick Name", "psst");

  const auto channel = log.tail_channel("#lcos", 10);
  CHECK(channel.size() == 1);
  CHECK(channel[0].find("hello channel") != std::string::npos);

  const auto sneaky = log.tail_channel("../../tmp-escape", 10);
  CHECK(sneaky.size() == 1);
  CHECK(sneaky[0].find("secret") != std::string::npos);

  const auto query_dir = root + "/partyline/logs/irc.example";
  CHECK(Glib::file_test(query_dir, Glib::FILE_TEST_IS_DIR));
  CHECK(Glib::file_test(query_dir + "/#lcos.txt", Glib::FILE_TEST_IS_REGULAR));
  /* Characters outside [a-z0-9._-] (and # for channels) are %XX-escaped, so
   * the leaf stays one name inside the host directory. */
  CHECK(Glib::file_test(query_dir + "/..%2f..%2ftmp-escape.txt", Glib::FILE_TEST_IS_REGULAR));
  CHECK(Glib::file_test(query_dir + "/nick%20name.txt", Glib::FILE_TEST_IS_REGULAR));
  CHECK(!Glib::file_test("/tmp/tmp-escape.txt", Glib::FILE_TEST_EXISTS));

  CHECK(log.tail_channel("#lcos", 0).empty());

  /* Different channels and nicks never share a file. */
  log.write_channel("#c++", "plus");
  log.write_channel("#c__", "under");
  const auto plus = log.tail_channel("#c++", 10);
  const auto under = log.tail_channel("#c__", 10);
  CHECK(plus.size() == 1);
  CHECK(under.size() == 1);
  if (plus.size() == 1 && under.size() == 1) {
    CHECK(plus[0].find("plus") != std::string::npos);
    CHECK(under[0].find("under") != std::string::npos);
  }
  CHECK(Glib::file_test(query_dir + "/#c__.txt", Glib::FILE_TEST_IS_REGULAR));
  log.write_query("foo~bar", "tilde");
  log.write_query("foo_bar", "underscore");
  log.write_query("foo%7ebar", "percent");
  CHECK(Glib::file_test(query_dir + "/foo_bar.txt", Glib::FILE_TEST_IS_REGULAR));
  CHECK(Glib::file_test(query_dir + "/foo%7ebar.txt", Glib::FILE_TEST_IS_REGULAR));
  CHECK(Glib::file_test(query_dir + "/foo%257ebar.txt", Glib::FILE_TEST_IS_REGULAR));

  std::filesystem::remove_all(root);
  return suite_test::done("log");
}
