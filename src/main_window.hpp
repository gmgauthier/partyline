/* SPDX-License-Identifier: Unlicense */

#pragma once

#include "irc_session.hpp"

#include <gtkmm.h>

#include <memory>

namespace partyline {

class MainWindow : public Gtk::Window {
 public:
  MainWindow();
  ~MainWindow() override;

 private:
  void load_css();
  void build_menu();
  void build_toolbar();
  void build_body();
  void fill_tree();
  void set_status(const Glib::ustring& text);
  void append_line(const Glib::ustring& text);
  void show_not_yet(const Glib::ustring& feature);
  void set_connected_ui(bool on);
  void on_servers();
  void on_connect();
  void on_disconnect();
  void on_join();
  void on_quit();
  void on_about();
  void on_session_line(const Glib::ustring& text);
  void on_session_registered();
  void on_session_finished(const Glib::ustring& reason);
  void on_toggle_tree();
  void on_toggle_nicks();
  void on_toggle_status();

  Gtk::MenuItem* add_item(Gtk::Menu& menu, const Glib::ustring& label,
                          const sigc::slot<void()>& slot, guint key = 0,
                          Gdk::ModifierType mods = Gdk::ModifierType(0));

  Gtk::Box root_{Gtk::ORIENTATION_VERTICAL, 0};
  Gtk::MenuBar menubar_;
  Gtk::Box toolbar_{Gtk::ORIENTATION_HORIZONTAL, 4};
  Gtk::Button btn_connect_{"Connect"};
  Gtk::Button btn_disconnect_{"Disconnect"};
  Gtk::Button btn_join_{"Join…"};
  Gtk::CheckMenuItem* view_tree_item_ = nullptr;
  Gtk::CheckMenuItem* view_nicks_item_ = nullptr;
  Gtk::CheckMenuItem* view_status_item_ = nullptr;
  Glib::RefPtr<Gtk::AccelGroup> accel_;

  Gtk::Paned outer_{Gtk::ORIENTATION_HORIZONTAL};
  Gtk::Paned inner_{Gtk::ORIENTATION_HORIZONTAL};
  Gtk::ScrolledWindow tree_scroll_;
  Gtk::TreeView tree_view_;
  Gtk::Box centre_{Gtk::ORIENTATION_VERTICAL, 0};
  Gtk::ScrolledWindow buffer_scroll_;
  Gtk::TextView buffer_;
  Gtk::Box input_row_{Gtk::ORIENTATION_HORIZONTAL, 4};
  Gtk::Label input_target_{"[Status]"};
  Gtk::Entry input_;
  Gtk::Button btn_send_{"Send"};
  Gtk::ScrolledWindow nick_scroll_;
  Gtk::TreeView nick_view_;
  Gtk::Statusbar status_;
  guint status_ctx_ = 0;

  Glib::RefPtr<Gtk::TreeStore> tree_store_;
  Gtk::TreeModelColumn<Glib::ustring> col_tree_name_;
  Gtk::TreeModelColumnRecord tree_cols_;

  Glib::RefPtr<Gtk::ListStore> nick_store_;
  Gtk::TreeModelColumn<Glib::ustring> col_nick_;
  Gtk::TreeModelColumnRecord nick_cols_;

  std::unique_ptr<IrcSession> session_;
  Glib::ustring connected_host_;
  Glib::ustring connected_nick_;
  bool registered_ = false;
};

}  // namespace partyline
