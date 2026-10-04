#pragma once

enum class Side {
    Buy,
    Sell
};

enum class OrderType {
    Limit,
    Market,
    StopMarket
};

enum class TimeInForce {
    GTC, // Limit czeka w ksiazce, az zostanie wykonany lub anulowany.
    IOC, // Wykonaj dostepna czesc teraz, anuluj reszte.
    FOK  // Wykonaj wszystko teraz albo nic.
};

enum class OrderStatus {
    Unknown,         // Nie znamy tego ID.
    New,             // Przyjete, ale jeszcze nic nie wykonano.
    PartiallyFilled, // Wykonano czesc, a reszta nadal czeka.
    Filled,          // Wykonano cala ilosc.
    Cancelled        // Anulowano niewykonana reszte.
};

struct Order {
    int id;
    Side side;
    OrderType type;
    double price; // Limit: granica ceny. StopMarket: prog aktywacji. Market: ignorowane.
    int quantity;
    // Domyslna wartosc pozwala nadal uzywac starych przykladow z 5 polami.
    TimeInForce timeInForce = TimeInForce::GTC;
};
