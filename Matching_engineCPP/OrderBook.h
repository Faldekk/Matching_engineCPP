#pragma once
#include <map>
#include <list>
#include <unordered_map>
#include <vector>
#include <functional>
#include <cstdint>
#include "Order.h"
#include "Trade.h"

class OrderBook {
private:
    enum class Place { Bid, Ask, WaitingStop, TriggeredStop };
    struct OrderLocation {
        Place place;
        double price;
        std::list<Order>::iterator position;
    };
    // list zachowuje FIFO, ale umozliwia tez usuniecie wskazanego elementu.
    std::map<double, std::list<Order>, std::greater<double>> bids;
    std::map<double, std::list<Order>> asks;
    // Indeks ID przechowuje miejsce zlecenia, zamiast go za kazdym razem szukac.
    std::unordered_map<int, OrderLocation> orderIndex;
    std::vector<Trade> trades;
    std::uint64_t nextTradeId = 1;
    std::map<int, OrderStatus> statuses;
    std::list<Order> stopOrders;
    std::list<Order> triggeredStops;
    bool processingStops = false;

    void matchBuyOrder(Order& order);
    void matchSellOrder(Order& order);
    bool hasOrder(int id) const;
    void recordTrade(int buyId, int sellId, double price, int quantity);
    bool canFullyFill(const Order& order) const;
    void activateStops(double tradePrice);
    void processStopOrders();

public:
    OrderBook() = default;
    // Kopia indeksu wskazywalaby na liste starej ksiazki, wiec nie kopiujemy jej.
    OrderBook(const OrderBook&) = delete;
    OrderBook& operator=(const OrderBook&) = delete;
    OrderBook(OrderBook&&) = delete;
    OrderBook& operator=(OrderBook&&) = delete;
    const std::vector<Trade>& getTrades() const;
    bool getOrder(int id, Order& order) const;
    void addOrder(Order order);
    bool cancelOrder(int id);
    bool modifyOrder(int id, double newPrice, int newQuantity);
    OrderStatus getOrderStatus(int id) const;
    void printOrderStatus(int id) const;
    // false = brak wyniku; parametr pozostaje bez zmian.
    bool getBestBid(double& price) const;
    bool getBestAsk(double& price) const;
    bool getSpread(double& spread) const;
    std::int64_t getVolume(Side side, double price) const;
    bool getAverageExecutionPrice(int id, double& averagePrice) const;
    // Cene odniesienia pobierz przed zleceniem: ask dla BUY, bid dla SELL.
    bool getSlippage(int id, Side side, double referencePrice, double& slippage) const;
    void printMarketSummary() const;
    void printOrderBook() const;
    void printTrades() const;
    void printStopOrders() const;
};
