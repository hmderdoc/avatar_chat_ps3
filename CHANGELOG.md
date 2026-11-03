# Changelog

Releases are cut from the Actions tab (release > Run workflow with the
version), which builds the `.pkg` and publishes the matching section of
this file as the release notes.

## v0.3.0-alpha.2

### Fixed

- **No keyboard opened on Triangle or Cross** (alpha.1 of 0.3). The
  system dialog request could end with nothing shown and no fallback,
  and while it was pending every key was swallowed. The built-in pad
  keyboard is now the default (`osk = builtin`); `osk = system` tries
  the console's dialog first, and a dialog that fails, closes without
  text or stays invisible hands over to the built-in keyboard, as does
  pressing Triangle, Circle or Start while it is pending. F4 on a USB
  keyboard opens the pad keyboard too. The log now records pad
  connections and the first button presses.

## v0.3.0-alpha.1

The rest of the door's transcript features, and a keyboard that needs no
firmware dialog. Still untested on real hardware; it runs in RPCS3.

### Added

- **Built-in pad keyboard.** A keyboard drawn in cells: D-pad moves,
  Cross types, Square backspace, Triangle space, L1/R1 shift, Start
  sends, Circle closes. It takes over when the system on-screen keyboard
  fails or does not appear within six seconds, and `osk = builtin` in
  the ini picks it outright. Every step of the system dialog request is
  logged now.
- **Audio links.** `.mp3 .ogg .oga .flac .wav` links get a play line;
  `/play [N|list]` downloads the track and plays it through the PS3
  audio port (MP3 via libmad, Vorbis, FLAC, WAV), `/stop` stops.
- **TV lounge.** `[TVTUNER|host|port|channel]` markers tune the room to a
  telnetvision feed; the picture fills the transcript area with the
  caption bar and the newest chat lines over it, with the feed's sound.
  `/tvoff` leaves, `/tvon` rejoins, `/tvtuner <host> <port> <chan>` or
  `/tvtuner off` tunes for everyone. The durable room state
  (`channels.<room>.tvtuner`) is read on join.
- **Link cards.** The first plain link in a message shows site, title
  and description from the page's OpenGraph, Twitter or plain tags.
- **NSFW veil.** Pictures and cards from a line or link with a word on
  the shell's list are held back until `/nsfw`; `nsfw_keywords` in the
  ini overrides the list, `none` turns it off.
- **GIF** pictures (first frame).

### Not done

- WebP pictures, `/play` of video files (the door runs ffmpeg for those;
  the TV lounge covers shared video), uploads.

### Controls

Cross: Enter (empty line: keyboard). Circle: clear line / close.
Triangle: keyboard. Square: art viewer. Select: compact view. Start:
quit. D-pad Up/Down and L1/R1: scroll. D-pad Left/Right: switch rooms.

## v0.2.0-alpha.1

The second alpha brings most of the door's transcript to the console.
Still untested on real hardware; it runs in RPCS3.

### Added

- **Pictures in chat.** Image links (`.png .jpg .jpeg .gif .webp .bmp`)
  are downloaded by the console and drawn under the message as
  truecolor half-block cells, up to three a message and ten rows tall.
  PNG and JPEG decode today; GIF and WebP show a note. HTTPS goes
  through libcurl with PolarSSL; without a `cacert.pem` next to the app
  certificates are not checked (`ssl_verify`, `images` in the ini).
- **ANSI art.** `[BITMAP|...]` envelopes from the doors and the web page
  decode into a full-screen viewer: Square, F3 or `/img` opens the
  newest unseen piece; Left/Right page, Circle closes.
- **Handle colours and tags** from `nick.style`, drawn exactly: cells
  are 24-bit now, with the xterm 256-colour palette behind the CGA set.
- **Several rooms at once.** `/join` keeps the previous room subscribed;
  the header lists them with unread counts (highlighted when a line
  names you); D-pad Left/Right, `/next`, `/prev` switch; `/leave`
  drops one; `/rooms` lists joined and public rooms.
- **On-screen keyboard.** Triangle, or Cross on an empty line, opens the
  system keyboard; Enter sends. At startup it takes the nickname, so a
  pad is enough to chat.
- Own messages sit on the right with the avatar at the right edge, as in
  the door.
- `tools/symbolize.py` and the `symbolize` workflow turn an RPCS3 crash
  address into a function; `sdk-probe` runs a command in the toolchain
  image; release zips carry the ELF and map under `debug/`.

### Fixed

- Crash right after connecting: PSL1GHT's `select()` writes back a
  128-byte kernel `fd_set` over a 64-byte one. Socket readiness now uses
  `poll()`.

### Controls

Cross: Enter (empty line: keyboard). Circle: clear line / close.
Triangle: keyboard. Square: art viewer. Select: compact view. Start:
quit. D-pad Up/Down and L1/R1: scroll. D-pad Left/Right: switch rooms.

## v0.1.0-alpha.2

- Fixed the crash after entering a nickname (`select()` stack overwrite).
- The app logs to `/dev_hdd0/tmp/avatar_chat_ps3.log`.

## v0.1.0-alpha.1

- First build: rooms, history, presence, private messages, avatars in a
  gutter, compact mode, USB keyboard, reconnects. Built in CI with the
  ps3dev toolchain.
