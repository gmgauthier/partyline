/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <string>
#include <vector>

namespace partyline {

struct Server {
  std::string id;
  std::string name;
  std::string host;
  int port = 6697;
  bool tls = true;
  std::string nick;
};

struct Settings {
  std::string nick;
  std::string realname;
  std::string last_server;
  int window_w = 900;
  int window_h = 600;
  int palette = 0;  // 0 white, 1 eggshell, 2 black, 3 navy, 4 olive
  std::vector<Server> servers;

  void load();
  void save() const;
  void seed_if_empty();
  Server* find_id(const std::string& id);
  const Server* find_id(const std::string& id) const;
  static std::string make_id(const std::string& name);
  static std::string default_nick();
};

}  // namespace partyline
