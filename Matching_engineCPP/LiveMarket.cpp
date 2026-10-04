#include "BinanceFeed.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>



namespace {
void printSnapshot(const MarketSnapshot& snapshot, bool clearScreen) {
    if (clearScreen) std::cout << "\x1b[2J\x1b[H";
    auto now = std::time(nullptr);
    std::tm time{};
    localtime_s(&time, &now);
    std::cout << "Binance BTC/USDT | odbior lokalny: " << std::put_time(&time, "%H:%M:%S")
        << " | update ID: " << snapshot.updateId << '\n'
        << "Ceny: USDT za BTC. Ilosci: BTC. Koniec: Ctrl+C.\n"
        << "      BID cena      BID BTC |       ASK cena      ASK BTC\n";
    for (std::size_t i = 0; i < 5; ++i) {
        if (i < snapshot.bids.size())
            std::cout << std::fixed << std::setprecision(2) << std::setw(14) << snapshot.bids[i].price
                << std::setprecision(8) << std::setw(13) << snapshot.bids[i].quantity;
        else std::cout << std::setw(27) << "brak";
        std::cout << " | ";
        if (i < snapshot.asks.size())
            std::cout << std::setprecision(2) << std::setw(14) << snapshot.asks[i].price
                << std::setprecision(8) << std::setw(13) << snapshot.asks[i].quantity;
        else std::cout << std::setw(27) << "brak";
        std::cout << '\n';
    }
    if (!snapshot.bids.empty() && !snapshot.asks.empty())
        std::cout << "Spread: " << std::setprecision(8)
            << snapshot.asks.front().price - snapshot.bids.front().price << " USDT\n";
    else std::cout << "Spread: brak danych\n";
    std::cout << "To zagregowana ksiazka gieldy. Nasz engine nie wysyla zlecen.\n" << std::flush;
}

}

int runLiveMarket(int maxUpdates) {
    if (maxUpdates < 0) throw std::invalid_argument("Niepoprawny limit aktualizacji");
    DWORD originalMode = 0;
    HANDLE console = GetStdHandle(STD_OUTPUT_HANDLE);
    bool clearScreen = GetConsoleMode(console, &originalMode)
        && SetConsoleMode(console, originalMode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    std::atomic<bool> stop = false;
    int result = streamBinance(stop,
        [&](const MarketSnapshot& snapshot) { printSnapshot(snapshot, clearScreen); },
        [&](const std::string& message) {
            if (clearScreen) std::cout << "\x1b[2J\x1b[H";
            std::cout << "Rynek: " << message << '\n' << std::flush;
        }, maxUpdates);
    if (clearScreen) SetConsoleMode(console, originalMode);
    return result;
}
