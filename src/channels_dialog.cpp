/* SPDX-License-Identifier: Unlicense */

#include "channels_dialog.hpp"

namespace partyline {

ChannelsWindow::ChannelsWindow(Gtk::Window& parent)
{
  set_transient_for(parent);
  set_title("Channels");
  set_default_size(560, 400);
  set_type_hint(Gdk::WINDOW_TYPE_HINT_DIALOG);

  cols_.add(col_name_);
  cols_.add(col_users_);
  cols_.add(col_topic_);
  store_ = Gtk::ListStore::create(cols_);
  filtered_ = Gtk::TreeModelFilter::create(store_);
  filtered_->set_visible_func(sigc::mem_fun(*this, &ChannelsWindow::row_visible));

  view_.set_model(filtered_);
  view_.append_column("Channel", col_name_);
  view_.append_column("Users", col_users_);
  view_.append_column("Topic", col_topic_);
  view_.set_headers_visible(true);
  view_.set_enable_search(true);
  view_.set_search_column(0);
  if (auto* c = view_.get_column(0))
    c->set_min_width(120);
  if (auto* c = view_.get_column(1))
    c->set_min_width(48);
  if (auto* c = view_.get_column(2)) {
    c->set_expand(true);
    c->set_resizable(true);
  }
  view_.signal_row_activated().connect(sigc::mem_fun(*this, &ChannelsWindow::on_row_activated));

  scroll_.set_policy(Gtk::POLICY_AUTOMATIC, Gtk::POLICY_AUTOMATIC);
  scroll_.add(view_);

  filter_.set_placeholder_text("Filter…");
  filter_.signal_changed().connect(sigc::mem_fun(*this, &ChannelsWindow::on_filter));

  btn_join_.signal_clicked().connect(sigc::mem_fun(*this, &ChannelsWindow::on_join_clicked));
  status_.set_halign(Gtk::ALIGN_START);
  status_.set_hexpand(true);
  bottom_.set_border_width(4);
  bottom_.pack_start(status_, Gtk::PACK_EXPAND_WIDGET);
  bottom_.pack_start(btn_join_, Gtk::PACK_SHRINK);

  root_.set_border_width(4);
  root_.pack_start(filter_, Gtk::PACK_SHRINK);
  root_.pack_start(scroll_, Gtk::PACK_EXPAND_WIDGET);
  root_.pack_start(bottom_, Gtk::PACK_SHRINK);
  add(root_);
  show_all_children();
}

void ChannelsWindow::clear()
{
  store_->clear();
  n_ = 0;
  filter_.set_text("");
  status_.set_text("Listing…");
}

void ChannelsWindow::add_row(const Glib::ustring& channel, int users, const Glib::ustring& topic)
{
  auto row = *store_->append();
  row[col_name_] = channel;
  row[col_users_] = users;
  row[col_topic_] = topic;
  ++n_;
  if (n_ % 50 == 0)
    status_.set_text(Glib::ustring::format(n_, " channels…"));
}

void ChannelsWindow::set_listing(bool on)
{
  if (on)
    status_.set_text("Listing…");
}

void ChannelsWindow::finish()
{
  status_.set_text(Glib::ustring::format(n_, " channels"));
}

int ChannelsWindow::size() const
{
  return n_;
}

bool ChannelsWindow::row_visible(const Gtk::TreeModel::const_iterator& it)
{
  const Glib::ustring q = filter_.get_text().lowercase();
  if (q.empty())
    return true;
  const auto row = *it;
  const Glib::ustring name = row[col_name_];
  const Glib::ustring topic = row[col_topic_];
  return name.lowercase().find(q) != Glib::ustring::npos ||
         topic.lowercase().find(q) != Glib::ustring::npos;
}

void ChannelsWindow::on_filter()
{
  filtered_->refilter();
}

void ChannelsWindow::on_row_activated(const Gtk::TreeModel::Path&, Gtk::TreeViewColumn*)
{
  const Glib::ustring ch = selected_channel();
  if (!ch.empty())
    signal_join.emit(ch);
}

void ChannelsWindow::on_join_clicked()
{
  const Glib::ustring ch = selected_channel();
  if (!ch.empty())
    signal_join.emit(ch);
}

Glib::ustring ChannelsWindow::selected_channel()
{
  const auto sel = view_.get_selection();
  if (!sel)
    return {};
  const auto it = sel->get_selected();
  if (!it)
    return {};
  return (*it)[col_name_];
}

}  // namespace partyline
