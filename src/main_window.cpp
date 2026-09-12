/* SPDX-License-Identifier: Unlicense */

#include "main_window.hpp"
#include "about_dialog.hpp"
#include "join_dialog.hpp"
#include "paths.hpp"
#include "servers_dialog.hpp"

#include <glib.h>

#include <algorithm>
#include <iostream>

namespace partyline {
namespace {

Gtk::Separator* toolbar_sep()
{
  auto* sep = Gtk::manage(new Gtk::Separator(Gtk::ORIENTATION_VERTICAL));
  sep->set_margin_start(6);
  sep->set_margin_end(6);
  return sep;
}

void paint_nav_cell(Gtk::CellRenderer* cell, const Gtk::TreeModel::Path& path,
                    const Gtk::TreeModel::Path& current, const Gtk::TreeModel::Path& hover)
{
  if (!cell)
    return;
  /* Paint both cell-background and CellRendererText background. Adwaita
   * ignores the former when the view is unfocused (i3 / Debian). */
  const bool is_cur = current.size() > 0 && path.size() > 0 && path == current;
  const bool is_hov = hover.size() > 0 && path.size() > 0 && path == hover;
  auto* text = dynamic_cast<Gtk::CellRendererText*>(cell);
  if (is_cur || is_hov) {
    const char* color = is_cur ? "#8AADC8" : "#C5D4E8";
    cell->property_cell_background() = color;
    cell->property_cell_background_set() = true;
    if (text) {
      text->property_background() = color;
      text->property_background_set() = true;
    }
  } else {
    cell->property_cell_background_set() = false;
    if (text)
      text->property_background_set() = false;
  }
}

bool nav_motion(Gtk::TreeView& view, Gtk::TreeModel::Path& hover, GdkEventMotion* event)
{
  Gtk::TreeModel::Path path;
  Gtk::TreeViewColumn* col = nullptr;
  int cx = 0, cy = 0, bx = 0, by = 0;
  view.convert_widget_to_bin_window_coords(static_cast<int>(event->x),
                                           static_cast<int>(event->y), bx, by);
  if (view.get_path_at_pos(bx, by, path, col, cx, cy) && path.size() > 0) {
    if (hover.size() == 0 || hover != path) {
      hover = path;
      view.queue_draw();
    }
  } else if (hover.size() > 0) {
    hover.clear();
    view.queue_draw();
  }
  return false;
}

bool nav_leave(Gtk::TreeView& view, Gtk::TreeModel::Path& hover, GdkEventCrossing* event)
{
  if (event && event->detail == GDK_NOTIFY_INFERIOR)
    return false;
  if (hover.size() > 0) {
    hover.clear();
    view.queue_draw();
  }
  return false;
}

void scroll_end(Gtk::TextView& view)
{
  auto buf = view.get_buffer();
  auto mark = buf->create_mark("end", buf->end(), false);
  view.scroll_to(mark, 0.0);
  buf->delete_mark(mark);
}

Glib::ustring ensure_utf8(const Glib::ustring& text)
{
  const std::string raw = text.raw();
  if (raw.empty() || g_utf8_validate(raw.data(), static_cast<gssize>(raw.size()), nullptr))
    return text;
  gchar* v = g_utf8_make_valid(raw.data(), static_cast<gssize>(raw.size()));
  Glib::ustring u(v ? v : "");
  g_free(v);
  return u;
}

Glib::ustring strip_nick_prefix(const Glib::ustring& n)
{
  if (n.empty())
    return n;
  const gunichar c = n[0];
  if (c == '@' || c == '+' || c == '%' || c == '~' || c == '&')
    return n.substr(1);
  return n;
}

bool nick_eq(const Glib::ustring& a, const Glib::ustring& b)
{
  return g_ascii_strcasecmp(strip_nick_prefix(a).c_str(), strip_nick_prefix(b).c_str()) == 0;
}

}  // namespace

MainWindow::MainWindow()
{
  settings_.load();
  set_title("Partyline");
  set_default_size(settings_.window_w > 0 ? settings_.window_w : 900,
                   settings_.window_h > 0 ? settings_.window_h : 600);
  set_border_width(0);
  get_style_context()->add_class("partyline-window");

  accel_ = Gtk::AccelGroup::create();
  add_accel_group(accel_);

  load_css();
  build_menu();
  build_toolbar();
  build_body();

  status_ctx_ = status_.get_context_id("main");
  set_status("Not connected.");

  add(root_);
  show_all();
  if (settings_.palette >= 0 && settings_.palette < 5 && pal_item_[settings_.palette])
    pal_item_[settings_.palette]->set_active(true);
  apply_palette();
  signal_hide().connect(sigc::mem_fun(*this, &MainWindow::persist));
}

MainWindow::~MainWindow()
{
  stop_lag_timer();
  persist();
  if (session_)
    session_->stop();
}

void MainWindow::load_css()
{
  const std::string css_path = find_data_file("skin/lcos/lcos.css");
  if (css_path.empty()) {
    std::cerr << "partyline: lcos.css not found\n";
    return;
  }
  try {
    auto css = Gtk::CssProvider::create();
    css->load_from_path(css_path);
    Gtk::StyleContext::add_provider_for_screen(
        Gdk::Screen::get_default(), css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  } catch (const Glib::Error& e) {
    std::cerr << "partyline: CSS: " << e.what() << "\n";
  }
}

Gtk::MenuItem* MainWindow::add_item(Gtk::Menu& menu, const Glib::ustring& label,
                                    const sigc::slot<void()>& slot, guint key,
                                    Gdk::ModifierType mods)
{
  auto* item = Gtk::manage(new Gtk::MenuItem(label, true));
  item->signal_activate().connect(slot);
  if (key != 0)
    item->add_accelerator("activate", accel_, key, mods, Gtk::ACCEL_VISIBLE);
  menu.append(*item);
  return item;
}

void MainWindow::build_menu()
{
  auto add_menu = [this](const Glib::ustring& label, Gtk::Menu& menu) {
    auto* top = Gtk::manage(new Gtk::MenuItem(label, true));
    top->set_submenu(menu);
    menubar_.append(*top);
  };

  auto* file = Gtk::manage(new Gtk::Menu());
  add_item(*file, "_Servers…", sigc::mem_fun(*this, &MainWindow::on_servers));
  add_item(*file, "_Connect", sigc::mem_fun(*this, &MainWindow::on_connect), GDK_KEY_o,
           Gdk::CONTROL_MASK);
  add_item(*file, "_Disconnect", sigc::mem_fun(*this, &MainWindow::on_disconnect));
  file->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  add_item(*file, "E_xit", sigc::mem_fun(*this, &MainWindow::on_quit));
  add_menu("_File", *file);

  auto* view = Gtk::manage(new Gtk::Menu());
  view_tree_item_ = Gtk::manage(new Gtk::CheckMenuItem("_Tree", true));
  view_tree_item_->set_active(true);
  view_tree_item_->signal_toggled().connect(sigc::mem_fun(*this, &MainWindow::on_toggle_tree));
  view->append(*view_tree_item_);
  view_nicks_item_ = Gtk::manage(new Gtk::CheckMenuItem("_Nick list", true));
  view_nicks_item_->set_active(true);
  view_nicks_item_->signal_toggled().connect(sigc::mem_fun(*this, &MainWindow::on_toggle_nicks));
  view->append(*view_nicks_item_);
  view_status_item_ = Gtk::manage(new Gtk::CheckMenuItem("_Status bar", true));
  view_status_item_->set_active(true);
  view_status_item_->signal_toggled().connect(
      sigc::mem_fun(*this, &MainWindow::on_toggle_status));
  view->append(*view_status_item_);
  view->append(*Gtk::manage(new Gtk::SeparatorMenuItem()));
  auto* pal_menu = Gtk::manage(new Gtk::Menu());
  Gtk::RadioButtonGroup pal_grp;
  const char* pal_labels[] = {"_White", "_Eggshell", "_Black", "_Navy", "_Olive"};
  for (int i = 0; i < 5; ++i) {
    pal_item_[i] = Gtk::manage(new Gtk::RadioMenuItem(pal_grp, pal_labels[i], true));
    pal_item_[i]->signal_toggled().connect([this, i]() { on_palette(i); });
    pal_menu->append(*pal_item_[i]);
  }
  auto* pal_top = Gtk::manage(new Gtk::MenuItem("_Palette", true));
  pal_top->set_submenu(*pal_menu);
  view->append(*pal_top);
  add_menu("_View", *view);

  auto* tools = Gtk::manage(new Gtk::Menu());
  add_item(*tools, "_Join…", sigc::mem_fun(*this, &MainWindow::on_join), GDK_KEY_j,
           Gdk::CONTROL_MASK);
  add_menu("_Tools", *tools);

  auto* help = Gtk::manage(new Gtk::Menu());
  add_item(*help, "_About Partyline", sigc::mem_fun(*this, &MainWindow::on_about));
  add_menu("_Help", *help);
}

void MainWindow::build_toolbar()
{
  toolbar_.set_border_width(4);
  toolbar_.pack_start(btn_connect_, Gtk::PACK_SHRINK);
  toolbar_.pack_start(btn_disconnect_, Gtk::PACK_SHRINK);
  toolbar_.pack_start(*toolbar_sep(), Gtk::PACK_SHRINK);
  toolbar_.pack_start(btn_join_, Gtk::PACK_SHRINK);

  btn_disconnect_.set_sensitive(false);
  btn_join_.set_sensitive(false);
  btn_connect_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_connect));
  btn_disconnect_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_disconnect));
  btn_join_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_join));
}

