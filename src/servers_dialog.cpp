/* SPDX-License-Identifier: Unlicense */

#include "servers_dialog.hpp"

#include <string>

namespace partyline {

ServersDialog::ServersDialog(Gtk::Window& parent, Settings& settings)
    : Gtk::Dialog("Servers", parent, true),
      settings_(settings),
      working_(settings.servers),
      port_(Gtk::Adjustment::create(6697, 1, 65535, 1, 10), 1.0, 0)
{
  set_default_size(560, 320);
  add_button("_Cancel", Gtk::RESPONSE_CANCEL);
  add_button("_OK", Gtk::RESPONSE_OK);
  set_default_response(Gtk::RESPONSE_OK);

  cols_.add(col_name_);
  cols_.add(col_index_);
  store_ = Gtk::ListStore::create(cols_);
  list_.set_model(store_);
  list_.append_column("Server", col_name_);
  list_.set_headers_visible(false);
  list_.get_selection()->signal_changed().connect([this]() {
    if (suppress_)
      return;
    store_row();
    load_row();
  });
  list_scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  list_scroll_.add(list_);
  list_scroll_.set_size_request(180, -1);
  list_btns_.pack_start(btn_add_, Gtk::PACK_SHRINK);
  list_btns_.pack_start(btn_remove_, Gtk::PACK_SHRINK);
  btn_add_.signal_clicked().connect(sigc::mem_fun(*this, &ServersDialog::on_add_server));
  btn_remove_.signal_clicked().connect(sigc::mem_fun(*this, &ServersDialog::on_remove_server));
  left_.pack_start(list_scroll_, Gtk::PACK_EXPAND_WIDGET);
  left_.pack_start(list_btns_, Gtk::PACK_SHRINK);

  port_.set_numeric(true);
  tls_.set_active(true);
  tls_verify_.set_active(true);
  tls_.signal_toggled().connect([this]() {
    if (suppress_)
      return;
    const int p = port_.get_value_as_int();
    if (tls_.get_active()) {
      if (p == 6667)
        port_.set_value(6697);
    } else if (p == 6697) {
      port_.set_value(6667);
    }
  });
  nick_.set_placeholder_text("blank = default nick");
  default_nick_.set_text(settings_.nick);
  default_nick_.set_max_length(16);
  nick_.set_max_length(16);

  grid_.set_row_spacing(8);
  grid_.set_column_spacing(8);
  auto add_row = [this](int row, const Glib::ustring& label, Gtk::Widget& w) {
    auto* l = Gtk::manage(new Gtk::Label(label, true));
    l->set_halign(Gtk::ALIGN_START);
    grid_.attach(*l, 0, row, 1, 1);
    grid_.attach(w, 1, row, 1, 1);
  };
  add_row(0, "Default _nick", default_nick_);
  add_row(1, "_Name", name_);
  add_row(2, "_Host", host_);
  add_row(3, "_Port", port_);
  auto* tls_row = Gtk::manage(new Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, 12));
  tls_row->pack_start(tls_, Gtk::PACK_SHRINK);
  tls_row->pack_start(tls_verify_, Gtk::PACK_SHRINK);
  grid_.attach(*tls_row, 1, 4, 1, 1);
  add_row(5, "Nick o_verride", nick_);

  body_.set_border_width(12);
  body_.pack_start(left_, Gtk::PACK_SHRINK);
  body_.pack_start(grid_, Gtk::PACK_EXPAND_WIDGET);
  get_content_area()->pack_start(body_, Gtk::PACK_EXPAND_WIDGET);

  signal_response().connect([this](int resp) {
    if (resp == Gtk::RESPONSE_OK)
      on_ok();
  });

  refill();
  if (!working_.empty())
    list_.get_selection()->select(store_->children().begin());
  show_all_children();
}

void ServersDialog::refill()
{
  suppress_ = true;
  store_->clear();
  for (int i = 0; i < static_cast<int>(working_.size()); ++i) {
    auto row = *store_->append();
    row[col_name_] = working_[static_cast<size_t>(i)].name;
    row[col_index_] = i;
  }
  suppress_ = false;
}

void ServersDialog::load_row()
{
  const auto sel = list_.get_selection()->get_selected();
  if (!sel) {
    current_ = -1;
    return;
  }
  current_ = (*sel)[col_index_];
  if (current_ < 0 || current_ >= static_cast<int>(working_.size())) {
    current_ = -1;
    return;
  }
  const Server& s = working_[static_cast<size_t>(current_)];
  suppress_ = true;
  name_.set_text(s.name);
  host_.set_text(s.host);
  port_.set_value(s.port);
  tls_.set_active(s.tls);
  tls_verify_.set_active(s.tls_verify);
  nick_.set_text(s.nick);
  suppress_ = false;
}

void ServersDialog::store_row()
{
  if (current_ < 0 || current_ >= static_cast<int>(working_.size()))
    return;
  Server& s = working_[static_cast<size_t>(current_)];
  s.name = name_.get_text().raw();
  s.host = host_.get_text().raw();
  s.port = port_.get_value_as_int();
  s.tls = tls_.get_active();
  s.tls_verify = tls_verify_.get_active();
  s.nick = nick_.get_text().raw();
  if (s.name.empty())
    s.name = s.host.empty() ? "Server" : s.host;
  unique_id(s);
  suppress_ = true;
  for (auto& row : store_->children()) {
    if (row[col_index_] == current_) {
      row[col_name_] = s.name;
      break;
    }
  }
  suppress_ = false;
}

void ServersDialog::unique_id(Server& s)
{
  std::string base = Settings::make_id(s.name.empty() ? s.host : s.name);
  std::string id = base;
  int n = 2;
  auto taken = [&](const std::string& cand) {
    for (const auto& o : working_) {
      if (&o != &s && o.id == cand)
        return true;
    }
    return false;
  };
  while (taken(id)) {
    id = base + "_" + std::to_string(n++);
  }
  s.id = id;
}

void ServersDialog::on_add_server()
{
  store_row();
  Server s;
  s.name = "New server";
  s.host = "irc.example.net";
  s.port = 6697;
  s.tls = true;
  s.tls_verify = true;
  unique_id(s);
  working_.push_back(std::move(s));
  refill();
  if (!store_->children().empty()) {
    auto it = store_->children().begin();
    for (auto n = store_->children().size(); n > 1; --n)
      ++it;
    list_.get_selection()->select(it);
  }
  load_row();
}

void ServersDialog::on_remove_server()
{
  if (current_ < 0 || current_ >= static_cast<int>(working_.size()))
    return;
  working_.erase(working_.begin() + current_);
  current_ = -1;
  refill();
  if (!store_->children().empty())
    list_.get_selection()->select(store_->children().begin());
  load_row();
}

void ServersDialog::on_ok()
{
  store_row();
  settings_.nick = default_nick_.get_text().raw();
  if (settings_.nick.empty())
    settings_.nick = Settings::default_nick();
  settings_.servers = working_;
  settings_.seed_if_empty();
  settings_.save();
}

}  // namespace partyline
