#include "ServerApi.h"
#include <cmath>
#include <stdexcept>

namespace {
crow::response jsonResponse(crow::json::wvalue value, int code = 200) {
    crow::response response(code, value.dump());
    response.set_header("Content-Type", "application/json; charset=utf-8");
    response.set_header("Cache-Control", "no-store");
    return response;
}

crow::response errorResponse(int code, const std::string& message) {
    crow::json::wvalue value;
    value["ok"] = false;
    value["error"] = message;
    return jsonResponse(std::move(value), code);
}

crow::json::wvalue levelsJson(const std::vector<PriceLevel>& levels) {
    std::vector<crow::json::wvalue> result;
    for (const PriceLevel& level : levels) {
        crow::json::wvalue item;
        item["price"] = level.price;
        item["quantity"] = level.quantity;
        result.push_back(std::move(item));
    }
    return crow::json::wvalue(std::move(result));
}

crow::json::wvalue marketJson(const PaperState& state, std::chrono::steady_clock::time_point now) {
    bool ready = state.account.marketReady(now);
    crow::json::wvalue value;
    value["symbol"] = state.symbol;
    value["ready"] = ready;
    value["status"] = state.status;
    // ID jako tekst: JavaScript nie reprezentuje dokladnie wszystkich uint64.
    value["updateId"] = ready ? crow::json::wvalue(std::to_string(state.market.updateId)) : crow::json::wvalue(nullptr);
    value["bids"] = levelsJson(ready ? state.market.bids : std::vector<PriceLevel>{});
    value["asks"] = levelsJson(ready ? state.market.asks : std::vector<PriceLevel>{});
    value["spread"] = nullptr;
    if (ready && !state.market.bids.empty() && !state.market.asks.empty())
        value["spread"] = state.market.asks.front().price - state.market.bids.front().price;
    return value;
}

crow::json::wvalue walletJson(const PaperState& state, std::chrono::steady_clock::time_point now) {
    crow::json::wvalue value;
    value["usdt"] = state.account.getUsdt();
    value["reservedUsdt"] = state.reservedUsdt;
    value["availableUsdt"] = state.account.getUsdt() - state.reservedUsdt;
    value["btc"] = 0.0;
    value["quantity"] = state.account.getBtc();
    value["costBasis"] = state.account.getCostBasis();
    double pnl = 0;
    double equity = state.account.getUsdt();
    bool valued = true;
    for (const auto& position : state.positions) {
        if (position.symbol == "BTCUSDT") value["btc"] = position.quantity;
        pnl += position.realizedPnl;
        equity += position.value;
        if (!position.valued) valued = false;
    }
    value["realizedPnl"] = pnl;
    value["tradingEnabled"] = state.account.marketReady(now);
    value["equity"] = nullptr;
    if (valued) value["equity"] = equity;
    std::vector<crow::json::wvalue> positions;
    for (const auto& position : state.positions) {
        crow::json::wvalue item;
        item["symbol"] = position.symbol;
        item["quantity"] = position.quantity;
        item["reserved"] = state.reservedAssets.at(position.symbol);
        item["available"] = position.quantity - state.reservedAssets.at(position.symbol);
        item["costBasis"] = position.costBasis;
        item["realizedPnl"] = position.realizedPnl;
        item["value"] = position.valued ? crow::json::wvalue(position.value) : crow::json::wvalue(nullptr);
        item["weight"] = valued && equity > 0 ? crow::json::wvalue(100 * position.value / equity) : crow::json::wvalue(nullptr);
        positions.push_back(std::move(item));
    }
    value["positions"] = std::move(positions);
    value["cashWeight"] = valued && equity > 0 ? crow::json::wvalue(100 * state.account.getUsdt() / equity) : crow::json::wvalue(nullptr);
    return value;
}

crow::json::wvalue historyJson(const PaperState& state) {
    std::vector<crow::json::wvalue> values;
    for (const auto& execution : state.fills) {
        const auto& fill = execution.fill;
        crow::json::wvalue item;
        item["symbol"] = execution.symbol;
        item["side"] = fill.side == Side::Buy ? "BUY" : "SELL";
        item["price"] = fill.price;
        item["quantity"] = fill.quantity;
        values.push_back(std::move(item));
    }
    return crow::json::wvalue(std::move(values));
}

crow::json::wvalue conditionalJson(const PaperState& state) {
    std::vector<crow::json::wvalue> values;
    for (const auto& order : state.conditionalOrders) {
        crow::json::wvalue item;
        item["id"] = order.id;
        item["symbol"] = order.symbol;
        item["type"] = order.type;
        item["price"] = order.price;
        item["quantity"] = order.quantity;
        item["filled"] = order.filled;
        item["triggered"] = order.triggered;
        item["status"] = order.status;
        values.push_back(std::move(item));
    }
    return crow::json::wvalue(std::move(values));
}

crow::response submitConditional(const crow::request& request, LivePaperSession& session) {
    if (request.body.size() > 4096) return errorResponse(413, "Za duza wiadomosc");
    if (request.get_header_value("Content-Type") != "application/json") return errorResponse(415, "Wymagany application/json");
    if (request.body.find('\0') != std::string::npos) return errorResponse(400, "Niepoprawny JSON");
    auto body = crow::json::load(request.body);
    if (!body || body.t() != crow::json::type::Object || body.size() != 4
        || !body.has("symbol") || !body.has("type") || !body.has("quantity") || !body.has("price")
        || body["symbol"].t() != crow::json::type::String || body["type"].t() != crow::json::type::String
        || body["quantity"].t() != crow::json::type::Number || body["price"].t() != crow::json::type::Number)
        return errorResponse(400, "Podaj symbol, type, quantity i price");
    try {
        int id = session.addConditionalOrder(body["symbol"].s(), body["type"].s(), body["quantity"].d(), body["price"].d());
        crow::json::wvalue value;
        value["ok"] = true;
        value["id"] = id;
        return jsonResponse(std::move(value));
    } catch (const std::invalid_argument& error) { return errorResponse(422, error.what()); }
    catch (const std::runtime_error& error) { return errorResponse(409, error.what()); }
}

crow::response submitOrder(const crow::request& request, LivePaperSession& session) {
    if (request.body.size() > 4096) return errorResponse(413, "Za duza wiadomosc");
    const std::string& contentType = request.get_header_value("Content-Type");
    if (contentType != "application/json" && contentType != "application/json; charset=utf-8")
        return errorResponse(415, "Wymagany Content-Type: application/json");
    if (request.body.find('\0') != std::string::npos) return errorResponse(400, "Niepoprawny JSON");
    auto body = crow::json::load(request.body);
    if (!body || body.t() != crow::json::type::Object || (body.size() != 2 && body.size() != 3)
        || !body.has("side") || !body.has("quantity")
        || body["side"].t() != crow::json::type::String
        || body["quantity"].t() != crow::json::type::Number)
        return errorResponse(400, "Podaj JSON: {\"side\":\"BUY\",\"quantity\":0.001}");
    if ((body.size() == 3 && !body.has("symbol")) || (body.has("symbol") && body["symbol"].t() != crow::json::type::String))
        return errorResponse(400, "Niepoprawny symbol rynku");
    std::string symbol = body.has("symbol") ? std::string(body["symbol"].s()) : "BTCUSDT";
    if (!LivePaperSession::supports(symbol)) return errorResponse(422, "Dostepne rynki: BTCUSDT, ETHUSDT, SOLUSDT");
    std::string side = body["side"].s();
    double quantity = 0;
    try { quantity = body["quantity"].d(); }
    catch (const std::exception&) { return errorResponse(422, "Niepoprawna ilosc aktywa"); }
    if ((side != "BUY" && side != "SELL") || !std::isfinite(quantity) || quantity <= 0)
        return errorResponse(422, "Strona musi byc BUY lub SELL, a ilosc aktywa dodatnia");
    try {
        PaperResult result = session.order(side == "BUY" ? Side::Buy : Side::Sell, quantity, symbol);
        crow::json::wvalue value;
        value["ok"] = true;
        value["symbol"] = symbol;
        value["side"] = side;
        value["requested"] = result.requested;
        value["filled"] = result.filled;
        value["cancelled"] = result.cancelled;
        value["averagePrice"] = result.filled > 0 ? crow::json::wvalue(result.averagePrice) : crow::json::wvalue(nullptr);
        value["slippage"] = result.filled > 0 ? crow::json::wvalue(result.slippage) : crow::json::wvalue(nullptr);
        return jsonResponse(std::move(value));
    }
    catch (const std::invalid_argument& error) { return errorResponse(422, error.what()); }
    catch (const std::runtime_error& error) { return errorResponse(409, error.what()); }
}
}