void MainWindow::build_body()
{
  tree_cols_.add(col_tree_name_);
  tree_cols_.add(col_tree_kind_);
  tree_cols_.add(col_server_id_);
  tree_store_ = Gtk::TreeStore::create(tree_cols_);
  tree_view_.set_model(tree_store_);
  tree_view_.append_column("Servers", col_tree_name_);
  tree_view_.set_headers_visible(false);
  tree_view_.set_enable_search(false);
  tree_view_.set_can_focus(true);
  tree_view_.get_selection()->set_mode(Gtk::SELECTION_NONE);
  tree_view_.get_style_context()->add_class("partyline-tree");
  style_nav_column(tree_view_);
  if (auto* col = tree_view_.get_column(0)) {
    const auto cells = col->get_cells();
    if (!cells.empty()) {
      if (auto* text = dynamic_cast<Gtk::CellRendererText*>(cells[0]))
        col->set_cell_data_func(*text, sigc::mem_fun(*this, &MainWindow::on_tree_cell_data));
    }
  }
  tree_view_.add_events(Gdk::POINTER_MOTION_MASK | Gdk::LEAVE_NOTIFY_MASK |
                        Gdk::BUTTON_PRESS_MASK);
  tree_view_.signal_motion_notify_event().connect(
      sigc::mem_fun(*this, &MainWindow::on_tree_motion), false);
  tree_view_.signal_leave_notify_event().connect(
      sigc::mem_fun(*this, &MainWindow::on_tree_leave), false);
  tree_view_.signal_button_press_event().connect(
      sigc::mem_fun(*this, &MainWindow::on_tree_button), false);
  tree_scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  tree_scroll_.add(tree_view_);
  tree_scroll_.set_size_request(160, -1);
  fill_tree_idle();

  status_buf_ = Gtk::TextBuffer::create();
  status_buf_->set_text(
      "Not connected.\n\n"
      "Select a server in the tree, then Connect.\n"
      "File → Servers… edits the list (Libera and OFTC are seeded).\n"
      "Join… or /join #channel once you are connected.\n");

  buffer_.set_editable(false);
  buffer_.set_wrap_mode(Gtk::WRAP_WORD_CHAR);
  buffer_.set_cursor_visible(false);
  buffer_.set_left_margin(10);
  buffer_.set_right_margin(10);
  buffer_.set_top_margin(8);
  buffer_.set_bottom_margin(8);
  buffer_.get_style_context()->add_class("partyline-buffer");
  buffer_.set_buffer(status_buf_);
  buffer_scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  buffer_scroll_.add(buffer_);

  input_target_.set_width_chars(12);
  input_.set_hexpand(true);
  input_.set_sensitive(false);
  btn_send_.set_sensitive(false);
  input_.signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_send));
  input_.signal_key_press_event().connect(sigc::mem_fun(*this, &MainWindow::on_input_key),
                                          false);
  btn_send_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_send));
  input_row_.set_border_width(4);
  input_row_.pack_start(input_target_, Gtk::PACK_SHRINK);
  input_row_.pack_start(input_, Gtk::PACK_EXPAND_WIDGET);
  input_row_.pack_start(btn_send_, Gtk::PACK_SHRINK);

  centre_.pack_start(buffer_scroll_, Gtk::PACK_EXPAND_WIDGET);
  centre_.pack_start(input_row_, Gtk::PACK_SHRINK);

  nick_cols_.add(col_nick_);
  nick_store_ = Gtk::ListStore::create(nick_cols_);
  nick_view_.set_model(nick_store_);
  nick_view_.append_column("Nicks", col_nick_);
  nick_view_.set_headers_visible(false);
  nick_view_.set_enable_search(false);
  nick_view_.set_can_focus(true);
  nick_view_.get_selection()->set_mode(Gtk::SELECTION_NONE);
  nick_view_.get_style_context()->add_class("partyline-nicks");
  style_nav_column(nick_view_);
  if (auto* col = nick_view_.get_column(0)) {
    const auto cells = col->get_cells();
    if (!cells.empty()) {
      if (auto* text = dynamic_cast<Gtk::CellRendererText*>(cells[0]))
        col->set_cell_data_func(*text, sigc::mem_fun(*this, &MainWindow::on_nick_cell_data));
    }
  }
  nick_view_.add_events(Gdk::POINTER_MOTION_MASK | Gdk::LEAVE_NOTIFY_MASK |
                        Gdk::BUTTON_PRESS_MASK);
  nick_view_.signal_motion_notify_event().connect(
      sigc::mem_fun(*this, &MainWindow::on_nick_motion), false);
  nick_view_.signal_leave_notify_event().connect(
      sigc::mem_fun(*this, &MainWindow::on_nick_leave), false);
  nick_view_.signal_button_press_event().connect(
      sigc::mem_fun(*this, &MainWindow::on_nick_button), false);
  nick_scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  nick_scroll_.add(nick_view_);
  nick_scroll_.set_size_request(140, -1);

  inner_.pack1(centre_, true, false);
  inner_.pack2(nick_scroll_, false, true);
  inner_.set_position(620);

  outer_.pack1(tree_scroll_, false, true);
  outer_.pack2(inner_, true, false);
  outer_.set_position(180);

  root_.pack_start(menubar_, Gtk::PACK_SHRINK);
  root_.pack_start(toolbar_, Gtk::PACK_SHRINK);
  root_.pack_start(outer_, Gtk::PACK_EXPAND_WIDGET);
  root_.pack_start(status_, Gtk::PACK_SHRINK);
}

