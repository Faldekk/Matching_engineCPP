#include "OrderBook.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <chrono>
#include <iterator>

void OrderBook::recordTrade(int buyId, int sellId, double price, int quantity) {
    // system_clock daje aktualny czas zegara systemowego.
    auto now = std::chrono::system_clock::now();
    // duration_cast przelicza czas od poczatku epoki na milisekundy.
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch());

    Trade trade{};
    trade.id = nextTradeId;
    trade.buyOrderId = buyId;
    trade.sellOrderId = sellId;
    trade.price = price;
    trade.quantity = quantity;
    trade.timestampMs = milliseconds.count();

    trades.push_back(trade);
    nextTradeId++;
    // Kazda transakcja moze przekroczyc prog. Zapamietujemy takie stopy od razu.
    activateStops(price);
}

bool OrderBook::canFullyFill(const Order& order) const {
    // Odczytujemy listy bez zmiany zlecen. Nie wykonujemy zadnych transakcji.
    std::int64_t availableQuantity = 0;
    if (order.side == Side::Buy) {
        for (auto it = asks.begin(); it != asks.end(); ++it) {
            if (order.type == OrderType::Limit && it->first > order.price) {
                break;
            }
            for (const Order& resting : it->second) {
                availableQuantity += resting.quantity;
                if (availableQuantity >= order.quantity) return true;
            }
        }
    }
    else {
        for (auto it = bids.begin(); it != bids.end(); ++it) {
            if (order.type == OrderType::Limit && it->first < order.price) {
                break;
            }
            for (const Order& resting : it->second) {
                availableQuantity += resting.quantity;
                if (availableQuantity >= order.quantity) return true;
            }
        }
    }
    return false;
}

void OrderBook::activateStops(double tradePrice) {
    auto it = stopOrders.begin();
    while (it != stopOrders.end()) {
        bool triggered = false;
        if (it->side == Side::Buy && tradePrice >= it->price) triggered = true;
        if (it->side == Side::Sell && tradePrice <= it->price) triggered = true;
        if (triggered) {
            auto activated = it++;
            // Przenosimy wezel listy: indeks nadal wskazuje ten sam element.
            orderIndex.at(activated->id).place = Place::TriggeredStop;
            triggeredStops.splice(triggeredStops.end(), stopOrders, activated);
        }
        else ++it;
    }
}

void OrderBook::processStopOrders() {
    if (processingStops) {
        return; // addOrder wywolane ponizej nie uruchomi kolejnej takiej petli.
    }
    processingStops = true;
    try {
        while (!triggeredStops.empty()) {
            Order order = triggeredStops.front();
            orderIndex.erase(order.id);
            triggeredStops.pop_front();
            order.type = OrderType::Market;
            order.price = 0.0;
            addOrder(order);
            // Nowe transakcje moga dopisac nastepne stopy do kolejki.
        }
    }
    catch (...) {
        processingStops = false;
        throw;
    }
    processingStops = false;
}

bool OrderBook::hasOrder(int id) const {
    return orderIndex.find(id) != orderIndex.end();
}

bool OrderBook::getOrder(int id, Order& order) const {
    auto it = orderIndex.find(id);
    if (it == orderIndex.end()) return false;
    order = *it->second.position;
    return true;
}

