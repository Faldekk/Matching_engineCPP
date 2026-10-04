#include "OrderBook.h"
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <chrono>
#include <cmath>

void check(bool condition) {
    if (!condition) throw std::runtime_error("Regression check failed");
}

void testTimeInForce() {
    for (Side side : {Side::Buy, Side::Sell}) {
        const Side opposite = side == Side::Buy ? Side::Sell : Side::Buy;
        const double nextPrice = side == Side::Buy ? 101.0 : 99.0;
        for (OrderType type : {OrderType::Limit, OrderType::Market}) {
            OrderBook book;
            book.addOrder({1, opposite, OrderType::Limit, 100.0, 2});
            book.addOrder({2, opposite, OrderType::Limit, nextPrice, 3});
            // Za duza ilosc: FOK nie moze wykonac nawet pierwszych 2 sztuk.
            book.addOrder({3, side, type, nextPrice, 6, TimeInForce::FOK});
            check(book.getTrades().empty());
            check(book.getOrderStatus(3) == OrderStatus::Cancelled);
            check(book.getOrderStatus(1) == OrderStatus::New);
            check(book.getVolume(opposite, 100.0) == 2);
            check(book.getVolume(opposite, nextPrice) == 3);

            book.addOrder({4, side, type, nextPrice, 5, TimeInForce::FOK});
            check(book.getOrderStatus(4) == OrderStatus::Filled);
            check(book.getTrades().size() == 2);
            check(book.getTrades()[0].quantity == 2);
            check(book.getTrades()[1].quantity == 3);
            check(book.getTrades()[0].id == 1);

            OrderBook ioc;
            ioc.addOrder({1, opposite, OrderType::Limit, 100.0, 2});
            ioc.addOrder({2, side, type, 100.0, 5, TimeInForce::IOC});
            check(ioc.getTrades().size() == 1);
            check(ioc.getTrades()[0].quantity == 2);
            check(ioc.getOrderStatus(2) == OrderStatus::Cancelled);
            check(ioc.getVolume(side, 100.0) == 0);
            ioc.addOrder({3, opposite, OrderType::Limit, 100.0, 2});
            ioc.addOrder({4, side, type, 100.0, 2, TimeInForce::IOC});
            check(ioc.getOrderStatus(4) == OrderStatus::Filled);
        }

        OrderBook limited;
        limited.addOrder({1, opposite, OrderType::Limit, 100.0, 2});
        limited.addOrder({2, opposite, OrderType::Limit, nextPrice, 10});
        // Ilosci jest duzo, ale tylko 2 sztuki mieszcza sie w limicie ceny.
        limited.addOrder({3, side, OrderType::Limit, 100.0, 3, TimeInForce::FOK});
        check(limited.getTrades().empty());
        limited.addOrder({4, side, OrderType::Limit, 100.0, 3, TimeInForce::IOC});
        check(limited.getTrades().size() == 1);
        check(limited.getTrades()[0].quantity == 2);
        check(limited.getVolume(opposite, nextPrice) == 10);
        check(limited.getOrderStatus(4) == OrderStatus::Cancelled);

        OrderBook empty;
        empty.addOrder({1, side, OrderType::Limit, 100.0, 1, TimeInForce::IOC});
        empty.addOrder({2, side, OrderType::Market, 0.0, 1, TimeInForce::FOK});
        check(empty.getOrderStatus(1) == OrderStatus::Cancelled);
        check(empty.getOrderStatus(2) == OrderStatus::Cancelled);
        check(empty.getTrades().empty());
        check(empty.getVolume(side, 100.0) == 0);
    }
}

