#include <cstdint>
#include <iostream>
#include <string>
#include <fstream>
#include <filesystem>
#include "../headers/CanTp.hpp"
#include "../headers/Receiver.hpp"
#include "../headers/Sender.hpp"
#include "../headers/json.hpp"
#include "../headers/Logger.hpp"

using json = nlohmann::json;
//namespace fs = std::filesystem;

void saveConfig(const json& data) {
    const std::string filename = "config.json";
    const std::string tmpfile = filename + ".tmp";

    std::ofstream out(tmpfile);
    if (!out.is_open()) {
        Log::error("Failed to open " + tmpfile + " for writing");
        return;
    }

    try {
        out << data.dump(2);
    } catch (...) {
        Log::error("Failed to serialize JSON");
        out.close();
        std::remove(tmpfile.c_str());
        return;
    }

    out.flush();
    out.close();

    if (std::rename(tmpfile.c_str(), filename.c_str()) != 0) {
        Log::error("Failed to update config.json (rename failed)");
        std::remove(tmpfile.c_str());
        return;
    }

    Log::setup("   -> config.json updated successfully.");
}

// ---------------------------------------------------------------
void idSettings(CanTp &tp, int &idT, int &idR) {
    Log::setup("             (o) > Please enter the Tx and Rx IDs (1-2047)");
    std::cout << "        Tx: ";
    std::cin >> idT;
    std::cout << "        Rx: ";
    std::cin >> idR;

    std::ifstream file("config.json");
    if (!file.is_open()) {
        std::cerr << "\033[1;31mCould not open config.json\033[0m" << std::endl;
        return;
    }

    json cf;
    try {
        file >> cf;
    } catch (const json::parse_error& e) {
        Log::error("JSON parse error: " + std::string(e.what()));
        file.close();
        return;
    }
    file.close();

    // Update JSON
    cf["Tx"] = static_cast<uint16_t>(idT);
    cf["Rx"] = static_cast<uint16_t>(idR);

    // Save to file
    saveConfig(cf);

    // Apply to CanTp
    tp.setTx(static_cast<uint16_t>(idT));
    tp.setRx(static_cast<uint16_t>(idR));

    Log::setup("             -> IDs updated and saved.");
}

// ---------------------------------------------------------------
void busSettings(CanTp &tp) {
    while (true) {
        Log::setup("\n    ======== Bus Settings ========");
        std::cout << "    1. Separation Time\n";
        std::cout << "    2. Block Size\n";
        std::cout << "    0. Back\n";
        std::cout << "   Choice: ";
        int choice;
        std::cin >> choice;

        switch (choice) {
            case 1: {
                int st;
                std::cout << "   Enter new Separation Time: ";
                std::cin >> st;
                if (st > 0 && st <= tp.staticST) {
                    tp.setst(static_cast<uint8_t>(st));

                    std::ifstream file("config.json");
                    if (!file.is_open()) {
                        Log::error("Could not open config.json");
                        break;
                    }
                    json cf;
                    try { file >> cf; } catch (...) {
                        Log::error("Invalid JSON in config.json");
                        file.close();
                        break;
                    }
                    file.close();

                    cf["SeparationTime"] = st;  // MATCH EXACTLY
                    saveConfig(cf);

                    Log::setup("   -> Separation Time updated.");
                } else {
                    Log::error("   Invalid value!");
                }
                break;
            }
            case 2: {
                int bs;
                std::cout << "   Enter new Block Size: ";
                std::cin >> bs;
                if (bs > 0 && bs <= tp.staticBS) {
                    tp.setbs(static_cast<uint8_t>(bs));

                    std::ifstream file("config.json");
                    if (!file.is_open()) {
                        Log::error("Could not open config.json");
                        break;
                    }
                    json cf;
                    try { file >> cf; } catch (...) {
                        Log::error("Invalid JSON in config.json");
                        file.close();
                        break;
                    }
                    file.close();

                    cf["BlockSize"] = bs;
                    saveConfig(cf);

                    Log::setup("   -> Block Size updated.");
                } else {
                    Log::error("   Invalid value!");
                }
                break;
            }
            case 0:
                return;
            default:
                Log::error("   Invalid choice!");
        }
    }
}

