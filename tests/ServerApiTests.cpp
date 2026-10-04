#include "ServerApi.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <atomic>

void require(bool ok) { if (!ok) throw std::runtime_error("Server API test failed"); }

crow::response call(crow::SimpleApp& app, const std::string& path,
    const std::string& body = "", crow::HTTPMethod method = crow::HTTPMethod::GET,
    const std::string& contentType = "application/json") {
    crow::request request;
    request.url = path.substr(0, path.find('?'));
    request.url_params = crow::query_string(path);
    request.method = method;
    request.body = body;
    request.headers.emplace("Content-Type", contentType);
    crow::response response;
    app.handle_full(request, response);
    return response;
}

void reject(crow::SimpleApp& app, LivePaperSession& session, const std::string& body, int code,
    const std::string& contentType = "application/json") {
    auto before = session.read();
    auto response = call(app, "/api/orders", body, crow::HTTPMethod::POST, contentType);
    require(response.code == code);
    require(static_cast<bool>(crow::json::load(response.body)));
    auto after = session.read();
    require(before.account.getBtc() == after.account.getBtc()
        && before.account.getUsdt() == after.account.getUsdt()
        && before.account.getHistory().size() == after.account.getHistory().size());
}

int main() {
    try {
        LivePaperSession session;
        crow::SimpleApp app;
        registerApi(app, session);
        app.validate();
        auto initial = crow::json::load(call(app, "/api/state").body);
        require(initial && initial["wallet"]["usdt"].d() == 10000);
        require(initial["market"]["ready"].t() == crow::json::type::False);
        require(initial["market"]["bids"].size() == 0);
        reject(app, session, R"({"side":"BUY","quantity":0.01})", 409);
        session.onSnapshot({1, {{99, 1}}, {{100, 0.02}, {101, 0.03}}});
        reject(app, session, "broken", 400);
        reject(app, session, R"({"side":"BUY","quantity":0.01}trailing)", 400);
        reject(app, session, std::string("{\"side\":\"BUY\",\"quantity\":0.01}") + '\0' + "extra", 400);
        reject(app, session, "[]", 400);
        reject(app, session, R"({"side":"BUY"})", 400);
        reject(app, session, R"({"side":"BUY","quantity":"0.01"})", 400);
        reject(app, session, R"({"side":"BUY","quantity":0.01,"extra":1})", 400);
        reject(app, session, R"({"side":"BUY","quantity":0.01,"side":"SELL"})", 400);
        reject(app, session, R"({"side":"INVALID","quantity":0.01})", 422);
        reject(app, session, R"({"side":"BUY","quantity":-1})", 422);
        reject(app, session, R"({"side":"BUY","quantity":0})", 422);
        auto overflow = call(app, "/api/orders", R"({"side":"BUY","quantity":1e999})", crow::HTTPMethod::POST);
        require(overflow.code == 400 || overflow.code == 422);
        reject(app, session, R"({"side":"SELL","quantity":0.01})", 409);
        reject(app, session, std::string(4097, ' '), 413);
        reject(app, session, R"({"side":"BUY","quantity":0.01})", 415, "text/plain");
        require(call(app, "/api/orders").code == 405);
        auto buy = call(app, "/api/orders", R"({"side":"BUY","quantity":0.04})", crow::HTTPMethod::POST);
        require(buy.code == 200);
        auto execution = crow::json::load(buy.body);
        require(execution && std::abs(execution["averagePrice"].d() - 100.5) < 1e-8);
        auto state = crow::json::load(call(app, "/api/state").body);
        require(state["fills"].size() == 2);
        require(std::abs(state["wallet"]["usdt"].d() - 9995.98) < 1e-8);
        require(state["market"]["updateId"].t() == crow::json::type::String);
        auto partial = crow::json::load(call(app, "/api/orders", R"({"side":"BUY","quantity":1})", crow::HTTPMethod::POST).body);
        require(std::abs(partial["filled"].d() - 0.01) < 1e-8);
        session.onDisconnected("Test disconnect");
        reject(app, session, R"({"side":"SELL","quantity":0.01})", 409);
        auto wallet = crow::json::load(call(app, "/api/wallet").body);
        require(wallet["equity"].t() == crow::json::type::Null);
        session.onSnapshot({2, {{102, 1}}, {{103, 1}}});
        require(call(app, "/api/orders", R"({"side":"SELL","quantity":0.01})", crow::HTTPMethod::POST).code == 200);
        require(crow::json::load(call(app, "/api/history").body)["fills"].size() == 4);
        session.onSnapshot({3, {{102, 1}}, {{103, 1}}}, std::chrono::steady_clock::now() - std::chrono::seconds(6));
        reject(app, session, R"({"side":"BUY","quantity":0.01})", 409);
        require(crow::json::load(call(app, "/api/market").body)["asks"].size() == 0);
        session.onSnapshot({4, {{99, 1}}, {{20000, 1}}});
        reject(app, session, R"({"side":"BUY","quantity":1})", 409);
        session.onSnapshot({5, {{99, 1}}, {{100, 0.05}}});
        double previousBtc = session.read().account.getBtc();
        std::atomic<bool> concurrentFailure = false;
        std::vector<std::thread> clients;
        for (int t = 0; t < 4; ++t) clients.emplace_back([&] {
            for (int i = 0; i < 25; ++i) {
                if (call(app, "/api/orders", R"({"side":"BUY","quantity":0.001})", crow::HTTPMethod::POST).code != 200)
                    concurrentFailure = true;
                auto view = crow::json::load(call(app, "/api/state").body);
                if (!view || view["fills"].size() == 0) concurrentFailure = true;
            }
        });
        for (auto& client : clients) client.join();
        require(!concurrentFailure.load());
        require(std::abs(session.read().account.getBtc() - previousBtc - 0.05) < 1e-8);
        // Trzy aktywa korzystaja z jednego salda, a odrzucenie nie zmienia niczego.
        LivePaperSession diversified;
        crow::SimpleApp multi;
        registerApi(multi, diversified);
        multi.validate();
        require(crow::json::load(call(multi, "/api/state?symbol=ETHUSDT").body)["market"]["symbol"].s() == "ETHUSDT");
        require(call(multi, "/api/state?symbol=DOGEUSDT").code == 422);
        diversified.onSnapshot("BTCUSDT", {1, {{99, 10}}, {{100, 10}}});
        diversified.onSnapshot("ETHUSDT", {1, {{49, 100}}, {{50, 100}}});
        diversified.onSnapshot("SOLUSDT", {1, {{9, 1000}}, {{10, 1000}}});
        auto solMarket = crow::json::load(call(multi, "/api/market?symbol=SOLUSDT").body);
        require(solMarket["symbol"].s() == "SOLUSDT" && solMarket["bids"][0]["price"].d() == 9);
        for (const auto& body : {R"({"symbol":"BTCUSDT","side":"BUY","quantity":1})",
            R"({"symbol":"ETHUSDT","side":"BUY","quantity":2})", R"({"symbol":"SOLUSDT","side":"BUY","quantity":10})"})
            require(call(multi, "/api/orders", body, crow::HTTPMethod::POST).code == 200);
        auto portfolio = crow::json::load(call(multi, "/api/wallet").body);
        require(portfolio["usdt"].d() == 9700);
        require(portfolio["equity"].d() == 9987);
        require(portfolio["positions"].size() == 3);
        double weights = portfolio["cashWeight"].d();
        for (int i = 0; i < 3; ++i) weights += portfolio["positions"][i]["weight"].d();
        require(std::abs(weights - 100) < 1e-8);
        require(diversified.read("ETHUSDT").account.getBtc() == 2);
        require(diversified.read("SOLUSDT").account.getUsdt() == 9700);
        require(crow::json::load(call(multi, "/api/history").body)["fills"].size() == 3);
        reject(multi, diversified, R"({"symbol":"ETHUSDT","side":"SELL","quantity":3})", 409);
        reject(multi, diversified, R"({"symbol":"SOLUSDT","side":"BUY","quantity":1000})", 409);
        reject(multi, diversified, R"({"symbol":"DOGEUSDT","side":"BUY","quantity":1})", 422);
        reject(multi, diversified, R"({"symbol":null,"side":"BUY","quantity":1})", 400);
        require(call(multi, "/api/orders", R"({"symbol":"ETHUSDT","side":"SELL","quantity":1})", crow::HTTPMethod::POST).code == 200);
        require(diversified.read().account.getUsdt() == 9749);
        require(diversified.read("ETHUSDT").account.getBtc() == 1);
        diversified.onDisconnected("ETHUSDT", "Test");
        require(crow::json::load(call(multi, "/api/wallet").body)["equity"].t() == crow::json::type::Null);
        reject(multi, diversified, R"({"symbol":"ETHUSDT","side":"BUY","quantity":1})", 409);
        // Awaria ETH nie blokuje swiezego rynku SOL.
        require(call(multi, "/api/orders", R"({"symbol":"SOLUSDT","side":"SELL","quantity":1})", crow::HTTPMethod::POST).code == 200);
        diversified.onSnapshot("ETHUSDT", {2, {{49, 100}}, {{50, 100}}});
        require(crow::json::load(call(multi, "/api/wallet").body)["equity"].t() == crow::json::type::Number);
        LivePaperSession competing;
        competing.onSnapshot("ETHUSDT", {1, {{49, 1000}}, {{50, 1000}}});
        competing.onSnapshot("SOLUSDT", {1, {{9, 2000}}, {{10, 2000}}});
        std::atomic<int> accepted = 0;
        auto buyAll = [&](const std::string& symbol, double quantity) {
            try { competing.order(Side::Buy, quantity, symbol); ++accepted; }
            catch (const std::runtime_error&) { }
        };
        std::thread ethBuyer(buyAll, "ETHUSDT", 200);
        std::thread solBuyer(buyAll, "SOLUSDT", 1000);
        ethBuyer.join(); solBuyer.join();
        require(accepted == 1 && competing.read().account.getUsdt() == 0);
        LivePaperSession rules;
        crow::SimpleApp rulesApi;
        registerApi(rulesApi, rules); rulesApi.validate();
        rules.onSnapshot("ETHUSDT", {1, {{99, 10}}, {{100, 10}}});
        auto placed = call(rulesApi, "/api/conditional-orders", R"({"symbol":"ETHUSDT","type":"BUY_LIMIT","quantity":2,"price":95})", crow::HTTPMethod::POST);
        require(placed.code == 200);
        int ruleId = static_cast<int>(crow::json::load(placed.body)["id"].i());
        require(rules.read().reservedUsdt == 190);
        require(rules.read("ETHUSDT").account.getBtc() == 0);
        auto reservedWallet = crow::json::load(call(rulesApi, "/api/wallet").body);
        require(reservedWallet["availableUsdt"].d() == 9810);
        rules.onSnapshot("ETHUSDT", {2, {{94, 10}}, {{95, 0.5}, {96, 10}}});
        require(rules.read("ETHUSDT").account.getBtc() == 0.5); // Nie kupuje poziomu 96 ponad limitem.
        require(rules.read().conditionalOrders[0].status == "PARTIALLY_FILLED");
        require(rules.read().reservedUsdt == 142.5);
        rules.onSnapshot("ETHUSDT", {2, {{94, 10}}, {{95, 0.5}, {96, 10}}});
        require(rules.read("ETHUSDT").account.getBtc() == 0.5); // Ta sama aktualizacja nie odnawia plynnosci.
        require(call(rulesApi, "/api/conditional-orders/" + std::to_string(ruleId) + "/cancel", "{}", crow::HTTPMethod::POST).code == 200);
        require(rules.read().reservedUsdt == 0);
        require(!rules.cancelConditionalOrder(ruleId));
        rules.onSnapshot("ETHUSDT", {3, {{100, 10}}, {{101, 10}}});
        int stopId = rules.addConditionalOrder("ETHUSDT", "STOP_LOSS", 0.5, 90);
        require(rules.read().reservedAssets.at("ETHUSDT") == 0.5);
        reject(rulesApi, rules, R"({"symbol":"ETHUSDT","side":"SELL","quantity":0.1})", 409);
        rules.onSnapshot("ETHUSDT", {4, {{89, 0.25}}, {{90, 10}}});
        require(rules.read().conditionalOrders[1].triggered);
        require(rules.read("ETHUSDT").account.getBtc() == 0.25);
        require(rules.read().conditionalOrders[1].status == "PARTIALLY_FILLED");
        rules.onDisconnected("ETHUSDT", "Disconnected");
        require(rules.read().conditionalOrders[1].status == "PARTIALLY_FILLED");
        rules.onSnapshot("ETHUSDT", {5, {{92, 0.25}}, {{93, 10}}});
        require(rules.read("ETHUSDT").account.getBtc() == 0); // Stop po aktywacji pozostaje Market nawet po odbiciu.
        require(rules.read().conditionalOrders[1].status == "FILLED");
        require(!rules.cancelConditionalOrder(stopId));
        require(std::abs(rules.read("ETHUSDT").account.getRealizedPnl() + 2.25) < 1e-8);
        require(rules.read().reservedAssets.at("ETHUSDT") == 0);
        int immediate = rules.addConditionalOrder("ETHUSDT", "BUY_LIMIT", 1, 94);
        require(rules.read().conditionalOrders.back().id == immediate);
        require(rules.read().conditionalOrders.back().status == "FILLED");
        require(rules.read("ETHUSDT").account.getBtc() == 1);
        require(call(rulesApi, "/api/conditional-orders", R"({"symbol":"ETHUSDT","type":"BUY_LIMIT","quantity":1,"price":0})", crow::HTTPMethod::POST).code == 422);
        require(call(rulesApi, "/api/conditional-orders", R"({"symbol":"ETHUSDT","type":"BUY_LIMIT","quantity":1,"price":"94"})", crow::HTTPMethod::POST).code == 400);
        rules.onDisconnected("ETHUSDT", "Test");
        require(call(rulesApi, "/api/conditional-orders", R"({"symbol":"ETHUSDT","type":"BUY_LIMIT","quantity":1,"price":94})", crow::HTTPMethod::POST).code == 409);
        std::cout << "ServerApiTests: OK\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
