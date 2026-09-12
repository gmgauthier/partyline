/* SPDX-License-Identifier: Unlicense */

#pragma once

#include "irc_session.hpp"
#include "settings.hpp"

#include <gtkmm.h>

#include <memory>
#include <vector>

namespace partyline {

class MainWindow : public Gtk::Window {
 public:
  MainWindow();
  ~MainWindow() override;

 private:
  enum class Pane { Status, Channel };

  void load_css();
  void build_menu();
  void build_toolbar();
  void build_body();
  void fill_tree_idle();
  void fill_tree_connected();
  void persist();
  const Server* selected_server();
  void select_tree(int kind, const Glib::ustring& server_id, const Glib::ustring& channel = {});
  void set_status(const Glib::ustring& text);
  void refresh_status_bar();
  void append_status(const Glib::ustring& text);
  void append_channel(const Glib::ustring& channel, const Glib::ustring& text);
  void show_pane(Pane pane);
  void show_not_yet(const Glib::ustring& feature);
  void set_connected_ui(bool on);
  void reset_channels();
  void show_channel(const Glib::ustring& channel);
  void refresh_nicks();
  void add_nick(const Glib::ustring& channel, const Glib::ustring& nick);
  void remove_nick(const Glib::ustring& channel, const Glib::ustring& nick);
  void drop_channel(const Glib::ustring& channel);
  Glib::ustring normalize_channel(Glib::ustring c) const;
  bool same_chan(const Glib::ustring& a, const Glib::ustring& b) const;
  void do_join(const Glib::ustring& channel);
  void handle_command(const Glib::ustring& line);
  void on_servers();
  void on_connect();
  void on_disconnect();
  void on_join();
  void on_send();
  void on_quit();
  void on_about();
  void on_session_line(const Glib::ustring& text);
  void on_session_registered();
  void on_session_finished(const Glib::ustring& reason);
  void on_session_privmsg(const Glib::ustring& target, const Glib::ustring& nick,
                          const Glib::ustring& text);
  void on_session_join(const Glib::ustring& channel, const Glib::ustring& nick, bool me);
  void on_session_part(const Glib::ustring& channel, const Glib::ustring& nick, bool me);
  void on_session_quit(const Glib::ustring& nick);
  void on_session_names(const Glib::ustring& channel, const std::vector<Glib::ustring>& nicks);
  void style_tree_column();
  void on_tree_cell_data(Gtk::CellRenderer* cell, const Gtk::TreeModel::const_iterator& it);
  bool on_tree_motion(GdkEventMotion* event);
  bool on_tree_leave(GdkEventCrossing* event);
  bool on_tree_button(GdkEventButton* event);
  void apply_tree_path(const Gtk::TreeModel::Path& path);
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
  Gtk::TreeModelColumn<int> col_tree_kind_;
  Gtk::TreeModelColumn<Glib::ustring> col_server_id_;
  Gtk::TreeModelColumnRecord tree_cols_;

  Glib::RefPtr<Gtk::ListStore> nick_store_;
  Gtk::TreeModelColumn<Glib::ustring> col_nick_;
  Gtk::TreeModelColumnRecord nick_cols_;

  Glib::RefPtr<Gtk::TextBuffer> status_buf_;

  struct Chan {
    Glib::ustring name;
    Glib::RefPtr<Gtk::TextBuffer> buf;
    std::vector<Glib::ustring> nicks;
  };
  std::vector<Chan> channels_;
  Chan* find_chan(const Glib::ustring& name);
  const Chan* find_chan(const Glib::ustring& name) const;

  Settings settings_;
  std::unique_ptr<IrcSession> session_;
  Glib::ustring connected_host_;
  Glib::ustring connected_nick_;
  Glib::ustring connected_server_id_;
  Glib::ustring current_channel_;
  bool registered_ = false;
  bool suppress_tree_ = false;
  Pane pane_ = Pane::Status;
  Gtk::TreeModel::Path tree_current_path_;
  Gtk::TreeModel::Path tree_hover_path_;
};

}  // namespace partyline
