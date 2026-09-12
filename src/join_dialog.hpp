/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <gtkmm.h>

namespace partyline {

class JoinDialog : public Gtk::Dialog {
 public:
  explicit JoinDialog(Gtk::Window& parent);
  Glib::ustring channel() const;

 private:
  Gtk::Entry channel_;
};

}  // namespace partyline
