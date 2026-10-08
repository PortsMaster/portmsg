#!/bin/bash
# portmsg device test scenarios. Sourced by "PortMsg Test.sh" after control.txt.
# PORTMSG_TEST_DRY=/path/to/stub swaps the binary for a logging stub (chroot sequencing check only).

if [ -n "$PORTMSG_TEST_DRY" ]; then
  PORTMSG="$PORTMSG_TEST_DRY"
  PROC_NAME="$(basename "$PORTMSG_TEST_DRY")"
  nap() { sleep 0.1; }
else
  PORTMSG="$GAMEDIR/portmsg.${DEVICE_ARCH}"
  PROC_NAME="portmsg.${DEVICE_ARCH}"
  nap() { sleep "$1"; }
fi

# Proposed pm_message replacement (see /root/compile/portmsg/upstream-funcs-snippet.md)
pm_message_end() {
  if [ -n "$PM_MSG_PID" ]; then
    kill "$PM_MSG_PID" 2>/dev/null
    wait "$PM_MSG_PID" 2>/dev/null
    PM_MSG_PID=""
  fi
}

pm_message() {
  pm_message_end
  "$PORTMSG" --wait "$@" &
  PM_MSG_PID=$!
}

pm_show_error() {
  pm_message_end
  "$PORTMSG" "$@"
}

export PORTMSG_DEBUG=1
TOTAL=15
RESULTS=()

log() {
  echo "[portmsg-test $(date +%T)] $*"
}

procs() {
  local n=0 d
  for d in /proc/[0-9]*; do
    tr '\0' '\n' < "$d/cmdline" 2>/dev/null | grep -qxE "(.*/)?${PROC_NAME}" && n=$((n + 1))
  done
  echo "$n"
}

intro() {
  log "=== Test $1/$TOTAL: $2 ==="
  "$PORTMSG" --type info --title "Test $1/$TOTAL: $2" --text "$3\n\nPress A to start."
}

record() {
  local line="Test $1: rc=$2 procs=$(procs) $3"
  log "$line"
  RESULTS+=("$line")
}

timed() {
  local t0=$(date +%s)
  "$@"
  T_RC=$?
  T_SEC=$(( $(date +%s) - t0 ))
}

log "device=$DEVICE_NAME cfw=$CFW_NAME res=${DISPLAY_WIDTH}x${DISPLAY_HEIGHT} arch=$DEVICE_ARCH"
log "untested here: 20s auto-close without a controller (needs controller missing)"

# 1 old port extraction flow
intro 1 "Old port: extracting" "Next: an untitled card 'Extracting game data...' stays for 8 s (no Close button), then switches to 'Extraction successful.' for 2 s and disappears. Check: no title, no button, clean text switch."
pm_message "Extracting game data, this can take a few minutes..."
nap 8
pm_message "Extraction successful."
nap 2
pm_message_end
record 1 0 "check: wait card stayed, text switched"

# 2 old port error flow
intro 2 "Old port: error then exit" "Next: 'Error: Extraction failed.' stays about 5 s, then it is removed like the EXIT trap does."
pm_message "Error: Extraction failed."
nap 5
pm_message_end
record 2 0 "check: visible ~5s then gone"

# 3 wait card cannot be dismissed
intro 3 "Wait card ignores buttons" "Next card stays 8 s. Press A, B, Start and Select on it: it must NOT close."
pm_message "Press A, B, Start, Select now - this card must NOT close. It goes away by itself after 8 seconds."
nap 8
alive=$(procs)
pm_message_end
record 3 0 "procs_before_end=$alive (expect 1)"

# 4 rapid replacement
intro 4 "Rapid replacement" "Next: 5 messages 1.5 s apart, then the last one stays 2 s. Check that every message shows, and look for flicker, black frames or stuck cards."
for i in 1 2 3 4 5; do
  pm_message "Message $i of 5"
  nap 1.5
done
alive=$(procs)
nap 2
pm_message_end
record 4 0 "procs_during=$alive (expect 1)"

# 5 new form with title
intro 5 "New form: --title --text" "Next: a titled wait card for 5 s."
pm_message --title "Downloading data" --text "Fetching the latest level pack. This only happens on first launch."
nap 5
pm_message_end
record 5 0 "check: title, badge, divider"

