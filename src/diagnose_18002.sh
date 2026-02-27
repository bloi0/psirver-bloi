#!/usr/bin/env bash
set -u
pgrep -af 'psirver 18002' || true
cat /tmp/psirver18002.log || true
ls -la /tmp/pshome || true