void testStopOrders() {
    for (Side side : {Side::Buy, Side::Sell}) {
        const Side opposite = side == Side::Buy ? Side::Sell : Side::Buy;
        const double nextPrice = side == Side::Buy ? 101.0 : 99.0;
        OrderBook book;
        book.addOrder({10, side, OrderType::StopMarket, 100.0, 3});
        book.addOrder({11, side, OrderType::StopMarket, 100.0, 3});
        check(book.getOrderStatus(10) == OrderStatus::New);
        check(book.getVolume(side, 100.0) == 0);
        double bestPrice = 0.0;
        if (side == Side::Buy) check(!book.getBestBid(bestPrice));
        else check(!book.getBestAsk(bestPrice));

        // Samo dodanie oferty nie aktywuje stopa: potrzebna jest transakcja.
        book.addOrder({1, opposite, OrderType::Limit, 100.0, 2});
        book.addOrder({2, opposite, OrderType::Limit, nextPrice, 5});
        check(book.getTrades().empty());
        book.addOrder({3, side, OrderType::Market, 0.0, 2});
        check(book.getTrades().size() == 3);
        const Trade& firstStop = book.getTrades()[1];
        check((side == Side::Buy ? firstStop.buyOrderId : firstStop.sellOrderId) == 10);
        check(firstStop.quantity == 3 && firstStop.price == nextPrice);
        check(book.getOrderStatus(10) == OrderStatus::Filled);
        check(book.getOrderStatus(11) == OrderStatus::Cancelled); // Wykonano 2 z 3.
        check(!book.cancelOrder(10));

        // Stop dodany po przekroczeniu progu aktywuje sie od razu.
        book.addOrder({12, side, OrderType::StopMarket, 100.0, 1});
        check(book.getOrderStatus(12) == OrderStatus::Cancelled); // Brak plynnosci.

        OrderBook waiting;
        const double farTrigger = side == Side::Buy ? 200.0 : 50.0;
        waiting.addOrder({1, side, OrderType::StopMarket, farTrigger, 2});
        bool duplicateRejected = false;
        try { waiting.addOrder({1, opposite, OrderType::Limit, 100.0, 1}); }
        catch (const std::invalid_argument&) { duplicateRejected = true; }
        check(duplicateRejected);
        check(waiting.modifyOrder(1, farTrigger, 1));
        waiting.addOrder({2, opposite, OrderType::Limit, 100.0, 1});
        waiting.addOrder({3, side, OrderType::Market, 0.0, 1});
        check(waiting.getOrderStatus(1) == OrderStatus::New);
        check(waiting.cancelOrder(1));
        check(waiting.getOrderStatus(1) == OrderStatus::Cancelled);
        check(!waiting.modifyOrder(1, 100.0, 1));

        OrderBook modified;
        modified.addOrder({1, opposite, OrderType::Limit, 100.0, 5});
        modified.addOrder({2, side, OrderType::Market, 0.0, 1});
        modified.addOrder({3, side, OrderType::StopMarket, farTrigger, 2});
        check(modified.modifyOrder(3, 100.0, 3));
        check(modified.getOrderStatus(3) == OrderStatus::Filled);
        check(modified.getTrades().back().quantity == 3);

        // Zwiekszenie ilosci stopa odbiera mu pierwszenstwo aktywacji.
        OrderBook priority;
        priority.addOrder({1, side, OrderType::StopMarket, 100.0, 1});
        priority.addOrder({2, side, OrderType::StopMarket, 100.0, 1});
        check(priority.modifyOrder(1, 100.0, 2));
        priority.addOrder({3, opposite, OrderType::Limit, 100.0, 1});
        priority.addOrder({4, opposite, OrderType::Limit, nextPrice, 3});
        priority.addOrder({5, side, OrderType::Market, 0.0, 1});
        check(priority.getTrades().size() == 3);
        const Trade& earlierStop = priority.getTrades()[1];
        check((side == Side::Buy ? earlierStop.buyOrderId : earlierStop.sellOrderId) == 2);

        OrderBook cancelled;
        cancelled.addOrder({1, side, OrderType::StopMarket, 100.0, 1});
        check(cancelled.cancelOrder(1));
        cancelled.addOrder({2, opposite, OrderType::Limit, 100.0, 5});
        cancelled.addOrder({3, side, OrderType::Market, 0.0, 1});
        check(cancelled.getTrades().size() == 1);
        check(cancelled.getOrderStatus(1) == OrderStatus::Cancelled);
    }

    OrderBook invalid;
    for (Order order : {
        Order{1, Side::Buy, OrderType::Limit, 100.0, 1, static_cast<TimeInForce>(9)},
        Order{1, Side::Buy, OrderType::StopMarket, 100.0, 1, TimeInForce::IOC},
        Order{1, Side::Buy, OrderType::StopMarket, 100.0, 1, TimeInForce::FOK},
        Order{1, Side::Buy, OrderType::StopMarket, 0.0, 1},
        Order{1, Side::Buy, OrderType::StopMarket, std::numeric_limits<double>::quiet_NaN(), 1}}) {
        bool rejected = false;
        try { invalid.addOrder(order); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected);
        check(invalid.getOrderStatus(1) == OrderStatus::Unknown);
        check(invalid.getTrades().empty());
    }

    // Prog przekroczony przy pierwszej transakcji pozostaje aktywowany,
    // nawet gdy ostatnia transakcja przychodzacego zlecenia ma inna cene.
    OrderBook latched;
    latched.addOrder({1, Side::Sell, OrderType::StopMarket, 101.0, 1});
    latched.addOrder({2, Side::Buy, OrderType::Limit, 99.0, 1});
    latched.addOrder({3, Side::Sell, OrderType::Limit, 101.0, 1});
    latched.addOrder({4, Side::Sell, OrderType::Limit, 103.0, 1});
    latched.addOrder({5, Side::Buy, OrderType::Limit, 103.0, 2, TimeInForce::FOK});
    check(latched.getOrderStatus(5) == OrderStatus::Filled);
    check(latched.getOrderStatus(1) == OrderStatus::Filled);
    check(latched.getTrades().size() == 3);
    check(latched.getTrades()[0].price == 101.0);
    check(latched.getTrades()[1].price == 103.0);
    check(latched.getTrades()[2].price == 99.0);

    // Jeden stop moze wykonaniem uruchomic kolejny stop.
    OrderBook chain;
    chain.addOrder({1, Side::Buy, OrderType::StopMarket, 100.0, 1});
    chain.addOrder({2, Side::Buy, OrderType::StopMarket, 101.0, 1});
    chain.addOrder({3, Side::Sell, OrderType::Limit, 100.0, 1});
    chain.addOrder({4, Side::Sell, OrderType::Limit, 101.0, 1});
    chain.addOrder({5, Side::Sell, OrderType::Limit, 102.0, 1});
    chain.addOrder({6, Side::Buy, OrderType::Market, 0.0, 1});
    check(chain.getOrderStatus(1) == OrderStatus::Filled);
    check(chain.getOrderStatus(2) == OrderStatus::Filled);
    check(chain.getTrades().size() == 3);
    check(chain.getTrades()[2].buyOrderId == 2);
}

