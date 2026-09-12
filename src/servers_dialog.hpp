/* SPDX-License-Identifier: Unlicense */

#pragma once

#include "settings.hpp"

#include <gtkmm.h>

namespace partyline {

class ServersDialog : public Gtk::Dialog {
 public:
  ServersDialog(Gtk::Window& parent, Settings& settings);

 private:
  void refill();
  void load_row();
  void store_row();
  void on_add_server();
  void on_remove_server();
  void on_ok();
  void unique_id(Server& s);

  Settings& settings_;
  std::vector<Server> working_;
  int current_ = -1;

  Gtk::Box body_{Gtk::ORIENTATION_HORIZONTAL, 8};
  Gtk::Box left_{Gtk::ORIENTATION_VERTICAL, 4};
  Gtk::ScrolledWindow list_scroll_;
  Gtk::TreeView list_;
  Gtk::Box list_btns_{Gtk::ORIENTATION_HORIZONTAL, 4};
  Gtk::Button btn_add_{"Add"};
  Gtk::Button btn_remove_{"Remove"};
  Gtk::Grid grid_;
  Gtk::Entry name_;
  Gtk::Entry host_;
  Gtk::SpinButton port_;
  Gtk::CheckButton tls_{"TLS"};
  Gtk::Entry nick_;
  Gtk::Entry default_nick_;

  Glib::RefPtr<Gtk::ListStore> store_;
  Gtk::TreeModelColumn<Glib::ustring> col_name_;
  Gtk::TreeModelColumn<int> col_index_;
  Gtk::TreeModelColumnRecord cols_;
  bool suppress_ = false;
};

}  // namespace partyline
