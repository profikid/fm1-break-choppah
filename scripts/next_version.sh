#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# The version the next minor release gets (the last release tag plus one minor, never below ./VERSION): kept for
# the scripts and docs that ask it; scripts/bump_version.sh does the work, and knows patch and major too.
#   scripts/next_version.sh            the tags from origin (CI)
#   scripts/next_version.sh v0.1 v0.2  these tags instead (tests)
exec sh "$(dirname "$0")/bump_version.sh" minor "$@"
