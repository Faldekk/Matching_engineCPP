# Matching engine C++ — mała giełda w terminalu

Projekt do nauki C++, struktur danych i mechaniki rynku. Program dopasowuje zlecenia kupna i sprzedaży, zapisuje transakcje i pokazuje książkę zleceń w terminalu.

Działamy na jednym umownym instrumencie, bez GUI i sieci. Zlecenia podajemy w kodzie albo odtwarzamy z CSV. Dostępny jest także worker przyjmujący polecenia od wielu wątków, ale tylko jeden wątek zmienia daną książkę. Program nie łączy się z prawdziwą giełdą.

## 1. Uruchomienie

1. Otwórz `Matching_engineCPP.sln` w Visual Studio z obsługą C++.
2. Wybierz `Debug` i `x64`.
3. Uruchom przez **Ctrl + F5**.

Projekt używa C++20. Plik wykonywalny tej konfiguracji powstaje w `x64/Debug/Matching_engineCPP.exe`.

Bez argumentów aplikacja wykonuje scenariusze z `main.cpp`. Nie ma jeszcze menu ani wpisywania zleceń z klawiatury. Aby zmienić eksperyment, edytuj `main.cpp` albo przygotuj własny CSV do replayu.

Możesz też zbudować aplikację w **Developer Command Prompt for Visual Studio**, z głównego folderu projektu:

```bat
build-demo.cmd
build\MatchingEngineDemo.exe
build\MatchingEngineDemo.exe --replay data\replay.csv
build\MatchingEngineDemo.exe --replay-threaded data\replay.csv
```

Projekt Visual Studio używa toolsetu v145. Skrypty korzystają z kompilatora `cl` dostępnego w Developer Command Prompt i wymagają obsługi C++20. Nie korzystamy z dodatkowych bibliotek. Ścieżkę do CSV podajemy względem aktualnego folderu terminala.

## 2. Struktura projektu

```text
Matching_engineCPP/
├── Matching_engineCPP.sln
├── README.md
├── .gitignore
├── build-demo.cmd
├── run-tests.cmd
├── run-benchmarks.cmd
├── Matching_engineCPP/
│   ├── main.cpp
│   ├── Order.h
│   ├── Order.cpp
│   ├── Trade.h
│   ├── Trade.cpp
│   ├── OrderBook.h
│   ├── OrderBook.cpp
│   ├── EngineCommand.h
│   ├── EngineCommand.cpp
│   ├── EngineWorker.h
│   └── EngineWorker.cpp
├── tests/
│   ├── OrderBookTests.cpp
│   └── EngineFeaturesTests.cpp
├── data/
│   └── replay.csv
└── benchmarks/
    ├── Benchmark.cpp
    ├── before.csv
    └── after.csv
```

| Plik | Rola |
| --- | --- |
| `main.cpp` | Przykłady działania aplikacji. |
| `Order.h` | Definicja zlecenia, strony, typu i statusu. |
| `Trade.h` | Definicja wykonanej transakcji. |
| `OrderBook.h` | Deklaracja klasy: jakie dane i funkcje ma książka. |
| `OrderBook.cpp` | Implementacja: jak te funkcje działają. |
| `Order.cpp`, `Trade.cpp` | Obecnie tylko dołączają nagłówki. |
| `tests/OrderBookTests.cpp` | Testy poprawności silnika. |
| `EngineCommand.h/.cpp` | Polecenia, wyniki i parser CSV. |
| `EngineWorker.h/.cpp` | Kolejka poleceń i jeden wątek wykonujący matching. |
| `tests/EngineFeaturesTests.cpp` | Testy indeksu ID, CSV i wielowątkowości. |
| `benchmarks/Benchmark.cpp` | Pomiar czasu dodawania, modyfikacji, anulowania i matchingu. |

Pliki Markdown inne niż główny README pozostają lokalne i są ignorowane przez Git.

## 3. Zlecenie a transakcja

**Zlecenie** to prośba o kupno lub sprzedaż. **Transakcja** powstaje dopiero wtedy, kiedy znajdzie się druga strona.

```cpp
book.addOrder({1, Side::Buy, OrderType::Limit, 100.0, 10});
```

Czytamy: „zlecenie nr 1 — kup 10 sztuk, płacąc najwyżej 100 za sztukę”.

| Pole Order | Znaczenie |
| --- | --- |
| `id` | Numer zlecenia. |
| `side` | Buy = kupno, Sell = sprzedaż. |
| `type` | Limit, Market lub StopMarket. |
| `price` | Limit ceny dla Limit, próg aktywacji dla StopMarket; ignorowana dla Market. |
| `quantity` | Ilość sztuk. W książce jest to ilość pozostała do wykonania. |
| `timeInForce` | GTC, IOC lub FOK. Domyślnie GTC, więc stare przykłady z pięcioma polami nadal działają. |

Dla SELL limit działa odwrotnie: `SELL 10 @ 100` oznacza „sprzedaj 10 sztuk po co najmniej 100”.

**Limit jest granicą ceny, a nie obietnicą ceny wykonania.** BUY może kupić taniej niż jego limit, a SELL może sprzedać drożej.

