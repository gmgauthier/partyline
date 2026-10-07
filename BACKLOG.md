# Partyline backlog

Current release: **v1.0.0**. Last updated: 2026-10-07.

mIRC 5.x / 6. Binary `partyline`. Suite catalog: `lcos-projects/PRODUCT-BACKLOG.md`. Design: `lcos-projects/IRC.md`. Plan: [DEVELOPMENT.md](DEVELOPMENT.md). How to land work: [DEVELOPMENT.md](DEVELOPMENT.md#process) — `feature/` / `fix/` branches, PRs to `master`, lint gate, semver on shipped PRs.

A private message opens a query buffer for that nick and selects it. `/query nick` opens one. `/msg nick text` sends without opening one; an already open query still keeps the line.

## High Priority

Nothing queued.

## Low Priority

- Auto-join (checkbox, default off — not a reconnect storm)
- SASL (parked unless the TLS wrap makes it cheap)
- Highlight
- Notify
- DCC
- Ident daemon

## Out of Scope

- Bouncer / Quassel-style core process
- Matrix bridge
- IRCv3 as identity (caps soup, chathistory, soju)
- Plugin store, scripts (Perl/Python/Lua)
- Switchbar (tree is the chrome; switchbar looks like HexChat)
- Message bubbles, header bar, URL unfurl
- Tray icon as the app
- Auto-connect + auto-join as *defaults*
- Re-theming HexChat / ZoiteChat / Srain
- Debian `libircclient1` (no OpenSSL)
- Writing an IRCd
- Electron
- Custom title bar; Bryan’s seal
- USENET (Pan) or telnet (PuTTY)

## Shipped

**v1.0.0** — A private message opens a query window.

**v0.2.14** — A join key and a part reason are sent.

**v0.2.13** — An empty server list stays empty.

**v0.2.12** — Removing the remembered server clears it.

**v0.2.11** — A kick is now shown as a kick, with who did it and the reason.

**v0.2.10** — Channels or nicks that differ only in punctuation no longer share a log file.

**v0.2.9** — Renaming the last-used server no longer makes Connect fall back to the first server.

**v0.2.8** — A per-server nick no longer replaces the default nick, and /nick is remembered for the right server.

**v0.2.7** — A nick the server refuses at connect is retried with an alternate instead of leaving the input disabled.

**v0.2.6** — /me actions now show in the channel, and only a real CTCP VERSION is answered.

**v0.2.5** — A channel NOTICE now shows in its channel.

**v0.2.4** — The lag ping no longer races the socket thread.

**v0.2.3** — A long message is split into several on character boundaries, and the pane shows only what reached the server.

**v0.2.2** — A line break in a nick, channel, or message can no longer inject a second IRC command.

**v0.2.1** — Headless meson test suite, and known defects recorded in BUG-BACKLOG.md.

**v0.1.0 (M0–M6)** — GIO TLS (not `libircclient`); server list in `~/.config/partyline/partyline.ini`; several channels; Tab nick-complete; `/join` `/part` `/quit` `/nick` `/msg` `/quote`; palettes (white / eggshell / black / navy / olive); lag / tls / usercount; `.deb` / tarball / AppImage.

**v0.1.1** — Nav hover/sticky on tree and nick list (`#C5D4E8` / `#8AADC8`); first-run seeds **Undernet** (6667, TLS off), **EFNet**, **OFTC**, **Rizon UK**, **Libera**; TLS hostname verify; connect timeout; UTF-8 sanitization; Eggshell palette; TLS checkbox flips 6667/6697.
