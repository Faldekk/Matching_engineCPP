#include "ServerApi.h"
#include "BinanceFeed.h"
#include <atomic>
#include <iostream>
#include <thread>
#include <cstdlib>

int main(int argc, char*[]) {
    if (argc != 1) {
        std::cerr << "Uzycie: MatchingEngineServer\n";
        return 2;
    }
    LivePaperSession session;
    int port = 18080;
    // Osobny port pozwala sprawdzic nowa wersje bez kasowania trwajacej sesji.
    char testPort[16]{};
    std::size_t portLength = 0;
    if (getenv_s(&portLength, testPort, sizeof(testPort), "MATCHING_PORT") != 0) {
        std::cerr << "Niepoprawny MATCHING_PORT\n";
        return 2;
    }
    if (portLength > 0) {
        try {
            std::string text = testPort;
            std::size_t parsed = 0;
            port = std::stoi(text, &parsed);
            if (parsed != text.size() || port < 1024 || port > 65535) throw std::invalid_argument("port");
        } catch (const std::exception&) {
            std::cerr << "Niepoprawny MATCHING_PORT\n";
            return 2;
        }
    }
    crow::SimpleApp app;
    registerApi(app, session);
    std::atomic<bool> stop = false;
    std::vector<std::thread> feeds;
    for (const std::string symbol : {"BTCUSDT", "ETHUSDT", "SOLUSDT"}) {
        feeds.emplace_back([&, symbol] {
            streamBinance(stop, [&](const MarketSnapshot& market) { session.onSnapshot(symbol, market); },
                [&](const std::string& error) { session.onDisconnected(symbol, error); }, 0, symbol);
        });
    }
    int result = 0;
    try {
        std::cout << "API: http://127.0.0.1:" << port << "/api/state\n"
            << "10 000 wirtualnych USDT. Bez prowizji. Koniec: Ctrl+C.\n" << std::flush;
        app.bindaddr("127.0.0.1").port(static_cast<std::uint16_t>(port)).concurrency(2).loglevel(crow::LogLevel::Warning).run();
    }
    catch (const std::exception& error) {
        std::cerr << "Blad serwera: " << error.what() << '\n';
        result = 1;
    }
    stop = true;
    for (auto& feed : feeds) feed.join();
    return result;
}
