# portmsg

A fast, minimal fullscreen message card for PortMaster launch scripts. It's meant to replace
`pm_message`/`pm_show_error`, which start the whole pugwash Python GUI just to show a line of text.

- Native SDL2, using SDL's software renderer. It needs no GL context and works whatever graphics
  stack the game uses.
- Anti-aliased text (stb_truetype) with fonts compiled in. The only runtime dependency is the
  device's own `libSDL2-2.0.so.0`.
- Scales from 480x320 to 720x720 to 1080p+. Long text wraps and scrolls.

## Usage

```bash
portmsg.aarch64 [--type error|warning|info] [--title TITLE] [--timeout SECONDS] [--wait] [--text] MESSAGE
portmsg.aarch64 --title="TITLE" --text="MESSAGE"
```

| Option | Effect |
|--|--|
| `MESSAGE` / `--text` | Body text. `\n` breaks a line, `\n\n` starts a paragraph. |
| `--title` | Title with a coloured badge and divider. Without it, the card shows only the text. |
| `--type` | `error` (red `!`, default), `warning` (amber `!`), `info` (blue `i`). |
| `--wait` | No Close button, can't be dismissed, no timeout. Stays until killed (SIGTERM). |
| `--timeout N` | Closes by itself after N seconds, with a countdown in the footer. |

- Without `--wait`, A, B, Start or Select close the card (or Enter, Esc or Space on a keyboard),
  on button release. The press must start on this card, so a tap from the previous screen can't
  close it.
- The card scrolls with the D-pad, either stick or L1/R1.
- With no controller detected, a closable card closes itself after 20 s, so a script can't hang.
- Exit status is 0, or 2 if no display could be opened.
- The text is always echoed to stderr, so it ends up in the port's `log.txt`.
- Export `SDL_GAMECONTROLLERCONFIG="$sdl_controllerconfig"` before the first call.

### In a port today

Ship `portmsg.aarch64` in the port folder, and `LICENSE` plus `fonts/OFL.txt` in its `licenses/`.

```bash
if [ ! -f "$GAMEDIR/data/game.dat" ]; then
  ./portmsg.${DEVICE_ARCH} --title "Game data missing" --text "Copy your game files to ports/portname/data/."
  exit 1
fi
```

Please-wait message around long work, for example extracting a bundled archive:

```bash
./portmsg.${DEVICE_ARCH} --wait --text "Extracting game data, this can take a few minutes..." &
PORTMSG_PID=$!
gunzip -c "$ARCHIVE_FILE" | tar --no-same-owner -xf -
EXTRACT_RC=$?
kill $PORTMSG_PID; wait $PORTMSG_PID
```

`wait` matters: the card has to release the display before the next card or the game starts.

### As pm_message (proposal)

[docs/pm_message.md](docs/pm_message.md) shows how `funcs.txt`'s `pm_message`/`pm_show_error` can
run on portmsg so that existing ports get it unchanged. This isn't live in PortMaster yet.

## Build

```bash
apt-get install -y libsdl2-dev xxd
make                # -> portmsg.aarch64 (ARCH=armhf / x86_64 to name other native builds)
```

`make fonts` regenerates `fonts/PortmsgSans-*.ttf` from Lato with `tools/subset_font.py`, which
needs `python3-fonttools`. The subset covers Basic Latin, Latin-1, Latin Extended-A, general
punctuation, arrows, € and ™. Other characters render as `?`.

Dev preview, no display needed: `portmsg.aarch64 --snapshot out.bmp --size 640x480 [--snapshot-scroll PX|-1] ...`
Startup timings go to stderr with `PORTMSG_DEBUG=1`.

## Testing

- `test/device/` is a PortMaster-style test port with 15 scenarios. Copy `portmsg.aarch64` into
  `test/device/portmsgtest/` and the folder plus `PortMsg Test.sh` to `ports/`. Results go to
  `ports/portmsgtest/log.txt`.
- `PORTMSG_TEST_DRY=$PWD/test/stub_portmsg.sh` runs `tests.sh` without a display, to check the
  sequencing only.

## License

MIT, see [LICENSE](LICENSE). Bundled: stb_truetype (public domain / MIT) and Portmsg Sans
(a renamed subset of Lato, SIL OFL 1.1, see [fonts/OFL.txt](fonts/OFL.txt)).