## 4. Bid, ask i spread

Order book, czyli książka zleceń, przechowuje oczekujące zlecenia Limit.

- **Bids** — oferty kupna. Najlepszy bid to najwyższa cena.
- **Asks** — oferty sprzedaży. Najlepszy ask to najniższa cena.
- **Spread** — najlepszy ask minus najlepszy bid.

```text
ASKS
101 | 15
102 | 20

BIDS
100 | 10
 99 | 30
```

Tutaj best bid = 100, best ask = 101, spread = 1. Kupujący oferują mniej, niż żądają sprzedający, więc nie ma transakcji.

Środek spreadu, czyli **mid-price**, wynosi:

```text
mid = (best ask + best bid) / 2
    = (101 + 100) / 2
    = 100.5
```

Mid-price jest punktem odniesienia. Nie oznacza, że można teraz handlować po tej cenie.

Jeśli jedna strona książki jest pusta, brakuje danych do obliczenia spreadu i mid-price. Brak ceny nie oznacza ceny zero. Best bid, best ask i spread mają już osobne funkcje. Mid-price na razie obliczamy tylko w przykładzie — nie ma jeszcze funkcji do jego odczytu.

### Jak odczytać te dane w kodzie?

```cpp
double price = 0.0;
double spread = 0.0;

if (book.getBestBid(price)) {
    std::cout << "Best bid: " << price << '\n';
}
if (book.getBestAsk(price)) {
    std::cout << "Best ask: " << price << '\n';
}
if (book.getSpread(spread)) {
    std::cout << "Spread: " << spread << '\n';
}

book.printMarketSummary(); // Wypisuje wszystkie trzy informacje.
```

Każda funkcja zwraca `true`, jeśli udało się odczytać wynik, i zapisuje go w przekazanej zmiennej. Znak `&` w parametrze oznacza referencję: funkcja może zmienić naszą zmienną.

Gdy nie ma wyniku, funkcja zwraca `false` i pozostawia zmienną bez zmian. Dlatego sprawdzamy wynik funkcji przed użyciem ceny — zmienna może nadal zawierać starą wartość. `getSpread` wymaga obu stron książki.

Przykładowy wydruk przy samych ofertach sprzedaży:

```text
Best bid: brak
Best ask: 101
Spread: brak (potrzebne obie strony rynku)
```

Odczyt najlepszych cen jest szybki: mapy są już posortowane, więc wystarczy pierwszy poziom. Nie trzeba przeszukiwać wszystkich zleceń.

## 5. Po co map, list i indeks ID?

```cpp
std::map<double, std::list<Order>, std::greater<double>> bids;
std::map<double, std::list<Order>> asks;
```

`map` wiąże cenę z kolejką zleceń i porządkuje poziomy:

- asks rosnąco: `asks.begin()` daje najtańszą sprzedaż;
- bids malejąco dzięki `std::greater<double>`: `bids.begin()` daje najwyższą ofertę kupna.

`list` przechowuje zlecenia w kolejności FIFO: dodajemy na końcu przez `push_back()`, a matching bierze pierwszy element przez `front()`.

Wcześniej używaliśmy `queue`, ale nie pozwalała usuwać dowolnego zlecenia ze środka. Anulowanie wymagało kopiowania i przepisywania kolejki. Lista umożliwia usunięcie elementu wskazanego iteratorem. Indeks `unordered_map` przechowuje dla każdego aktywnego ID jego położenie i iterator, więc nie przeszukujemy całej książki.

```cpp
Order order{};
if (book.getOrder(3, order)) {
    std::cout << order.quantity << " @ " << order.price << '\n';
}
```

`getOrder` zwraca kopię aktywnego zlecenia. Przy braku ID zwraca `false` i nie zmienia przekazanej zmiennej. Indeks usuwa wpis przy anulowaniu lub pełnym wykonaniu. Książki nie kopiujemy ani nie przenosimy: skopiowane iteratory mogłyby wskazywać elementy innego obiektu.

To **price-time priority**: najpierw najlepsza cena, potem czas dodania przy tej samej cenie. ID nie decyduje o kolejności.

Przykład: SELL nr 8 i SELL nr 2 mają cenę 101. Jeśli nr 8 dodano wcześniej, zostanie obsłużony wcześniej, mimo większego ID.

To reguła wybrana dla tego projektu. Inne systemy mogą stosować inne zasady dopasowania.

## 6. Matching krok po kroku

BUY Limit może kupić, gdy:

```text
limit kupna >= najlepszy ask
```

SELL Limit może sprzedać, gdy:

```text
limit sprzedaży <= najlepszy bid
```

Czeka `SELL 15 @ 101`. Przychodzi `BUY 10 @ 102`.

Cena jest dopuszczalna, bo 102 >= 101. Ilość transakcji to mniejsza z obu ilości:

```cpp
int tradedQuantity = std::min(order.quantity, sellOrder.quantity);
```

Wykonujemy 10 sztuk po 101. BUY wykonano w całości. Z SELL zostało 5 sztuk. To **partial fill**, czyli częściowe wykonanie SELL.

