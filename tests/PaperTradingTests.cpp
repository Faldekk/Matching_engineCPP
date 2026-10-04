#include "PaperTrading.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

void require(bool condition) {
    if (!condition) throw std::runtime_error("PaperTrading test failed");
}
void near(double actual, double expected) { require(std::abs(actual - expected) < 1e-8); }

void rejectOrder(PaperTrading& account, Side side, double quantity,
    std::chrono::steady_clock::time_point now) {
    double cash = account.getUsdt(), btc = account.getBtc();
    auto historySize = account.getHistory().size();
    bool rejected = false;
    try { account.marketOrder(side, quantity, now); }
    catch (const std::exception&) { rejected = true; }
    require(rejected);
    near(account.getUsdt(), cash);
    near(account.getBtc(), btc);
    require(account.getHistory().size() == historySize);
}

int main() {
    try {
        auto now = std::chrono::steady_clock::now();
        PaperTrading account;
        near(account.getUsdt(), 10000);
        rejectOrder(account, Side::Buy, 1, now);
        MarketSnapshot market{1, {{99, 0.01}, {98, 0.05}}, {{100, 0.02}, {101, 0.03}}};
        account.updateMarket(market, now);
        rejectOrder(account, Side::Sell, 0.01, now);
        rejectOrder(account, Side::Buy, 0, now);
        rejectOrder(account, Side::Buy, -1, now);
        rejectOrder(account, Side::Buy, std::numeric_limits<double>::quiet_NaN(), now);
        rejectOrder(account, Side::Buy, std::numeric_limits<double>::infinity(), now);
        rejectOrder(account, static_cast<Side>(42), 1, now);
        auto buy = account.marketOrder(Side::Buy, 0.04, now);
        near(buy.filled, 0.04); near(buy.cancelled, 0);
        near(buy.averagePrice, 100.5); near(buy.slippage, 0.5);
        near(account.getUsdt(), 9995.98); near(account.getCostBasis(), 4.02);
        near(account.equity(now), 9999.94);
        require(account.getHistory().size() == 2);
        auto sell = account.marketOrder(Side::Sell, 0.02, now);
        near(sell.averagePrice, 98.5); near(sell.slippage, 0.5);
        near(account.getRealizedPnl(), -0.04); near(account.getCostBasis(), 2.01);
        account.updateMarket(market, now + std::chrono::seconds(4));
        auto partial = account.marketOrder(Side::Buy, 1, now);
        near(partial.filled, 0.01); near(partial.cancelled, 0.99);
        near(account.marketOrder(Side::Buy, 1, now).filled, 0);
        require(!account.marketReady(now + std::chrono::seconds(6)));
        rejectOrder(account, Side::Buy, 1, now + std::chrono::seconds(6));
        require(!account.marketReady(now - std::chrono::seconds(1)));
        account.disconnect();
        rejectOrder(account, Side::Sell, 0.01, now);
        account.updateMarket({2, {{110, 1}}, {{111, 1}}}, now);
        account.marketOrder(Side::Sell, account.getBtc(), now);
        near(account.getBtc(), 0); near(account.getCostBasis(), 0);
        PaperTrading poor;
        poor.updateMarket({1, {{19999, 1}}, {{20000, 1}}}, now);
        rejectOrder(poor, Side::Buy, 1, now);
        near(poor.marketOrder(Side::Buy, 0.5, now).filled, 0.5);
        near(poor.getUsdt(), 0);
        rejectOrder(poor, Side::Buy, 0.1, now);
        PaperTrading shallow;
        shallow.updateMarket({1, {}, {{100, 0.01}}}, now);
        near(shallow.marketOrder(Side::Buy, 0.5, now).filled, 0.01);
        auto noBid = shallow.marketOrder(Side::Sell, 0.01, now);
        near(noBid.filled, 0); near(noBid.cancelled, 0.01);
        bool noValue = false;
        try { shallow.equity(now); } catch (const std::exception&) { noValue = true; }
        require(noValue);
        bool badMarket = false;
        try { shallow.updateMarket({2, {{101, 1}}, {{100, 1}}}, now); }
        catch (const std::exception&) { badMarket = true; }
        require(badMarket);
        std::cout << "PaperTradingTests: OK\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
