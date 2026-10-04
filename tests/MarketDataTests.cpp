#include "MarketData.h"
#include <iostream>
#include <stdexcept>

void require(bool condition) {
    if (!condition) throw std::runtime_error("Test danych rynkowych nie przeszedl");
}

void reject(const std::string& text) {
    bool failed = false;
    try { parseMarketSnapshot(text); }
    catch (const std::exception&) { failed = true; }
    require(failed);
}

int main() {
    try {
        auto snapshot = parseMarketSnapshot(R"({"lastUpdateId":42,"bids":[["100.00","0.001"],["99.50","2.5"]],"asks":[["101.00","1.25"]]})");
        require(snapshot.updateId == 42 && snapshot.bids.size() == 2 && snapshot.asks.size() == 1);
        require(snapshot.bids[0].quantity == 0.001 && snapshot.asks[0].price == 101);
        require(snapshot.asks[0].price - snapshot.bids[0].price == 1);
        auto empty = parseMarketSnapshot(" { \"asks\" : [], \"bids\" : [], \"lastUpdateId\" : 0 } \n");
        require(empty.bids.empty() && empty.asks.empty());
        reject(R"({"lastUpdateId":1,"bids":[],"bids":[]})");
        reject(R"({"lastUpdateId":1,"bids":[]})");
        reject(R"({"lastUpdateId":-1,"bids":[],"asks":[]})");
        reject(R"({"lastUpdateId":18446744073709551616,"bids":[],"asks":[]})");
        reject(R"({"lastUpdateId":1,"bids":[["100","-1"]],"asks":[]})");
        reject(R"({"lastUpdateId":1,"bids":[["NaN","1"]],"asks":[]})");
        reject(R"({"lastUpdateId":1,"bids":[["100oops","1"]],"asks":[]})");
        reject(R"({"lastUpdateId":1,"bids":[["0","1"]],"asks":[]})");
        reject(R"({"lastUpdateId":1,"bids":[["100","0"]],"asks":[]})");
        reject(R"({"lastUpdateId":1,"bids":[["99","1"],["100","1"]],"asks":[]})");
        reject(R"({"lastUpdateId":1,"bids":[],"asks":[["102","1"],["101","1"]]})");
        reject(R"({"lastUpdateId":1,"bids":[["101","1"]],"asks":[["100","1"]]})");
        reject(R"({"lastUpdateId":1,"bids":[],"asks":[]}garbage)");
        reject(R"({"lastUpdateId":1,"bids":[["100","1"]],"asks":[])");
        reject(R"({"lastUpdateId":1,"bids":[],"asks":[],"extra":1})");
        reject(R"({"lastUpdateId":1,"bids":[["100","1"],["99","1"],["98","1"],["97","1"],["96","1"],["95","1"]],"asks":[]})");
        reject(std::string(65537, 'x'));
        std::cout << "MarketDataTests: OK\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
