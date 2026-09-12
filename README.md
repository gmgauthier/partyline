# Partyline

**Vended by Grok Build**

An **mIRC-shaped IRC client** for The Lunduke Computer Operating System (LCOS). The window is mIRC 5.x / 6, not HexChat.

Binary: `partyline`. Unlicense.

LCOS itself: [https://github.com/BryanLunduke/LCOS](https://github.com/BryanLunduke/LCOS)

## Status

**M2 in tree.** Connect, join one channel, talk, nick list. Several channels at once is M4.

| Doc | What |
|---|---|
| [DEVELOPMENT.md](DEVELOPMENT.md) | Locked decisions, architecture, milestones M0–M6 |

## Build

```
sudo apt install build-essential meson ninja-build pkg-config g++ libgtkmm-3.0-dev
meson setup build
meson compile -C build
./build/partyline
```

## License

[The Unlicense](https://unlicense.org). See [UNLICENSE](UNLICENSE).
