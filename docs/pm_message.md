# Proposed funcs.txt change: pm_message via portmsg

Old ports keep calling `pm_message "text"` unchanged and get an untitled card.
New ports can add a title: `pm_message --title "Title" --text "Text"` (or `--title=`/`--text=`).

`pm_message` is non-blocking (scripts keep working while it shows, e.g. during tar extraction),
so it runs portmsg in `--wait` mode in the background: no Close button, can't be dismissed, no
timeout. Each call replaces the previous card. `pm_show_error` stays blocking (waits for A).

```bash
pm_message_end() {
    if [ -n "$PM_MSG_PID" ]; then
        kill "$PM_MSG_PID" 2>/dev/null
        wait "$PM_MSG_PID" 2>/dev/null
        PM_MSG_PID=""
    fi
}

trap pm_message_end EXIT

pm_message() {
    pm_message_end
    "$controlfolder/portmsg.${DEVICE_ARCH}" --wait "$@" &
    PM_MSG_PID=$!
}

pm_show_error() {
    pm_message_end
    "$controlfolder/portmsg.${DEVICE_ARCH}" "$@"
}
```

- `wait` after `kill` matters: portmsg exits on SIGTERM through SDL's own handler (`SDL_QUIT`, clean
  `SDL_Quit()`), and the next card or the game must only start once the display is released.
- `pm_platform_helper` and `pm_finish` must call `pm_message_end`. This includes every
  `mod_*.txt` override of `pm_platform_helper`, which today only call `PortMasterDialogExit`
  when `$PM_PIPE` exists, and that pipe no longer exists with this change.
- `controlfolder` needs `portmsg.aarch64`, `portmsg.armhf` and `portmsg.x86_64`. Only aarch64 is
  built here. If the binary is missing, fall back to the existing pugwash path.
- portmsg prints the text to stderr, so it still lands in the port's `log.txt` like the old `echo`.