void testSlippage() {
    for (Side side : {Side::Buy, Side::Sell}) {
        const Side opposite = side == Side::Buy ? Side::Sell : Side::Buy;
        const double firstPrice = side == Side::Buy ? 101.0 : 103.0;
        const double lastPrice = side == Side::Buy ? 103.0 : 101.0;
        OrderBook book;
        double reference = 0.0;
        double slippage = -999.0;
        check(!book.getSlippage(999, side, 101.0, slippage));
        check(slippage == -999.0);
        book.addOrder({1, opposite, OrderType::Limit, firstPrice, 5});
        book.addOrder({2, opposite, OrderType::Limit, 102.0, 10});
        book.addOrder({3, opposite, OrderType::Limit, lastPrice, 20});

        bool hasReference = false;
        if (side == Side::Buy) {
            hasReference = book.getBestAsk(reference);
        }
        else {
            hasReference = book.getBestBid(reference);
        }
        check(hasReference && reference == firstPrice);
        book.addOrder({4, side, OrderType::Limit, lastPrice, 25});
        check(book.getSlippage(4, side, reference, slippage));
        check(std::abs(slippage - 1.2) < 0.000001);

        // Odczyt pozniej nadal korzysta z zapamietanej ceny sprzed wykonania.
        check(book.cancelOrder(3));
        check(book.getSlippage(4, side, reference, slippage));
        check(std::abs(slippage - 1.2) < 0.000001);
        check(book.getTrades().size() == 3);

        OrderBook singleLevel;
        singleLevel.addOrder({1, opposite, OrderType::Limit, 100.0, 3});
        singleLevel.addOrder({2, side, OrderType::Market, 0.0, 5});
        // Market moze miec CANCELLED, ale jego wykonana czesc ma slippage.
        check(singleLevel.getOrderStatus(2) == OrderStatus::Cancelled);
        check(singleLevel.getSlippage(2, side, 100.0, slippage) && slippage == 0.0);
        // Ujemny wynik oznacza lepsza cene wzgledem podanego odniesienia.
        const double betterReference = side == Side::Buy ? 101.0 : 99.0;
        check(singleLevel.getSlippage(2, side, betterReference, slippage) && slippage == -1.0);

        for (double invalidReference : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::quiet_NaN()}) {
            slippage = -999.0;
            check(!singleLevel.getSlippage(2, side, invalidReference, slippage));
            check(slippage == -999.0);
        }

        OrderBook empty;
        reference = 0.0;
        if (side == Side::Buy) {
            check(!empty.getBestAsk(reference));
        }
        else {
            check(!empty.getBestBid(reference));
        }
        empty.addOrder({1, side, OrderType::Market, 0.0, 5});
        check(!empty.getSlippage(1, side, reference, slippage));
    }
}

