#!/bin/bash
# Build script for psirver

echo "Building psirver..."
g++ -std=c++11 -Wall -Wextra -Wpedantic -g -fPIC -fstack-protector-all -c psirver.cc Requests.cc
g++ -o psirver psirver.o Requests.o
echo "Build complete! Run with: ./run.sh"