void MainWindow::fill_tree_idle()
{
  suppress_tree_ = true;
  tree_hover_path_.clear();
  tree_store_->clear();
  for (const auto& s : settings_.servers) {
    auto row = *tree_store_->append();
    row[col_tree_name_] = s.name;
    row[col_tree_kind_] = 0;
    row[col_server_id_] = s.id;
    auto status = *tree_store_->append(row.children());
    status[col_tree_name_] = "Status";
    status[col_tree_kind_] = 1;
    status[col_server_id_] = s.id;
  }
  tree_view_.expand_all();
  suppress_tree_ = false;
  if (!settings_.last_server.empty())
    select_tree(1, settings_.last_server);
  else if (!settings_.servers.empty())
    select_tree(1, settings_.servers.front().id);
}

void MainWindow::fill_tree_connected()
{
  suppress_tree_ = true;
  tree_hover_path_.clear();
  tree_store_->clear();
  for (const auto& s : settings_.servers) {
    auto row = *tree_store_->append();
    row[col_tree_name_] = s.name;
    row[col_tree_kind_] = 0;
    row[col_server_id_] = s.id;
    auto status = *tree_store_->append(row.children());
    status[col_tree_name_] = "Status";
    status[col_tree_kind_] = 1;
    status[col_server_id_] = s.id;
    if (s.id == connected_server_id_.raw()) {
      for (const auto& ch : channels_) {
        auto crow = *tree_store_->append(row.children());
        crow[col_tree_name_] = ch.name;
        crow[col_tree_kind_] = 2;
        crow[col_server_id_] = s.id;
      }
    }
  }
  tree_view_.expand_all();
  suppress_tree_ = false;
}

