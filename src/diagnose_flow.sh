#!/usr/bin/env bash
set -u
pgrep -af psirver || true
cat /tmp/psirver18001.log || true
ls -la /tmp/pshome || true
