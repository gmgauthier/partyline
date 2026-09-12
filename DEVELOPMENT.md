# Partyline development plan

A gtkmm-3 **mIRC** for LCOS. The *window* is mIRC 5.x / 6.

Display name: **Partyline**  
Binary / repo / package: `partyline`  
License: The Unlicense (`UNLICENSE`)  
Repos: https://gitea.scriptorium/gmgauthier/partyline (origin), https://github.com/gmgauthier/partyline

## Status (2026-09-12)

**v0.1.1.** Packaged. Keys, palettes, lag, `/msg` `/quote` `/nick`. Nav hover/sticky works on Adwaita.

## 1. Locked decisions

| Decision | Choice |
|---|---|
| Product | Original. Chrome is mIRC, not HexChat / Srain |
| Name | Partyline. Binary `partyline`. APP_ID `org.gmgauthier.Partyline` |
| Toolkit | C++17, gtkmm-3.0, GTK3 CSS, Meson |
| Look | One decorated window. Left **tree** (servers → channels + Status). Centre buffer. Right nick list. Input `Entry` at the bottom. Statusbar |
| Tree vs switchbar | **Tree.** mIRC 6. Switchbar looks like HexChat; do not add a second chrome |
| v1 servers | Several, user-edited. Seed the empty list with **Libera** (`irc.libera.chat:6697` TLS) and **OFTC** (`irc.oftc.net:6697` TLS) as examples — do not auto-connect |
| Nick | One default nick in the ini; per-server override allowed |
| TLS | **Required in v1.** Plaintext is a debug toggle, off by default |
| Engine | **GIO `SocketClient` + TLS** (glib-networking) and a tiny RFC1459 speaker (`NICK`/`USER`/`PING`/`PONG`/`QUIT`). Debian `libircclient1` has no OpenSSL |
| Thread | Library/socket on a worker. UI thread only paints. `Glib::Dispatcher` (or equivalent) across the gap |
| Commands | Typed line: ordinary text is `PRIVMSG` to the current target. Lines starting `/` are client commands: `/join` `/part` `/quit` `/nick` `/msg` `/quote`. Unknown `/` → `/quote` |
| Never as v1 | DCC, ident daemon, SASL (unless the wrap makes it cheap), bouncer, Matrix, plugins, scripts, tray, bubbles, header bar, URL unfurl, channel logs as identity |
| Network | User-configured host/port/nick. No account service. No daemon to launch |
| Init | No systemd. Config `~/.config/partyline/partyline.ini` |
| Brand | LCOS beige / navy. No Bryan’s seal. Mark is two handsets on a shared line |
| License | The Unlicense |
| Versioning | `meson.build` is the source of truth |

## Why we write our own

**PuTTY fills telnet.** Nothing fills mIRC. HexChat / ZoiteChat / Srain are not this product. See the product note in `lcos-projects/IRC.md`.

## 2. Window

```
+------------------------------------------------------------------+
| File  View  Tools  Help                                          |
+------------------------------------------------------------------+
| [Connect] [Disconnect] [Join…]                                   |
+------------+------------------------------------+----------------+
| servers    |  #lcos                             |  nicks         |
|  Libera    |  <alice> hello                     |  alice         |
|   Status   |  <bob> hi                          |  bob           |
|   #lcos    |                                    |  you           |
|   #devuan  |                                    |                |
|  OFTC      |                                    |                |
+------------+------------------------------------+----------------+
| [#lcos] ______________________________________________  [Send]   |
+------------------------------------------------------------------+
| Libera  tls  lag 0.2s  12 users                                  |
+------------------------------------------------------------------+
```

### Menus

```
File              View              Tools             Help
 Servers…          ● Tree            Join…             About Partyline
 Connect           Nick list
 Disconnect        Status bar
 ────────
 Exit
```

## 3. Architecture

```
MainWindow (gtkmm)
  ServerTree | BufferView | NickList
  InputEntry
       ^
       | Glib::Dispatcher / queued events
       v
IrcSession  (one per connected server, worker)
```

One `IrcSession` per **server**, not per channel. Channels are buffers owned by that session.

## 4. Protocol verbs (v1)

Speak these; ignore the rest without crashing.

| Direction | Verb | UI |
|---|---|---|
| out | `NICK` `USER` | Connect |
| out | `JOIN` `PART` `QUIT` | Join dialog / commands / Disconnect |
| out | `PRIVMSG` | Input line |
| out | `PONG` | Library or three lines of ours |
| in | `001`–`004` motd | Status buffer |
| in | `PING` | Pong |
| in | `PRIVMSG` `NOTICE` | Channel or query buffer. Query windows *later* — v1 may dump queries on Status |
| in | `JOIN` `PART` `QUIT` `NICK` | Channel line + nick list |
| in | `353` `366` | Names list |
| in | `433` nick in use | Status error; do not loop forever |
| in | `ERROR` / socket drop | Status + disconnected |

TLS to 6697 is the happy path.

## 5. Work plan

v1 is M0 through M6. Do not open M7+ until this set has been lived with.

| Milestone | Done when |
|---|---|
| **M0 — Window** | Menus, toolbar, paned tree/buffer/nicks, About, CSS. Matches the ASCII mock. No socket. **Done.** |
| **M1 — One server** | TLS connect to a typed host:port as nick. Status shows motd / errors. Disconnect. **Done.** |
| **M2 — One channel** | `/join` or Join…. PRIVMSG in and out. Nick list from `353`. **Done.** |
| **M3 — Server list** | Ini + Servers… dialog. Seed Libera / OFTC. Last nick remembered. **Done.** |
| **M4 — Several channels** | Two channels on one server; tree switches buffers; the other stays joined. **Done.** |
| **M5 — Polish** | Keys, status `tls` / lag / usercount, `/msg` `/quote` `/nick`. Chat pane palettes. **Done.** |
| **M6 — Package** | `debian/`, `scripts/release.sh` → `.deb`, tarball, AppImage. Tag `v0.1.1`. **Done.** |

After v1: SASL, query windows, channel logs, highlight, notify, DCC, ident, auto-join, `/whois`. Chat palettes ship in **M5** if they fit; extra skins wait.

## Traps

- Re-theming HexChat / ZoiteChat and calling it done
- Debian `libircclient1` without SSL
- Writing an IRCd, a bouncer, or a Matrix bridge
- IRCv3 as identity
- Plugin store, scripts
- Message bubbles, header bars, URL unfurling
- A tray icon that is the app
- DCC in v1
- Quassel-style core process
- Auto-connect + auto-join as defaults
