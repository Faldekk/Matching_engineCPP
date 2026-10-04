#include "LivePaper.h"
#include <atomic>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

void require(bool condition) {
    if (!condition) throw std::runtime_error("LivePaper test failed");
}
void reject(LivePaperSession& session, const std::string& command) {
    auto before = session.read();
    bool failed = false;
    try { executePaperCommand(session, command); }
    catch (const std::exception&) { failed = true; }
    auto after = session.read();
    require(failed && before.account.getUsdt() == after.account.getUsdt()
        && before.account.getBtc() == after.account.getBtc()
        && before.account.getHistory().size() == after.account.getHistory().size());
}

int main() {
    try {
        LivePaperSession session;
        reject(session, "buy 0.001");
        session.onSnapshot({1, {{99, 1}}, {{100, 1}}});
        reject(session, "buy 0.1oops");
        reject(session, "buy 0,1");
        reject(session, "buy 0.1 extra");
        reject(session, "buy -1");
        reject(session, "wallet extra");
        reject(session, "unknown");
        auto output = executePaperCommand(session, "buy 0.01");
        require(output.find("0.01000000") != std::string::npos);
        require(executePaperCommand(session, "history").find("BUY") != std::string::npos);
        require(executePaperCommand(session, "book").find("Binance BTC/USDT") != std::string::npos);
        session.onDisconnected("Utracono polaczenie");
        reject(session, "buy 0.001");
        reject(session, "sell 0.001");
        require(session.read().account.getHistory().size() == 1);
        session.onSnapshot({2, {{101, 1}}, {{102, 1}}});
        executePaperCommand(session, "sell 0.01");
        require(session.read().account.getBtc() == 0);
        require(std::abs(session.read().account.getRealizedPnl() - 0.01) < 1e-8);
        session.onSnapshot({3, {{101, 1}}, {{102, 1}}},
            std::chrono::steady_clock::now() - std::chrono::seconds(6));
        reject(session, "buy 0.001");
        require(executePaperCommand(session, "wallet").find("zablokowany") != std::string::npos);

        LivePaperSession concurrent;
        concurrent.onSnapshot({1, {{99, 1}}, {{100, 0.05}}});
        std::atomic<bool> failed = false;
        std::vector<std::thread> producers;
        for (int t = 0; t < 4; ++t) producers.emplace_back([&] {
            try {
                for (int i = 0; i < 25; ++i) {
                    concurrent.order(Side::Buy, 0.001);
                    auto state = concurrent.read();
                    if (std::abs(state.account.getUsdt() + state.account.getBtc() * 100 - 10000) > 1e-8)
                        failed = true;
                }
            }
            catch (...) { failed = true; }
        });
        for (auto& producer : producers) producer.join();
        auto state = concurrent.read();
        require(!failed.load());
        require(std::abs(state.account.getBtc() - 0.05) < 1e-8);
        require(std::abs(state.account.getUsdt() - 9995) < 1e-8);
        std::cout << "LivePaperTests: OK\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