### Dlaczego cena wynosi 101, a nie 102?

W tym silniku cena transakcji pochodzi ze zlecenia już oczekującego w książce — **resting order**. Nowe zlecenie to **incoming order**.

Kupujący pozwala zapłacić do 102, ale ktoś już sprzedaje po 101. Używamy więc 101.

Analogicznie: jeśli czeka BUY @ 100, a przychodzi SELL @ 99, transakcja będzie po 100.

### Co robi while?

Dopóki nowe zlecenie ma pozostałą ilość i istnieje druga strona:

1. Pobieramy najlepszą cenę.
2. Dla Limit sprawdzamy, czy cena mieści się w limicie.
3. Pobieramy pierwsze zlecenie z kolejki na tym poziomie.
4. Zapisujemy transakcję i zmniejszamy ilości obu zleceń.
5. Usuwamy całkowicie wykonane zlecenie.
6. Usuwamy pusty poziom ceny.
7. Próbujemy wykonać kolejną część.

Transakcję zapisujemy **przed** `pop_front()`. Po usunięciu zlecenia referencja do niego nie nadaje się już do odczytu.

## 7. Limit i Market

### Limit

Kontroluje cenę, ale nie gwarantuje wykonania.

```cpp
book.addOrder({1, Side::Buy, OrderType::Limit, 100.0, 10});
```

Jeśli najlepszy ask to 101, zlecenie niczego nie kupi i pozostanie w książce na cenie 100. Po częściowym wykonaniu niewykonana reszta również zostaje w książce.

### Market

Korzysta z dostępnych cen po drugiej stronie, bez limitu ceny.

```cpp
book.addOrder({2, Side::Buy, OrderType::Market, 0.0, 10});
```

`0.0` jest tutaj tylko ignorowaną wartością pola. Nie oznacza kupna za zero.

Market BUY zaczyna od najniższego ask. Market SELL zaczyna od najwyższego bid. W razie potrzeby przechodzą przez kolejne poziomy.

**W naszej aplikacji niewykonana reszta Market jest odrzucana.** Jeśli chcesz kupić 10 sztuk, ale dostępne są tylko 4, kupisz 4. Pozostałe 6 nie trafi do książki. Jeśli druga strona jest pusta, nie powstanie żadna transakcja.

Market wykonany w całości ma status `FILLED`. Jeśli zostaje niewykonana reszta, końcowy status to `CANCELLED`, nawet gdy część już wykonano. Zapisane transakcje pozostają w historii.

### GTC, IOC i FOK

Typ zlecenia mówi, jak traktujemy cenę. `TimeInForce` określa, co musi wykonać się od razu i czy reszta może czekać.

| TIF | Działanie w tym projekcie |
| --- | --- |
| GTC — Good Till Cancelled | Niewykonana część Limit pozostaje w książce do wykonania lub anulowania. Market nadal odrzuca resztę. |
| IOC — Immediate or Cancel | Wykonujemy dostępną część natychmiast, resztę anulujemy. |
| FOK — Fill or Kill | Wykonujemy całość natychmiast albo nie wykonujemy nic. |

Załóżmy, że dostępne są 3 sztuki po 101 i 4 sztuki po 102. Wysyłamy BUY na 5 sztuk z limitem 101:

- IOC kupi 3 po 101 i anuluje pozostałe 2.
- FOK nie kupi nic, bo w limicie dostępne są tylko 3 sztuki.
- GTC kupi 3 i pozostawi BUY na 2 sztuki po 101 w książce.

```cpp
book.addOrder({1, Side::Buy, OrderType::Limit, 101.0, 5, TimeInForce::IOC});
book.addOrder({2, Side::Buy, OrderType::Limit, 101.0, 5, TimeInForce::FOK});
```

To przykłady niezależnych wywołań; wcześniejsze zlecenie może zmienić płynność dla następnego. Dla FOK najpierw sprawdzamy sumę ilości mieszczących się w limicie, bez zmiany książki. FOK może wykonać się na kilku cenach — wymaga pełnej ilości, nie jednej ceny. Limit 102 pozwoliłby kupić 3 po 101 i 2 po 102.

IOC/FOK obsługujemy dla Limit i Market. Niepełne IOC oraz niewykonalne FOK kończą jako `CANCELLED`; pełne wykonanie daje `FILLED`.

### Stop Market

```cpp
book.addOrder({3, Side::Buy, OrderType::StopMarket, 101.0, 5});
```

Tutaj 101 to **próg aktywacji, nie maksymalna cena zakupu**. Przyjęliśmy następujące zasady:

- Stop BUY aktywuje się, gdy cena transakcji osiąga lub przekracza próg.
- Stop SELL aktywuje się, gdy cena transakcji osiąga próg lub spada poniżej niego.
- Po aktywacji zlecenie staje się Market.
- Bez żadnej wcześniejszej transakcji stop czeka. Sama zmiana bid/ask go nie uruchamia.
- Jeśli ostatnia cena już spełnia warunek przy dodaniu lub zmianie progu, aktywacja jest natychmiastowa.

