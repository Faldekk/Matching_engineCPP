#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct PriceLevel {
    double price;
    double quantity; // BTC ma ulamkowe ilosci, np. 0.001 BTC.
};

struct MarketSnapshot {
    std::uint64_t updateId = 0;
    std::vector<PriceLevel> bids;
    std::vector<PriceLevel> asks;
};

// Parser tylko formatu Binance depth5, a nie dowolnego JSON.
MarketSnapshot parseMarketSnapshot(const std::string& message);
int runLiveMarket(int maxUpdates = 0);
