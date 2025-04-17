#!/bin/sh

start-stop-daemon -S --name aesdsocket -x "/usr/bin/aesdsocket" -- -d
