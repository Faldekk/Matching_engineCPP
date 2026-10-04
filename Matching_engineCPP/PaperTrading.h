#pragma once
#include "MarketData.h"
#include "Order.h"
#include <chrono>
#include <vector>

struct PaperFill {
    Side side;
    double price;
    double quantity;
};

struct PaperResult {
    double requested = 0;
    double filled = 0;
    double cancelled = 0;
    double averagePrice = 0;
    double slippage = 0;
};

// Osobny model handlu na L2. OrderBook nadal prowadzi wlasne zlecenia i FIFO.
class PaperTrading {
    // Sesja synchronizuje wspolne USDT pomiedzy rynkami; btc oznacza ilosc aktywa.
    friend class LivePaperSession;
    double usdt = 10000;
    double btc = 0;
    double costBasis = 0;
    double realizedPnl = 0;
    MarketSnapshot market;
    bool hasMarket = false;
    std::chrono::steady_clock::time_point receivedAt{};
    std::vector<PaperFill> history;
public:
    void updateMarket(const MarketSnapshot& snapshot,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    void disconnect();
    bool marketReady(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) const;
    PaperResult marketOrder(Side side, double quantity,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now(), double buyLimit = 0);
    double getUsdt() const { return usdt; }
    double getBtc() const { return btc; }
    double getCostBasis() const { return costBasis; }
    double getRealizedPnl() const { return realizedPnl; }
    double equity(std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now()) const;
    const std::vector<PaperFill>& getHistory() const { return history; }
};

int runPaperDemo();
