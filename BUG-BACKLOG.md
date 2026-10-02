# Bug backlog

Reviewed 2026-10-01 against the 0.2.0 sources.

`meson test` runs `tests/test_log.cpp` (`log`) and `tests/test_session.cpp` (`session`, an `IrcSession` against a one-client fake server on 127.0.0.1 from `tests/fake_irc.hpp`). It checks that a log written before `set_host` is dropped, that the host directory is lowercased, and that a leaf such as `../../tmp-escape` stays inside the host directory as `.._.._tmp-escape.txt`. It does not treat two different channel names collapsing to one file as correct. `/quote` is the intentional raw-command path and is not a defect. `session` checks that a CR or LF in a nick, real name, channel, mask, target, message, or `/quote` never reaches the socket as a second command.

## Open

### Lag ping races the socket thread on a std::string

- Severity: crash
- Confidence: high
- Where: `src/irc_session.cpp:319`, `src/irc_session.cpp:503`, `src/main_window.cpp:1275`
- Trigger: Connect, or leave the client up. `send_lag_ping` runs on the UI thread from registration and from the 60-second lag timeout. `handle_line` runs on the socket thread.
- Outcome: `send_lag_ping` assigns `lag_token_` and `lag_sent_us_` with no lock. `handle_line` compares `token == lag_token_` and reads `lag_sent_us_` on the other thread. `nick_mu_` does not cover these. Reassigning the string under the reader is a use-after-free. `lag_sent_us_` can tear.

### Channel NOTICE never reaches the channel buffer

- Severity: incorrect
- Confidence: high
- Where: `src/irc_session.cpp:432`
- Trigger: The server sends a channel `NOTICE`.
- Outcome: Only `PRIVMSG` builds `Event::Privmsg`. A channel NOTICE falls through to the status line. `DEVELOPMENT.md` says an incoming NOTICE goes to the channel or query buffer. A query PRIVMSG shown on Status is intentional. A channel NOTICE is not.

### ACTION and other CTCP stay on the status line

- Severity: incorrect
- Confidence: high
- Where: `src/irc_session.cpp:435`, `src/irc_session.cpp:441`
- Trigger: Someone sends a normal `/me` (`\x01ACTION waves\x01`). Or a CTCP whose command merely starts with `VERSION`.
- Outcome: After the VERSION prefix check, any text whose first byte is `\x01` is enqueued as a status line and returned. It never hits the channel buffer. `is_ctcp_version` is a prefix compare, so a CTCP that starts with `VERSION` is answered as a version query.

### A rejected nick leaves the input disabled

- Severity: incorrect
- Confidence: high
- Where: `src/main_window.cpp:540`, `src/main_window.cpp:919`, `src/irc_session.cpp:580`
- Trigger: Connect with a nick the server rejects (numeric 433) and the socket stays open waiting for another NICK. Nothing in `src/` handles 433.
- Outcome: `registered_` is set only from numeric 001. The input, Send, Join, and channel list stay insensitive until then. Status shows the numeric. `/nick` cannot be sent. Disconnect is the control that still works.

### Connect and /nick overwrite the global nick with a per-server nick

- Severity: data-loss
- Confidence: high
- Where: `src/main_window.cpp:785`, `src/main_window.cpp:922`, `src/main_window.cpp:1214`
- Trigger: A server row has its own nick. Connect. Or use `/nick` while connected.
- Outcome: Connect uses `Server::nick` when it is set. On 001 the connected nick is written into `settings_.nick` and saved, replacing the global default. `/nick` does the same and does not update `Server::nick`, so the per-server override wins again on the next connect and the `/nick` is discarded.

### Renaming a server leaves last-server pointing at the old id

- Severity: incorrect
- Confidence: high
- Where: `src/servers_dialog.cpp:156`, `src/servers_dialog.cpp:208`, `src/main_window.cpp:769`
- Trigger: Rename a server so `Settings::make_id(name)` changes, with that server selected as the last server. Connect.
- Outcome: `store_row` always recomputes `id` from the name. `on_ok` saves the nick and the server list and does not rewrite `last_server`. `find_id` misses. Connect uses the first server in the list.

### Channel and query log names collide

- Severity: data-loss
- Confidence: high
- Where: `src/log.cpp:21`
- Trigger: Join `#c++` and `#c__` on the same host, or query `foo~bar` and `foo_bar`. Leave and rejoin.
- Outcome: `safe_leaf` keeps only ASCII alnum, `.`, `-`, `_`, and `#`, then lowercases. Everything else becomes `_`. `#c++` and `#c__` are both `#c__.txt`. The two nicks are both `foo_bar.txt`. Rejoin replays the other conversation (the last 500 lines). The leaf stays inside the host directory. This is a collision, not a path escape.

### Kick is shown as a part, and the reason is dropped

- Severity: incorrect
- Confidence: high
- Where: `src/irc_session.cpp:466`, `src/main_window.cpp:996`
- Trigger: Someone is kicked, or you are kicked.
- Outcome: PART and KICK both become `Event::Part`. The channel line is always `* nick has left <channel>`. The kick reason is dropped. A self-kick uses the same path ("You have left") and closes the channel.

## Closed

None.

## Closed

### The channel pane shows text the server did not get

- Severity: incorrect
- Confidence: high
- Where: `src/irc_session.cpp:285`, `src/main_window.cpp:902`
- Trigger: Send a PRIVMSG longer than 400 bytes, or a send whose `write_line` fails.
- Outcome: The body is `resize`d to 400 and sent as one PRIVMSG. There is no further split. `resize(400)` can cut a UTF-8 sequence. The channel pane and the log append the original string. A failed write is ignored by the UI.
- Fixed in v0.2.3: `privmsg` splits each line into PRIVMSGs of at most 400 bytes, only between UTF-8 characters, stops at the first failed write, and returns the bodies it sent. The channel pane, status line, and log show only those, plus a "not sent in full" line when a write failed.

### A newline in a nick, channel, or message is a second IRC command

- Severity: security
- Confidence: high
- Where: `src/irc_session.cpp:398`
- Trigger: A nick, real name, channel, or message that contains CR or LF reaches `write_line`. JOIN, PART, PRIVMSG, WHOIS, LIST, NICK, and USER concatenate those fields and do not filter breaks. A KeyFile value is one way in. This is not a claim about paste into the input entry: GTK often strips line breaks on insert, and that path was not executed.
- Outcome: `write_line` appends `\r\n` only when the buffer does not already end in `\r\n`, then writes the whole buffer. A break in the middle is a second command on the socket.
- Fixed in v0.2.2: Nick, channel, target, and mask parameters end at the first space, CR, LF, or NUL. The real name has line breaks turned into spaces. A message with line breaks is sent as one PRIVMSG per line. `write_line` drops anything after a CR, LF, or NUL, so even `/quote` sends one command.
