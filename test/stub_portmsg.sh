#!/bin/bash
echo "STUB portmsg $*" >&2
for a in "$@"; do
  if [ "$a" = "--wait" ]; then
    trap 'exit 0' TERM
    while true; do sleep 0.2; done
  fi
done
exit 0
