#include "LivePaper.h"
#include "BinanceFeed.h"
#include <atomic>
#include <iomanip>
#include <iostream>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <cmath>

LivePaperSession::LivePaperSession() {
    for (const std::string symbol : {"BTCUSDT", "ETHUSDT", "SOLUSDT"}) {
        markets[symbol].symbol = symbol;
    }
}

bool LivePaperSession::supports(const std::string& symbol) {
    return symbol == "BTCUSDT" || symbol == "ETHUSDT" || symbol == "SOLUSDT";
}

void LivePaperSession::onSnapshot(const MarketSnapshot& market, std::chrono::steady_clock::time_point now) {
    onSnapshot("BTCUSDT", market, now);
}

void LivePaperSession::onSnapshot(const std::string& symbol, const MarketSnapshot& market, std::chrono::steady_clock::time_point now) {
    std::lock_guard lock(mutex);
    auto& state = markets.at(symbol);
    state.account.updateMarket(market, now);
    if (market.updateId >= state.market.updateId) state.market = market;
    state.status = "Polaczono z Binance";
    processConditionalOrders(symbol);
}

void LivePaperSession::onDisconnected(const std::string& message) {
    onDisconnected("BTCUSDT", message);
}

void LivePaperSession::onDisconnected(const std::string& symbol, const std::string& message) {
    std::lock_guard lock(mutex);
    auto& state = markets.at(symbol);
    state.account.disconnect();
    state.market = {};
    state.status = message;
}

PaperState LivePaperSession::read(const std::string& symbol) {
    std::lock_guard lock(mutex);
    auto state = markets.at(symbol);
    state.account.usdt = cash;
    state.fills = fills;
    state.conditionalOrders = conditionalOrders;
    state.reservedUsdt = reservedCash();
    for (const auto& [name, asset] : markets) state.reservedAssets[name] = reservedAsset(name);
    auto now = std::chrono::steady_clock::now();
    for (const auto& [name, asset] : markets) {
        AssetPosition position;
        position.symbol = name;
        position.quantity = asset.account.getBtc();
        position.costBasis = asset.account.getCostBasis();
        position.realizedPnl = asset.account.getRealizedPnl();
        position.valued = position.quantity == 0 || (asset.account.marketReady(now) && !asset.market.bids.empty());
        if (position.quantity > 0 && position.valued) position.value = position.quantity * asset.market.bids.front().price;
        state.positions.push_back(position);
    }
    // Zwracamy kopie: wyswietlanie nie trzyma blokady i nie moze zmienic portfela.
    return state;
}

PaperResult LivePaperSession::order(Side side, double quantity, const std::string& symbol) {
    std::lock_guard lock(mutex);
    return executeOrder(side, quantity, symbol);
}

double LivePaperSession::reservedCash(int excludeId) const {
    double total = 0;
    for (const auto& order : conditionalOrders)
        if (order.id != excludeId && order.type == "BUY_LIMIT" && (order.status == "NEW" || order.status == "PARTIALLY_FILLED"))
            total += (order.quantity - order.filled) * order.price;
    return total;
}

double LivePaperSession::reservedAsset(const std::string& symbol, int excludeId) const {
    double total = 0;
    for (const auto& order : conditionalOrders)
        if (order.id != excludeId && order.symbol == symbol && order.type == "STOP_LOSS" && (order.status == "NEW" || order.status == "PARTIALLY_FILLED"))
            total += order.quantity - order.filled;
    return total;
}

PaperResult LivePaperSession::executeOrder(Side side, double quantity, const std::string& symbol, int excludeId, double buyLimit) {
    if (!supports(symbol)) throw std::invalid_argument("Nieobslugiwany rynek");
    auto& state = markets.at(symbol);
    if (side == Side::Sell && quantity > state.account.getBtc() - reservedAsset(symbol, excludeId))
        throw std::runtime_error("Za malo dostepnego aktywa; czesc moze byc zarezerwowana");
    // Liczymy na kopii: blad nie zmieni salda wspolnego ani historii.
    auto account = state.account;
    const double budget = cash - reservedCash(excludeId);
    account.usdt = budget;
    const auto previousCount = account.getHistory().size();
    auto result = account.marketOrder(side, quantity, std::chrono::steady_clock::now(), buyLimit);
    fills.reserve(fills.size() + account.getHistory().size() - previousCount);
    for (auto i = previousCount; i < account.getHistory().size(); ++i)
        fills.push_back({symbol, account.getHistory()[i]});
    cash += account.getUsdt() - budget;
    account.usdt = cash;
    state.account = std::move(account);
    return result;
}

int LivePaperSession::addConditionalOrder(const std::string& symbol, const std::string& type, double quantity, double price) {
    std::lock_guard lock(mutex);
    if (!supports(symbol) || (type != "BUY_LIMIT" && type != "STOP_LOSS")) throw std::invalid_argument("Niepoprawny rynek lub typ zlecenia");
    if (!std::isfinite(quantity) || !std::isfinite(price) || quantity <= 0 || price <= 0 || !std::isfinite(quantity * price))
        throw std::invalid_argument("Ilosc i cena musza byc dodatnie");
    const auto& account = markets.at(symbol).account;
    if (!account.marketReady()) throw std::runtime_error("Brak swiezych danych rynku");
    if (type == "BUY_LIMIT" && quantity * price > cash - reservedCash()) throw std::runtime_error("Za malo dostepnych USDT");
    if (type == "STOP_LOSS" && quantity > account.getBtc() - reservedAsset(symbol)) throw std::runtime_error("Za malo dostepnego aktywa");
    ConditionalOrder order;
    order.id = nextConditionalId;
    order.symbol = symbol;
    order.type = type;
    order.quantity = quantity;
    order.price = price;
    conditionalOrders.push_back(order);
    ++nextConditionalId;
    processConditionalOrders(symbol);
    return order.id;
}

