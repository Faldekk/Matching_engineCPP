#include "OrderBook.h"
#include <iostream>
#include <fstream>
#include <string>
#include "EngineWorker.h"

int runReplay(const std::string& path, bool threaded) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot open CSV: " + path);
    // Najpierw sprawdzamy format calego pliku, dopiero potem wykonujemy polecenia.
    auto commands = loadReplay(input);
    OrderBook book;
    EngineWorker worker;
    for (const EngineCommand& command : commands) {
        try {
            if (threaded) printEngineResult(worker.submit(command).get());
            else printEngineResult(executeCommand(book, command));
        }
        catch (const std::exception& error) {
            throw std::runtime_error("Event " + std::to_string(command.sequence) + ": " + error.what());
        }
    }
    EngineCommand snapshot;
    EngineResult result;
    if (threaded) result = worker.submit(snapshot).get();
    else result = executeCommand(book, snapshot);
    worker.close();
    std::cout << "\nReplay zakonczony. Transakcje: " << result.trades.size() << '\n';
    if (result.hasBid) std::cout << "Best bid: " << result.bestBid << '\n';
    else std::cout << "Best bid: brak\n";
    if (result.hasAsk) std::cout << "Best ask: " << result.bestAsk << '\n';
    else std::cout << "Best ask: brak\n";
    return 0;
}

