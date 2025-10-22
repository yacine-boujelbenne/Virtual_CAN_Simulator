#include <cstdint>
#include <iostream>
#include <string>
#include "../headers/CanTp.hpp"
#include "../headers/Receiver.hpp"
#include "../headers/Sender.hpp"
#include "../headers/Logger.hpp"

void idSettings(CanTp &tp, int &idT, int &idR) {
    Log::setup("             (o) > Please enter the Tx and Rx IDs (1-2047)");
    std::cout << "        Tx: ";
    std::cin >> idT;
    std::cout << "        Rx: ";
    std::cin >> idR;

    tp.setTx(static_cast<uint16_t>(idT));
    tp.setRx(static_cast<uint16_t>(idR));
    Log::setup("             -> IDs updated.");
}

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

int main() {
    try {
        int idT, idR;
        Log::info("======= Welcome to Virtual CAN Simulator =======");
        std::cout << "Enter ECU Tx ID (1-2047): ";
        std::cin >> idT;
        std::cout << "Enter ECU Rx ID (1-2047): ";
        std::cin >> idR;

        CanTp tp(static_cast<uint16_t>(idT), static_cast<uint16_t>(idR));

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
                    std::cin.ignore(); // flush newline
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