bool LivePaperSession::cancelConditionalOrder(int id) {
    std::lock_guard lock(mutex);
    for (auto& order : conditionalOrders) {
        if (order.id == id && (order.status == "NEW" || order.status == "PARTIALLY_FILLED")) {
            order.status = "CANCELLED";
            return true;
        }
    }
    return false;
}

void LivePaperSession::processConditionalOrders(const std::string& symbol) {
    const auto& state = markets.at(symbol);
    if (!state.account.marketReady()) return;
    // Symulacja L2: kolejnosc naszych zlecen jest FIFO, kolejki Binance nie znamy.
    for (auto& order : conditionalOrders) {
        if (order.symbol != symbol || (order.status != "NEW" && order.status != "PARTIALLY_FILLED")) continue;
        if (order.type == "STOP_LOSS") {
            if (!state.market.bids.empty() && state.market.bids.front().price <= order.price) order.triggered = true;
            if (!order.triggered) continue;
        } else if (state.market.asks.empty() || state.market.asks.front().price > order.price) continue;
        auto result = executeOrder(order.type == "BUY_LIMIT" ? Side::Buy : Side::Sell,
            order.quantity - order.filled, symbol, order.id, order.type == "BUY_LIMIT" ? order.price : 0);
        order.filled += result.filled;
        if (order.filled >= order.quantity) order.status = "FILLED";
        else if (order.filled > 0) order.status = "PARTIALLY_FILLED";
    }
}

std::string executePaperCommand(LivePaperSession& session, const std::string& line) {
    std::istringstream input(line);
    input.imbue(std::locale::classic());
    std::string command;
    input >> command;
    std::ostringstream output;
    output << std::fixed << std::setprecision(8);
    if (command == "buy" || command == "sell") {
        double quantity = 0;
        if (!(input >> quantity)) throw std::invalid_argument("Podaj ilosc BTC z kropka, np. buy 0.001");
        input >> std::ws;
        if (!input.eof()) throw std::invalid_argument("Za duzo argumentow");
        auto result = session.order(command == "buy" ? Side::Buy : Side::Sell, quantity);
        output << "Wykonano: " << result.filled << " BTC; anulowano: " << result.cancelled
            << "; srednia: " << result.averagePrice << "; slippage: " << result.slippage << " USDT/BTC\n";
        return output.str();
    }
    input >> std::ws;
    if (!input.eof()) throw std::invalid_argument("Ta komenda nie przyjmuje argumentow");
    if (command == "help" || command.empty())
        return "Komendy: buy 0.001, sell 0.001, wallet, book, history, help, quit\n";
    auto state = session.read();
    if (command == "wallet") {
        output << "Saldo: " << state.account.getUsdt() << " USDT + " << state.account.getBtc() << " BTC\n"
            << "Zrealizowany wynik: " << state.account.getRealizedPnl() << " USDT\n";
        try { output << "Wycena: " << state.account.equity() << " USDT\n"; }
        catch (const std::exception&) { output << "brak swiezej ceny do wyceny\n"; }
    }
    else if (command == "book") {
        if (state.account.marketReady()) {
            output << "Binance BTC/USDT, update ID: " << state.market.updateId << "\nBID: cena | BTC\n";
            for (const auto& level : state.market.bids) output << level.price << " | " << level.quantity << '\n';
            output << "ASK: cena | BTC\n";
            for (const auto& level : state.market.asks) output << level.price << " | " << level.quantity << '\n';
        }
        else output << "Brak swiezej ksiazki rynku\n";
    }
    else if (command == "history") {
        if (state.account.getHistory().empty()) output << "Brak transakcji\n";
        for (const PaperFill& fill : state.account.getHistory())
            output << (fill.side == Side::Buy ? "BUY " : "SELL ") << fill.quantity << " BTC @ " << fill.price << '\n';
    }
    else throw std::invalid_argument("Nieznana komenda. Wpisz help");
    output << "Rynek: " << state.status << "; handel: "
        << (state.account.marketReady() ? "aktywny" : "zablokowany") << '\n';
    return output.str();
}

int runLivePaper() {
    LivePaperSession session;
    std::atomic<bool> stop = false;
    std::thread feed([&] {
        streamBinance(stop, [&](const MarketSnapshot& market) { session.onSnapshot(market); },
            [&](const std::string& error) { session.onDisconnected(error); });
    });
    std::cout << "Paper trading BTC/USDT: 10 000 wirtualnych USDT, bez prowizji.\n"
        << "Wykonania symulowane na top 5. Polaczenie trwa w tle.\n"
        << executePaperCommand(session, "help");
    std::string line;
    while (std::cout << "> " << std::flush, std::getline(std::cin, line)) {
        if (line == "quit") break;
        try { std::cout << executePaperCommand(session, line); }
        catch (const std::exception& error) { std::cout << "Blad: " << error.what() << '\n'; }
    }
    stop = true;
    feed.join();
    std::cout << "Symulator zakonczony. Portfel nie jest jeszcze zapisywany.\n";
    return 0;
}
