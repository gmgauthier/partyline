/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <gtkmm.h>

namespace partyline {

class ConnectDialog : public Gtk::Dialog {
 public:
  explicit ConnectDialog(Gtk::Window& parent);

  Glib::ustring host() const
  {
    return host_.get_text();
  }
  guint16 port() const
  {
    return static_cast<guint16>(port_.get_value_as_int());
  }
  bool tls() const
  {
    return tls_.get_active();
  }
  Glib::ustring nick() const
  {
    return nick_.get_text();
  }

 private:
  Gtk::Grid grid_;
  Gtk::Entry host_;
  Gtk::SpinButton port_;
  Gtk::CheckButton tls_{"TLS"};
  Gtk::Entry nick_;
};

}  // namespace partyline
