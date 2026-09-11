#!/bin/sh
# ANX-03 gate: no blocking/allocating calls inside Forbid()/Disable() regions
exec python3 "$(dirname "$0")/check_forbid.py" "$@"