void MainWindow::persist()
{
  int w = 0, h = 0;
  get_size(w, h);
  if (w > 0)
    settings_.window_w = w;
  if (h > 0)
    settings_.window_h = h;
  settings_.save();
}

const Server* MainWindow::selected_server()
{
  if (tree_current_path_.size() == 0)
    return nullptr;
  auto it = tree_store_->get_iter(tree_current_path_);
  if (!it)
    return nullptr;
  const Glib::ustring id = (*it)[col_server_id_];
  return settings_.find_id(id.raw());
}

void MainWindow::select_tree(int kind, const Glib::ustring& server_id,
                             const Glib::ustring& channel)
{
  suppress_tree_ = true;
  tree_store_->foreach_iter([this, kind, server_id, channel](const Gtk::TreeModel::iterator& it) {
    if ((*it)[col_tree_kind_] != kind || (*it)[col_server_id_] != server_id)
      return false;
    if (kind == 2 && !same_chan((*it)[col_tree_name_], channel))
      return false;
    const Gtk::TreeModel::Path path(it);
    tree_current_path_ = path;
    tree_view_.scroll_to_row(path);
    tree_view_.queue_draw();
    return true;
  });
  suppress_tree_ = false;
}

void MainWindow::set_status(const Glib::ustring& text)
{
  status_.pop(status_ctx_);
  status_.push(text, status_ctx_);
}

void MainWindow::refresh_status_bar()
{
  if (!registered_)
    return;
  Glib::ustring s = connected_nick_ + " @ " + connected_host_;
  if (session_ && session_->tls())
    s += "  tls";
  if (session_) {
    const int lag = session_->lag_ms();
    if (lag >= 0) {
      char buf[32];
      g_snprintf(buf, sizeof(buf), "  lag %.1fs", lag / 1000.0);
      s += buf;
    }
  }
  if (const Chan* ch = find_chan(current_channel_))
    s += "  " + ch->name + "  " + std::to_string(ch->nicks.size()) + " users";
  set_status(s);
}

void MainWindow::append_status(const Glib::ustring& text)
{
  status_buf_->insert(status_buf_->end(), ensure_utf8(text) + "\n");
  if (pane_ == Pane::Status)
    scroll_end(buffer_);
}

void MainWindow::append_channel(const Glib::ustring& channel, const Glib::ustring& text)
{
  Chan* ch = find_chan(channel);
  if (!ch)
    return;
  ch->buf->insert(ch->buf->end(), ensure_utf8(text) + "\n");
  if (pane_ == Pane::Channel && same_chan(current_channel_, channel))
    scroll_end(buffer_);
}

void MainWindow::show_pane(Pane pane)
{
  pane_ = pane;
  if (pane == Pane::Status) {
    buffer_.set_buffer(status_buf_);
    input_target_.set_text("[Status]");
    scroll_end(buffer_);
    nick_store_->clear();
  } else {
    Chan* ch = find_chan(current_channel_);
    if (!ch) {
      show_pane(Pane::Status);
      return;
    }
    buffer_.set_buffer(ch->buf);
    input_target_.set_text(ch->name);
    scroll_end(buffer_);
    refresh_nicks();
  }
  refresh_status_bar();
}

void MainWindow::show_not_yet(const Glib::ustring& feature)
{
  Gtk::MessageDialog dlg(*this, feature + " is later.", false, Gtk::MESSAGE_INFO,
                         Gtk::BUTTONS_OK, true);
  dlg.set_title("Partyline");
  dlg.set_secondary_text("Polish (keys, palettes, /nick) is M5.");
  dlg.run();
}

void MainWindow::set_connected_ui(bool on)
{
  btn_connect_.set_sensitive(!on);
  btn_disconnect_.set_sensitive(on);
  btn_join_.set_sensitive(on && registered_);
  input_.set_sensitive(on && registered_);
  btn_send_.set_sensitive(on && registered_);
  if (!on) {
    registered_ = false;
    reset_channels();
    fill_tree_idle();
    show_pane(Pane::Status);
    set_title("Partyline");
    set_status("Not connected.");
  }
}

MainWindow::Chan* MainWindow::find_chan(const Glib::ustring& name)
{
  for (auto& ch : channels_) {
    if (same_chan(ch.name, name))
      return &ch;
  }
  return nullptr;
}

const MainWindow::Chan* MainWindow::find_chan(const Glib::ustring& name) const
{
  for (const auto& ch : channels_) {
    if (same_chan(ch.name, name))
      return &ch;
  }
  return nullptr;
}

void MainWindow::reset_channels()
{
  channels_.clear();
  current_channel_.clear();
  nick_hover_path_.clear();
  nick_current_path_.clear();
  nick_store_->clear();
}

void MainWindow::show_channel(const Glib::ustring& channel)
{
  if (!find_chan(channel))
    return;
  current_channel_ = find_chan(channel)->name;
  fill_tree_connected();
  select_tree(2, connected_server_id_, current_channel_);
  show_pane(Pane::Channel);
}

