/* SPDX-License-Identifier: Unlicense */

#include "settings.hpp"

#include <glib.h>
#include <glibmm/fileutils.h>
#include <glibmm/keyfile.h>
#include <glibmm/miscutils.h>

#include <cctype>

namespace partyline {
namespace {

std::string config_dir()
{
  return Glib::build_filename(Glib::get_user_config_dir(), "partyline");
}

std::string config_path()
{
  return Glib::build_filename(config_dir(), "partyline.ini");
}

int get_int(Glib::KeyFile& kf, const char* group, const char* key, int fallback)
{
  try {
    if (kf.has_key(group, key))
      return kf.get_integer(group, key);
  } catch (const Glib::Error&) {
  }
  return fallback;
}

bool get_bool(Glib::KeyFile& kf, const char* group, const char* key, bool fallback)
{
  try {
    if (kf.has_key(group, key))
      return kf.get_boolean(group, key);
  } catch (const Glib::Error&) {
  }
  return fallback;
}

std::string get_str(Glib::KeyFile& kf, const Glib::ustring& group, const char* key)
{
  try {
    if (kf.has_key(group, key))
      return kf.get_string(group, key);
  } catch (const Glib::Error&) {
  }
  return {};
}

}  // namespace

std::string Settings::default_nick()
{
  const char* raw = g_get_user_name();
  const std::string n = raw ? raw : "user";
  std::string out;
  for (unsigned char c : n) {
    const bool special = c == '[' || c == ']' || c == '\\' || c == '`' || c == '_' || c == '^' ||
                         c == '{' || c == '|' || c == '}' || c == '-';
    if (out.empty()) {
      if (std::isalpha(c) || (special && c != '-'))
        out.push_back(static_cast<char>(c));
    } else if (std::isalnum(c) || special) {
      out.push_back(static_cast<char>(c));
    }
    if (out.size() >= 16)
      break;
  }
  if (out.empty())
    out = "user";
  return out;
}

std::string Settings::make_id(const std::string& name)
{
  std::string id;
  for (unsigned char c : name) {
    if (std::isalnum(c))
      id.push_back(static_cast<char>(std::tolower(c)));
    else if (!id.empty() && id.back() != '_')
      id.push_back('_');
    if (id.size() >= 24)
      break;
  }
  while (!id.empty() && id.back() == '_')
    id.pop_back();
  if (id.empty())
    id = "server";
  return id;
}

void Settings::seed_if_empty()
{
  if (!servers.empty())
    return;
  servers.push_back({"libera", "Libera", "irc.libera.chat", 6697, true, {}});
  servers.push_back({"oftc", "OFTC", "irc.oftc.net", 6697, true, {}});
}

Server* Settings::find_id(const std::string& id)
{
  for (auto& s : servers) {
    if (s.id == id)
      return &s;
  }
  return nullptr;
}

const Server* Settings::find_id(const std::string& id) const
{
  for (const auto& s : servers) {
    if (s.id == id)
      return &s;
  }
  return nullptr;
}

void Settings::load()
{
  if (nick.empty())
    nick = default_nick();
  Glib::KeyFile kf;
  try {
    kf.load_from_file(config_path());
  } catch (const Glib::Error&) {
    seed_if_empty();
    return;
  }
  const std::string n = get_str(kf, "user", "nick");
  if (!n.empty())
    nick = n;
  const std::string rn = get_str(kf, "user", "realname");
  if (!rn.empty())
    realname = rn;
  last_server = get_str(kf, "user", "last_server");
  window_w = get_int(kf, "ui", "width", window_w);
  window_h = get_int(kf, "ui", "height", window_h);
  palette = get_int(kf, "ui", "palette", palette);
  if (window_w < 400)
    window_w = 400;
  if (window_h < 300)
    window_h = 300;
  if (palette < 0 || palette > 4)
    palette = 0;

  servers.clear();
  for (const Glib::ustring& group : kf.get_groups()) {
    const std::string g = group.raw();
    const char prefix[] = "server.";
    if (g.compare(0, 7, prefix) != 0)
      continue;
    Server s;
    s.id = g.substr(7);
    s.name = get_str(kf, group, "name");
    if (s.name.empty())
      s.name = s.id;
    s.host = get_str(kf, group, "host");
    s.port = get_int(kf, group.raw().c_str(), "port", 6697);
    if (s.port < 1 || s.port > 65535)
      s.port = 6697;
    s.tls = get_bool(kf, group.raw().c_str(), "tls", true);
    s.nick = get_str(kf, group, "nick");
    if (!s.host.empty())
      servers.push_back(std::move(s));
  }
  seed_if_empty();
}

void Settings::save() const
{
  g_mkdir_with_parents(config_dir().c_str(), 0700);
  Glib::KeyFile kf;
  kf.set_string("user", "nick", nick);
  kf.set_string("user", "realname", realname);
  kf.set_string("user", "last_server", last_server);
  kf.set_integer("ui", "width", window_w);
  kf.set_integer("ui", "height", window_h);
  kf.set_integer("ui", "palette", palette);
  for (const auto& s : servers) {
    const std::string group = "server." + s.id;
    kf.set_string(group, "name", s.name);
    kf.set_string(group, "host", s.host);
    kf.set_integer(group, "port", s.port);
    kf.set_boolean(group, "tls", s.tls);
    kf.set_string(group, "nick", s.nick);
  }
  try {
    kf.save_to_file(config_path());
  } catch (const Glib::Error&) {
  }
}

}  // namespace partyline
