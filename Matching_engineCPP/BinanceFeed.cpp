#include "BinanceFeed.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

#pragma comment(lib, "winhttp.lib")

namespace {
void check(bool ok) {
    if (!ok) throw std::runtime_error("WinHTTP blad " + std::to_string(GetLastError()));
}
struct HttpHandle {
    HINTERNET value;
    explicit HttpHandle(HINTERNET handle) : value(handle) { check(value != nullptr); }
    ~HttpHandle() { WinHttpCloseHandle(value); }
    HttpHandle(const HttpHandle&) = delete;
    HttpHandle& operator=(const HttpHandle&) = delete;
};

// Ta mala warstwa pozwala przerwac czekanie na siec po wpisaniu quit.
// WinHTTP konczy operacje w callbacku. Portfel nie jest dotykany w tym callbacku.
struct AsyncRequest {
    HINTERNET value;
    std::mutex mutex;
    std::condition_variable changed;
    bool complete = false;
    bool closed = false;
    DWORD error = 0;
    DWORD bytes = 0;
    WINHTTP_WEB_SOCKET_BUFFER_TYPE type = WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
    char buffer[4096]{};

    explicit AsyncRequest(HINTERNET handle) : value(handle) {
        check(value != nullptr);
        DWORD_PTR context = reinterpret_cast<DWORD_PTR>(this);
        if (!WinHttpSetOption(value, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context))) {
            DWORD failure = GetLastError();
            WinHttpCloseHandle(value);
            throw std::runtime_error("Kontekst WinHTTP blad " + std::to_string(failure));
        }
    }
    ~AsyncRequest() {
        WinHttpCloseHandle(value);
        // Kontekst i bufor musza zyc do ostatniego callbacku, nawet po anulowaniu.
        std::unique_lock lock(mutex);
        changed.wait(lock, [this] { return closed; });
    }
    AsyncRequest(const AsyncRequest&) = delete;
    AsyncRequest& operator=(const AsyncRequest&) = delete;

    void prepare() {
        std::lock_guard lock(mutex);
        complete = false;
        error = 0;
    }
    void wait(const std::atomic<bool>& stop) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        std::unique_lock lock(mutex);
        while (!complete) {
            if (stop.load()) throw std::runtime_error("Zatrzymano odbiornik");
            if (std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error("Przekroczony czas oczekiwania na dane");
            changed.wait_for(lock, std::chrono::milliseconds(100));
        }
        if (error) throw std::runtime_error("WinHTTP blad " + std::to_string(error));
    }
};

void CALLBACK statusCallback(HINTERNET, DWORD_PTR context, DWORD status, void* information, DWORD) {
    if (!context) return;
    auto& request = *reinterpret_cast<AsyncRequest*>(context);
    std::lock_guard lock(request.mutex);
    if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) request.closed = true;
    else if (status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) {
        request.error = static_cast<WINHTTP_ASYNC_RESULT*>(information)->dwError;
        request.complete = true;
    }
    else if (status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE) {
        auto* result = static_cast<WINHTTP_WEB_SOCKET_STATUS*>(information);
        request.bytes = result->dwBytesTransferred;
        request.type = result->eBufferType;
        request.complete = true;
    }
    else if (status == WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE
        || status == WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE) request.complete = true;
    request.changed.notify_all();
}

void receive(const std::atomic<bool>& stop, const std::function<void(const MarketSnapshot&)>& onSnapshot,
    int maxUpdates, int& updates, const std::string& symbol) {
    HttpHandle session(WinHttpOpen(L"StudentMatchingEngine/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
    check(WinHttpSetTimeouts(session.value, 10000, 10000, 10000, 15000) != FALSE);
    check(WinHttpSetStatusCallback(session.value, statusCallback,
        WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0)
        != WINHTTP_INVALID_STATUS_CALLBACK);
    HttpHandle connection(WinHttpConnect(session.value, L"data-stream.binance.vision", 443, 0));
    std::wstring path = L"/ws/";
    for (char letter : symbol) path += static_cast<wchar_t>(letter >= 'A' && letter <= 'Z' ? letter + ('a' - 'A') : letter);
    path += L"@depth5";
    AsyncRequest request(WinHttpOpenRequest(connection.value, L"GET", path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
    check(WinHttpSetOption(request.value, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0) != FALSE);
    request.prepare();
    check(WinHttpSendRequest(request.value, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        WINHTTP_NO_REQUEST_DATA, 0, 0, reinterpret_cast<DWORD_PTR>(&request)) != FALSE);
    request.wait(stop);
    request.prepare();
    check(WinHttpReceiveResponse(request.value, nullptr) != FALSE);
    request.wait(stop);
    DWORD status = 0, size = sizeof(status);
    check(WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX) != FALSE);
    if (status != 101) throw std::runtime_error("Binance HTTP " + std::to_string(status));
    AsyncRequest socket(WinHttpWebSocketCompleteUpgrade(request.value, 0));
    std::string message;
    std::uint64_t previousId = 0;
    while (!stop.load() && (maxUpdates == 0 || updates < maxUpdates)) {
        socket.prepare();
        DWORD error = WinHttpWebSocketReceive(socket.value, socket.buffer, sizeof(socket.buffer), nullptr, nullptr);
        if (error != NO_ERROR) throw std::runtime_error("WebSocket blad " + std::to_string(error));
        socket.wait(stop);
        if (stop.load()) return;
        if (socket.type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
            throw std::runtime_error("Binance zamknal polaczenie");
        if (socket.type != WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE
            && socket.type != WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE)
            throw std::runtime_error("Oczekiwano tekstowej wiadomosci");
        message.append(socket.buffer, socket.bytes);
        if (message.size() > 65536) throw std::runtime_error("Za duza wiadomosc WebSocket");
        if (socket.type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE) continue;
        MarketSnapshot snapshot = parseMarketSnapshot(message);
        message.clear();
        if (snapshot.updateId < previousId) throw std::runtime_error("Cofniete ID aktualizacji");
        previousId = snapshot.updateId;
        onSnapshot(snapshot);
        ++updates;
    }
}
}

int streamBinance(const std::atomic<bool>& stop,
    const std::function<void(const MarketSnapshot&)>& onSnapshot,
    const std::function<void(const std::string&)>& onDisconnected, int maxUpdates, const std::string& symbol) {
    if (symbol != "BTCUSDT" && symbol != "ETHUSDT" && symbol != "SOLUSDT") throw std::invalid_argument("Nieobslugiwany rynek Binance");
    if (maxUpdates < 0) throw std::invalid_argument("Niepoprawny limit aktualizacji");
    int updates = 0;
    int failures = 0;
    while (!stop.load()) {
        onDisconnected("Laczenie z Binance; czekamy na swieze dane");
        try {
            receive(stop, onSnapshot, maxUpdates, updates, symbol);
            onDisconnected("Odbiornik zatrzymany");
            return 0;
        }
        catch (const std::exception& error) {
            onDisconnected(stop.load() ? "Odbiornik zatrzymany" : error.what());
            if (stop.load()) return 0;
            if (maxUpdates > 0 && ++failures >= 4) return 1;
            // Krotkie kroki czekania pozwalaja szybko obsluzyc quit.
            for (int i = 0; i < 20 && !stop.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    onDisconnected("Odbiornik zatrzymany");
    return 0;
}
