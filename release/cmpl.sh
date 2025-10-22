#!/bin/bash

# Compiler
CXX=g++
# Compiler flags
CXXFLAGS="-Wall -Wextra -std=c++17 -O2"

# Source files (all .cpp in src plus SimulationManager.cpp)
SRC_FILES=$(find ../src -name "*.cpp")

# Output binary name
OUTPUT="myapp"

echo " Checking for cppcheck..."
if command -v cppcheck >/dev/null 2>&1; then
    echo " Running cppcheck..."
    cppcheck --enable=all --inconclusive --quiet $SRC_FILES $MAIN_FILE
        cppcheck --enable=all --inconclusive --quiet \
             --suppress=*:${PWD}/headers/json.hpp \
             $SRC_FILES $MAIN_FILE
else
    echo "  cppcheck not found. Skipping static analysis."
fi

echo "  Compiling project..."
$CXX $CXXFLAGS $SRC_FILES $MAIN_FILE -o $OUTPUT

if [ $? -eq 0 ]; then
    echo " Compilation successful. Run with ./$OUTPUT"
else
    echo " Compilation failed."
    exit 1
fi