void MainWindow::refresh_nicks()
{
  Glib::ustring kept;
  if (nick_current_path_.size() > 0) {
    auto it = nick_store_->get_iter(nick_current_path_);
    if (it)
      kept = (*it)[col_nick_];
  }
  nick_hover_path_.clear();
  nick_current_path_.clear();
  nick_store_->clear();
  Chan* ch = find_chan(current_channel_);
  if (!ch || pane_ != Pane::Channel)
    return;
  std::vector<Glib::ustring> sorted = ch->nicks;
  std::sort(sorted.begin(), sorted.end(), [](const Glib::ustring& a, const Glib::ustring& b) {
    return g_ascii_strcasecmp(a.c_str(), b.c_str()) < 0;
  });
  for (const auto& n : sorted) {
    auto row = *nick_store_->append();
    row[col_nick_] = n;
    if (!kept.empty() && nick_eq(n, kept))
      nick_current_path_ = nick_store_->get_path(row);
  }
}

void MainWindow::add_nick(const Glib::ustring& channel, const Glib::ustring& nick)
{
  Chan* ch = find_chan(channel);
  if (!ch)
    return;
  for (const auto& n : ch->nicks) {
    if (nick_eq(n, nick))
      return;
  }
  ch->nicks.push_back(nick);
  if (same_chan(current_channel_, channel)) {
    refresh_nicks();
    refresh_status_bar();
  }
}

void MainWindow::remove_nick(const Glib::ustring& channel, const Glib::ustring& nick)
{
  Chan* ch = find_chan(channel);
  if (!ch)
    return;
  ch->nicks.erase(std::remove_if(ch->nicks.begin(), ch->nicks.end(),
                                 [&](const Glib::ustring& n) { return nick_eq(n, nick); }),
                  ch->nicks.end());
  if (same_chan(current_channel_, channel)) {
    refresh_nicks();
    refresh_status_bar();
  }
}

void MainWindow::drop_channel(const Glib::ustring& channel)
{
  channels_.erase(std::remove_if(channels_.begin(), channels_.end(),
                                 [&](const Chan& c) { return same_chan(c.name, channel); }),
                  channels_.end());
  if (same_chan(current_channel_, channel))
    current_channel_.clear();
}

Glib::ustring MainWindow::normalize_channel(Glib::ustring c) const
{
  while (!c.empty() && c[0] == ' ')
    c = c.substr(1);
  while (!c.empty() && c[c.size() - 1] == ' ')
    c = c.substr(0, c.size() - 1);
  if (c.empty())
    return {};
  const gunichar first = c[0];
  if (first != '#' && first != '&' && first != '+' && first != '!')
    c = "#" + c;
  return c;
}

bool MainWindow::same_chan(const Glib::ustring& a, const Glib::ustring& b) const
{
  return g_ascii_strcasecmp(a.c_str(), b.c_str()) == 0;
}

void MainWindow::do_join(const Glib::ustring& channel)
{
  if (!session_ || !registered_ || channel.empty())
    return;
  if (find_chan(channel)) {
    show_channel(channel);
    return;
  }
  session_->join(channel.raw());
}

void MainWindow::handle_command(const Glib::ustring& line)
{
  Glib::ustring rest;
  Glib::ustring cmd = line.substr(1);
  const auto sp = cmd.find(' ');
  if (sp != Glib::ustring::npos) {
    rest = cmd.substr(sp + 1);
    cmd = cmd.substr(0, sp);
  }
  const Glib::ustring low = cmd.lowercase();
  if (low == "join")
    do_join(normalize_channel(rest));
  else if (low == "part") {
    Glib::ustring ch = normalize_channel(rest);
    if (ch.empty())
      ch = current_channel_;
    if (!ch.empty() && session_)
      session_->part(ch.raw());
  } else if (low == "quit")
    on_disconnect();
  else if (low == "quote") {
    if (session_ && !rest.empty())
      session_->quote(rest.raw());
  } else if (low == "nick") {
    if (session_ && !rest.empty())
      session_->change_nick(rest.raw());
  } else if (low == "msg") {
    Glib::ustring target, msg;
    const auto sp2 = rest.find(' ');
    if (sp2 == Glib::ustring::npos)
      target = rest;
    else {
      target = rest.substr(0, sp2);
      msg = rest.substr(sp2 + 1);
    }
    if (session_ && !target.empty() && !msg.empty()) {
      session_->privmsg(target.raw(), msg.raw());
      if (find_chan(target))
        append_channel(target, "<" + connected_nick_ + "> " + msg);
      else
        append_status("* -> " + target + ": " + msg);
    }
  } else if (session_)
    session_->quote(line.substr(1).raw());
}

void MainWindow::on_servers()
{
  if (session_ && session_->running()) {
    Gtk::MessageDialog dlg(*this, "Disconnect before editing servers.", false, Gtk::MESSAGE_INFO,
                           Gtk::BUTTONS_OK, true);
    dlg.run();
    return;
  }
  ServersDialog dlg(*this, settings_);
  dlg.run();
  fill_tree_idle();
}