Oczekujące stopy są poza bids/asks, nie zwiększają dostępnego wolumenu i mają status `NEW`. Można je anulować oraz modyfikować; `printStopOrders()` wypisuje ich progi.

Przekroczenie progu zapamiętujemy przy każdej transakcji. Aktywowane stopy wykonujemy w kolejności aktywacji po zakończeniu bieżącego zlecenia. Stopy aktywowane tą samą transakcją zachowują kolejność oczekiwania. Dzięki temu nie zabierają płynności w środku FOK. Transakcja stopa może uruchomić kolejne stopy.

Zmiana progu lub zwiększenie ilości przesuwa oczekujący stop na koniec listy. Zmniejszenie ilości bez zmiany progu zachowuje jego miejsce. Oczekujący StopMarket przyjmujemy tylko z GTC.

Stop BUY z progiem 101 może ostatecznie kupić po 102 lub drożej. Jeśli po aktywacji zabraknie płynności, niewykonana część zostanie anulowana. To reguły tego modelu, a nie pełna specyfikacja konkretnej giełdy.

## 8. Płynność, głębokość i maker/taker

**Płynność** w tym modelu to dostępne zlecenia, z którymi można handlować. Sama najlepsza cena nie mówi jeszcze, jak dużą ilość można po niej wykonać.

**Głębokość książki**, czyli depth, pokazuje ilości na kolejnych poziomach ceny.

Wydruk `101 | 15` oznacza łącznie 15 sztuk przy cenie 101. Mogą to być trzy zlecenia po 5. Wydruk sumuje ilości, a matching nadal obsługuje każde zlecenie osobno według FIFO.

```cpp
std::int64_t volume = book.getVolume(Side::Sell, 101.0);
```

To suma **niewykonanych** sztuk na wskazanej cenie i stronie. Brak poziomu daje 0. To dostępna płynność, a nie wolumen historycznych transakcji. Nieprawidłowa strona lub niedodatnia/nieskończona cena powoduje wyjątek.

W kontekście pojedynczego wykonania:

- **maker** — oczekujące zlecenie, które dostarczyło płynność;
- **taker** — przychodzące zlecenie, które wykorzystało tę płynność.

Limit też może być takerem. BUY Limit @ 102 kupujący natychmiast od SELL @ 101 wykorzystuje istniejącą płynność. Jeśli jego reszta trafi do książki, może później dostarczyć płynność.

Nie naliczamy jeszcze prowizji maker/taker.

## 9. Średnia cena wykonania, VWAP i slippage

Załóżmy, że książka wygląda tak:

```text
ASKS
101 | 5
102 | 10
103 | 20
```

Przychodzi `BUY 25 @ 103`:

| Ilość | Cena | Wartość |
| --- | --- | --- |
| 5 | 101 | 505 |
| 10 | 102 | 1020 |
| 10 | 103 | 1030 |
| **25** | | **2555** |

Zostaje `SELL 10 @ 103`.

### Średnia ważona ilością

Nie liczymy zwykłej średniej trzech cen, bo na różnych cenach wykonano różne ilości.

```text
średnia cena = suma(cena × wykonana ilość) / suma(wykonana ilość)
            = 2555 / 25
            = 102.20
```

To średnia cena wykonania zlecenia ważona wolumenem. Wzór na **VWAP** ma tę samą postać. Rynkowy VWAP może jednak obejmować transakcje z całego wybranego okresu, a nie tylko jedno nasze zlecenie.

Jeżeli nie było wykonania, średnia cena nie jest określona. Nie dzielimy przez zero.

```cpp
double averagePrice = 0.0;
if (book.getAverageExecutionPrice(4, averagePrice)) {
    std::cout << averagePrice; // Dla przykladu powyzej: 102.2.
}
```

Funkcja odczytuje historię dla danego ID, uwzględnia zarówno kupno, jak i sprzedaż. Przy braku wykonań zwraca `false` i zostawia zmienną bez zmian. Anulowanie reszty nie usuwa już wykonanych transakcji ze średniej. Ponowne użycie ID łączy wyniki różnych zleceń o tym numerze.

### Slippage, czyli różnica względem ceny odniesienia

Najpierw trzeba powiedzieć, jaka jest cena odniesienia. Tutaj używamy najlepszej ceny po odpowiedniej stronie książki przed zleceniem.

Dla BUY:

```text
slippage = średnia cena kupna - początkowy best ask
         = 102.20 - 101
         = 1.20 na sztukę
```

Łączna różnica względem hipotetycznego kupna 25 sztuk po 101 wynosi 25 × 1.20 = 30. Taki zakup nie był jednak dostępny: na 101 czekało tylko 5 sztuk.

Dla SELL przy tej samej konwencji:

```text
slippage = początkowy best bid - średnia cena sprzedaży
```

W obu przypadkach dodatni wynik oznacza gorsze wykonanie. Inny punkt odniesienia, np. mid-price, da inny wynik.

