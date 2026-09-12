/* SPDX-License-Identifier: Unlicense */

#include "main_window.hpp"
#include "about_dialog.hpp"
#include "connect_dialog.hpp"
#include "paths.hpp"

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
  btn_connect_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_connect));
  btn_disconnect_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_disconnect));
  btn_join_.signal_clicked().connect(sigc::mem_fun(*this, &MainWindow::on_join));
}

void MainWindow::build_body()
{
  tree_cols_.add(col_tree_name_);
  tree_store_ = Gtk::TreeStore::create(tree_cols_);
  tree_view_.set_model(tree_store_);
  tree_view_.append_column("Servers", col_tree_name_);
  tree_view_.set_headers_visible(false);
  tree_view_.get_style_context()->add_class("partyline-tree");
  tree_scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  tree_scroll_.add(tree_view_);
  tree_scroll_.set_size_request(160, -1);
  fill_tree();

  buffer_.set_editable(false);
  buffer_.set_wrap_mode(Gtk::WRAP_WORD_CHAR);
  buffer_.set_cursor_visible(false);
  buffer_.get_style_context()->add_class("partyline-buffer");
  buffer_.get_buffer()->set_text(
      "Not connected.\n\n"
      "File → Connect (or the Connect button): host, port, nick, TLS.\n"
      "Join is M2. Servers… is M3.\n");
  buffer_scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  buffer_scroll_.add(buffer_);

  input_target_.set_width_chars(10);
  input_.set_hexpand(true);
  input_.set_sensitive(false);
  btn_send_.set_sensitive(false);
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

void MainWindow::fill_tree()
{
  tree_store_->clear();
  auto add_server = [this](const Glib::ustring& name) {
    auto row = *tree_store_->append();
    row[col_tree_name_] = name;
    auto status = *tree_store_->append(row.children());
    status[col_tree_name_] = "Status";
  };
  add_server("Libera");
  add_server("OFTC");
  tree_view_.expand_all();
}

void MainWindow::set_status(const Glib::ustring& text)
{
  status_.pop(status_ctx_);
  status_.push(text, status_ctx_);
}

void MainWindow::append_line(const Glib::ustring& text)
{
  auto buf = buffer_.get_buffer();
  buf->insert(buf->end(), text + "\n");
  auto mark = buf->create_mark("end", buf->end(), false);
  buffer_.scroll_to(mark, 0.0);
  buf->delete_mark(mark);
}

void MainWindow::show_not_yet(const Glib::ustring& feature)
{
  Gtk::MessageDialog dlg(*this, feature + " is later.", false, Gtk::MESSAGE_INFO,
                         Gtk::BUTTONS_OK, true);
  dlg.set_title("Partyline");
  dlg.set_secondary_text("M1 is one TLS server. Join is M2. The server list is M3.");
  dlg.run();
}

void MainWindow::set_connected_ui(bool on)
{
  btn_connect_.set_sensitive(!on);
  btn_disconnect_.set_sensitive(on);
  if (!on) {
    set_title("Partyline");
    set_status("Not connected.");
  }
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
  buffer_.get_buffer()->set_text("");
  set_status(Glib::ustring("Connecting to ") + host + "…");
  set_title("Partyline — " + nick + " @ " + host);
  btn_connect_.set_sensitive(false);
  btn_disconnect_.set_sensitive(true);

  session_ = std::make_unique<IrcSession>();
  session_->signal_line.connect(sigc::mem_fun(*this, &MainWindow::on_session_line));
  session_->signal_registered.connect(sigc::mem_fun(*this, &MainWindow::on_session_registered));
  session_->signal_finished.connect(sigc::mem_fun(*this, &MainWindow::on_session_finished));
  session_->start(host.raw(), dlg.port(), dlg.tls(), nick.raw(), nick.raw());
}

void MainWindow::on_disconnect()
{
  if (session_)
    session_->stop();
}

void MainWindow::on_join()
{
  show_not_yet("Join");
}

void MainWindow::on_quit()
{
  if (session_)
    session_->stop();
  hide();
}

void MainWindow::on_session_line(const Glib::ustring& text)
{
  append_line(text);
}

void MainWindow::on_session_registered()
{
  registered_ = true;
  set_status(connected_nick_ + " @ " + connected_host_ + "  tls");
}

void MainWindow::on_session_finished(const Glib::ustring& reason)
{
  append_line("*** " + reason);
  set_connected_ui(false);
  registered_ = false;
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