void MainWindow::on_connect()
{
  if (session_ && session_->running())
    return;

  const Server* s = selected_server();
  if (!s)
    s = settings_.find_id(settings_.last_server);
  if (!s && !settings_.servers.empty())
    s = &settings_.servers.front();
  if (!s) {
    on_servers();
    return;
  }
  if (s->host.empty()) {
    Gtk::MessageDialog err(*this, "That server has no host.", false, Gtk::MESSAGE_ERROR,
                           Gtk::BUTTONS_OK, true);
    err.run();
    return;
  }

  Glib::ustring nick = s->nick.empty() ? settings_.nick : s->nick;
  if (nick.empty())
    nick = Settings::default_nick();

  connected_host_ = s->host;
  connected_nick_ = nick;
  connected_server_id_ = s->id;
  settings_.last_server = s->id;
  settings_.save();

  registered_ = false;
  reset_channels();
  status_buf_->set_text("");
  show_pane(Pane::Status);
  fill_tree_connected();
  select_tree(1, connected_server_id_);
  set_status(Glib::ustring("Connecting to ") + s->name + "…");
  set_title("Partyline — " + nick + " @ " + s->name);
  btn_connect_.set_sensitive(false);
  btn_disconnect_.set_sensitive(true);

  session_ = std::make_unique<IrcSession>();
  session_->signal_line.connect(sigc::mem_fun(*this, &MainWindow::on_session_line));
  session_->signal_registered.connect(sigc::mem_fun(*this, &MainWindow::on_session_registered));
  session_->signal_finished.connect(sigc::mem_fun(*this, &MainWindow::on_session_finished));
  session_->signal_privmsg.connect(sigc::mem_fun(*this, &MainWindow::on_session_privmsg));
  session_->signal_join.connect(sigc::mem_fun(*this, &MainWindow::on_session_join));
  session_->signal_part.connect(sigc::mem_fun(*this, &MainWindow::on_session_part));
  session_->signal_quit_nick.connect(sigc::mem_fun(*this, &MainWindow::on_session_quit));
  session_->signal_names.connect(sigc::mem_fun(*this, &MainWindow::on_session_names));
  session_->signal_nick.connect(sigc::mem_fun(*this, &MainWindow::on_session_nick));
  session_->signal_lag.connect(sigc::mem_fun(*this, &MainWindow::on_session_lag));
  session_->start(s->host, static_cast<guint16>(s->port), s->tls, s->tls_verify, nick.raw(),
                  settings_.realname.empty() ? nick.raw() : settings_.realname);
}

void MainWindow::on_disconnect()
{
  if (session_)
    session_->stop();
}

void MainWindow::on_join()
{
  if (!registered_ || !session_)
    return;
  JoinDialog dlg(*this);
  if (dlg.run() != Gtk::RESPONSE_OK)
    return;
  do_join(dlg.channel());
}

void MainWindow::on_send()
{
  if (!session_ || !registered_)
    return;
  Glib::ustring text = input_.get_text();
  input_.set_text("");
  if (text.empty())
    return;
  history_.push_back(text);
  history_pos_ = -1;
  history_draft_.clear();
  tab_index_ = -1;
  if (text[0] == '/') {
    handle_command(text);
    return;
  }
  if (pane_ != Pane::Channel || current_channel_.empty()) {
    append_status("* not on a channel — /join #name or Join…");
    show_pane(Pane::Status);
    return;
  }
  session_->privmsg(current_channel_.raw(), text.raw());
  append_channel(current_channel_, "<" + connected_nick_ + "> " + text);
}

void MainWindow::on_quit()
{
  if (session_)
    session_->stop();
  hide();
}

void MainWindow::on_session_line(const Glib::ustring& text)
{
  append_status(text);
}

void MainWindow::on_session_registered()
{
  registered_ = true;
  if (session_)
    connected_nick_ = session_->nick();
  settings_.nick = connected_nick_.raw();
  settings_.last_server = connected_server_id_.raw();
  settings_.save();
  btn_join_.set_sensitive(true);
  input_.set_sensitive(true);
  btn_send_.set_sensitive(true);
  start_lag_timer();
  if (session_)
    session_->send_lag_ping();
  refresh_status_bar();
}

void MainWindow::on_session_finished(const Glib::ustring& reason)
{
  stop_lag_timer();
  append_status("*** " + reason);
  set_connected_ui(false);
}

void MainWindow::on_session_privmsg(const Glib::ustring& target, const Glib::ustring& nick,
                                    const Glib::ustring& text)
{
  if (find_chan(target))
    append_channel(target, "<" + nick + "> " + text);
  else if (nick_eq(target, connected_nick_))
    append_status("* " + nick + ": " + text);
}

void MainWindow::on_session_join(const Glib::ustring& channel, const Glib::ustring& nick, bool me)
{
  if (me) {
    if (!find_chan(channel)) {
      Chan ch;
      ch.name = channel;
      ch.buf = Gtk::TextBuffer::create();
      channels_.push_back(std::move(ch));
    }
    append_channel(channel, "* Now talking in " + channel);
    show_channel(channel);
    return;
  }
  if (find_chan(channel)) {
    add_nick(channel, nick);
    append_channel(channel, "* " + nick + " has joined " + channel);
  }
}

void MainWindow::on_session_part(const Glib::ustring& channel, const Glib::ustring& nick, bool me)
{
  if (me) {
    append_channel(channel, "* You have left " + channel);
    const bool was_current = same_chan(current_channel_, channel);
    drop_channel(channel);
    fill_tree_connected();
    if (was_current && !channels_.empty())
      show_channel(channels_.back().name);
    else if (was_current) {
      select_tree(1, connected_server_id_);
      show_pane(Pane::Status);
    } else if (!current_channel_.empty())
      select_tree(2, connected_server_id_, current_channel_);
    return;
  }
  if (find_chan(channel)) {
    remove_nick(channel, nick);
    append_channel(channel, "* " + nick + " has left " + channel);
  }
}