```cpp
double referencePrice = 0.0;
bool hasReference = book.getBestAsk(referencePrice); // PRZED wyslaniem BUY.
book.addOrder({4, Side::Buy, OrderType::Limit, 103.0, 25});

double slippage = 0.0;
if (hasReference && book.getSlippage(4, Side::Buy, referencePrice, slippage)) {
    std::cout << slippage; // 1.2 na sztuke w opisanej ksiazce.
}
```

Dla SELL pobieramy wcześniej best bid. Funkcja nie zapisuje ceny odniesienia automatycznie — przekazuje ją wywołujący. Brak wykonań lub poprawnej ceny odniesienia daje `false`, a wynik pozostaje bez zmian. Ujemny wynik oznacza wykonanie lepsze od podanego odniesienia. Używaj właściwej strony i niepowtarzających się ID; funkcja nie odróżnia kolejnych zleceń używających tego samego numeru.

## 10. Market impact

Kupno z przykładu usunęło oferty na 101 i 102. Best ask zmienił się ze 101 na 103. To bezpośrednia zmiana książki spowodowana naszym zleceniem.

Slippage opisuje cenę wykonania względem punktu odniesienia. Market impact opisuje wpływ zlecenia na rynek. Są powiązane, ale nie są tym samym.

Nie modelujemy reakcji innych uczestników ani trwałego wpływu na cenę. Średnia cena i slippage mają już funkcje odczytu. Zmianę best ask/bid można zaobserwować przed zleceniem i po nim; osobnej funkcji mierzącej market impact nie ma.

## 11. Anulowanie zlecenia

```cpp
bool cancelled = book.cancelOrder(3);
```

- `true`: znaleziono aktywne zlecenie i usunięto jego pozostałą ilość.
- `false`: w książce nie ma takiego zlecenia, np. wcześniej je wykonano albo anulowano.

Anulowanie nie cofa transakcji. Jeśli z 20 sztuk sprzedano 10, anulujemy tylko pozostałe 10. Historia transakcji zostaje.

Indeks ID wskazuje węzeł listy, który usuwamy bez przepisywania pozostałych zleceń. Ich FIFO zostaje zachowane. Pusty poziom ceny usuwamy. Anulowanie działa też dla oczekującego StopMarket.

## 12. Modyfikowanie zlecenia

```cpp
book.modifyOrder(2, 101.0, 5);
```

Czytamy: „zmień aktywne zlecenie nr 2 na cenę 101 i pozostałą ilość 5”.

**Nowa ilość oznacza ilość jeszcze do wykonania, nie pierwotną ilość całkowitą.** Jeśli z 10 sztuk wykonano 4, zostało 6. Modyfikacja na 3 pozostawia kolejne 3 do wykonania.

| Zmiana | Priorytet |
| --- | --- |
| Ta sama cena, mniejsza ilość | Zachowuje miejsce w FIFO. |
| Ta sama cena i ilość | Zachowuje miejsce w FIFO. |
| Ta sama cena, większa ilość | Traci priorytet czasu. |
| Inna cena | Traci priorytet czasu. |

Przy utracie priorytetu anulujemy zlecenie, zmieniamy dane i dodajemy je ponownie z tym samym ID. Ponownie próbujemy matchingu. Dopiero niewykonana reszta dołącza na koniec kolejki przy swojej cenie.

Przykład:

```text
czeka SELL 10 @ 101
czeka BUY   5 @ 99

zmieniamy BUY na 5 @ 101
→ wykonanie 5 @ 101
→ zostaje SELL 5 @ 101
```

Nie zmieniamy strony ani typu. Brak aktywnego ID daje `false`. Ilość zero jest błędem; do usuwania zlecenia służy `cancelOrder`.

Modyfikacja nie cofa wcześniejszych wykonań. Jeśli zlecenie miało status `PARTIALLY_FILLED`, zmiana ceny lub ilości nie zmieni go na `NEW`. Po wykonaniu całej pozostałej ilości przejdzie na `FILLED`.

To zasady przyjęte w projekcie, a nie uniwersalna reguła wszystkich giełd.

## 13. Historia i wydruk

```cpp
struct Trade {
    std::uint64_t id;
    int buyOrderId;
    int sellOrderId;
    double price;
    int quantity;
    std::int64_t timestampMs;
};
```

Historia znajduje się w `std::vector<Trade> trades`. Jeden przychodzący order może utworzyć wiele transakcji, gdy trafia na kilka oczekujących zleceń.

```cpp
book.printOrderBook(); // Aktualne zlecenia, zsumowane wedlug ceny.
book.printTrades();    // Cala dotychczasowa historia tej ksiazki.
```

`getTrades()` pozwala odczytać historię bez wypisywania jej. Matching zapisuje dane, a funkcje `print...` je wyświetlają.

`buyOrderId` i `sellOrderId` to numery zleceń. Osobne pole `id` jest numerem transakcji. Każdy OrderBook numeruje swoje transakcje od 1. Dodanie, anulowanie lub modyfikacja bez wykonania nie zużywa numeru transakcji.

`timestampMs` to czas wykonania w milisekundach od 1 stycznia 1970, czyli timestamp Unix. Pobieramy go z `std::chrono::system_clock` w funkcji `recordTrade()`. Czas zapisuje się raz przy wykonaniu, a nie przy każdym wydruku historii.