void testAverageExecutionPrice() {
    for (Side side : {Side::Buy, Side::Sell}) {
        const Side opposite = side == Side::Buy ? Side::Sell : Side::Buy;
        const double firstPrice = side == Side::Buy ? 101.0 : 103.0;
        const double lastPrice = side == Side::Buy ? 103.0 : 101.0;
        OrderBook book;
        double average = -1.0;
        check(!book.getAverageExecutionPrice(999, average));
        check(average == -1.0);
        book.addOrder({1, opposite, OrderType::Limit, firstPrice, 5});
        book.addOrder({2, opposite, OrderType::Limit, 102.0, 10});
        book.addOrder({3, opposite, OrderType::Limit, lastPrice, 20});
        check(!book.getAverageExecutionPrice(1, average));
        check(average == -1.0);
        book.addOrder({4, side, OrderType::Limit, lastPrice, 25});
        const double expected = side == Side::Buy ? 102.2 : 101.8;
        check(book.getAverageExecutionPrice(4, average));
        check(std::abs(average - expected) < 0.000001);
        check(book.getAverageExecutionPrice(1, average) && average == firstPrice);
        check(book.getAverageExecutionPrice(3, average) && average == lastPrice);
        check(book.cancelOrder(3));
        check(book.getAverageExecutionPrice(3, average) && average == lastPrice);
        check(book.getTrades().size() == 3);
        check(book.getTrades()[0].quantity == 5);
        check(book.getTrades()[1].quantity == 10);
        check(book.getTrades()[2].quantity == 10);

        OrderBook partial;
        partial.addOrder({1, opposite, OrderType::Limit, 100.0, 3});
        partial.addOrder({2, side, OrderType::Limit, 100.0, 10});
        check(partial.getAverageExecutionPrice(2, average) && average == 100.0);
        check(partial.modifyOrder(2, 100.0, 5));
        partial.addOrder({3, opposite, OrderType::Market, 0.0, 2});
        check(partial.getAverageExecutionPrice(2, average) && average == 100.0);
        check(partial.cancelOrder(2));
        check(partial.getAverageExecutionPrice(2, average) && average == 100.0);

        OrderBook market;
        market.addOrder({1, side, OrderType::Market, 0.0, 5});
        average = -1.0;
        check(!market.getAverageExecutionPrice(1, average));
        check(average == -1.0);
        market.addOrder({2, opposite, OrderType::Limit, 100.0, 3});
        market.addOrder({3, side, OrderType::Market, 0.0, 5});
        check(market.getOrderStatus(3) == OrderStatus::Cancelled);
        check(market.getAverageExecutionPrice(3, average) && average == 100.0);

        // Ponowne uzycie ID laczy jego wszystkie wykonania z historii.
        OrderBook reused;
        reused.addOrder({1, opposite, OrderType::Limit, 100.0, 2});
        reused.addOrder({2, side, OrderType::Market, 0.0, 2});
        reused.addOrder({3, opposite, OrderType::Limit, 110.0, 6});
        reused.addOrder({2, side, OrderType::Market, 0.0, 6});
        check(reused.getAverageExecutionPrice(2, average) && average == 107.5);

        OrderBook large;
        large.addOrder({1, opposite, OrderType::Limit, 101.0, 2147483647});
        large.addOrder({2, side, OrderType::Market, 0.0, 2147483647});
        large.addOrder({3, opposite, OrderType::Limit, 101.0, 2147483647});
        large.addOrder({2, side, OrderType::Market, 0.0, 2147483647});
        check(large.getAverageExecutionPrice(2, average) && average == 101.0);
    }
}

void testBestPrices() {
    OrderBook book;
    double price = -1.0;
    double spread = -1.0;
    check(!book.getBestBid(price));
    check(!book.getBestAsk(price));
    check(!book.getSpread(spread));
    check(price == -1.0 && spread == -1.0);

    book.addOrder({1, Side::Buy, OrderType::Limit, 99.0, 5});
    check(book.getBestBid(price) && price == 99.0);
    check(!book.getBestAsk(price));
    check(!book.getSpread(spread));
    book.addOrder({2, Side::Buy, OrderType::Limit, 100.0, 5});
    book.addOrder({3, Side::Sell, OrderType::Limit, 102.0, 5});
    book.addOrder({4, Side::Sell, OrderType::Limit, 101.0, 5});
    check(book.getBestBid(price) && price == 100.0);
    check(book.getBestAsk(price) && price == 101.0);
    check(book.getSpread(spread) && spread == 1.0);

    // Czesciowe wykonanie nie zmienia najlepszej ceny.
    book.addOrder({5, Side::Buy, OrderType::Market, 0.0, 2});
    check(book.getBestAsk(price) && price == 101.0);
    check(book.getSpread(spread) && spread == 1.0);
    // Zuzycie calego poziomu odslania kolejny ask.
    book.addOrder({6, Side::Buy, OrderType::Market, 0.0, 3});
    check(book.getBestAsk(price) && price == 102.0);
    check(book.getSpread(spread) && spread == 2.0);
    check(book.cancelOrder(2));
    check(book.getBestBid(price) && price == 99.0);
    check(book.getSpread(spread) && spread == 3.0);

    check(book.modifyOrder(1, 100.0, 5));
    check(book.getBestBid(price) && price == 100.0);
    check(book.getSpread(spread) && spread == 2.0);
    // Zmiana ceny uruchamia matching i oproznia obie strony.
    check(book.modifyOrder(1, 102.0, 5));
    check(!book.getBestBid(price));
    check(!book.getBestAsk(price));
    check(!book.getSpread(spread));

    book.addOrder({7, Side::Sell, OrderType::Limit, 101.0, 5});
    check(book.getBestAsk(price) && price == 101.0);
    check(!book.getBestBid(price));
    check(!book.getSpread(spread));

    std::ostringstream output;
    auto* previous = std::cout.rdbuf(output.rdbuf());
    book.printMarketSummary();
    std::cout.rdbuf(previous);
    check(output.str().find("Best bid: brak") != std::string::npos);
    check(output.str().find("Best ask: 101") != std::string::npos);
    check(output.str().find("Spread: brak") != std::string::npos);
    check(book.getTrades().size() == 3);
    check(book.getOrderStatus(7) == OrderStatus::New);
}

