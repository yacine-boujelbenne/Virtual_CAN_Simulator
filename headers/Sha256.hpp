#pragma once
#include <string>

// SHA-256 (FIPS 180-4) of a byte string, returned as lowercase hexadecimal.
// Used to identify a .vcanproj project by its content: the same project always
// hashes to the same value, on any machine and in any process.
std::string sha256Hex(const std::string &data);
