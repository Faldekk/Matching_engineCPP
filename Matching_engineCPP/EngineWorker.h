#pragma once
#include "EngineCommand.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <future>

class EngineWorker {
private:
    struct Job {
        EngineCommand command;
        std::promise<EngineResult> completion;
    };
    // Tylko run() ma dostep do ksiazki. Producent dotyka jedynie kolejki.
    OrderBook book;
    std::queue<Job> jobs;
    std::mutex mutex;
    std::condition_variable ready;
    bool stopping = false;
    std::uint64_t lastSequence = 0;
    std::thread worker;
    void run();

public:
    EngineWorker();
    ~EngineWorker();
    EngineWorker(const EngineWorker&) = delete;
    EngineWorker& operator=(const EngineWorker&) = delete;
    // Kolejnosc ustala przyjecie pod mutexem. Wczesniej przyjete = wczesniej wykonane.
    std::future<EngineResult> submit(EngineCommand command);
    // Wywoluje wlasciciel workera; konczy przyjete zadania i dolacza watek.
    void close();
};
