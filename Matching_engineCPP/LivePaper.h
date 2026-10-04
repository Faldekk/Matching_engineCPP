#pragma once
#include "PaperTrading.h"
#include <mutex>
#include <string>
#include <map>

struct AssetPosition {
    std::string symbol;
    double quantity = 0;
    double costBasis = 0;
    double realizedPnl = 0;
    double value = 0;
    bool valued = false;
};

struct PortfolioFill {
    std::string symbol;
    PaperFill fill;
};

struct ConditionalOrder {
    int id = 0;
    std::string symbol;
    std::string type; // BUY_LIMIT lub STOP_LOSS (sprzedaz Market po aktywacji).
    double price = 0;
    double quantity = 0;
    double filled = 0;
    bool triggered = false;
    std::string status = "NEW";
};

struct PaperState {
    PaperTrading account;
    MarketSnapshot market;
    std::string status = "Czekamy na dane Binance";
    std::string symbol = "BTCUSDT";
    std::vector<AssetPosition> positions;
    std::vector<PortfolioFill> fills;
    std::vector<ConditionalOrder> conditionalOrders;
    double reservedUsdt = 0;
    std::map<std::string, double> reservedAssets;
};

class LivePaperSession {
    std::mutex mutex;
    std::map<std::string, PaperState> markets;
    double cash = 10000;
    std::vector<PortfolioFill> fills;
    std::vector<ConditionalOrder> conditionalOrders;
    int nextConditionalId = 1;
    double reservedCash(int excludeId = 0) const;
    double reservedAsset(const std::string& symbol, int excludeId = 0) const;
    PaperResult executeOrder(Side side, double quantity, const std::string& symbol,
        int excludeId = 0, double buyLimit = 0);
    void processConditionalOrders(const std::string& symbol);
public:
    LivePaperSession();
    static bool supports(const std::string& symbol);
    void onSnapshot(const MarketSnapshot& market,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    void onDisconnected(const std::string& message);
    void onSnapshot(const std::string& symbol, const MarketSnapshot& market,
        std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now());
    void onDisconnected(const std::string& symbol, const std::string& message);
    PaperState read(const std::string& symbol = "BTCUSDT");
    PaperResult order(Side side, double quantity, const std::string& symbol = "BTCUSDT");
    int addConditionalOrder(const std::string& symbol, const std::string& type, double quantity, double price);
    bool cancelConditionalOrder(int id);
};

std::string executePaperCommand(LivePaperSession& session, const std::string& line);
int runLivePaper();
