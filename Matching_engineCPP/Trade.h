#pragma once

#include <cstdint>

struct Trade {
    std::uint64_t id;
    int buyOrderId;
    int sellOrderId;
    double price;
    int quantity;
    // Milisekundy od 1 stycznia 1970 (czas Unix).
    std::int64_t timestampMs;
};