int main(int argc, char* argv[]) {
    if (argc != 1) {
        try {
            if (argc == 3 && std::string(argv[1]) == "--replay") return runReplay(argv[2], false);
            if (argc == 3 && std::string(argv[1]) == "--replay-threaded") return runReplay(argv[2], true);
            std::cerr << "Usage: MatchingEngineDemo [--replay|--replay-threaded path.csv]\n";
            return 2;
        }
        catch (const std::exception& error) {
            std::cerr << "Error: " << error.what() << '\n';
            return 1;
        }
    }
    OrderBook book;

    book.addOrder({
        1,
        Side::Sell,
        OrderType::Limit,
        101.0,
        5
        });

    book.addOrder({
        2,
        Side::Sell,
        OrderType::Limit,
        102.0,
        10
        });

    book.addOrder({
        3,
        Side::Sell,
        OrderType::Limit,
        103.0,
        20
        });

    book.printOrderBook();

    book.printOrderStatus(3); // NEW: jeszcze nic nie sprzedano.

    // Zapamietujemy ask PRZED zakupem. Po zakupie najlepsza cena moze sie zmienic.
    double referencePrice = 0.0;
    bool hasReferencePrice = book.getBestAsk(referencePrice);

    book.addOrder({
        4,
        Side::Buy,
        OrderType::Limit,
        103.0,
        25
        });

    book.printOrderBook();
    book.printTrades();
    double averagePrice = 0.0;
    if (book.getAverageExecutionPrice(4, averagePrice)) {
        // (5 * 101 + 10 * 102 + 10 * 103) / 25 = 102.2.
        std::cout << "Srednia cena BUY #4: " << averagePrice << '\n';
    }
    else {
        std::cout << "BUY #4: brak wykonan\n";
    }
    double slippage = 0.0;
    if (hasReferencePrice && book.getSlippage(4, Side::Buy, referencePrice, slippage)) {
        // Srednia 102.2 minus poczatkowy ask 101 = 1.2 na sztuke.
        std::cout << "Slippage BUY #4: " << slippage << " na sztuke\n";
    }
    else {
        std::cout << "Slippage BUY #4: brak ceny odniesienia lub wykonan\n";
    }
    book.printOrderStatus(1); // FILLED: sprzedano wszystkie 5 sztuk.
    book.printOrderStatus(3); // PARTIALLY_FILLED: sprzedano 10 z 20 sztuk.
    book.printOrderStatus(4); // FILLED: kupiono wszystkie 25 sztuk.
    std::cout << "\nCancel #3: " << (book.cancelOrder(3) ? "OK" : "NOT FOUND") << '\n';
    book.printOrderBook();
    std::cout << "Cancel #3 again: " << (book.cancelOrder(3) ? "OK" : "NOT FOUND") << '\n';
    book.printOrderStatus(3);   // CANCELLED: anulowano pozostale 10 sztuk.
    book.printOrderStatus(999); // UNKNOWN: takiego ID nie dodalismy.
    book.printMarketSummary(); // Pusta ksiazka: brak obu cen i spreadu.

    std::cout << "\n--- MODIFY ORDER ---\n";
    OrderBook modifyExample;
    modifyExample.addOrder({1, Side::Sell, OrderType::Limit, 101.0, 10});
    modifyExample.addOrder({2, Side::Buy, OrderType::Limit, 99.0, 5});
    modifyExample.printOrderBook();

    // Bid 99, ask 101, spread 2.
    modifyExample.printMarketSummary();

    // Podnosimy limit kupna: zlecenie moze teraz kupic po 101.
    modifyExample.modifyOrder(2, 101.0, 5);
    modifyExample.printOrderBook();
    modifyExample.printTrades();
    // BUY wykonal sie w calosci, wiec nie mamy juz bidu ani spreadu.
    modifyExample.printMarketSummary();

    std::cout << "\n--- VOLUME AT PRICE ---\n";
    OrderBook volumeExample;
    volumeExample.addOrder({1, Side::Sell, OrderType::Limit, 101.0, 5});
    volumeExample.addOrder({2, Side::Sell, OrderType::Limit, 101.0, 10});
    std::cout << "SELL @ 101: " << volumeExample.getVolume(Side::Sell, 101.0) << '\n';

    // Kupujemy 4 sztuki: z 15 dostepnych zostaje 11.
    volumeExample.addOrder({3, Side::Buy, OrderType::Market, 0.0, 4});
    std::cout << "Po zakupie 4: " << volumeExample.getVolume(Side::Sell, 101.0) << '\n';

    // Anulujemy drugie SELL na 10 sztuk: zostaje 1 sztuka pierwszego.
    volumeExample.cancelOrder(2);
    std::cout << "Po anulowaniu #2: " << volumeExample.getVolume(Side::Sell, 101.0) << '\n';

    volumeExample.modifyOrder(1, 102.0, 3);
    std::cout << "Po zmianie #1, SELL @ 101: " << volumeExample.getVolume(Side::Sell, 101.0) << '\n';
    std::cout << "Po zmianie #1, SELL @ 102: " << volumeExample.getVolume(Side::Sell, 102.0) << '\n';
    Order activeOrder{};
    if (volumeExample.getOrder(1, activeOrder)) {
        std::cout << "Lookup ID #1: " << activeOrder.quantity << " @ " << activeOrder.price << '\n';
    }

    std::cout << "\n--- IOC ---\n";
    OrderBook iocExample;
    iocExample.addOrder({1, Side::Sell, OrderType::Limit, 101.0, 3});
    iocExample.addOrder({2, Side::Buy, OrderType::Limit, 101.0, 5, TimeInForce::IOC});
    iocExample.printTrades(); // Kupiono 3, pozostale 2 anulowano.
    iocExample.printOrderStatus(2);

    std::cout << "\n--- FOK ---\n";
    OrderBook fokExample;
    fokExample.addOrder({1, Side::Sell, OrderType::Limit, 101.0, 3});
    fokExample.addOrder({2, Side::Buy, OrderType::Limit, 101.0, 5, TimeInForce::FOK});
    fokExample.printOrderStatus(2); // CANCELLED, nie kupiono nic.
    fokExample.printOrderBook();   // Wszystkie 3 sztuki nadal czekaja.
    fokExample.addOrder({3, Side::Buy, OrderType::Limit, 101.0, 3, TimeInForce::FOK});
    fokExample.printTrades();
    fokExample.printOrderStatus(3); // FILLED.

    std::cout << "\n--- STOP MARKET ---\n";
    OrderBook stopExample;
    // 101 to prog aktywacji, NIE maksymalna cena zakupu.
    stopExample.addOrder({1, Side::Buy, OrderType::StopMarket, 101.0, 2});
    stopExample.addOrder({2, Side::Sell, OrderType::Limit, 101.0, 1});
    stopExample.addOrder({3, Side::Sell, OrderType::Limit, 102.0, 5});
    stopExample.printStopOrders();
    // Transakcja po 101 uruchamia stop, ktory kupuje 2 sztuki po 102.
    stopExample.addOrder({4, Side::Buy, OrderType::Market, 0.0, 1});
    stopExample.printTrades();
    stopExample.printOrderStatus(1);
    stopExample.printStopOrders();

    return 0;
}