void MainWindow::on_session_quit(const Glib::ustring& nick)
{
  for (auto& ch : channels_) {
    bool present = false;
    for (const auto& n : ch.nicks) {
      if (nick_eq(n, nick)) {
        present = true;
        break;
      }
    }
    if (!present)
      continue;
    remove_nick(ch.name, nick);
    append_channel(ch.name, "* " + nick + " has quit");
  }
}

void MainWindow::on_session_names(const Glib::ustring& channel,
                                 const std::vector<Glib::ustring>& nicks)
{
  Chan* ch = find_chan(channel);
  if (!ch)
    return;
  ch->nicks = nicks;
  if (same_chan(current_channel_, channel))
    refresh_nicks();
  refresh_status_bar();
}

void MainWindow::style_nav_column(Gtk::TreeView& view)
{
  if (auto* col = view.get_column(0)) {
    col->set_expand(true);
    const auto cells = col->get_cells();
    if (!cells.empty()) {
      if (auto* text = dynamic_cast<Gtk::CellRendererText*>(cells[0])) {
        text->property_ellipsize() = Pango::ELLIPSIZE_END;
        text->property_xpad() = 6;
      }
    }
  }
}

void MainWindow::on_tree_cell_data(Gtk::CellRenderer* cell,
                                   const Gtk::TreeModel::const_iterator& it)
{
  if (!it)
    return;
  paint_nav_cell(cell, tree_store_->get_path(it), tree_current_path_, tree_hover_path_);
}

bool MainWindow::on_tree_motion(GdkEventMotion* event)
{
  return nav_motion(tree_view_, tree_hover_path_, event);
}

bool MainWindow::on_tree_leave(GdkEventCrossing* event)
{
  return nav_leave(tree_view_, tree_hover_path_, event);
}

bool MainWindow::on_tree_button(GdkEventButton* event)
{
  if (!event || event->button != 1 || event->type != GDK_BUTTON_PRESS)
    return false;
  Gtk::TreeModel::Path path;
  Gtk::TreeViewColumn* col = nullptr;
  int cx = 0, cy = 0, bx = 0, by = 0;
  tree_view_.convert_widget_to_bin_window_coords(static_cast<int>(event->x),
                                                 static_cast<int>(event->y), bx, by);
  if (!tree_view_.get_path_at_pos(bx, by, path, col, cx, cy) || path.size() == 0)
    return false;
  apply_tree_path(path);
  return false;
}

void MainWindow::on_nick_cell_data(Gtk::CellRenderer* cell,
                                   const Gtk::TreeModel::const_iterator& it)
{
  if (!it)
    return;
  paint_nav_cell(cell, nick_store_->get_path(it), nick_current_path_, nick_hover_path_);
}

bool MainWindow::on_nick_motion(GdkEventMotion* event)
{
  return nav_motion(nick_view_, nick_hover_path_, event);
}

bool MainWindow::on_nick_leave(GdkEventCrossing* event)
{
  return nav_leave(nick_view_, nick_hover_path_, event);
}

bool MainWindow::on_nick_button(GdkEventButton* event)
{
  if (!event || event->button != 1 || event->type != GDK_BUTTON_PRESS)
    return false;
  Gtk::TreeModel::Path path;
  Gtk::TreeViewColumn* col = nullptr;
  int cx = 0, cy = 0, bx = 0, by = 0;
  nick_view_.convert_widget_to_bin_window_coords(static_cast<int>(event->x),
                                                 static_cast<int>(event->y), bx, by);
  if (!nick_view_.get_path_at_pos(bx, by, path, col, cx, cy) || path.size() == 0)
    return false;
  nick_current_path_ = path;
  nick_view_.queue_draw();
  return false;
}

void MainWindow::apply_tree_path(const Gtk::TreeModel::Path& path)
{
  tree_current_path_ = path;
  tree_view_.queue_draw();
  if (suppress_tree_)
    return;
  auto it = tree_store_->get_iter(path);
  if (!it)
    return;
  const int kind = (*it)[col_tree_kind_];
  if (kind == 2) {
    const Glib::ustring name = (*it)[col_tree_name_];
    if (find_chan(name)) {
      current_channel_ = find_chan(name)->name;
      show_pane(Pane::Channel);
      return;
    }
  }
  show_pane(Pane::Status);
}

void MainWindow::on_about()
{
  AboutDialog dlg(*this);
  dlg.run();
}

void MainWindow::on_toggle_tree()
{
  if (view_tree_item_)
    tree_scroll_.set_visible(view_tree_item_->get_active());
}

void MainWindow::on_toggle_nicks()
{
  if (view_nicks_item_)
    nick_scroll_.set_visible(view_nicks_item_->get_active());
}

void MainWindow::on_toggle_status()
{
  if (view_status_item_)
    status_.set_visible(view_status_item_->get_active());
}

void MainWindow::on_session_nick(const Glib::ustring& old_nick, const Glib::ustring& new_nick,
                                 bool me)
{
  for (auto& ch : channels_) {
    for (auto& n : ch.nicks) {
      if (!nick_eq(n, old_nick))
        continue;
      Glib::ustring pref;
      const Glib::ustring core = strip_nick_prefix(n);
      if (n != core)
        pref = n.substr(0, 1);
      n = pref + new_nick;
      append_channel(ch.name, "* " + old_nick + " is now known as " + new_nick);
    }
  }
  if (me) {
    connected_nick_ = new_nick;
    settings_.nick = new_nick.raw();
    settings_.save();
    append_status("* You are now known as " + new_nick);
  }
  refresh_nicks();
  refresh_status_bar();
}