// ---------------------------------------------------------------
void setting(CanTp &tp, int &idT, int &idR) {
    while (true) {
        Log::setup("\n    ======== Settings ========");
        std::cout << "    1. ID Setup\n";
        std::cout << "    2. Bus Settings\n";
        std::cout << "    0. Back\n";
        std::cout << "   Choice: ";
        int choice;
        std::cin >> choice;

        switch (choice) {
            case 1:
                idSettings(tp, idT, idR);
                break;
            case 2:
                busSettings(tp);
                break;
            case 0:
                return;
            default:
                Log::error("   Invalid choice!");
        }
    }
}

// ---------------------------------------------------------------
int main() {
    try {
        int idT, idR;
        Log::info("======= Welcome to Virtual CAN Simulator =======");

        std::ifstream file("config.json");
        if (!file.is_open()) {
            std::cerr << "\033[1;31mCould not open config.json\033[0m" << std::endl;
            return 1;
        }
        json cf;
        try {
            file >> cf;
        } catch (...) {
            Log::error("Invalid JSON in config.json");
            return 1;
        }
        file.close();

        std::cout << "(!) Do you want to configure the Rx and Tx manually ? (y/n)" << std::endl;
        char ans;
        std::cout << "(o)> ";
        std::cin >> ans;

        if (ans == 'y' || ans == 'Y') {
            std::cout << "(o) > Please enter the Tx and Rx configuration (1-2047)" << std::endl;
            std::cout << "Enter ECU Tx ID (1-2047): ";
            std::cin >> idT;
            std::cout << "Enter ECU Rx ID (1-2047): ";
            std::cin >> idR;

            // Save manual config
            cf["Tx"] = static_cast<uint16_t>(idT);
            cf["Rx"] = static_cast<uint16_t>(idR);
            saveConfig(cf);
        } else {
            std::cout << "(!) Using default configuration..." << std::endl;
            idT = cf["Tx"];
            idR = cf["Rx"];
        }
        std::cout << cf["Tx"] <<cf["Rx"]<< std::endl;


        CanTp tp(static_cast<uint16_t>(idT), static_cast<uint16_t>(idR));
        tp.setbs(cf["BlockSize"]);
        tp.setst(cf["SeparationTime"]);  // FIXED KEY NAME

        std::string ecuName;
        std::cout << "Enter ECU Name: ";
        std::cin >> ecuName;

        bool availability = true;

        int choice;
        while (true) {
            Log::setup("\n    ======= Home Menu =======");
            std::cout << "    1. Send Message\n";
            std::cout << "    2. Receive Message\n";
            std::cout << "    3. Settings\n";
            std::cout << "    0. Quit\n";
            std::cout << "   Choice: ";
            std::cin >> choice;

            switch (choice) {
                case 1: {
                    Sender sender(ecuName, availability);
                    Log::tx("Enter message to send:");
                    std::cin.ignore();
                    std::string msg;
                    std::getline(std::cin, msg);
                    sender.sendEcuData(msg, tp);
                    break;
                }
                case 2: {
                    Receiver receiver(ecuName, availability);
                    receiver.receiveEcuData(tp);
                    Log::rx("Message received: " + receiver.getmessage());
                    break;
                }
                case 3:
                    setting(tp, idT, idR);
                    break;
                case 0:
                    Log::info("Closing the app...");
                    return 0;
                default:
                    Log::error("Invalid choice!");
            }
        }
    } catch (const std::exception &e) {
        Log::error(std::string("Error: ") + e.what());
        return 1;
    }

    return 0;
}