void registerApi(crow::SimpleApp& app, LivePaperSession& session) {
    CROW_ROUTE(app, "/")([] {
        crow::json::wvalue value;
        value["name"] = "Matching engine - paper trading";
        value["endpoints"] = std::vector<std::string>{"GET /api/state", "GET /api/market",
            "GET /api/wallet", "GET /api/history", "POST /api/orders"};
        return jsonResponse(std::move(value));
    });
    CROW_ROUTE(app, "/api/market")([&session](const crow::request& request) {
        std::string symbol = request.url_params.get("symbol") ? request.url_params.get("symbol") : "BTCUSDT";
        if (!LivePaperSession::supports(symbol)) return errorResponse(422, "Nieobslugiwany rynek");
        auto state = session.read(symbol);
        return jsonResponse(marketJson(state, std::chrono::steady_clock::now()));
    });
    CROW_ROUTE(app, "/api/wallet")([&session] {
        auto state = session.read();
        return jsonResponse(walletJson(state, std::chrono::steady_clock::now()));
    });
    CROW_ROUTE(app, "/api/history")([&session] {
        crow::json::wvalue value;
        value["fills"] = historyJson(session.read());
        return jsonResponse(std::move(value));
    });
    CROW_ROUTE(app, "/api/state")([&session](const crow::request& request) {
        // Jeden odczyt pod mutexem zapewnia zgodne salda i historie.
        std::string symbol = request.url_params.get("symbol") ? request.url_params.get("symbol") : "BTCUSDT";
        if (!LivePaperSession::supports(symbol)) return errorResponse(422, "Nieobslugiwany rynek");
        auto state = session.read(symbol);
        auto now = std::chrono::steady_clock::now();
        crow::json::wvalue value;
        value["market"] = marketJson(state, now);
        value["wallet"] = walletJson(state, now);
        value["fills"] = historyJson(state);
        value["conditionalOrders"] = conditionalJson(state);
        return jsonResponse(std::move(value));
    });
    CROW_ROUTE(app, "/api/orders").methods(crow::HTTPMethod::POST)([&session](const crow::request& request) {
        return submitOrder(request, session);
    });
    CROW_ROUTE(app, "/api/conditional-orders").methods(crow::HTTPMethod::POST)([&session](const crow::request& request) {
        return submitConditional(request, session);
    });
    CROW_ROUTE(app, "/api/conditional-orders/<int>/cancel").methods(crow::HTTPMethod::POST)([&session](int id) {
        if (!session.cancelConditionalOrder(id)) return errorResponse(409, "Zlecenie nie istnieje lub juz zakonczone");
        crow::json::wvalue value;
        value["ok"] = true;
        return jsonResponse(std::move(value));
    });
}
