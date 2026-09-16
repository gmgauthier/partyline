/* SPDX-License-Identifier: Unlicense */

#pragma once

#include <gtkmm.h>

namespace partyline {

class ChannelsWindow : public Gtk::Window {
 public:
  explicit ChannelsWindow(Gtk::Window& parent);

  void clear();
  void add_row(const Glib::ustring& channel, int users, const Glib::ustring& topic);
  void set_listing(bool on);
  void finish();
  int size() const;

  sigc::signal<void, Glib::ustring> signal_join;

 private:
  bool row_visible(const Gtk::TreeModel::const_iterator& it);
  void on_filter();
  void on_row_activated(const Gtk::TreeModel::Path& path, Gtk::TreeViewColumn* col);
  void on_join_clicked();
  Glib::ustring selected_channel();

  Gtk::Box root_{Gtk::ORIENTATION_VERTICAL, 4};
  Gtk::Entry filter_;
  Gtk::ScrolledWindow scroll_;
  Gtk::TreeView view_;
  Gtk::Box bottom_{Gtk::ORIENTATION_HORIZONTAL, 8};
  Gtk::Label status_;
  Gtk::Button btn_join_{"Join"};
  Glib::RefPtr<Gtk::ListStore> store_;
  Glib::RefPtr<Gtk::TreeModelFilter> filtered_;
  Gtk::TreeModelColumn<Glib::ustring> col_name_;
  Gtk::TreeModelColumn<int> col_users_;
  Gtk::TreeModelColumn<Glib::ustring> col_topic_;
  Gtk::TreeModelColumnRecord cols_;
  int n_ = 0;
};

}  // namespace partyline
