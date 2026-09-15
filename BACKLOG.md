# Partyline backlog

Current release: **v0.1.1**. Last updated: 2026-09-15.

mIRC 5.x / 6. Binary `partyline`. Suite catalog: `lcos-projects/PRODUCT-BACKLOG.md`. Design: `lcos-projects/IRC.md`. Plan: [DEVELOPMENT.md](DEVELOPMENT.md). How to land work: [DEVELOPMENT.md](DEVELOPMENT.md#process) — `feature/` / `fix/` branches, PRs to `master`, lint gate, semver on shipped PRs.

v1 dumps private messages on the Status buffer. Live with that before opening a new milestone.

## High Priority

Nothing queued as a next slice. After v1 has been lived with, the first real hole is **query windows** (PRIVMSG to a nick currently lands on Status). Until then, parked work stays under Low Priority.

## Low Priority

- Query windows
- `/whois`
- Auto-join (checkbox, default off — not a reconnect storm)
- SASL (parked unless the TLS wrap makes it cheap)
- Channel logs
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

**v0.1.0 (M0–M6)** — GIO TLS (not `libircclient`); server list in `~/.config/partyline/partyline.ini`; several channels; Tab nick-complete; `/join` `/part` `/quit` `/nick` `/msg` `/quote`; palettes (white / eggshell / black / navy / olive); lag / tls / usercount; `.deb` / tarball / AppImage.

**v0.1.1** — Nav hover/sticky on tree and nick list (`#C5D4E8` / `#8AADC8`); first-run seeds **Undernet** (6667, TLS off), **EFNet**, **OFTC**, **Rizon UK**, **Libera**; TLS hostname verify; connect timeout; UTF-8 sanitization; Eggshell palette; TLS checkbox flips 6667/6697.
