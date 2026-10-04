#include "PaperTrading.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <stdexcept>

namespace {
void validateLevels(const std::vector<PriceLevel>& levels, bool bids) {
    if (levels.size() > 5) throw std::invalid_argument("Oczekiwano top 5 rynku");
    double previous = 0;
    for (const PriceLevel& level : levels) {
        if (!std::isfinite(level.price) || !std::isfinite(level.quantity)
            || level.price <= 0 || level.quantity <= 0)
            throw std::invalid_argument("Niepoprawna cena lub ilosc rynku");
        if (previous > 0 && ((bids && level.price >= previous) || (!bids && level.price <= previous)))
            throw std::invalid_argument("Niepoprawna kolejnosc cen");
        previous = level.price;
    }
}
}

void PaperTrading::updateMarket(const MarketSnapshot& snapshot, std::chrono::steady_clock::time_point now) {
    validateLevels(snapshot.bids, true);
    validateLevels(snapshot.asks, false);
    if (!snapshot.bids.empty() && !snapshot.asks.empty() && snapshot.bids[0].price >= snapshot.asks[0].price)
        throw std::invalid_argument("Skrzyzowana ksiazka rynku");
    // Ten sam obraz nie moze ponownie udostepnic zuzytej plynnosci ani odswiezyc czasu.
    if (hasMarket && snapshot.updateId <= market.updateId) return;
    market = snapshot;
    receivedAt = now;
    hasMarket = true;
}

void PaperTrading::disconnect() {
    hasMarket = false;
    market = {};
}

bool PaperTrading::marketReady(std::chrono::steady_clock::time_point now) const {
    return hasMarket && now >= receivedAt && now - receivedAt <= std::chrono::seconds(5);
}

PaperResult PaperTrading::marketOrder(Side side, double quantity, std::chrono::steady_clock::time_point now, double buyLimit) {
    if (!std::isfinite(buyLimit) || buyLimit < 0) throw std::invalid_argument("Niepoprawny limit ceny");
    if (side != Side::Buy && side != Side::Sell) throw std::invalid_argument("Niepoprawna strona");
    if (!std::isfinite(quantity) || quantity <= 0) throw std::invalid_argument("Ilosc musi byc dodatnia");
    if (!marketReady(now)) throw std::runtime_error("Brak swiezych danych rynku");
    if (side == Side::Sell && quantity > btc) throw std::runtime_error("Za malo jednostek aktywa");
    auto& levels = side == Side::Buy ? market.asks : market.bids;
    PaperResult result;
    result.requested = quantity;
    double remaining = quantity;
    double total = 0;
    double reference = 0;
    std::vector<PaperFill> fills;
    // Najpierw liczymy wynik na kopii. Odrzucone zlecenie nie zmienia portfela ani rynku.
    for (const PriceLevel& level : levels) {
        if (side == Side::Buy && buyLimit > 0 && level.price > buyLimit) break;
        if (remaining <= 0) break;
        if (level.quantity <= 0) continue;
        if (reference == 0) reference = level.price;
        double filled = std::min(remaining, level.quantity);
        total += filled * level.price;
        result.filled += filled;
        remaining -= filled;
        fills.push_back({side, level.price, filled});
    }
    if (!std::isfinite(total) || !std::isfinite(result.filled))
        throw std::invalid_argument("Zbyt duza wartosc zlecenia");
    if (side == Side::Buy && total > usdt) throw std::runtime_error("Za malo USDT");
    result.cancelled = remaining;
    if (result.filled == 0) return result;
    result.averagePrice = total / result.filled;
    result.slippage = side == Side::Buy ? result.averagePrice - reference : reference - result.averagePrice;
    double nextUsdt = side == Side::Buy ? usdt - total : usdt + total;
    double nextBtc = side == Side::Buy ? btc + result.filled : btc - result.filled;
    double soldCost = side == Side::Sell ? (costBasis / btc) * result.filled : 0;
    double nextCost = side == Side::Buy ? costBasis + total : costBasis - soldCost;
    double nextPnl = realizedPnl + (side == Side::Sell ? total - soldCost : 0);
    if (!std::isfinite(nextUsdt) || !std::isfinite(nextBtc) || !std::isfinite(nextCost) || !std::isfinite(nextPnl))
        throw std::invalid_argument("Przekroczony zakres portfela");

    history.reserve(history.size() + fills.size());
    remaining = result.filled;
    for (PriceLevel& level : levels) {
        double filled = std::min(remaining, level.quantity);
        level.quantity -= filled;
        remaining -= filled;
        if (remaining <= 0) break;
    }
    usdt = nextUsdt;
    btc = nextBtc;
    costBasis = btc == 0 ? 0 : nextCost;
    realizedPnl = nextPnl;
    for (const PaperFill& fill : fills) history.push_back(fill);
    return result;
}

double PaperTrading::equity(std::chrono::steady_clock::time_point now) const {
    if (btc == 0) return usdt;
    if (!marketReady(now) || market.bids.empty()) throw std::runtime_error("Brak ceny do wyceny BTC");
    // Wycena po best bid: przy sprzedazy dostajemy bid, nie ask.
    return usdt + btc * market.bids.front().price;
}

int runPaperDemo() {
    PaperTrading account;
    account.updateMarket({1, {{99, 1}}, {{100, 0.02}, {101, 0.03}}});
    auto buy = account.marketOrder(Side::Buy, 0.04);
    std::cout << std::fixed << std::setprecision(8)
        << "Przyklad offline: wirtualny portfel, bez prowizji.\n"
        << "BUY: " << buy.filled << " BTC, srednia " << buy.averagePrice
        << ", slippage " << buy.slippage << " USDT/BTC\n";
    account.updateMarket({2, {{102, 0.5}}, {{103, 0.5}}});
    auto sell = account.marketOrder(Side::Sell, 0.01);
    std::cout << "SELL: " << sell.filled << " BTC @ " << sell.averagePrice
        << "\nPortfel: " << account.getUsdt() << " USDT + " << account.getBtc() << " BTC"
        << "\nWycena: " << account.equity() << " USDT"
        << "\nZrealizowany wynik: " << account.getRealizedPnl() << " USDT\n";
    return 0;
}
