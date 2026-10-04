#include "EngineWorker.h"
#include <sstream>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <stdexcept>

void check(bool condition) {
    if (!condition) throw std::runtime_error("Engine feature check failed");
}

void sameTrades(const std::vector<Trade>& first, const std::vector<Trade>& second) {
    check(first.size() == second.size());
    for (std::size_t i = 0; i < first.size(); ++i) {
        check(first[i].id == second[i].id);
        check(first[i].buyOrderId == second[i].buyOrderId);
        check(first[i].sellOrderId == second[i].sellOrderId);
        check(first[i].price == second[i].price);
        check(first[i].quantity == second[i].quantity);
        // Czas zegarowy roznych uruchomien moze sie roznic.
    }
}

void testReplay() {
    std::ifstream file("data/replay.csv");
    check(static_cast<bool>(file));
    auto commands = loadReplay(file);
    check(commands.size() == 12);
    OrderBook book;
    EngineWorker worker;
    for (const EngineCommand& command : commands) {
        auto sync = executeCommand(book, command);
        auto threaded = worker.submit(command).get();
        check(sync.applied == threaded.applied);
        check(sync.status == threaded.status);
        check(sync.hasBid == threaded.hasBid && sync.hasAsk == threaded.hasAsk);
        check(sync.bestBid == threaded.bestBid && sync.bestAsk == threaded.bestAsk);
        sameTrades(sync.trades, threaded.trades);
    }
    EngineCommand snapshot;
    auto final = worker.submit(snapshot).get();
    sameTrades(book.getTrades(), final.trades);
    check(final.trades.size() == 6);
    check(book.getOrderStatus(9) == OrderStatus::Cancelled);
    check(book.getOrderStatus(5) == OrderStatus::Filled);
    worker.close();

    for (const std::string& text : {
        "1,ADD,1,BUY,LIMIT,100,5,GTC\n1,CANCEL,1",
        "1,ADD,1,BAD,LIMIT,100,5,GTC",
        "1,ADD,1,BUY,LIMIT,100abc,5,GTC",
        "1,ADD,1,BUY,LIMIT,nan,5,GTC",
        "1,MODIFY,1,100,0",
        "1,CANCEL,1,extra",
        "-1,CANCEL,1",
        "1,ADD,1,BUY,LIMIT,100,5,GTC,"}) {
        std::istringstream input(text);
        bool rejected = false;
        try { loadReplay(input); }
        catch (const std::exception&) { rejected = true; }
        check(rejected);
    }
    std::istringstream bom("\xEF\xBB\xBFsequence,action,id,side,type,price,quantity,tif\r\n# comment\n1,CANCEL,1\n");
    check(loadReplay(bom).size() == 1);
}

void testIndex() {
    OrderBook book;
    Order order{};
    order.id = -1;
    check(!book.getOrder(999, order) && order.id == -1);
    // Duzy indeks sprawdza rowniez przebudowy tablicy unordered_map.
    for (int id = 1; id <= 2000; ++id) {
        book.addOrder({id, Side::Sell, OrderType::Limit, 101.0 + id % 5, 2});
    }
    for (int id = 1; id <= 2000; id += 2) check(book.cancelOrder(id));
    for (int id = 2; id <= 2000; id += 2) {
        check(book.getOrder(id, order) && order.id == id);
        check(book.modifyOrder(id, 106.0, 3));
        check(book.getOrder(id, order) && order.price == 106.0 && order.quantity == 3);
    }
    book.addOrder({3000, Side::Buy, OrderType::Market, 0.0, 3000});
    check(book.getTrades().size() == 1000);
    for (int id = 1; id <= 2000; ++id) check(!book.getOrder(id, order));
    check(book.getVolume(Side::Sell, 106.0) == 0);
    // Po usunieciu indeksu ID moze byc ponownie uzyte zgodnie ze starymi zasadami.
    book.addOrder({2, Side::Sell, OrderType::Limit, 101.0, 1});
    check(book.getOrder(2, order) && order.quantity == 1);
}

void testConcurrentProducers() {
    EngineWorker worker;
    std::mutex recordMutex;
    std::vector<EngineCommand> accepted;
    std::exception_ptr failure;
    std::vector<std::thread> producers;
    for (int producer = 0; producer < 4; ++producer) {
        producers.emplace_back([&, producer]() {
            try {
                for (int offset = 1; offset <= 50; ++offset) {
                    EngineCommand command;
                    command.action = EngineAction::Add;
                    command.order = {producer * 50 + offset, Side::Sell, OrderType::Limit, 101.0, 1};
                    auto result = worker.submit(command).get();
                    check(result.applied && result.status == OrderStatus::New);
                    command.sequence = result.sequence;
                    std::lock_guard<std::mutex> lock(recordMutex);
                    accepted.push_back(command);
                }
            }
            catch (...) {
                std::lock_guard<std::mutex> lock(recordMutex);
                failure = std::current_exception();
            }
        });
    }
    for (auto& producer : producers) producer.join();
    if (failure) std::rethrow_exception(failure);
    std::sort(accepted.begin(), accepted.end(), [](const EngineCommand& a, const EngineCommand& b) {
        return a.sequence < b.sequence;
    });
    check(accepted.size() == 200);
    OrderBook reference;
    for (const auto& command : accepted) executeCommand(reference, command);
    EngineCommand buy;
    buy.action = EngineAction::Add;
    buy.order = {1000, Side::Buy, OrderType::Market, 0.0, 200};
    auto sync = executeCommand(reference, buy);
    auto threaded = worker.submit(buy).get();
    sameTrades(sync.trades, threaded.trades);
    check(threaded.trades.size() == 200);
    worker.close();
}

void testShutdownAndErrors() {
    EngineWorker worker;
    std::vector<std::future<EngineResult>> results;
    for (int id = 1; id <= 100; ++id) {
        EngineCommand command;
        command.action = EngineAction::Add;
        command.order = {id, Side::Buy, OrderType::Limit, 100.0, 1};
        results.push_back(worker.submit(command));
    }
    worker.close(); // Musi dokonczyc wszystkie juz przyjete zadania.
    for (auto& result : results) check(result.get().applied);
    bool rejected = false;
    try { worker.submit(EngineCommand{}); }
    catch (const std::exception&) { rejected = true; }
    check(rejected);

    EngineWorker errors;
    EngineCommand command;
    command.action = EngineAction::Add;
    command.order = {1, Side::Buy, OrderType::Limit, 100.0, 1};
    errors.submit(command).get();
    rejected = false;
    try { errors.submit(command).get(); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected);
    command.action = EngineAction::Cancel;
    check(errors.submit(command).get().applied);
    command.sequence = 1;
    rejected = false;
    try { errors.submit(command); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected);
}

int main() {
    testIndex();
    testReplay();
    testConcurrentProducers();
    testShutdownAndErrors();
    std::cout << "Index, replay and multithreading tests passed.\n";
}