void testVolumeAtPrice() {
    for (Side side : {Side::Buy, Side::Sell}) {
        const Side opposite = side == Side::Buy ? Side::Sell : Side::Buy;
        OrderBook book;
        check(book.getVolume(side, 101.0) == 0);
        book.addOrder({1, side, OrderType::Limit, 101.0, 5});
        book.addOrder({2, side, OrderType::Limit, 101.0, 10});
        book.addOrder({3, side, OrderType::Limit, 100.0, 7});
        check(book.getVolume(side, 101.0) == 15);
        check(book.getVolume(side, 100.0) == 7);
        check(book.getVolume(side, 999.0) == 0);
        check(book.getVolume(opposite, 101.0) == 0);

        // Usuwamy inny poziom, by Market trafil na 101 po obu stronach.
        check(book.cancelOrder(3));
        book.addOrder({4, opposite, OrderType::Market, 0.0, 4});
        check(book.getVolume(side, 101.0) == 11);
        check(book.cancelOrder(2));
        check(book.getVolume(side, 101.0) == 1);
        check(book.modifyOrder(1, 101.0, 3));
        check(book.getVolume(side, 101.0) == 3);
        check(book.modifyOrder(1, 101.0, 2));
        check(book.getVolume(side, 101.0) == 2);
        check(book.modifyOrder(1, 102.0, 2));
        check(book.getVolume(side, 101.0) == 0);
        check(book.getVolume(side, 102.0) == 2);
        book.addOrder({5, opposite, OrderType::Market, 0.0, 2});
        check(book.getVolume(side, 102.0) == 0);
        check(book.getTrades().size() == 2);
        check(book.getOrderStatus(1) == OrderStatus::Filled);

        // Odczyt nie moze zmienic kolejki ani priorytetu FIFO.
        OrderBook fifo;
        fifo.addOrder({1, side, OrderType::Limit, 101.0, 5});
        fifo.addOrder({2, side, OrderType::Limit, 101.0, 10});
        check(fifo.getVolume(side, 101.0) == 15);
        check(fifo.getVolume(side, 101.0) == 15);
        fifo.addOrder({3, opposite, OrderType::Market, 0.0, 15});
        check(fifo.getTrades().size() == 2);
        const Trade& first = fifo.getTrades()[0];
        check((side == Side::Buy ? first.buyOrderId : first.sellOrderId) == 1);
        check(first.quantity == 5);

        OrderBook large;
        large.addOrder({1, side, OrderType::Limit, 101.0, 2147483647});
        large.addOrder({2, side, OrderType::Limit, 101.0, 2147483647});
        check(large.getVolume(side, 101.0) == 4294967294LL);
    }

    OrderBook book;
    for (double invalidPrice : {0.0, -1.0, std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::quiet_NaN()}) {
        bool rejected = false;
        try { book.getVolume(Side::Buy, invalidPrice); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected);
    }
    bool rejected = false;
    try { book.getVolume(static_cast<Side>(9), 101.0); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected);
}

void testTradeMetadata() {
    for (Side side : {Side::Buy, Side::Sell}) {
        const Side opposite = side == Side::Buy ? Side::Sell : Side::Buy;
        OrderBook book;
        book.addOrder({1, side, OrderType::Limit, 100.0, 2});
        book.addOrder({2, side, OrderType::Limit, 100.0, 3});
        book.addOrder({3, side, OrderType::Limit, 100.0, 4});

        const auto before = std::chrono::system_clock::now();
        book.addOrder({4, opposite, OrderType::Market, 0.0, 9});
        const auto after = std::chrono::system_clock::now();
        const auto beforeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            before.time_since_epoch()).count();
        const auto afterMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            after.time_since_epoch()).count();

        check(book.getTrades().size() == 3);
        for (std::size_t i = 0; i < book.getTrades().size(); ++i) {
            const Trade& trade = book.getTrades()[i];
            check(trade.id == i + 1);
            check(trade.timestampMs >= beforeMs && trade.timestampMs <= afterMs);
        }
        const Trade firstTrade = book.getTrades()[0];

        // Dodanie i anulowanie bez wykonania nie zuzywa numeru transakcji.
        book.addOrder({5, side, OrderType::Limit, 100.0, 1});
        check(book.cancelOrder(5));
        book.addOrder({6, side, OrderType::Limit, 100.0, 2});
        const double passivePrice = side == Side::Buy ? 101.0 : 99.0;
        book.addOrder({7, opposite, OrderType::Limit, passivePrice, 2});
        check(book.getTrades().size() == 3);
        check(book.modifyOrder(7, 100.0, 2));
        check(book.getTrades().size() == 4);
        check(book.getTrades()[3].id == 4);
        check(book.getTrades()[0].id == firstTrade.id);
        check(book.getTrades()[0].timestampMs == firstTrade.timestampMs);

        // Odczyt historii nie zmienia metadanych.
        std::ostringstream output;
        auto* previous = std::cout.rdbuf(output.rdbuf());
        book.printTrades();
        std::cout.rdbuf(previous);
        check(output.str().find("TRADE #1 | BUY #") != std::string::npos);
        check(output.str().find(" | UNIX_MS: ") != std::string::npos);
        check(book.getTrades()[0].timestampMs == firstTrade.timestampMs);

        OrderBook separateBook;
        separateBook.addOrder({1, side, OrderType::Limit, 100.0, 1});
        separateBook.addOrder({2, opposite, OrderType::Market, 0.0, 1});
        check(separateBook.getTrades()[0].id == 1);
    }
}

