# Installing Partyline

Four ways to get a binary, in the order LCOS cares about:

| Artifact | Who it is for |
|---|---|
| **`.deb`** | LCOS, Devuan Excalibur, Debian Trixie. Preferred. |
| **Source tarball** | Distro packagers and `meson setup && ninja install`. |
| **AppImage** | Fallback for distros that do not install `.deb` files. gtkmm only. Published on the GitHub/Gitea release. |
| **Git build** | Developers. See below. |

Version comes from `meson.build` (currently `0.1.1`).

## Runtime needs

- GTK 3 / gtkmm-3.0
- **glib-networking** (GIO TLS — Libera, OFTC, Rizon, EFNet)

On Debian / Devuan / LCOS:

```
sudo apt install libgtkmm-3.0-1t64 glib-networking
```

(Package names on older Debian may be `libgtkmm-3.0-1v5`.)

## 1. Debian package (preferred)

From a release `.deb`:

```
sudo apt install ./dist/partyline_0.1.1-1_amd64.deb
```

Or, from this tree:

```
./scripts/release.sh deb
sudo apt install ./dist/partyline_0.1.1-1_amd64.deb
```

That installs:

- `/usr/bin/partyline`
- `/usr/share/applications/partyline.desktop`
- `/usr/share/icons/hicolor/scalable/apps/partyline.svg`
- `/usr/share/partyline/skin/lcos/lcos.css`
- `/usr/share/partyline/brand/icon-tile.svg`
- `/usr/share/partyline/partyline.ini` (default server list)

Launch from the menu or `partyline`. Config is `~/.config/partyline/partyline.ini` (created on first run from the shipped list).

Uninstall: `sudo apt remove partyline`.

## 2. Source tarball

`meson dist` produces `build/meson-dist/partyline-VERSION.tar.xz`.

```
tar -xf partyline-0.1.1.tar.xz
cd partyline-0.1.1
sudo apt install build-essential meson ninja-build pkg-config \
  libgtkmm-3.0-dev
meson setup build --prefix=/usr
meson compile -C build
sudo meson install -C build
```

`./scripts/release.sh tarball` runs `meson dist` for you.

## 3. AppImage (fallback)

LCOS 0.3 already runs AppImages. The image bundles gtkmm from the build host and the GIO GnuTLS module for IRC TLS.

```
./scripts/release.sh appimage
```

Requires `linuxdeploy` on `$PATH` (see <https://github.com/linuxdeploy/linuxdeploy>). Output lands under `dist/`.

```
chmod +x Partyline-*.AppImage partyline-*.AppImage
./Partyline-*.AppImage
```

The AppImage runtime sets `APPDIR`; Partyline looks for skin, brand, and the default ini under `$APPDIR/usr/share/partyline`. Leave `APPDIR` unset for `.deb` and `meson install` builds.

## 4. Developer build (no install)

```
meson setup build
meson compile -C build
./build/partyline
```

The binary finds CSS and `partyline.ini` via `SOURCE_ROOT` in the build tree. `PARTYLINE_DATA` overrides that.

## One command for every artifact

```
./scripts/release.sh all
```

Writes tarball, `.deb`, and AppImage (if `linuxdeploy` is there) under `dist/`. The GitHub/Gitea release includes the AppImage as the non-deb fallback.

## What this project will not ship

- A bouncer, ident daemon, or IRCd
- SASL / DCC / logs as v1 identity
- A systemd unit
- Vendored Clearlooks / xfwm themes