Przykładowy wiersz:

```text
TRADE #1 | BUY #4 <-> SELL #1 | QTY: 5 | PRICE: 101 | UNIX_MS: 1791113234626
```

Kilka transakcji może dostać ten sam timestamp, jeśli nastąpiły w tej samej milisekundzie. Ich kolejność w książce określają kolejne ID. Zegar systemowy może zostać skorygowany, więc timestamp nie służy nam do ustalania FIFO ani do mierzenia opóźnień. FIFO nadal wynika z kolejności dodania do kolejki.

### Statusy zleceń

Statusy przechowujemy osobno w `std::map<int, OrderStatus> statuses`. Kluczem jest ID zlecenia. Dzięki temu status można sprawdzić także po usunięciu zlecenia z książki.

| Wydruk | Wartość w C++ | Znaczenie |
| --- | --- | --- |
| `NEW` | `OrderStatus::New` | Zlecenie przyjęte, jeszcze nic nie wykonano; stop może czekać na aktywację. |
| `PARTIALLY_FILLED` | `OrderStatus::PartiallyFilled` | Część wykonano, reszta nadal czeka. |
| `FILLED` | `OrderStatus::Filled` | Całą ilość wykonano. |
| `CANCELLED` | `OrderStatus::Cancelled` | Niewykonaną resztę anulowano. |
| `UNKNOWN` | `OrderStatus::Unknown` | Nie znamy tego ID. |

```cpp
OrderStatus status = book.getOrderStatus(3);
if (status == OrderStatus::PartiallyFilled) {
    std::cout << "Czesc zlecenia nadal czeka\n";
}
book.printOrderStatus(3);
```

Przykład SELL na 20 sztuk:

```text
Dodanie             → NEW
Sprzedaż 10 sztuk    → PARTIALLY_FILLED
Anulowanie reszty    → CANCELLED
```

Gdyby zamiast anulowania sprzedano pozostałe 10, status końcowy byłby `FILLED`. Ponowne anulowanie zlecenia zakończonego zwraca `false` i nie zmienia jego statusu.

Trzymamy ostatni status, a nie pełną historię zmian statusu. Odrzucone przy dodawaniu zlecenie nie dostaje statusu `NEW`.

## 14. Scenariusze w main.cpp

Pierwsza książka:

1. Dodaje SELL: 5 @ 101, 10 @ 102, 20 @ 103.
2. Dodaje BUY 25 @ 103.
3. Wykonuje 5 @ 101, 10 @ 102 i 10 @ 103.
4. Pokazuje pozostałe SELL 10 @ 103.
5. Anuluje pozostałą część zlecenia nr 3.
6. Ponowne anulowanie nr 3 daje `NOT FOUND`.

Program pokazuje też status nr 3: `NEW`, potem `PARTIALLY_FILLED`, a po anulowaniu `CANCELLED`. Zlecenia nr 1 i 4 są `FILLED`. Odczyt nr 999 daje `UNKNOWN`. Po anulowaniu ostatniego zlecenia podsumowanie rynku pokazuje brak obu cen i spreadu.

Druga książka, `modifyExample`, jest niezależna:

1. Dodaje SELL 10 @ 101 i BUY 5 @ 99.
2. Podnosi limit BUY do 101.
3. Wykonuje 5 @ 101 i pozostawia SELL 5 @ 101.

Przed modyfikacją podsumowanie pokazuje bid 99, ask 101 i spread 2. Po wykonaniu BUY znika z książki: pozostaje ask 101, ale nie ma już bidu ani spreadu.

Każdy obiekt OrderBook ma własne zlecenia i historię.

Dalsze przykłady pokazują odczyt wolumenu, lookup ID, częściowe IOC, anulowane i wykonane FOK oraz Stop BUY aktywowany przy 101, który kupuje po 102. Bez argumentów wszystkie te przykłady uruchamiają się kolejno.

## 15. Walidacja i numery zleceń

Dodawanie odrzuca przez `std::invalid_argument`:

- ilość mniejszą lub równą zero;
- nieprawidłową stronę lub typ;
- cenę Limit niedodatnią, nieskończoną albo NaN;
- niepoprawny próg StopMarket;
- nieprawidłowy TimeInForce lub StopMarket z IOC/FOK;
- ID już aktywne w książce lub wśród stopów.

Modyfikacja także wymaga dodatniej ilości i dodatniej, skończonej ceny. Sprawdza dane przed wyszukaniem ID, więc błędne dane powodują wyjątek również dla nieistniejącego ID.

Kod nie wymaga dodatniego ID. Sprawdza unikalność tylko wśród aktywnych zleceń. Po anulowaniu lub pełnym wykonaniu można technicznie użyć numeru ponownie. Dla czytelnej historii nadawaj jednak kolejne, niepowtarzające się ID.

Statusy pozwalają odróżnić zlecenie anulowane od wykonanego. Jeśli jednak ponownie użyjesz nieaktywnego ID, zapisany status zostanie zastąpiony statusem nowego zlecenia. Stare transakcje pozostaną w historii z tym samym ID zlecenia. Dlatego niepowtarzające się ID ułatwiają odczyt wyników.