void OrderBook::addOrder(Order order) {
    if (order.quantity <= 0) {
        throw std::invalid_argument("Quantity must be positive");
    }
    if (order.side != Side::Buy && order.side != Side::Sell) {
        throw std::invalid_argument("Invalid order side");
    }
    if (order.type != OrderType::Limit && order.type != OrderType::Market &&
        order.type != OrderType::StopMarket) {
        throw std::invalid_argument("Invalid order type");
    }
    if (order.timeInForce != TimeInForce::GTC && order.timeInForce != TimeInForce::IOC &&
        order.timeInForce != TimeInForce::FOK) {
        throw std::invalid_argument("Invalid time in force");
    }
    if (order.type == OrderType::StopMarket && order.timeInForce != TimeInForce::GTC) {
        throw std::invalid_argument("Waiting StopMarket requires GTC");
    }
    if (order.type == OrderType::Limit || order.type == OrderType::StopMarket) {
        if (!std::isfinite(order.price) || order.price <= 0.0) {
            throw std::invalid_argument("Limit price must be finite and positive");
        }
    }
    if (hasOrder(order.id)) {
        throw std::invalid_argument("Order ID is already active");
    }

    // Status zapisujemy dopiero po sprawdzeniu poprawnosci zlecenia.
    statuses[order.id] = OrderStatus::New;

    if (order.type == OrderType::StopMarket) {
        stopOrders.push_back(order);
        orderIndex[order.id] = {Place::WaitingStop, order.price, std::prev(stopOrders.end())};
        // Bez transakcji nie mamy ceny do aktywacji stopa.
        if (!trades.empty()) {
            activateStops(trades.back().price);
        }
        processStopOrders();
        return;
    }

    if (order.timeInForce == TimeInForce::FOK && !canFullyFill(order)) {
        statuses[order.id] = OrderStatus::Cancelled;
        return; // Brak calej ilosci: ksiazka i historia pozostaja bez zmian.
    }

    if (order.side == Side::Buy) {
        matchBuyOrder(order);

        if (order.quantity > 0 && order.type == OrderType::Limit &&
            order.timeInForce == TimeInForce::GTC) {
            auto& orders = bids[order.price];
            orders.push_back(order);
            orderIndex[order.id] = {Place::Bid, order.price, std::prev(orders.end())};
        }
    }
    else {
        matchSellOrder(order);

        if (order.quantity > 0 && order.type == OrderType::Limit &&
            order.timeInForce == TimeInForce::GTC) {
            auto& orders = asks[order.price];
            orders.push_back(order);
            orderIndex[order.id] = {Place::Ask, order.price, std::prev(orders.end())};
        }
    }

    // Market i IOC nie czekaja w ksiazce. Niewykonana reszta jest anulowana.
    // Nawet jesli czesc wykonano, koncowy status to CANCELLED.
    if (order.quantity > 0 &&
        (order.type == OrderType::Market || order.timeInForce != TimeInForce::GTC)) {
        statuses[order.id] = OrderStatus::Cancelled;
    }
    // Najpierw konczymy przychodzace zlecenie, potem wykonujemy aktywowane stopy.
    // Dzieki temu stop nie zabierze plynnosci w srodku wykonania FOK.
    processStopOrders();
}

bool OrderBook::cancelOrder(int id) {
    auto found = orderIndex.find(id);
    if (found == orderIndex.end()) return false;
    OrderLocation location = found->second;
    if (location.place == Place::Bid) {
        auto level = bids.find(location.price);
        level->second.erase(location.position);
        if (level->second.empty()) bids.erase(level);
    }
    else if (location.place == Place::Ask) {
        auto level = asks.find(location.price);
        level->second.erase(location.position);
        if (level->second.empty()) asks.erase(level);
    }
    else if (location.place == Place::WaitingStop) {
        stopOrders.erase(location.position);
    }
    else {
        triggeredStops.erase(location.position);
    }
    orderIndex.erase(found);
    statuses[id] = OrderStatus::Cancelled;
    return true;
}

bool OrderBook::modifyOrder(int id, double newPrice, int newQuantity) {
    if (newQuantity <= 0) throw std::invalid_argument("Quantity must be positive");
    if (!std::isfinite(newPrice) || newPrice <= 0.0) {
        throw std::invalid_argument("Price must be finite and positive");
    }
    auto found = orderIndex.find(id);
    if (found == orderIndex.end()) return false;
    OrderLocation location = found->second;
    Order& current = *location.position;
    bool keepPriority = newPrice == current.price && newQuantity <= current.quantity;
    if (location.place == Place::WaitingStop) {
        current.price = newPrice;
        current.quantity = newQuantity;
        found->second.price = newPrice;
        if (!keepPriority) {
            // splice przenosi element na koniec bez zmiany jego iteratora.
            stopOrders.splice(stopOrders.end(), stopOrders, location.position);
        }
        if (!trades.empty()) activateStops(trades.back().price);
        processStopOrders();
    }
    else if (keepPriority) {
        current.quantity = newQuantity;
    }
    else {
        Order order = current;
        OrderStatus previousStatus = getOrderStatus(id);
        cancelOrder(id);
        order.price = newPrice;
        order.quantity = newQuantity;
        addOrder(order);
        // Modyfikacja nie cofa juz wykonanych transakcji.
        if (previousStatus == OrderStatus::PartiallyFilled && getOrderStatus(id) == OrderStatus::New) {
            statuses[id] = OrderStatus::PartiallyFilled;
        }
    }
    return true;
}
void OrderBook::matchBuyOrder(Order& order) {
    while (order.quantity > 0 && !asks.empty()) {

        auto bestAskIt = asks.begin();
        double bestAskPrice = bestAskIt->first;

        if (order.type == OrderType::Limit && order.price < bestAskPrice) {
            break;
        }

        auto& sellQueue = bestAskIt->second;
        Order& sellOrder = sellQueue.front();

        int tradedQuantity = std::min(
            order.quantity,
            sellOrder.quantity
        );
        // Zapis przed pop_front(): potem sellOrder moze juz nie istniec.
        recordTrade(order.id, sellOrder.id, bestAskPrice, tradedQuantity);

        order.quantity -= tradedQuantity;
        sellOrder.quantity -= tradedQuantity;

        // Oba zlecenia dostaja status zgodny z pozostala iloscia.
        if (order.quantity == 0) {
            statuses[order.id] = OrderStatus::Filled;
        }
        else {
            statuses[order.id] = OrderStatus::PartiallyFilled;
        }
        if (sellOrder.quantity == 0) {
            statuses[sellOrder.id] = OrderStatus::Filled;
        }
        else {
            statuses[sellOrder.id] = OrderStatus::PartiallyFilled;
        }

        if (sellOrder.quantity == 0) {
            orderIndex.erase(sellOrder.id);
            sellQueue.pop_front();
        }

        if (sellQueue.empty()) {
            asks.erase(bestAskIt);
        }
    }
}

