#!/bin/sh
# Självtest för tests/check_constexpr.py: en kompilator som dör utan diagnos.
echo "clang++: error: unable to execute command: Killed" >&2
exit 1