## 16. Ważne szczegóły C++

- `addOrder(Order order)` dostaje kopię zlecenia. Zmniejszanie jej ilości nie zmienia obiektu przekazanego przez użytkownika.
- `matchBuyOrder(Order& order)` dostaje referencję do tej kopii i może zmieniać jej ilość.
- `it->first` to cena w mapie, `it->second` to kolejka na tej cenie.
- `front()` daje pierwszy element listy, `push_back()` dodaje na końcu, `pop_front()` usuwa pierwszy.
- Iterator wskazuje element listy. Usuwanie innych elementów nie unieważnia tego wskazania.
- `splice()` przenosi węzeł listy bez kopiowania zlecenia i zmiany iteratora — używamy go do aktywacji stopów.
- `erase(it)` usuwa poziom ceny z mapy.
- `const` przy funkcji oznacza, że nie zmienia ona stanu książki.
- `getTrades()` zwraca stałą referencję: odczytujemy historię bez kopiowania wektora i bez możliwości zmiany danych przez tę referencję.

Cena jest typu `double`. Ułamki dziesiętne nie zawsze dają się zapisać dokładnie, a kod porównuje ceny bez tolerancji. Później możemy przejść na całkowitą liczbę ticków, czyli jednostek minimalnego kroku ceny.

Sumowanie ilości na poziomie używa `std::int64_t`, ponieważ suma kilku wartości `int` może przekroczyć zakres pojedynczego `int`.

## 17. Testy

W Developer Command Prompt for Visual Studio przejdź do folderu z plikiem rozwiązania i uruchom:

```bat
run-tests.cmd
```

Skrypt kompiluje testy do folderu `build` i je uruchamia. Poprawny wynik:

```text
All matching engine regression tests passed.
Index, replay and multithreading tests passed.
```

Testy obejmują obie strony rynku, FIFO, częściowe wykonania, limity cen, Market na wielu poziomach i pustej książce, anulowanie, modyfikowanie, walidację i duże sumy ilości.

Sprawdzają także przejścia statusów, zachowanie statusu po modyfikacji, kolejne ID i czas transakcji oraz best bid, best ask i spread po wykonaniu, anulowaniu i modyfikacji. Pusta lub jednostronna książka jest sprawdzana osobno.

Testy obejmują też wolumen, średnią wykonania, slippage, IOC/FOK, aktywację i kaskady stopów, duży indeks ID, błędny CSV, zgodność replayu zwykłego i wielowątkowego, FIFO przy czterech producentach, przekazywanie wyjątków oraz opróżnienie kolejki przy zamykaniu workera.

## 18. Co jest gotowe, a co dalej?

| Funkcja | Stan |
| --- | --- |
| Limit i Market | Gotowe |
| Price-time priority i partial fills | Gotowe |
| Anulowanie i modyfikacja | Gotowe |
| Historia transakcji | Gotowa, z osobnymi ID i timestampami |
| Statusy zleceń | Gotowe |
| ID i czas transakcji | Gotowe |
| Best bid, best ask i spread jako funkcje | Gotowe |
| Odczyt wolumenu poziomów jako funkcja | Gotowy |
| Średnia cena wykonania i slippage | Gotowe |
| Stop Market, IOC i FOK | Gotowe |
| Indeks ID i szybsze anulowanie | Gotowe |
| Benchmark i profil czasu operacji | Gotowe |
| Replay zdarzeń z CSV | Gotowy |
| Wielowątkowe przyjmowanie poleceń | Gotowe; matching jednej książki wykonuje jeden worker |

Nie ma jeszcze kont, kontroli środków, prowizji, wielu instrumentów ani trwałego zapisu. Po zakończeniu programu stan znika.

Indeks ID ma przeciętny koszt odczytu O(1). Usunięcie wskazanego węzła listy jest O(1), ale odszukanie poziomu ceny w mapie nadal kosztuje O(log P), gdzie P to liczba poziomów. Zmniejszenie ilości przy tej samej cenie nie przepisuje listy. Sprawdzanie progów stopów pozostaje liniowe. Kod jest modelem do nauki, a nie produkcyjnym systemem HFT.

## 19. Replay z CSV

Przykład `data/replay.csv` zawiera syntetyczne zdarzenia zleceń. Żeby wykorzystać dane z innego źródła, trzeba przekształcić je do tego formatu. Same historyczne ceny transakcji nie odtworzą pełnego order booka.

```csv
sequence,action,id,side,type,price,quantity,tif
1,ADD,1,SELL,LIMIT,101,5,GTC
2,ADD,2,BUY,MARKET,0,2,IOC
3,MODIFY,1,102,4
4,CANCEL,1
```

ADD ma 8 pól, MODIFY 5, CANCEL 3. `sequence` musi być dodatni i ściśle rosnący; może mieć przerwy. BUY/SELL określa stronę, LIMIT/MARKET/STOP_MARKET typ, GTC/IOC/FOK warunek wykonania. MODIFY ustawia nową cenę/próg i pozostałą ilość.

