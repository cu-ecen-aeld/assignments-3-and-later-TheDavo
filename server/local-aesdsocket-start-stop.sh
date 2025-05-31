#!/bin/sh

case "$1" in
  start)
    echo "Starting aesdsocket"
    start-stop-daemon -S --name aesdsocket -x "${HOME}/Documents/ecea5305/assignment-1-TheDavo/server/aesdsocket" -- -d
    ;;
  stop)
    echo "Stopping aesdocket"
    start-stop-daemon -K -n aesdsocket
    ;;
  *)
    echo "Usage: $0 {start|stop}"
    exit 1
    ;;

esac

exit 0
