#pragma once
#include <cstdint>
#include <iostream>
#include <iomanip>
#include <string>
#include <vector>

namespace Log {

    inline void info(const std::string& msg) {
        std::cout << "\033[1;36m" << msg << "\033[0m" << std::endl;
    }

    inline void tx(const std::string& msg) {
        std::cout << "\033[1;32m" << msg << "\033[0m" << std::endl;
    }

    inline void rx(const std::string& msg) {
        std::cout << "\033[1;33m" << msg << "\033[0m" << std::endl;
    }

    inline void setup(const std::string& msg) {
        std::cout << "\033[1;34m" << msg << "\033[0m" << std::endl;
    }

    inline void error(const std::string& msg) {
        std::cerr << "\033[1;31m" << msg << "\033[0m" << std::endl;
    }

    inline void printBytes(const std::string& label, const std::vector<uint8_t>& data, const std::string& colorType = "info") {
        std::string color;
        if (colorType == "tx") color = "\033[1;32m";
        else if (colorType == "rx") color = "\033[1;33m";
        else if (colorType == "setup") color = "\033[1;34m";
        else if (colorType == "error") color = "\033[1;31m";
        else color = "\033[1;36m";

        std::cout << color << label << ": ";
        for (auto b : data)
            std::cout << std::hex << std::uppercase << std::setfill('0') << std::setw(2)
                      << static_cast<int>(b) << " ";
        std::cout << "\033[0m" << std::endl;
    }
}
