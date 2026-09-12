/* SPDX-License-Identifier: Unlicense */

#include "join_dialog.hpp"

namespace partyline {

JoinDialog::JoinDialog(Gtk::Window& parent)
    : Gtk::Dialog("Join", parent, true)
{
  set_resizable(false);
  add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  add_button("_Join", Gtk::RESPONSE_OK);
  set_default_response(Gtk::RESPONSE_OK);

  channel_.set_text("#");
  channel_.set_activates_default(true);
  channel_.set_width_chars(24);

  auto* box = get_content_area();
  box->set_border_width(12);
  box->set_spacing(8);
  auto* l = Gtk::manage(new Gtk::Label("C_hannel", true));
  l->set_mnemonic_widget(channel_);
  box->pack_start(*l, Gtk::PACK_SHRINK);
  box->pack_start(channel_, Gtk::PACK_SHRINK);
  show_all_children();
  channel_.grab_focus();
  channel_.select_region(1, 1);
}

Glib::ustring JoinDialog::channel() const
{
  Glib::ustring c = channel_.get_text();
  while (!c.empty() && (c[0] == ' ' || c[c.size() - 1] == ' ')) {
    if (c[0] == ' ')
      c = c.substr(1);
    else
      c = c.substr(0, c.size() - 1);
  }
  if (c.empty())
    return {};
  const gunichar first = c[0];
  if (first != '#' && first != '&' && first != '+' && first != '!')
    c = "#" + c;
  return c;
}

}  // namespace partyline
