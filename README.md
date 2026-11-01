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

**Status: alpha.** The portable core (protocol, layout, avatars) is
unit-tested and has been run end to end against `avatar_chat_server`
through the host build. The PSL1GHT platform layer (`src/platform/ps3`)
builds in CI, boots in RPCS3 with working video and keyboard, and is
being debugged from tester reports; see
[Testing on a PS3 or in RPCS3](#testing-on-a-ps3-or-in-rpcs3). Nobody
has run it on a real console yet.

## What it does

- Joins rooms, loads recent history, shows who's here, and streams live
  messages with join/leave notices. Several rooms at once: the header
  lists them with unread counts, D-pad Left/Right switches.
- Draws each sender's avatar in a gutter beside their messages, grouped
  the way the doors and the web page group them, your own on the right;
  handle colours and tags exactly as set, since the cells are 24-bit.
  **Select** switches to a compact one-line-per-message view.
- Pictures: image links in messages are downloaded by the console and
  drawn under the text as truecolor half-blocks (PNG and JPEG; GIF and
  WebP show a note). ANSI art sent from the doors or the web page opens
  in a full-screen viewer (Square).
- Types with a USB keyboard or the system's on-screen keyboard
  (Triangle), which also takes the nickname at startup.
- Sends your messages with your own avatar (`avatar.bin`) and board name.
- Private messages in and out, including to standalone web users'
  mailboxes; `/r` replies to the last one.
- `/join`, `/leave`, `/next`, `/prev`, `/rooms`, `/who`, `/msg`, `/r`,
  `/img`, `/compact`, `/quit`.
- Reconnects on its own when the connection drops.
- Picks a font scale for the TV: about 80 columns at 480p, 720p and
  1080p, with an overscan margin.

Not yet: audio and video links, link preview cards, the TV lounge, GIF
and WebP pictures, the NSFW veil, uploads.

## Controls

| Input | Does |
| --- | --- |
| USB keyboard | Type; **Enter** sends; **Esc** clears the line; **PgUp / PgDn** and **Up / Down** scroll; **F1** help; **F2** compact view; **F3** art viewer. |
| Pad **Cross** | Enter: sends the line. On an empty line, opens the on-screen keyboard. |
| Pad **Triangle** | On-screen keyboard, with the current line to edit; its Enter sends. |
| Pad **Circle** | Clear the line; close the art viewer. |
| Pad **Square** | Art viewer: the newest unseen piece. Left/Right page through them. |
| Pad **Select** | Toggle compact view. |
| Pad **Up / Down**, **L1 / R1** | Scroll the transcript. |
| Pad **Left / Right** | Switch between joined rooms. |
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
4. Optional: a `cacert.pem` (Mozilla's bundle from
   [curl.se/ca](https://curl.se/ca/cacert.pem)) next to the ini lets
   pictures over HTTPS be fetched with certificate checks. Without it they
   are fetched unverified, which for pictures from links that are
   already untrusted is a fair trade; `images = off` turns them off.
5. Launch. A USB keyboard is handy, but the pad alone works through the
   on-screen keyboard.

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

## Testing on a PS3 or in RPCS3

It runs in [RPCS3](https://rpcs3.net/) too (File > Install Package, or
boot the `.self`), which is the quickest way to try a build. The first
tester's run brought up video and keyboard fine and crashed on the first
`select()` after connecting: PSL1GHT's `net/select.h` makes `fd_set`
64 bytes while the kernel writes back a 128-byte `sys_net_fd_set`, so the
stack got overwritten. The client uses `poll()` now. Lessons for the next
one:

- **Crash addresses can be symbolized.** RPCS3 logs a fault as
  `PPU[...] Thread (main_thread) [0x00011490]: VM: Access violation ...`;
  the number in brackets is the program counter. Actions tab >
  **symbolize** > Run workflow with that address prints the function and
  the disassembly around it. Release zips also carry `debug/` with the
  ELF, the link map and `tools/symbolize.py` for doing it locally. With
  RPCS3's recompiler the address can be the most recent call site rather
  than the faulting instruction, so read it as "somewhere after here".
- **The log.** The app writes `/dev_hdd0/tmp/avatar_chat_ps3.log` (in
  RPCS3: `dev_hdd0/tmp/` under the emulator's folder): video setup, the
  connect, each receive and any socket error. The last line before a
  crash says what it was doing.
- **Still unverified on hardware:** `init_video` follows the PSL1GHT
  `rsxutil` sample; `poll_keyboard` uses `KB_CODETYPE_ASCII` and maps
  the raw codes in `raw_to_key`; `gethostbyname` falls back from
  `inet_aton` (put an IP in `host` if names don't resolve); the config
  path comes from `argv[0]` with `/dev_hdd0/game/AVCH00001/USRDIR/` as
  the fallback.
- **Checking an SDK header** without the toolchain: Actions tab >
  **sdk-probe** runs a shell command inside the ps3dev image.

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
src/core/art.[ch]          [BITMAP|...] art envelopes
src/core/inflate.[ch]      DEFLATE/zlib decoder (art, PNG)
src/core/png.[ch]          PNG decoder
src/core/pics.[ch]         picture cache, link detection, half-block scaling
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
