# pm_message via portmsg (PortMaster-GUI funcs.txt)

Old ports keep calling `pm_message "text"` unchanged and get an untitled card.
New ports put the message first and any portmsg options after it:

```bash
pm_message "Extracting game data..." --title "Please wait"
pm_show_error "Copy sundog.st to ports/sundog/." --title "Disk image missing" --type warning
pm_show_error "Saved." --title "Done" --type info --timeout 5
```

Every portmsg option works this way. An older PortMaster's `pm_message`/`pm_show_error` only read
`$1`, so the same call still shows the message there (in pugwash) and ignores the options. The
option-only form (`pm_message --title "T" --text "M"`) also works, but an older PortMaster would
show `--title` as the message, so ports shouldn't use it.

`pm_message` is non-blocking (scripts keep working while it shows, e.g. during tar extraction),
so it runs portmsg in the background, by default in `--wait` mode: no Close button, can't be
dismissed. Each call replaces the previous card. `pm_show_error` stays blocking (waits for A).

```bash
# portmsg draws pm_message/pm_show_error, pugwash is the fallback when it is missing.
PM_MSG_BIN="$controlfolder/portmsg.${DEVICE_ARCH}"
PM_MSG_PID=""

pm_message_close() {
    # Removes the current message right away, used before the game takes the screen.
    if [ -n "$PM_MSG_PID" ]; then
        kill "$PM_MSG_PID" 2> /dev/null
        wait "$PM_MSG_PID" 2> /dev/null
        PM_MSG_PID=""
    fi

    if [ -e "$PM_PIPE" ]; then
        PortMasterDialogExit
    fi
}

pm_message() {
    ...
    if [ -x "$PM_MSG_BIN" ]; then
        if [ "${1#--}" = "$1" ]; then
            local text="$1"
            shift
            set -- --text "$text" "$@"
        fi
        local mode="--wait"
        case " $* " in *" --timeout "*) mode="" ;; esac
        pm_message_close
        $ESUDO env $SAVE_CLEAN_ENV LD_LIBRARY_PATH="$SAVE_LD_LIBRARY_PATH" \
            SDL_GAMECONTROLLERCONFIG="${SDL_GAMECONTROLLERCONFIG:-$sdl_controllerconfig}" \
            "$PM_MSG_BIN" $mode --type info "$@" &
        PM_MSG_PID=$!
        return
    fi
    ... # unchanged pugwash path
}
```

- `pm_message` cards default to `info` (blue `i`); a port's own `--type` comes later on the
  command line and wins. `pm_show_error` keeps portmsg's `error` default (red `!`).
- `pm_message` runs `--wait` (no close button) unless `--timeout` is given; then the card closes
  itself. It always runs in the background and is replaced by the next call.
- A first argument that doesn't start with `--` is passed as `--text`, so an old message that
  starts with `-` can't be read as an option.
- portmsg runs with the same clean environment as pugwash (`SAVE_CLEAN_ENV`,
  `SAVE_LD_LIBRARY_PATH`), so a port's own `LD_LIBRARY_PATH` or SDL variables don't leak into it.
- The portmsg command isn't wrapped in a shell function before `&`: `$!` has to be portmsg
  itself (or `env`/`sudo`, which exec or forward SIGTERM), not a subshell that would leave it
  orphaned when killed.
- `wait` after `kill` matters: portmsg exits on SIGTERM through SDL's own handler (`SDL_QUIT`,
  clean `SDL_Quit()`), and the next card or the game must only start once the display is released.
- `pm_platform_helper` (and every `mod_*.txt` override of it) calls `pm_message_close`.
  `pm_message_end` (EXIT trap, `pm_finish`) keeps the old 3 s pause so a last message stays readable.
- If `portmsg.${DEVICE_ARCH}` is missing (armhf and x86_64 for now), the pugwash path runs as before.
- portmsg prints the text to stderr, so it still lands in the port's `log.txt` like the old `echo`.
