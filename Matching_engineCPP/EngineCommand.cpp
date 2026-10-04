#include "EngineCommand.h"
#include <sstream>
#include <iostream>
#include <stdexcept>
#include <cmath>

EngineResult executeCommand(OrderBook& book, const EngineCommand& command) {
    std::size_t firstTrade = book.getTrades().size();
    EngineResult result;
    result.sequence = command.sequence;
    if (command.action == EngineAction::Add) {
        book.addOrder(command.order);
        result.applied = true;
    }
    else if (command.action == EngineAction::Cancel) {
        result.applied = book.cancelOrder(command.order.id);
    }
    else if (command.action == EngineAction::Modify) {
        result.applied = book.modifyOrder(command.order.id, command.order.price, command.order.quantity);
    }
    else if (command.action == EngineAction::Snapshot) {
        result.applied = true;
        firstTrade = 0;
    }
    else throw std::invalid_argument("Unknown command action");

    result.status = book.getOrderStatus(command.order.id);
    result.hasActiveOrder = book.getOrder(command.order.id, result.activeOrder);
    result.hasBid = book.getBestBid(result.bestBid);
    result.hasAsk = book.getBestAsk(result.bestAsk);
    const auto& trades = book.getTrades();
    for (std::size_t i = firstTrade; i < trades.size(); ++i) result.trades.push_back(trades[i]);
    return result;
}

// Sprawdzamy cale pole: stoi("12abc") bez tego sprawdzenia zaakceptowaloby 12.
int readInt(const std::string& text) {
    std::size_t used = 0;
    int value = std::stoi(text, &used);
    if (used != text.size()) throw std::invalid_argument("Invalid integer");
    return value;
}

double readPrice(const std::string& text) {
    std::size_t used = 0;
    double value = std::stod(text, &used);
    if (used != text.size() || !std::isfinite(value)) throw std::invalid_argument("Invalid price");
    return value;
}

std::string trim(const std::string& text) {
    auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::vector<EngineCommand> loadReplay(std::istream& input) {
    std::vector<EngineCommand> commands;
    std::string line;
    int lineNumber = 0;
    std::uint64_t previousSequence = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        // Akceptujemy tez plik UTF-8 zapisany z BOM.
        if (lineNumber == 1 && line.compare(0, 3, "\xEF\xBB\xBF") == 0) line.erase(0, 3);
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line == "sequence,action,id,side,type,price,quantity,tif") continue;
        try {
            std::vector<std::string> fields;
            std::stringstream row(line);
            std::string field;
            while (std::getline(row, field, ',')) fields.push_back(trim(field));
            if (!line.empty() && line.back() == ',') fields.push_back("");
            if (fields.size() < 3) throw std::invalid_argument("Too few columns");
            // Numer musi byc dodatni i bez znaku minus.
            for (char character : fields[0]) {
                if (character < '0' || character > '9') throw std::invalid_argument("Invalid sequence");
            }
            EngineCommand command;
            command.sequence = std::stoull(fields[0]);
            if (command.sequence <= previousSequence) throw std::invalid_argument("Sequence must increase");
            command.order.id = readInt(fields[2]);
            if (fields[1] == "ADD") {
                if (fields.size() != 8) throw std::invalid_argument("ADD requires 8 columns");
                command.action = EngineAction::Add;
                if (fields[3] == "BUY") command.order.side = Side::Buy;
                else if (fields[3] == "SELL") command.order.side = Side::Sell;
                else throw std::invalid_argument("Unknown side");
                if (fields[4] == "LIMIT") command.order.type = OrderType::Limit;
                else if (fields[4] == "MARKET") command.order.type = OrderType::Market;
                else if (fields[4] == "STOP_MARKET") command.order.type = OrderType::StopMarket;
                else throw std::invalid_argument("Unknown order type");
                command.order.price = readPrice(fields[5]);
                command.order.quantity = readInt(fields[6]);
                if (fields[7] == "GTC") command.order.timeInForce = TimeInForce::GTC;
                else if (fields[7] == "IOC") command.order.timeInForce = TimeInForce::IOC;
                else if (fields[7] == "FOK") command.order.timeInForce = TimeInForce::FOK;
                else throw std::invalid_argument("Unknown time in force");
                if (command.order.quantity <= 0) throw std::invalid_argument("Quantity must be positive");
                if (command.order.type != OrderType::Market && command.order.price <= 0) {
                    throw std::invalid_argument("Price must be positive");
                }
                if (command.order.type == OrderType::StopMarket && command.order.timeInForce != TimeInForce::GTC) {
                    throw std::invalid_argument("Stop requires GTC");
                }
            }
            else if (fields[1] == "CANCEL") {
                if (fields.size() != 3) throw std::invalid_argument("CANCEL requires 3 columns");
                command.action = EngineAction::Cancel;
            }
            else if (fields[1] == "MODIFY") {
                if (fields.size() != 5) throw std::invalid_argument("MODIFY requires 5 columns");
                command.action = EngineAction::Modify;
                command.order.price = readPrice(fields[3]);
                command.order.quantity = readInt(fields[4]);
                if (command.order.price <= 0 || command.order.quantity <= 0) {
                    throw std::invalid_argument("Price and quantity must be positive");
                }
            }
            else throw std::invalid_argument("Unknown action");
            previousSequence = command.sequence;
            commands.push_back(command);
        }
        catch (const std::exception& error) {
            throw std::runtime_error("CSV line " + std::to_string(lineNumber) + ": " + error.what());
        }
    }
    if (input.bad()) throw std::runtime_error("Cannot read replay stream");
    return commands;
}

void printEngineResult(const EngineResult& result) {
    std::cout << "Event #" << result.sequence << (result.applied ? " OK" : " NOT FOUND") << '\n';
    for (const Trade& trade : result.trades) {
        std::cout << "TRADE #" << trade.id << " | BUY #" << trade.buyOrderId
            << " | SELL #" << trade.sellOrderId << " | PRICE: " << trade.price
            << " | QTY: " << trade.quantity << '\n';
    }
}
