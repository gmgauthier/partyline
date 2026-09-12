/* SPDX-License-Identifier: Unlicense */

#include "main_window.hpp"
#include "about_dialog.hpp"
#include "connect_dialog.hpp"
#include "join_dialog.hpp"
#include "paths.hpp"

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

void scroll_end(Gtk::TextView& view)
{
  auto buf = view.get_buffer();
  auto mark = buf->create_mark("end", buf->end(), false);
  view.scroll_to(mark, 0.0);
  buf->delete_mark(mark);
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
  set_title("Partyline");
  set_default_size(900, 600);
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
}

MainWindow::~MainWindow()
{
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
  tree_store_ = Gtk::TreeStore::create(tree_cols_);
  tree_view_.set_model(tree_store_);
  tree_view_.append_column("Servers", col_tree_name_);
  tree_view_.set_headers_visible(false);
  tree_view_.get_style_context()->add_class("partyline-tree");
  tree_view_.signal_cursor_changed().connect(sigc::mem_fun(*this, &MainWindow::on_tree_cursor));
  tree_scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  tree_scroll_.add(tree_view_);
  tree_scroll_.set_size_request(160, -1);
  fill_tree_idle();

  status_buf_ = Gtk::TextBuffer::create();
  channel_buf_ = Gtk::TextBuffer::create();
  status_buf_->set_text(
      "Not connected.\n\n"
      "File → Connect: host, port, nick, TLS.\n"
      "Join… or /join #channel once you are connected.\n");

  buffer_.set_editable(false);
  buffer_.set_wrap_mode(Gtk::WRAP_WORD_CHAR);
  buffer_.set_cursor_visible(false);
  buffer_.get_style_context()->add_class("partyline-buffer");
  buffer_.set_buffer(status_buf_);
  buffer_scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  buffer_scroll_.add(buffer_);

  input_target_.set_width_chars(12);
  input_.set_hexpand(true);
  input_.set_sensitive(false);
  btn_send_.set_sensitive(false);
  input_.signal_activate().connect(sigc::mem_fun(*this, &MainWindow::on_send));
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
  nick_view_.get_style_context()->add_class("partyline-nicks");
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
  tree_store_->clear();
  auto add_server = [this](const Glib::ustring& name) {
    auto row = *tree_store_->append();
    row[col_tree_name_] = name;
    row[col_tree_kind_] = 0;
    auto status = *tree_store_->append(row.children());
    status[col_tree_name_] = "Status";
    status[col_tree_kind_] = 1;
  };
  add_server("Libera");
  add_server("OFTC");
  tree_view_.expand_all();
  suppress_tree_ = false;
}

void MainWindow::fill_tree_connected()
{
  suppress_tree_ = true;
  tree_store_->clear();
  auto server = *tree_store_->append();
  server[col_tree_name_] = connected_host_;
  server[col_tree_kind_] = 0;
  auto status = *tree_store_->append(server.children());
  status[col_tree_name_] = "Status";
  status[col_tree_kind_] = 1;
  if (!channel_name_.empty()) {
    auto ch = *tree_store_->append(server.children());
    ch[col_tree_name_] = channel_name_;
    ch[col_tree_kind_] = 2;
  }
  tree_view_.expand_all();
  suppress_tree_ = false;
}

void MainWindow::set_status(const Glib::ustring& text)
{
  status_.pop(status_ctx_);
  status_.push(text, status_ctx_);
}

void MainWindow::append_status(const Glib::ustring& text)
{
  status_buf_->insert(status_buf_->end(), text + "\n");
  if (pane_ == Pane::Status)
    scroll_end(buffer_);
}

void MainWindow::append_channel(const Glib::ustring& text)
{
  channel_buf_->insert(channel_buf_->end(), text + "\n");
  if (pane_ == Pane::Channel)
    scroll_end(buffer_);
}

void MainWindow::show_pane(Pane pane)
{
  pane_ = pane;
  if (pane == Pane::Status) {
    buffer_.set_buffer(status_buf_);
    input_target_.set_text("[Status]");
    scroll_end(buffer_);
  } else {
    buffer_.set_buffer(channel_buf_);
    input_target_.set_text(channel_name_.empty() ? "[Channel]" : channel_name_);
    scroll_end(buffer_);
  }
}

void MainWindow::show_not_yet(const Glib::ustring& feature)
{
  Gtk::MessageDialog dlg(*this, feature + " is later.", false, Gtk::MESSAGE_INFO,
                         Gtk::BUTTONS_OK, true);
  dlg.set_title("Partyline");
  dlg.set_secondary_text("The server list is M3. Several channels at once is M4.");
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
    reset_channel();
    fill_tree_idle();
    show_pane(Pane::Status);
    set_title("Partyline");
    set_status("Not connected.");
  }
}

void MainWindow::reset_channel()
{
  channel_name_.clear();
  nicks_.clear();
  nick_store_->clear();
  channel_buf_->set_text("");
}

void MainWindow::ensure_channel_row()
{
  fill_tree_connected();
  select_tree_kind(2);
  show_pane(Pane::Channel);
}

void MainWindow::select_tree_kind(int kind)
{
  suppress_tree_ = true;
  tree_store_->foreach_iter([this, kind](const Gtk::TreeModel::iterator& it) {
    if ((*it)[col_tree_kind_] == kind) {
      const Gtk::TreeModel::Path path(it);
      tree_view_.get_selection()->select(it);
      tree_view_.scroll_to_row(path);
      return true;
    }
    return false;
  });
  suppress_tree_ = false;
}

