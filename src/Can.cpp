#include <vector>
#include <iostream>
#include <iomanip>
#include <cstdint>
#include "../headers/Can.hpp"


Can::Can() : id_(0), data_() {}

Can::Can(uint32_t id, const std::vector<uint8_t> &data) : id_(id), data_(data) {}

void Can::print() {
    std::cout << "\033[1;35m" // Magenta color for CAN frame
              << "      ~> CAN Frame: ID = 0x" 
              << std::hex << std::setw(3) << std::setfill('0') << id_
              << "  DLC = " << std::dec << data_.size()
              << ", Data = ";

    for (uint8_t byte : data_) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') 
                  << static_cast<int>(byte) << " ";
    }
    std::cout << "\033[0m" << std::endl; // Reset color
}

