# avatar_chat_ps3

A native PlayStation 3 client for Avatar Chat, the BBS chat that the
[avatar_chat_universal](https://github.com/hmderdoc/avatar_chat_universal)
door, the Synchronet `avatar_chat` door, the bridges and the web pages all
share. It runs as homebrew on GameOS (CFW or HEN), draws the chat with the
same 10×6 CP437 avatars the doors show, and talks to any Avatar Chat
server over plain TCP, the public `futureland.today:10088` or a
self-hosted `avatar_chat_server`.

Written in C against [PSL1GHT](https://github.com/ps3dev/PSL1GHT). No Go
on the Cell: the official Go toolchain needs a POWER8 and the PS3's PPE is
not one, so this is a from-scratch client that speaks the same protocol
([PROTOCOL.md](https://github.com/hmderdoc/avatar_chat_universal/blob/main/PROTOCOL.md)).

**Status: alpha, not yet run on a real PS3.** The portable core (protocol,
layout, avatars) is unit-tested and has been run end to end against
`avatar_chat_server` through the host build. The PSL1GHT platform layer
(`src/platform/ps3`) builds cleanly with the real toolchain in CI, which
produces the `.pkg`, but nobody has booted it on hardware yet. Reports
from a console are the next step; see [Testing on a PS3](#testing-on-a-ps3).

## What it does

- Joins a room, loads recent history, shows who's here, and streams live
  messages with join/leave notices.
- Draws each sender's avatar in a gutter beside their messages, grouped the
  way the doors and the web page group them; **Select** switches to a
  compact one-line-per-message view.
- Sends your messages with your own avatar (`avatar.bin`) and board name.
- Private messages in and out, including to standalone web users'
  mailboxes; `/r` replies to the last one.
- `/join`, `/who`, `/rooms`, `/msg`, `/r`, `/compact`, `/quit`.
- Reconnects on its own when the connection drops.
- Picks a font scale for the TV: about 80 columns at 480p, 720p and
  1080p, with an overscan margin.

Not yet: images and ANSI art in messages (shown as a notice), handle
colours, the on-screen keyboard (a USB keyboard is needed to type), the
TV lounge, multiple rooms at once.

## Controls

| Input | Does |
| --- | --- |
| USB keyboard | Type; **Enter** sends; **Esc** clears the line; **PgUp / PgDn** and **Up / Down** scroll; **F1** help; **F2** compact view. |
| Pad **Select** | Toggle compact view. |
| Pad **Up / Down**, **L1 / R1** | Scroll the transcript. |
| Pad **Cross** | Enter (sends the line). **Circle** clears it. |
| Pad **Start** | Quit. `/quit` and the XMB's quit also work. |

## Install on the PS3

Nobody needs the toolchain to use it: GitHub Actions builds the package.

- **Releases:** every tagged version has `avatar_chat_ps3_vX.Y.Z.pkg` on the
  [Releases](../../releases) page.
- **Latest commit:** the `ci` workflow on the
  [Actions](../../actions) tab keeps `avatar_chat_ps3.self` and `.pkg`
  as a downloadable artifact for each push (GitHub login needed, kept 90
  days).

Then, on a PS3 running CFW or HEN (homebrew can't be installed on stock
firmware):

1. Copy the `.pkg` to the root of a FAT32 USB stick, plug it into the
   PS3, and install it from the XMB: **Game > Install Package Files** (HEN:
   the Package Manager). It appears as **Avatar Chat** under Game.
2. Edit `/dev_hdd0/game/AVCH00001/USRDIR/avatar_chat_ps3.ini` over FTP:
   at least `nick`, and `host`/`port` if you run your own server. The
   file in this repo documents every key. Without a `nick` the app asks
   for one at startup (keyboard needed).
3. Optional: drop a 120-byte `avatar.bin` next to the ini (one avatar, or
   a collection plus `avatar_index`). The door's bundled collections and
   the editor in the web page produce these; the format is in
   [AVATARS.md](https://github.com/hmderdoc/avatar_chat_universal/blob/main/AVATARS.md).
4. Plug in a USB keyboard and launch.

For development, `make` produces `avatar_chat_ps3.self` which `ps3load`
sends straight to a console running the PSL1GHT loader, with no install.

## Build

### PS3 (PSL1GHT)

Needs the [ps3dev toolchain](https://github.com/ps3dev/ps3toolchain) with
PSL1GHT, and `PS3DEV`, `PSL1GHT` and `PATH` set as its instructions say.
The ps3dev Docker image has it all; this script pulls it and builds, and
is what CI runs:

```sh
.github/scripts/build-ps3.sh
```

```sh
make        # avatar_chat_ps3.elf and .self
make pkg    # avatar_chat_ps3.pkg (uses pkg/ICON0.PNG and pkg/sfo.xml)
```

### Host (Linux / macOS)

The same client on a UTF-8 terminal, for development, and usable as a
plain desktop client:

```sh
make -f Makefile.host          # ./avatar_chat_host
make -f Makefile.host test     # the core tests
make -f Makefile.host check-ps3  # syntax-check the PS3 layer against tests/ps3shim
./avatar_chat_host             # reads ./avatar_chat_ps3.ini, or $AVATAR_CHAT_DIR/
```

On the host, Ctrl-L toggles compact view and Ctrl-C quits.

## Testing on a PS3

The PSL1GHT layer compiles, but is untested on hardware. Things most
likely to need a fix on first boot, in order:

1. **Video:** `init_video` in `src/platform/ps3/platform_ps3.c` follows the
   PSL1GHT `rsxutil` sample (XRGB buffers, `gcmSetFlip` / `gcmSetWaitFlip`).
   If the screen stays black, start there.
2. **Keyboard:** `poll_keyboard` uses `KB_CODETYPE_ASCII` and
   `KB_RMODE_INPUTCHAR`; keys with no ASCII arrive with `KB_RAWDAT` set
   and are mapped in `raw_to_key`. If Enter or Backspace do nothing,
   print `data.keycode[k]` to `/dev_hdd0/tmp/avatar_chat_ps3.log`.
3. **Network:** `netInitialize` then BSD sockets; `gethostbyname` falls
   back from `inet_aton`. If names don't resolve, put the IP in `host`.
4. **Config path:** derived from `argv[0]`; falls back to
   `/dev_hdd0/game/AVCH00001/USRDIR/`.

`plat_log` writes to `/dev_hdd0/tmp/avatar_chat_ps3.log`.

## Layout

```
src/main.c                 the loop: socket, keys, draw
src/config.c               the ini
src/platform.h             what a platform provides
src/platform/ps3/          PSL1GHT: RSX framebuffer, io/kb, io/pad, lv2 sockets
src/platform/host/         POSIX terminal and sockets
src/core/proto.[ch]        line framing and the JSON packets
src/core/chat.[ch]         session state: rooms, history, presence, PMs, echo dedupe
src/core/ui.[ch]           the cell screen: avatar gutter, grouping, wrap, input, status
src/core/json.[ch]         a small arena JSON parser/escaper
src/core/cp437.[ch]        UTF-8 <-> CP437
src/core/avatar.[ch]       120-byte avatars, .bin collections, SAUCE
src/core/font_cp437.h      8x16 font (generated by tools/gen_font.py)
tests/test_core.c          unit tests for all of the above
tests/ps3shim/             PSL1GHT header stand-ins for the syntax check
pkg/                       ICON0.PNG and sfo.xml for the .pkg
```

The core is C99 with no allocation and no platform calls, so it runs the
same on the big-endian PPE and on a PC; everything console-specific is
behind `src/platform.h`.

## License

MIT (see LICENSE). The CP437 font comes from the
[Moebius](https://github.com/blocktronics/moebius) editor, Apache-2.0
(LICENSE-moebius.txt).
