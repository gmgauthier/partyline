/* SPDX-License-Identifier: Unlicense */

#include "connect_dialog.hpp"

#include <glib.h>

#include <cctype>
#include <string>

namespace partyline {
namespace {

Glib::ustring default_nick()
{
  const char* raw = g_get_user_name();
  const std::string n = raw ? raw : "user";
  Glib::ustring out;
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

}  // namespace

ConnectDialog::ConnectDialog(Gtk::Window& parent)
    : Gtk::Dialog("Connect", parent, true),
      port_(Gtk::Adjustment::create(6697, 1, 65535, 1, 10))
{
  set_resizable(false);
  add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  add_button("_Connect", Gtk::RESPONSE_OK);
  set_default_response(Gtk::RESPONSE_OK);

  host_.set_text("irc.libera.chat");
  host_.set_activates_default(true);
  port_.set_numeric(true);
  tls_.set_active(true);
  nick_.set_text(default_nick());
  nick_.set_activates_default(true);
  nick_.set_max_length(16);

  grid_.set_row_spacing(8);
  grid_.set_column_spacing(8);
  grid_.set_border_width(12);

  auto add_row = [this](int row, const Glib::ustring& label, Gtk::Widget& w) {
    auto* l = Gtk::manage(new Gtk::Label(label, true));
    l->set_halign(Gtk::ALIGN_START);
    grid_.attach(*l, 0, row, 1, 1);
    grid_.attach(w, 1, row, 1, 1);
  };
  add_row(0, "_Host", host_);
  add_row(1, "_Port", port_);
  grid_.attach(tls_, 1, 2, 1, 1);
  add_row(3, "_Nick", nick_);

  get_content_area()->pack_start(grid_, Gtk::PACK_SHRINK);
  show_all_children();
}

}  // namespace partyline
