#!/bin/sh
# PocketPlex for Onion (Miyoo Mini Plus). Onion's launcher has already cd'd into this
# App dir and prepended LD_PRELOAD=...libpadsp.so, so we just run the binary.
# See the spec core / §9 Q1.
cd "$(dirname "$0")"
mkdir -p conf
export XDG_DATA_HOME="$PWD/conf"
exec ./PocketPlex