void OrderBook::matchSellOrder(Order& order) {
    while (order.quantity > 0 && !bids.empty()) {

        auto bestBidIt = bids.begin();
        double bestBidPrice = bestBidIt->first;

        if (order.type == OrderType::Limit && order.price > bestBidPrice) {
            break;
        }

        auto& buyQueue = bestBidIt->second;
        Order& buyOrder = buyQueue.front();

        int tradedQuantity = std::min(
            order.quantity,
            buyOrder.quantity
        );
        recordTrade(buyOrder.id, order.id, bestBidPrice, tradedQuantity);

        order.quantity -= tradedQuantity;
        buyOrder.quantity -= tradedQuantity;

        if (order.quantity == 0) {
            statuses[order.id] = OrderStatus::Filled;
        }
        else {
            statuses[order.id] = OrderStatus::PartiallyFilled;
        }
        if (buyOrder.quantity == 0) {
            statuses[buyOrder.id] = OrderStatus::Filled;
        }
        else {
            statuses[buyOrder.id] = OrderStatus::PartiallyFilled;
        }

        if (buyOrder.quantity == 0) {
            orderIndex.erase(buyOrder.id);
            buyQueue.pop_front();
        }

        if (buyQueue.empty()) {
            bids.erase(bestBidIt);
        }
    }
}

bool OrderBook::getSlippage(int id, Side side, double referencePrice, double& slippage) const {
    if (side != Side::Buy && side != Side::Sell) {
        throw std::invalid_argument("Invalid order side");
    }
    if (!std::isfinite(referencePrice) || referencePrice <= 0.0) {
        return false;
    }

    double averagePrice = 0.0;
    if (!getAverageExecutionPrice(id, averagePrice)) {
        return false;
    }

    // Dodatni wynik oznacza gorsze wykonanie po obu stronach rynku.
    if (side == Side::Buy) {
        slippage = averagePrice - referencePrice;
    }
    else {
        slippage = referencePrice - averagePrice;
    }
    return true;
}

bool OrderBook::getAverageExecutionPrice(int id, double& averagePrice) const {
    double totalValue = 0.0;
    std::int64_t totalQuantity = 0;

    // Uwzgledniamy tylko transakcje, w ktorych uczestniczylo to zlecenie.
    // Ponowne uzycie ID polaczy wykonania roznych zlecen o tym numerze.
    for (const Trade& trade : trades) {
        if (trade.buyOrderId == id || trade.sellOrderId == id) {
            totalValue += trade.price * trade.quantity;
            totalQuantity += trade.quantity;
        }
    }

    if (totalQuantity == 0) {
        return false; // Nie ma sredniej bez wykonan: nie dzielimy przez zero.
    }

    // Wieksze wykonania maja wieksza wage w sredniej.
    averagePrice = totalValue / totalQuantity;
    return true;
}

