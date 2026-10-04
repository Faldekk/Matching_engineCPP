#pragma once
#include "MarketData.h"
#include <atomic>
#include <functional>

// Callback to funkcja wywolywana po odebraniu danych lub zmianie polaczenia.
// Wywolania sa sekwencyjne, w watku uruchamiajacym streamBinance.
int streamBinance(const std::atomic<bool>& stop,
    const std::function<void(const MarketSnapshot&)>& onSnapshot,
    const std::function<void(const std::string&)>& onDisconnected,
    int maxUpdates = 0, const std::string& symbol = "BTCUSDT");
