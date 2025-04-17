#!/bin/bash

start-stop-daemon -S --name aesdsocket -x "/usr/bin/aesdsocket" -- -d