void MainWindow::on_session_lag()
{
  refresh_status_bar();
}

void MainWindow::apply_palette()
{
  static const char* bg[] = {"#FFFFFF", "#F7F5EF", "#000000", "#0B1D38", "#3D4A1A"};
  static const char* fg[] = {"#1A1A1A", "#1A1A1A", "#C0C0C0", "#E8F2FF", "#F7F5EF"};
  int i = settings_.palette;
  if (i < 0 || i > 4)
    i = 0;
  const std::string css =
      Glib::ustring::compose(
          "textview.partyline-buffer, textview.partyline-buffer text {"
          " background-color: %1; color: %2; }",
          bg[i], fg[i])
          .raw();
  if (!palette_css_) {
    palette_css_ = Gtk::CssProvider::create();
    Gtk::StyleContext::add_provider_for_screen(
        Gdk::Screen::get_default(), palette_css_, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
  }
  try {
    palette_css_->load_from_data(css);
  } catch (const Glib::Error& e) {
    std::cerr << "partyline: palette CSS: " << e.what() << "\n";
  }
}

void MainWindow::on_palette(int id)
{
  if (id < 0 || id > 4 || !pal_item_[id] || !pal_item_[id]->get_active())
    return;
  settings_.palette = id;
  apply_palette();
  settings_.save();
}

void MainWindow::start_lag_timer()
{
  stop_lag_timer();
  lag_conn_ = Glib::signal_timeout().connect_seconds(
      sigc::mem_fun(*this, &MainWindow::on_lag_tick), 60);
}

void MainWindow::stop_lag_timer()
{
  if (lag_conn_.connected())
    lag_conn_.disconnect();
}

bool MainWindow::on_lag_tick()
{
  if (session_ && session_->running() && registered_)
    session_->send_lag_ping();
  return true;
}

void MainWindow::complete_nick()
{
  Chan* ch = find_chan(current_channel_);
  if (!ch || pane_ != Pane::Channel)
    return;
  const Glib::ustring text = input_.get_text();
  if (tab_index_ < 0) {
    Glib::ustring::size_type i = text.size();
    while (i > 0 && text[i - 1] != ' ')
      --i;
    tab_before_ = text.substr(0, i);
    tab_prefix_ = text.substr(i);
    tab_after_.clear();
    tab_matches_.clear();
    if (tab_prefix_.empty())
      return;
    for (const auto& n : ch->nicks) {
      const Glib::ustring core = strip_nick_prefix(n);
      if (g_ascii_strncasecmp(core.c_str(), tab_prefix_.c_str(),
                              static_cast<int>(tab_prefix_.size())) == 0)
        tab_matches_.push_back(core);
    }
    if (tab_matches_.empty())
      return;
    std::sort(tab_matches_.begin(), tab_matches_.end(),
              [](const Glib::ustring& a, const Glib::ustring& b) {
                return g_ascii_strcasecmp(a.c_str(), b.c_str()) < 0;
              });
    tab_index_ = 0;
  } else if (!tab_matches_.empty()) {
    tab_index_ = (tab_index_ + 1) % static_cast<int>(tab_matches_.size());
  } else {
    return;
  }
  const Glib::ustring nick = tab_matches_[static_cast<size_t>(tab_index_)];
  const Glib::ustring out = tab_before_ + nick + (tab_before_.empty() ? ": " : " ");
  input_.set_text(out);
  input_.set_position(-1);
}

void MainWindow::history_prev()
{
  if (history_.empty())
    return;
  if (history_pos_ < 0) {
    history_draft_ = input_.get_text();
    history_pos_ = static_cast<int>(history_.size()) - 1;
  } else if (history_pos_ > 0)
    --history_pos_;
  input_.set_text(history_[static_cast<size_t>(history_pos_)]);
  input_.set_position(-1);
}

void MainWindow::history_next()
{
  if (history_pos_ < 0)
    return;
  if (history_pos_ + 1 < static_cast<int>(history_.size())) {
    ++history_pos_;
    input_.set_text(history_[static_cast<size_t>(history_pos_)]);
  } else {
    history_pos_ = -1;
    input_.set_text(history_draft_);
  }
  input_.set_position(-1);
}

void MainWindow::scroll_buffer(int pages)
{
  auto adj = buffer_scroll_.get_vadjustment();
  if (!adj)
    return;
  adj->set_value(adj->get_value() + pages * adj->get_page_size());
}

bool MainWindow::on_input_key(GdkEventKey* event)
{
  if (!event)
    return false;
  if (event->keyval != GDK_KEY_Tab && event->keyval != GDK_KEY_ISO_Left_Tab)
    tab_index_ = -1;
  if (event->keyval == GDK_KEY_Tab) {
    complete_nick();
    return true;
  }
  if (event->keyval == GDK_KEY_Up) {
    history_prev();
    return true;
  }
  if (event->keyval == GDK_KEY_Down) {
    history_next();
    return true;
  }
  if (event->keyval == GDK_KEY_Page_Up) {
    scroll_buffer(-1);
    return true;
  }
  if (event->keyval == GDK_KEY_Page_Down) {
    scroll_buffer(1);
    return true;
  }
  return false;
}

}  // namespace partyline