# 6 --title= / --text= form
intro 6 "New form: --title= --text=" "Next: same as test 5 using the = form, 5 s."
pm_message --title="Preparing" --text="Converting assets for your device, please wait..."
nap 5
pm_message_end
record 6 0 "check: same look as test 5"

# 7 pm_show_error untitled, blocking
intro 7 "pm_show_error (blocking)" "Next: an untitled error card with a Close button. Wait a few seconds, then press A. The script must wait for you."
timed pm_show_error "Something failed while starting the game. Press A to continue."
record 7 "$T_RC" "waited=${T_SEC}s (expect your wait time)"

# 8 blocking with title
intro 8 "Blocking error with title" "Next: red titled error card. Press A to close."
timed "$PORTMSG" --type error --title "Disk image missing" --text "Copy your game files to ports/portmsgtest/ and launch again."
record 8 "$T_RC" "waited=${T_SEC}s"

# 9 types
intro 9 "Warning and info types" "Next: amber warning card, then blue info card. Press A on each."
timed "$PORTMSG" --type warning --title "Low storage" --text "Less than 100 MB free. Saves may fail."
w=$T_RC
timed "$PORTMSG" --type info --title "Tip" --text "Hold X for precise cursor movement."
record 9 "$w/$T_RC" "check: amber ! and blue i"

# 10 long text scrolling
intro 10 "Long text and scrolling" "Next: a long message. Scroll with D-pad, both sticks and L1/R1. Check scrollbar and fade edges, then press A."
LONG="Paragraph 1. The game could not start because one or more required files are missing. This paragraph is long on purpose so that it wraps over several lines on every screen size.\n\nParagraph 2. Copy your legally obtained game files into ports/portmsgtest/. The folder must contain the data files exactly as they come from the original release, with the original names.\n\nParagraph 3. You can find more details in ports/portmsgtest/log.txt.\n\nParagraph 4. If the problem persists, report it on the PortMaster Discord with your device name, CFW version and the log file attached.\n\nParagraph 5. This is the end of the message. If you can read this line, scrolling reached the bottom."
timed "$PORTMSG" --type error --title "Missing files" --text "$LONG"
record 10 "$T_RC" "check: scroll dpad/sticks/L1/R1"

# 11 unicode and long unbroken path
intro 11 "Unicode and long path" "Next: special characters and a path without spaces that must break across lines. Press A."
timed "$PORTMSG" --type info --title "Zeichen / Caractères" --text "ÄÖÜ äöü ß é è à ç ñ — “quotes” ‘single’ … € ™\n\n/roms/ports/portmsgtest/a_very_long_unbroken_path/that/cannot/wrap/at/spaces/because/it/has/none/file.dat"
record 11 "$T_RC" "check: no ? boxes, path breaks"

# 12 timeout
intro 12 "Timeout countdown" "Next: a card with a 10 s countdown in the footer. Do not press anything; it must close by itself."
timed "$PORTMSG" --type info --title "Auto close" --timeout 10 --text "This card closes by itself in 10 seconds."
record 12 "$T_RC" "closed_after=${T_SEC}s (expect ~10)"

# 13 held button from previous card
intro 13 "Held button / tap" "Tap A quickly to close this card. The next card must stay open. Then press and HOLD A on it: it closes only when you release."
timed "$PORTMSG" --type info --title "Still here?" --text "This card must not have closed from your tap. Press and hold A now: it closes on release."
record 13 "$T_RC" "waited=${T_SEC}s (expect > 1)"

# 14 display handoff
intro 14 "Display handoff" "Next: a wait card for 3 s, then it is removed and a new card must appear immediately (like a game taking the display)."
pm_message "Wait card - will be replaced by a blocking card in 3 seconds."
nap 3
pm_message_end
timed "$PORTMSG" --type info --title "Display released" --text "If you can read this, the previous card released the display correctly. Press A."
record 14 "$T_RC" "check: second card appeared promptly"

# 15 summary
SUMMARY=$(printf '%s\\n' "${RESULTS[@]}")
log "=== Summary ==="
printf '%s\n' "${RESULTS[@]}"
"$PORTMSG" --type info --title "Test 15/$TOTAL: Summary" --text "${SUMMARY}\nAll results are also in ports/portmsgtest/log.txt. Press A to exit."
pm_message_end
log "leftover portmsg processes: $(procs) (expect 0)"
