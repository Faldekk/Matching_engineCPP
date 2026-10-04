#include "OrderBook.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <vector>

// Mierzymy czas trwania, wiec uzywamy steady_clock (nie cofa sie).
double elapsedNs(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<double, std::nano>(
        std::chrono::steady_clock::now() - start).count();
}

void report(const char* phase, int count, std::vector<double>& samples) {
    double total = 0;
    for (double sample : samples) total += sample;
    std::sort(samples.begin(), samples.end());
    // Percentyl p95: okolo 95% zmierzonych operacji trwalo nie dluzej.
    std::cout << phase << ',' << count << ',' << total / 1000000.0 << ','
        << samples[samples.size() / 2] << ','
        << samples[samples.size() * 95 / 100] << ','
        << samples[samples.size() * 99 / 100] << '\n';
}

void benchmark(int count, bool print) {
    OrderBook book;
    std::vector<double> samples;
    samples.reserve(count);
    for (int id = 1; id <= count; ++id) {
        auto start = std::chrono::steady_clock::now();
        book.addOrder({id, Side::Buy, OrderType::Limit, 100.0, 10});
        samples.push_back(elapsedNs(start));
    }
    if (print) report("add", count, samples);
    samples.clear();
    for (int id = count; id >= 1; --id) {
        auto start = std::chrono::steady_clock::now();
        bool changed = book.modifyOrder(id, 100.0, 9);
        samples.push_back(elapsedNs(start));
        if (!changed) throw std::runtime_error("Benchmark modify failed");
    }
    if (print) report("modify", count, samples);
    samples.clear();
    for (int id = count; id >= 1; --id) {
        auto start = std::chrono::steady_clock::now();
        bool removed = book.cancelOrder(id);
        samples.push_back(elapsedNs(start));
        if (!removed) throw std::runtime_error("Benchmark cancel failed");
    }
    if (print) report("cancel", count, samples);
    samples.clear();
    // Osobny profil matchingu: pojedyncze wykonania, setup poza pomiarem.
    for (int id = 1; id <= count; ++id) {
        book.addOrder({id, Side::Sell, OrderType::Limit, 101.0, 1});
    }
    for (int id = 1; id <= count; ++id) {
        auto start = std::chrono::steady_clock::now();
        book.addOrder({count + id, Side::Buy, OrderType::Market, 0.0, 1});
        samples.push_back(elapsedNs(start));
    }
    if (print) report("match", count, samples);
    if (book.getTrades().size() != static_cast<std::size_t>(count)) {
        throw std::runtime_error("Benchmark trade count failed");
    }
}

int main() {
    benchmark(100, false); // Rozgrzewka, bez drukowania.
    std::cout << "phase,orders,total_ms,p50_ns,p95_ns,p99_ns\n";
    for (int round = 0; round < 3; ++round) {
        for (int count : {1000, 5000, 10000}) benchmark(count, true);
    }
}