std::string snapshot(const OrderBook& book) {
    std::ostringstream output;
    auto* previous = std::cout.rdbuf(output.rdbuf());
    book.printOrderBook();
    std::cout.rdbuf(previous);
    return output.str();
}

void testOrderStatuses() {
    for (Side side : {Side::Buy, Side::Sell}) {
        const Side opposite = side == Side::Buy ? Side::Sell : Side::Buy;
        OrderBook book;
        check(book.getOrderStatus(999) == OrderStatus::Unknown);

        book.addOrder({1, side, OrderType::Limit, 100.0, 10});
        check(book.getOrderStatus(1) == OrderStatus::New);
        book.addOrder({2, opposite, OrderType::Limit, 100.0, 4});
        check(book.getOrderStatus(1) == OrderStatus::PartiallyFilled);
        check(book.getOrderStatus(2) == OrderStatus::Filled);

        // Zmniejszenie ilosci i zmiana ceny nie kasuja historii wykonan.
        check(book.modifyOrder(1, 100.0, 5));
        check(book.getOrderStatus(1) == OrderStatus::PartiallyFilled);
        check(book.modifyOrder(1, 100.0, 8));
        check(book.getOrderStatus(1) == OrderStatus::PartiallyFilled);
        check(book.modifyOrder(1, 101.0, 8));
        check(book.getOrderStatus(1) == OrderStatus::PartiallyFilled);
        book.addOrder({3, opposite, OrderType::Market, 0.0, 8});
        check(book.getOrderStatus(1) == OrderStatus::Filled);
        check(book.getOrderStatus(3) == OrderStatus::Filled);
        check(!book.cancelOrder(1));
        check(!book.modifyOrder(1, 100.0, 1));
        check(book.getOrderStatus(1) == OrderStatus::Filled);

        // Przychodzacy Limit tez moze zostac czesciowo wykonany.
        OrderBook incoming;
        incoming.addOrder({1, opposite, OrderType::Limit, 100.0, 3});
        incoming.addOrder({2, side, OrderType::Limit, 100.0, 5});
        check(incoming.getOrderStatus(1) == OrderStatus::Filled);
        check(incoming.getOrderStatus(2) == OrderStatus::PartiallyFilled);
        check(incoming.cancelOrder(2));
        check(incoming.getOrderStatus(2) == OrderStatus::Cancelled);
        check(!incoming.cancelOrder(2));
        check(!incoming.modifyOrder(2, 100.0, 1));
        check(incoming.getOrderStatus(2) == OrderStatus::Cancelled);
        check(incoming.getTrades().size() == 1);
        check(!incoming.cancelOrder(999));
        check(incoming.getOrderStatus(999) == OrderStatus::Unknown);

        OrderBook market;
        market.addOrder({1, side, OrderType::Market, 0.0, 5});
        check(market.getOrderStatus(1) == OrderStatus::Cancelled);
        market.addOrder({2, opposite, OrderType::Limit, 100.0, 3});
        market.addOrder({3, side, OrderType::Market, 0.0, 5});
        check(market.getOrderStatus(2) == OrderStatus::Filled);
        check(market.getOrderStatus(3) == OrderStatus::Cancelled);
        check(market.getTrades().size() == 1);
        check(market.getTrades()[0].quantity == 3);

        OrderBook modified;
        const double passivePrice = side == Side::Buy ? 99.0 : 102.0;
        modified.addOrder({1, side, OrderType::Limit, passivePrice, 5});
        modified.addOrder({2, opposite, OrderType::Limit, 101.0, 8});
        check(modified.modifyOrder(1, 101.0, 5));
        check(modified.getOrderStatus(1) == OrderStatus::Filled);
        check(modified.getOrderStatus(2) == OrderStatus::PartiallyFilled);
        check(modified.cancelOrder(2));
        check(modified.getOrderStatus(2) == OrderStatus::Cancelled);
    }

    OrderBook validation;
    bool rejected = false;
    try { validation.addOrder({1, Side::Buy, OrderType::Limit, 100.0, 0}); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected);
    check(validation.getOrderStatus(1) == OrderStatus::Unknown);
    validation.addOrder({1, Side::Buy, OrderType::Limit, 100.0, 5});
    rejected = false;
    try { validation.addOrder({1, Side::Sell, OrderType::Market, 0.0, 5}); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected);
    check(validation.getOrderStatus(1) == OrderStatus::New);
    rejected = false;
    try { validation.modifyOrder(1, 0.0, 5); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected);
    check(validation.getOrderStatus(1) == OrderStatus::New);
    check(validation.cancelOrder(1));
    check(validation.getOrderStatus(1) == OrderStatus::Cancelled);

    // Tak jak dotychczas, ponowne uzycie nieaktywnego ID jest dozwolone.
    // Status dotyczy wtedy nowego zlecenia o tym ID.
    validation.addOrder({1, Side::Buy, OrderType::Limit, 100.0, 5});
    check(validation.getOrderStatus(1) == OrderStatus::New);
}

