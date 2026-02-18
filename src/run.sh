#!/bin/bash
# Run script for psirver

export PSIRVER_HOME=$(pwd)
echo "Starting psirver on port 8000..."
echo "PSIRVER_HOME=$PSIRVER_HOME"
echo "Press Ctrl+C to stop"
./psirver 8000
