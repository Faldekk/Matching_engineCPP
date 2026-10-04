#include "MarketData.h"
#include <cmath>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>

namespace {
void expect(std::istream& input, char expected) {
    char value = 0;
    if (!(input >> value) || value != expected)
        throw std::runtime_error("Niepoprawny format danych Binance");
}

std::string readString(std::istream& input) {
    input >> std::ws;
    if (input.peek() != '"') throw std::runtime_error("Oczekiwano tekstu JSON");
    std::string value;
    if (!(input >> std::quoted(value))) throw std::runtime_error("Niepelny tekst JSON");
    return value;
}

double readDecimal(std::istream& input) {
    const std::string text = readString(input);
    // Binance wysyla ceny i ilosci jako tekst z kropka dziesietna.
    bool hasDigit = false;
    int dots = 0;
    for (char c : text) {
        if (c >= '0' && c <= '9') hasDigit = true;
        else if (c == '.') ++dots;
        else throw std::runtime_error("Niepoprawna cena lub ilosc");
    }
    if (!hasDigit || dots > 1) throw std::runtime_error("Niepoprawna liczba");
    std::istringstream number(text);
    number.imbue(std::locale::classic());
    double value = 0;
    if (!(number >> value) || !std::isfinite(value) || value <= 0)
        throw std::runtime_error("Cena i ilosc musza byc dodatnie");
    return value;
}

std::vector<PriceLevel> readLevels(std::istream& input, bool bids) {
    std::vector<PriceLevel> levels;
    expect(input, '[');
    input >> std::ws;
    if (input.peek() == ']') { input.get(); return levels; }
    while (true) {
        expect(input, '[');
        double price = readDecimal(input);
        expect(input, ',');
        double quantity = readDecimal(input);
        expect(input, ']');
        if (!levels.empty()) {
            double previous = levels.back().price;
            if ((bids && price >= previous) || (!bids && price <= previous))
                throw std::runtime_error("Niepoprawna kolejnosc poziomow cen");
        }
        levels.push_back({price, quantity});
        if (levels.size() > 5) throw std::runtime_error("Za duzo poziomow dla depth5");
        input >> std::ws;
        if (input.peek() == ']') { input.get(); break; }
        expect(input, ',');
    }
    return levels;
}
}

MarketSnapshot parseMarketSnapshot(const std::string& message) {
    if (message.size() > 65536) throw std::runtime_error("Za duza wiadomosc");
    std::istringstream input(message);
    MarketSnapshot snapshot;
    bool hasId = false, hasBids = false, hasAsks = false;
    expect(input, '{');
    for (int field = 0; field < 3; ++field) {
        if (field > 0) expect(input, ',');
        std::string key = readString(input);
        expect(input, ':');
        if (key == "lastUpdateId" && !hasId) {
            input >> std::ws;
            std::string digits;
            while (input.peek() >= '0' && input.peek() <= '9')
                digits += static_cast<char>(input.get());
            if (digits.empty()) throw std::runtime_error("Brak ID aktualizacji");
            snapshot.updateId = std::stoull(digits);
            hasId = true;
        }
        else if (key == "bids" && !hasBids) {
            snapshot.bids = readLevels(input, true);
            hasBids = true;
        }
        else if (key == "asks" && !hasAsks) {
            snapshot.asks = readLevels(input, false);
            hasAsks = true;
        }
        else throw std::runtime_error("Nieoczekiwane lub powtorzone pole Binance");
    }
    expect(input, '}');
    input >> std::ws;
    if (!input.eof() || !hasId || !hasBids || !hasAsks)
        throw std::runtime_error("Niepelna wiadomosc Binance");
    if (!snapshot.bids.empty() && !snapshot.asks.empty()
        && snapshot.bids.front().price >= snapshot.asks.front().price)
        throw std::runtime_error("Skrzyzowana ksiazka Binance");
    return snapshot;
}