void MainWindow::refresh_nicks()
{
  nick_store_->clear();
  std::vector<Glib::ustring> sorted = nicks_;
  std::sort(sorted.begin(), sorted.end(), [](const Glib::ustring& a, const Glib::ustring& b) {
    return g_ascii_strcasecmp(a.c_str(), b.c_str()) < 0;
  });
  for (const auto& n : sorted) {
    auto row = *nick_store_->append();
    row[col_nick_] = n;
  }
}

void MainWindow::add_nick(const Glib::ustring& nick)
{
  for (const auto& n : nicks_) {
    if (nick_eq(n, nick))
      return;
  }
  nicks_.push_back(nick);
  refresh_nicks();
}

void MainWindow::remove_nick(const Glib::ustring& nick)
{
  nicks_.erase(std::remove_if(nicks_.begin(), nicks_.end(),
                              [&](const Glib::ustring& n) { return nick_eq(n, nick); }),
               nicks_.end());
  refresh_nicks();
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
  if (!channel_name_.empty() && !same_chan(channel_name_, channel)) {
    session_->part(channel_name_.raw());
    reset_channel();
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
    if (!channel_name_.empty() && session_)
      session_->part(channel_name_.raw());
  } else if (low == "quit")
    on_disconnect();
  else if (low == "quote") {
    if (session_ && !rest.empty())
      session_->quote(rest.raw());
  } else if (session_)
    session_->quote(line.substr(1).raw());
}

void MainWindow::on_servers()
{
  show_not_yet("Servers");
}

void MainWindow::on_connect()
{
  if (session_ && session_->running())
    return;

  ConnectDialog dlg(*this);
  if (dlg.run() != Gtk::RESPONSE_OK)
    return;

  const Glib::ustring host = dlg.host();
  const Glib::ustring nick = dlg.nick();
  if (host.empty() || nick.empty()) {
    Gtk::MessageDialog err(*this, "Host and nick are required.", false, Gtk::MESSAGE_ERROR,
                           Gtk::BUTTONS_OK, true);
    err.run();
    return;
  }

  connected_host_ = host;
  connected_nick_ = nick;
  registered_ = false;
  reset_channel();
  status_buf_->set_text("");
  show_pane(Pane::Status);
  fill_tree_connected();
  select_tree_kind(1);
  set_status(Glib::ustring("Connecting to ") + host + "…");
  set_title("Partyline — " + nick + " @ " + host);
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
  session_->start(host.raw(), dlg.port(), dlg.tls(), nick.raw(), nick.raw());
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
  if (text[0] == '/') {
    handle_command(text);
    return;
  }
  if (pane_ != Pane::Channel || channel_name_.empty()) {
    append_status("* not on a channel — /join #name or Join…");
    show_pane(Pane::Status);
    return;
  }
  session_->privmsg(channel_name_.raw(), text.raw());
  append_channel("<" + connected_nick_ + "> " + text);
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
  btn_join_.set_sensitive(true);
  input_.set_sensitive(true);
  btn_send_.set_sensitive(true);
  set_status(connected_nick_ + " @ " + connected_host_ + "  tls");
}

void MainWindow::on_session_finished(const Glib::ustring& reason)
{
  append_status("*** " + reason);
  set_connected_ui(false);
}

void MainWindow::on_session_privmsg(const Glib::ustring& target, const Glib::ustring& nick,
                                    const Glib::ustring& text)
{
  if (!channel_name_.empty() && same_chan(target, channel_name_))
    append_channel("<" + nick + "> " + text);
  else if (nick_eq(target, connected_nick_))
    append_status("* " + nick + ": " + text);
}

void MainWindow::on_session_join(const Glib::ustring& channel, const Glib::ustring& nick, bool me)
{
  if (me) {
    channel_name_ = channel;
    nicks_.clear();
    channel_buf_->set_text("");
    append_channel("* Now talking in " + channel);
    ensure_channel_row();
    set_status(connected_nick_ + " @ " + connected_host_ + "  " + channel);
    return;
  }
  if (same_chan(channel, channel_name_)) {
    add_nick(nick);
    append_channel("* " + nick + " has joined " + channel);
  }
}

void MainWindow::on_session_part(const Glib::ustring& channel, const Glib::ustring& nick, bool me)
{
  if (me && same_chan(channel, channel_name_)) {
    append_channel("* You have left " + channel);
    reset_channel();
    fill_tree_connected();
    select_tree_kind(1);
    show_pane(Pane::Status);
    set_status(connected_nick_ + " @ " + connected_host_ + "  tls");
    return;
  }
  if (same_chan(channel, channel_name_)) {
    remove_nick(nick);
    append_channel("* " + nick + " has left " + channel);
  }
}

void MainWindow::on_session_quit(const Glib::ustring& nick)
{
  if (channel_name_.empty())
    return;
  bool present = false;
  for (const auto& n : nicks_) {
    if (nick_eq(n, nick)) {
      present = true;
      break;
    }
  }
  if (!present)
    return;
  remove_nick(nick);
  append_channel("* " + nick + " has quit");
}

void MainWindow::on_session_names(const Glib::ustring& channel,
                                 const std::vector<Glib::ustring>& nicks)
{
  if (!same_chan(channel, channel_name_))
    return;
  nicks_ = nicks;
  refresh_nicks();
  set_status(connected_nick_ + " @ " + connected_host_ + "  " + channel_name_ + "  " +
             std::to_string(nicks_.size()) + " users");
}

void MainWindow::on_tree_cursor()
{
  if (suppress_tree_)
    return;
  const auto sel = tree_view_.get_selection()->get_selected();
  if (!sel)
    return;
  const int kind = (*sel)[col_tree_kind_];
  if (kind == 2 && !channel_name_.empty())
    show_pane(Pane::Channel);
  else
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

}  // namespace partyline