std::int64_t OrderBook::getVolume(Side side, double price) const {
    if (side != Side::Buy && side != Side::Sell) throw std::invalid_argument("Invalid side");
    if (!std::isfinite(price) || price <= 0.0) throw std::invalid_argument("Invalid price");
    std::int64_t total = 0;
    if (side == Side::Buy) {
        auto level = bids.find(price);
        if (level == bids.end()) return 0;
        for (const Order& order : level->second) total += order.quantity;
    }
    else {
        auto level = asks.find(price);
        if (level == asks.end()) return 0;
        for (const Order& order : level->second) total += order.quantity;
    }
    return total;
}
bool OrderBook::getBestBid(double& price) const {
    if (bids.empty()) {
        return false;
    }

    // Bids sa posortowane malejaco, wiec pierwsza cena jest najwyzsza.
    price = bids.begin()->first;
    return true;
}

bool OrderBook::getBestAsk(double& price) const {
    if (asks.empty()) {
        return false;
    }

    // Asks sa posortowane rosnaco, wiec pierwsza cena jest najnizsza.
    price = asks.begin()->first;
    return true;
}

bool OrderBook::getSpread(double& spread) const {
    double bestBid = 0.0;
    double bestAsk = 0.0;

    if (!getBestBid(bestBid) || !getBestAsk(bestAsk)) {
        return false;
    }

    spread = bestAsk - bestBid;
    return true;
}

void OrderBook::printMarketSummary() const {
    double price = 0.0;
    double spread = 0.0;

    std::cout << "\n--- MARKET ---\n";
    if (getBestBid(price)) {
        std::cout << "Best bid: " << price << '\n';
    }
    else {
        std::cout << "Best bid: brak\n";
    }

    if (getBestAsk(price)) {
        std::cout << "Best ask: " << price << '\n';
    }
    else {
        std::cout << "Best ask: brak\n";
    }

    if (getSpread(spread)) {
        std::cout << "Spread: " << spread << '\n';
    }
    else {
        std::cout << "Spread: brak (potrzebne obie strony rynku)\n";
    }
}

void OrderBook::printOrderBook() const {
    std::cout << "\n--- ORDER BOOK ---\n";

    std::cout << "\nASKS:\n";
    for (const auto& [price, orders] : asks) {
        std::int64_t totalQuantity = 0;
        for (const Order& order : orders) totalQuantity += order.quantity;

        std::cout << price << " | " << totalQuantity << "\n";
    }

    std::cout << "\nBIDS:\n";
    for (const auto& [price, orders] : bids) {
        std::int64_t totalQuantity = 0;
        for (const Order& order : orders) totalQuantity += order.quantity;

        std::cout << price << " | " << totalQuantity << "\n";
    }
}

void OrderBook::printTrades() const
{
    std::cout << "\n--- TRADES ---\n";

    for (const auto& trade : trades) {
        std::cout
            << "TRADE #" << trade.id
            << " | BUY #" << trade.buyOrderId
            << " <-> SELL #" << trade.sellOrderId
            << " | QTY: " << trade.quantity
            << " | PRICE: " << trade.price
            << " | UNIX_MS: " << trade.timestampMs
            << "\n";
    }
}

const std::vector<Trade>& OrderBook::getTrades() const {
    return trades;
}

void OrderBook::printStopOrders() const {
    std::cout << "\n--- WAITING STOP MARKET ---\n";
    for (const Order& order : stopOrders) {
        std::cout << "Order #" << order.id;
        if (order.side == Side::Buy) {
            std::cout << " | BUY";
        }
        else {
            std::cout << " | SELL";
        }
        std::cout << " | TRIGGER: " << order.price << " | QTY: " << order.quantity << '\n';
    }
}

OrderStatus OrderBook::getOrderStatus(int id) const {
    auto it = statuses.find(id);
    if (it == statuses.end()) {
        return OrderStatus::Unknown;
    }
    return it->second;
}

void OrderBook::printOrderStatus(int id) const {
    std::cout << "Order #" << id << ": ";
    switch (getOrderStatus(id)) {
    case OrderStatus::New:
        std::cout << "NEW";
        break;
    case OrderStatus::PartiallyFilled:
        std::cout << "PARTIALLY_FILLED";
        break;
    case OrderStatus::Filled:
        std::cout << "FILLED";
        break;
    case OrderStatus::Cancelled:
        std::cout << "CANCELLED";
        break;
    case OrderStatus::Unknown:
        std::cout << "UNKNOWN";
        break;
    }
    std::cout << '\n';
}
