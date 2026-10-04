#include "EngineWorker.h"
#include <stdexcept>
#include <limits>

EngineWorker::EngineWorker() : worker(&EngineWorker::run, this) {}
EngineWorker::~EngineWorker() { close(); }

std::future<EngineResult> EngineWorker::submit(EngineCommand command) {
    Job job;
    auto result = job.completion.get_future();
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping) throw std::runtime_error("Worker is closed");
        if (command.sequence == 0) {
            if (lastSequence == std::numeric_limits<std::uint64_t>::max()) {
                throw std::overflow_error("Sequence exhausted");
            }
            command.sequence = lastSequence + 1;
        }
        if (command.sequence <= lastSequence) throw std::invalid_argument("Sequence must increase");
        job.command = command;
        // promise nie jest kopiowalne: move przekazuje je do kolejki.
        jobs.push(std::move(job));
        lastSequence = command.sequence;
    }
    ready.notify_one();
    return result;
}

void EngineWorker::run() {
    while (true) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex);
            // wait zwalnia mutex na czas oczekiwania. Petla obsluguje tez falszywe pobudki.
            while (jobs.empty() && !stopping) ready.wait(lock);
            if (jobs.empty() && stopping) return;
            job = std::move(jobs.front());
            jobs.pop();
        }
        try {
            job.completion.set_value(executeCommand(book, job.command));
        }
        catch (...) {
            // future.get() przekaze blad producentowi. Kolejne zadania nadal dzialaja.
            job.completion.set_exception(std::current_exception());
        }
    }
}

void EngineWorker::close() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        stopping = true;
    }
    ready.notify_one();
    if (worker.joinable()) worker.join();
}
