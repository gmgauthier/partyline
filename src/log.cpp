/* SPDX-License-Identifier: Unlicense */

#include "log.hpp"

#include <glib.h>
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>

#include <cstdio>
#include <cstring>
#include <vector>

namespace partyline {
namespace {

std::string safe_leaf(const std::string& in, bool allow_hash)
{
  std::string out;
  out.reserve(in.size());
  for (unsigned char c : in) {
    if (g_ascii_isalnum(c) || c == '.' || c == '-' || c == '_' || (allow_hash && c == '#'))
      out.push_back(static_cast<char>(g_ascii_tolower(c)));
    else
      out.push_back('_');
  }
  if (out.empty())
    out = "unknown";
  return out;
}

/* File name for a channel or query log. ASCII letters are lowercased (IRC
 * names are case-insensitive); letters, digits, '.', '-', '_' (and '#' for
 * channels) are kept; every other byte, '%' included, becomes %xx. Distinct
 * names therefore never share a file, and names made of the kept characters
 * keep the file they had before. */
std::string log_leaf(const std::string& in, bool allow_hash)
{
  static const char hex[] = "0123456789abcdef";
  std::string out;
  out.reserve(in.size());
  for (unsigned char c : in) {
    if (g_ascii_isalnum(c) || c == '.' || c == '-' || c == '_' || (allow_hash && c == '#')) {
      out.push_back(static_cast<char>(g_ascii_tolower(c)));
    } else {
      out.push_back('%');
      out.push_back(hex[c >> 4]);
      out.push_back(hex[c & 0x0f]);
    }
  }
  if (out.empty())
    out = "unknown";
  return out;
}

std::string stamp()
{
  GDateTime* dt = g_date_time_new_now_local();
  gchar* s = g_date_time_format(dt, "%Y-%m-%d %H:%M:%S");
  std::string out = s ? s : "";
  g_free(s);
  g_date_time_unref(dt);
  return out;
}

}  // namespace

void ChatLog::set_host(const std::string& host)
{
  host_key_ = safe_leaf(host, false);
}

std::string ChatLog::dir_path() const
{
  return Glib::build_filename(Glib::get_user_data_dir(), "partyline", "logs", host_key_);
}

std::string ChatLog::file_path(const std::string& leaf) const
{
  return Glib::build_filename(dir_path(), leaf);
}

void ChatLog::write_channel(const std::string& channel, const std::string& line)
{
  write(log_leaf(channel, true) + ".txt", line);
}

void ChatLog::write_query(const std::string& nick, const std::string& line)
{
  write(log_leaf(nick, false) + ".txt", line);
}

std::vector<std::string> ChatLog::tail_channel(const std::string& channel, int max_lines) const
{
  std::vector<std::string> out;
  if (host_key_.empty() || max_lines <= 0)
    return out;
  const std::string path = file_path(log_leaf(channel, true) + ".txt");
  FILE* fp = std::fopen(path.c_str(), "rb");
  if (!fp)
    return out;
  if (std::fseek(fp, 0, SEEK_END) != 0) {
    std::fclose(fp);
    return out;
  }
  const long sz = std::ftell(fp);
  if (sz <= 0) {
    std::fclose(fp);
    return out;
  }
  const long cap = 256 * 1024;
  const long start = sz > cap ? sz - cap : 0;
  std::fseek(fp, start, SEEK_SET);
  std::string buf(static_cast<size_t>(sz - start), '\0');
  const size_t n = std::fread(buf.data(), 1, buf.size(), fp);
  std::fclose(fp);
  buf.resize(n);
  if (start > 0) {
    const auto nl = buf.find('\n');
    if (nl != std::string::npos)
      buf.erase(0, nl + 1);
  }
  std::vector<std::string> lines;
  size_t i = 0;
  while (i < buf.size()) {
    auto nl = buf.find('\n', i);
    if (nl == std::string::npos)
      nl = buf.size();
    std::string line = buf.substr(i, nl - i);
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    if (!line.empty())
      lines.push_back(std::move(line));
    i = nl + 1;
  }
  const size_t keep = static_cast<size_t>(max_lines);
  if (lines.size() > keep)
    out.assign(lines.end() - static_cast<std::ptrdiff_t>(keep), lines.end());
  else
    out = std::move(lines);
  return out;
}

void ChatLog::write(const std::string& leaf, const std::string& line)
{
  if (host_key_.empty() || line.empty())
    return;
  g_mkdir_with_parents(dir_path().c_str(), 0700);
  FILE* fp = std::fopen(file_path(leaf).c_str(), "ab");
  if (!fp)
    return;
  const std::string out = stamp() + " " + line + "\n";
  std::fwrite(out.data(), 1, out.size(), fp);
  std::fflush(fp);
  std::fclose(fp);
}

}  // namespace partyline