Nagłówek jest opcjonalny. Puste linie i linie zaczynające się od # są pomijane. Cena używa kropki dziesiętnej; cytowane pola zawierające przecinek nie są obsługiwane. Parser sprawdza format całego pliku przed wykonaniem pierwszego polecenia i podaje numer błędnej linii.

```bat
build\MatchingEngineDemo.exe --replay data\replay.csv
build\MatchingEngineDemo.exe --replay-threaded data\replay.csv
```

Oba tryby dają na dołączonym pliku **6 transakcji** i pustą książkę na końcu. Replay nie symuluje przerw czasowych. Timestampy pochodzą z zegara bieżącego uruchomienia, więc mogą się różnić.

NOT FOUND oznacza nieznane aktywne ID przy CANCEL/MODIFY. OK oznacza przetworzenie polecenia, nie pełne wykonanie zlecenia — FOK może zakończyć się anulowaniem. Inny błąd wykonania kończy replay z numerem zdarzenia i kodem wyjścia 1. Wcześniejsze zdarzenia mogły już zostać wykonane; nie ma wycofywania całego replayu.

## 20. Wątki i zachowanie FIFO

`EngineWorker` posiada OrderBook, kolejkę poleceń i jeden wątek wykonujący matching. Producenci mogą jednocześnie wywoływać `submit()`, ale nie dotykają bezpośrednio książki.

```cpp
EngineWorker worker;
EngineCommand command;
command.action = EngineAction::Add;
command.order = {1, Side::Buy, OrderType::Limit, 100.0, 5};
EngineResult result = worker.submit(command).get();
worker.close();
```

Do przykładu dołącz nagłówek `EngineWorker.h`. Mechanizm działa tak:

1. Mutex chroni przyjęcie polecenia i nadanie sequence. Wcześniej przyjęte polecenie trafia wcześniej do kolejki.
2. Worker pobiera polecenia FIFO i wykonuje je po jednym.
3. Condition variable usypia worker, gdy nie ma pracy.
4. Future zwraca wynik; `get()` czeka na zakończenie lub przekazuje wyjątek.
5. `close()` kończy już przyjęte zadania i dołącza wątek. Następne zgłoszenia są odrzucane.

Sequence = 0 oznacza nadanie numeru przez worker. Jawny numer musi być większy od ostatnio przyjętego. Kolejność przyjęcia od wielu producentów może różnić się między uruchomieniami. Po zapisaniu przydzielonych numerów odtworzenie tej kolejności daje te same transakcje i FIFO, poza timestampami zegarowymi.

`EngineResult` zawiera wynik polecenia, status, kopię aktywnego zlecenia, najlepsze ceny i nowe transakcje. Polecenie Snapshot zwraca całą historię transakcji. Odczyt odbywa się w workerze, więc nie ściga się z modyfikacją książki.

Bezpośredni OrderBook nie jest thread-safe. Nie wykonujemy równoległego matchingu tej samej książki. `close()` wywołuje właściciel workera, nie kilka wątków równocześnie. Obiekt musi żyć do zakończenia producentów.

## 21. Benchmark i profil czasu operacji

W Developer Command Prompt, z głównego folderu projektu:

```bat
run-benchmarks.cmd benchmarks\current.csv
```

Skrypt kompiluje z /O2 i mierzy obecną wersję. `benchmarks/before.csv` oraz `benchmarks/after.csv` zachowują pomiary sprzed i po zmianie `queue` na listy z indeksem ID. Benchmark używa `steady_clock`, który nie cofa się; timestampy transakcji nadal używają `system_clock`.

Po rozgrzewce wykonuje trzy przebiegi dla 1000, 5000 i 10 000 zleceń. CSV zawiera sumę zmierzonych czasów oraz p50, p95 i p99 w nanosekundach. Profil rozdziela dodawanie, modyfikację, anulowanie i matching. To pomiar faz, nie próbkowanie stosów CPU.

Średnia z trzech przebiegów, po 10 000 operacji:

| Faza | Przed, ms | Po, ms |
| --- | ---: | ---: |
| Dodawanie | 2938.64 | 3.80 |
| Modyfikacja ilości bez zmiany ceny | 12729.00 | 0.80 |
| Anulowanie | 6090.43 | 1.71 |
| Matching pojedynczych sztuk | 2837.33 | 4.68 |

To syntetyczny scenariusz z jedną ceną w danej fazie; anulowanie i modyfikacja idą od końca. Setup matchingu, sortowanie próbek i wydruk są poza pomiarem jego operacji. Wyniki obejmują koszt zegara; bardzo krótkie próbki mogą mieć wartość zero z powodu jego rozdzielczości.

Pomiary wykonano na tym samym komputerze z tym samym programem i opcjami kompilacji. Nie obejmują CSV, workerów ani dużej liczby stopów i nie gwarantują takich opóźnień przy innym obciążeniu. Aktualny skrypt nie przywraca starego kodu. System operacyjny, alokacje i obciążenie komputera wpływają na wynik.