int main() {
    testTimeInForce();
    testStopOrders();
    testSlippage();
    testAverageExecutionPrice();
    testVolumeAtPrice();
    testBestPrices();
    testTradeMetadata();
    testOrderStatuses();
    for (Side side : {Side::Buy, Side::Sell}) {
        const Side opposite = side == Side::Buy ? Side::Sell : Side::Buy;
        for (int newQuantity : {3, 5, 7}) {
            OrderBook book;
            book.addOrder({1, side, OrderType::Limit, 100.0, 5});
            book.addOrder({2, side, OrderType::Limit, 100.0, 5});
            check(book.modifyOrder(1, 100.0, newQuantity));
            book.addOrder({3, opposite, OrderType::Market, 0.0, 20});
            check(book.getTrades().size() == 2);
            const auto& first = book.getTrades()[0];
            const auto& second = book.getTrades()[1];
            const int expectedFirst = newQuantity > 5 ? 2 : 1;
            check((side == Side::Buy ? first.buyOrderId : first.sellOrderId) == expectedFirst);
            check((side == Side::Buy ? second.buyOrderId : second.sellOrderId) == 3 - expectedFirst);
            check(first.quantity + second.quantity == newQuantity + 5);
            check(snapshot(book).find(" | ") == std::string::npos);
            check(!book.modifyOrder(1, 100.0, 1));
        }

        // Po zmianie ceny zlecenie dolacza za zleceniem juz na tym poziomie.
        OrderBook moved;
        moved.addOrder({1, side, OrderType::Limit, 100.0, 5});
        moved.addOrder({2, side, OrderType::Limit, 101.0, 5});
        check(moved.modifyOrder(1, 101.0, 3));
        moved.addOrder({3, opposite, OrderType::Market, 0.0, 8});
        check(moved.getTrades().size() == 2);
        check((side == Side::Buy ? moved.getTrades()[0].buyOrderId : moved.getTrades()[0].sellOrderId) == 2);
        check(moved.getTrades()[1].quantity == 3);

        // Zmiana ceny moze od razu wykonac transakcje.
        OrderBook crossing;
        const double passivePrice = side == Side::Buy ? 99.0 : 102.0;
        crossing.addOrder({1, side, OrderType::Limit, passivePrice, 8});
        crossing.addOrder({2, opposite, OrderType::Limit, 101.0, 5});
        check(crossing.modifyOrder(1, 101.0, 8));
        check(crossing.getTrades().size() == 1);
        check(crossing.getTrades()[0].price == 101.0);
        check(crossing.getTrades()[0].quantity == 5);
        check(snapshot(crossing).find("101 | 3") != std::string::npos);
        check(crossing.modifyOrder(1, 101.0, 2));
        check(snapshot(crossing).find("101 | 2") != std::string::npos);

        const auto before = snapshot(crossing);
        check(!crossing.modifyOrder(999, 100.0, 1));
        check(snapshot(crossing) == before);
        for (double price : {0.0, -1.0, std::numeric_limits<double>::infinity(),
                std::numeric_limits<double>::quiet_NaN()}) {
            bool rejected = false;
            try { crossing.modifyOrder(1, price, 1); }
            catch (const std::invalid_argument&) { rejected = true; }
            check(rejected);
            check(snapshot(crossing) == before);
        }
        for (int quantity : {0, -1}) {
            bool rejected = false;
            try { crossing.modifyOrder(1, 101.0, quantity); }
            catch (const std::invalid_argument&) { rejected = true; }
            check(rejected);
            check(snapshot(crossing) == before);
        }
        check(crossing.getTrades().size() == 1);
    }

    for (Side side : {Side::Buy, Side::Sell}) {
        for (int cancelledId : {1, 2, 3}) {
            OrderBook book;
            const Side opposite = side == Side::Buy ? Side::Sell : Side::Buy;
            for (int id : {1, 2, 3}) {
                book.addOrder({id, side, OrderType::Limit, 100.0, 5});
            }
            const auto beforeCancel = snapshot(book);
            check(!book.cancelOrder(999));
            check(snapshot(book) == beforeCancel);
            check(book.cancelOrder(cancelledId));
            check(!book.cancelOrder(cancelledId));
            check(snapshot(book).find("100 | 10") != std::string::npos);
            book.addOrder({4, opposite, OrderType::Market, 0.0, 10});
            check(book.getTrades().size() == 2);
            std::size_t index = 0;
            for (int id : {1, 2, 3}) {
                if (id == cancelledId) continue;
                const auto& trade = book.getTrades()[index++];
                check((side == Side::Buy ? trade.buyOrderId : trade.sellOrderId) == id);
            }
            check(!book.cancelOrder(4));
            check(snapshot(book).find(" | ") == std::string::npos);
        }

        OrderBook partial;
        const Side opposite = side == Side::Buy ? Side::Sell : Side::Buy;
        partial.addOrder({1, side, OrderType::Limit, 100.0, 10});
        partial.addOrder({2, opposite, OrderType::Market, 0.0, 4});
        check(partial.cancelOrder(1));
        check(partial.getTrades().size() == 1);
        check(partial.getTrades()[0].quantity == 4);
        check(snapshot(partial).find(" | ") == std::string::npos);
    }

    OrderBook duplicate;
    duplicate.addOrder({1, Side::Buy, OrderType::Limit, 100.0, 10});
    const auto duplicateBefore = snapshot(duplicate);
    bool duplicateRejected = false;
    try { duplicate.addOrder({1, Side::Sell, OrderType::Market, 0.0, 5}); }
    catch (const std::invalid_argument&) { duplicateRejected = true; }
    check(duplicateRejected);
    check(snapshot(duplicate) == duplicateBefore);
    check(duplicate.getTrades().empty());
    check(duplicate.cancelOrder(1));

    for (Side restingSide : {Side::Buy, Side::Sell}) {
        OrderBook book;
        const Side incomingSide = restingSide == Side::Buy ? Side::Sell : Side::Buy;
        const double nextPrice = restingSide == Side::Buy ? 99.0 : 101.0;
        book.addOrder({1, restingSide, OrderType::Limit, 100.0, 2});
        book.addOrder({2, restingSide, OrderType::Limit, 100.0, 3});
        book.addOrder({3, restingSide, OrderType::Limit, nextPrice, 4});
        book.addOrder({4, incomingSide, OrderType::Limit, 100.0, 4});
        const auto& trades = book.getTrades();
        check(trades.size() == 2);
        check(trades[0].quantity == 2 && trades[1].quantity == 2);
        check((restingSide == Side::Buy ? trades[0].buyOrderId : trades[0].sellOrderId) == 1);
        check((restingSide == Side::Buy ? trades[1].buyOrderId : trades[1].sellOrderId) == 2);
        check(trades[0].price == 100.0);
        // Market orders ignore their price and sweep levels in price/time order.
        book.addOrder({5, incomingSide, OrderType::Market, 0.0, 20});
        check(trades.size() == 4);
        check(trades[2].quantity == 1 && trades[3].quantity == 4);
        check(trades[3].price == nextPrice);
        check(snapshot(book).find(" | ") == std::string::npos);
    }

    OrderBook empty;
    empty.addOrder({1, Side::Buy, OrderType::Market, 0.0, 10});
    empty.addOrder({2, Side::Sell, OrderType::Market, 0.0, 10});
    check(empty.getTrades().empty());
    check(snapshot(empty).find(" | ") == std::string::npos);

    OrderBook limits;
    limits.addOrder({1, Side::Sell, OrderType::Limit, 101.0, 5});
    limits.addOrder({2, Side::Buy, OrderType::Limit, 100.0, 8});
    check(limits.getTrades().empty());
    limits.addOrder({3, Side::Buy, OrderType::Limit, 102.0, 7});
    check(limits.getTrades().size() == 1);
    check(limits.getTrades()[0].price == 101.0);
    check(snapshot(limits).find("102 | 2") != std::string::npos);

    const auto before = snapshot(limits);
    for (Order invalid : {
        Order{4, Side::Buy, OrderType::Limit, 1.0, 0},
        Order{4, Side::Buy, OrderType::Limit, 1.0, -1},
        Order{4, Side::Buy, OrderType::Limit, 0.0, 1},
        Order{4, Side::Buy, OrderType::Limit, -1.0, 1},
        Order{4, Side::Buy, OrderType::Limit, std::numeric_limits<double>::quiet_NaN(), 1},
        Order{4, Side::Buy, OrderType::Limit, std::numeric_limits<double>::infinity(), 1},
        Order{4, static_cast<Side>(9), OrderType::Limit, 1.0, 1},
        Order{4, Side::Buy, static_cast<OrderType>(9), 1.0, 1}}) {
        bool rejected = false;
        try { limits.addOrder(invalid); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected);
        check(snapshot(limits) == before);
        check(limits.getTrades().size() == 1);
    }

    OrderBook large;
    large.addOrder({1, Side::Buy, OrderType::Limit, 1.0, 2147483647});
    large.addOrder({2, Side::Buy, OrderType::Limit, 1.0, 2147483647});
    check(snapshot(large).find("4294967294") != std::string::npos);
    std::cout << "All matching engine regression tests passed.\n";
}
