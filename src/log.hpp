/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <string>
#include <vector>

namespace partyline {

/* Append-only UTF-8 logs under ~/.local/share/partyline/logs/<host>/.
 * One file per channel (#lcos.txt) or query nick. Not a log browser. */
class ChatLog {
 public:
  void set_host(const std::string& host);
  void write_channel(const std::string& channel, const std::string& line);
  void write_query(const std::string& nick, const std::string& line);
  std::vector<std::string> tail_channel(const std::string& channel, int max_lines) const;
  std::vector<std::string> tail_query(const std::string& nick, int max_lines) const;

 private:
  std::string dir_path() const;
  std::string file_path(const std::string& leaf) const;
  void write(const std::string& leaf, const std::string& line);
  std::vector<std::string> tail_leaf(const std::string& leaf, int max_lines) const;
  std::string host_key_;
};

}  // namespace partyline
