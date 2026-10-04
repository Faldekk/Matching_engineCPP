#pragma once
#include "OrderBook.h"
#include <string>
#include <istream>

enum class EngineAction { Add, Cancel, Modify, Snapshot };

struct EngineCommand {
    std::uint64_t sequence = 0; // 0: worker nada numer przy przyjeciu polecenia.
    EngineAction action = EngineAction::Snapshot;
    Order order{}; // CANCEL/SNAPSHOT uzywa id; MODIFY: id, price i quantity.
};

struct EngineResult {
    std::uint64_t sequence = 0;
    bool applied = false;
    OrderStatus status = OrderStatus::Unknown;
    bool hasActiveOrder = false;
    Order activeOrder{};
    bool hasBid = false;
    bool hasAsk = false;
    double bestBid = 0;
    double bestAsk = 0;
    // Zwykle nowe transakcje; SNAPSHOT zwraca cala historie.
    std::vector<Trade> trades;
};

EngineResult executeCommand(OrderBook& book, const EngineCommand& command);
std::vector<EngineCommand> loadReplay(std::istream& input);
void printEngineResult(const EngineResult& result);
