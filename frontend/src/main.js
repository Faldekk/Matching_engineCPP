import './style.css';
import { startPriceChart } from './price-chart.js';

let stopChart;
try {
  stopChart = startPriceChart();
} catch {
  document.getElementById('chart-status').textContent = 'Nie udało się uruchomić wykresu. Odśwież stronę.';
}

const element = (id) => document.getElementById(id);
const money = new Intl.NumberFormat('pl-PL', { minimumFractionDigits: 2, maximumFractionDigits: 2 });
const coins = new Intl.NumberFormat('pl-PL', { minimumFractionDigits: 8, maximumFractionDigits: 8 });
const profit = new Intl.NumberFormat('pl-PL', { minimumFractionDigits: 2, maximumFractionDigits: 6 });
let state = null;
let side = 'BUY';
let busy = false;
let lastSuccess = 0;
let generation = 0;
let refreshPromise = null;
let apiAvailable = false;
let symbol = 'BTCUSDT';
const assetName = () => symbol.replace('USDT', '');

function quantityValue() {
  const text = element('quantity').value.trim().replace(',', '.');
  if (!/^\d+(\.\d+)?$/.test(text)) return NaN;
  return Number(text);
}

function tradingReady() {
  return apiAvailable && state?.market.symbol === symbol && state.market.ready && state.wallet.tradingEnabled && performance.now() - lastSuccess < 3500;
}

function updateForm() {
  const quantity = quantityValue();
  const valid = Number.isFinite(quantity) && quantity > 0;
  const ready = tradingReady();
  element('submit').disabled = busy || !ready || !valid;
  element('submit').textContent = busy ? 'Wysyłanie…' : `${side === 'BUY' ? 'Kup' : 'Sprzedaj'} ${assetName()}`;
  element('market-select').disabled = busy;
  element('submit').classList.toggle('sell', side === 'SELL');
  element('buy').disabled = busy;
  element('sell').disabled = busy;
  element('quantity').disabled = busy;
  const conditionalReady = ready && Array.isArray(state?.conditionalOrders);
  element('conditional-submit').disabled = busy || !conditionalReady;
  for (const id of ['conditional-type', 'conditional-quantity', 'conditional-price']) element(id).disabled = busy;
  element('order-hint').textContent = !ready ? 'Handel zablokowany — brak świeżych danych.' : 'Wykonanie po dostępnych cenach rynku · bez prowizji';
  element('estimate-label').textContent = side === 'BUY' ? 'Szacowany koszt' : 'Szacowany wpływ';
  const price = side === 'BUY' ? state?.market.asks[0]?.price : state?.market.bids[0]?.price;
  element('estimate').textContent = valid && ready && price ? `${money.format(quantity * price)} USDT*` : '— USDT';
  element('estimate').title = 'Przybliżenie po najlepszej cenie. Duże zlecenie może przejść przez kilka poziomów.';
}

function renderLevels(id, levels, buy) {
  const rows = [];
  const max = Math.max(0, ...levels.map((level) => level.quantity));
  for (let i = 0; i < 5; i++) {
    const row = document.createElement('tr');
    const price = document.createElement('td');
    const quantity = document.createElement('td');
    const level = levels[i];
    price.textContent = level ? money.format(level.price) : '—';
    quantity.textContent = level ? coins.format(level.quantity) : '—';
    price.className = buy ? 'buy-text' : 'sell-text';
    row.className = 'depth';
    row.style.setProperty('--depth', `${level && max ? 80 * level.quantity / max : 0}%`);
    row.style.setProperty('--depth-color', buy ? '#20383066' : '#422a3066');
    row.append(price, quantity);
    rows.push(row);
  }
  element(id).replaceChildren(...rows);
}

function renderHistory(fills) {
  element('history').closest('table').classList.toggle('empty', fills.length === 0);
  element('fill-count').textContent = fills.length;
  const rows = [];
  for (let i = fills.length - 1; i >= Math.max(0, fills.length - 20); i--) {
    const fill = fills[i];
    const row = document.createElement('tr');
    const values = [String(i + 1), `${(fill.symbol || 'BTCUSDT').replace('USDT', '')} / USDT`, fill.side === 'BUY' ? 'Kupno' : 'Sprzedaż', money.format(fill.price), coins.format(fill.quantity), money.format(fill.price * fill.quantity)];
    for (let c = 0; c < values.length; c++) {
      const cell = document.createElement('td');
      if (c === 2) {
        const tag = document.createElement('span');
        tag.className = `side-tag ${fill.side === 'SELL' ? 'sell' : ''}`;
        tag.textContent = values[c];
        cell.append(tag);
      } else cell.textContent = values[c];
      row.append(cell);
    }
    rows.push(row);
  }
  if (!rows.length) {
    const row = document.createElement('tr');
    const cell = document.createElement('td');
    cell.colSpan = 6;
    cell.className = 'empty-cell';
    cell.textContent = 'Jeszcze bez transakcji. Twoje pierwsze wykonanie pojawi się tutaj.';
    row.append(cell);
    rows.push(row);
  }
  element('history').replaceChildren(...rows);
}

function renderMarket() {
  const ready = tradingReady();
  renderLevels('bids', ready ? state.market.bids : [], true);
  renderLevels('asks', ready ? state.market.asks : [], false);
  element('best-price').textContent = ready && state.market.bids[0] ? money.format(state.market.bids[0].price) : '—';
  element('spread').textContent = ready && state.market.spread !== null ? `${money.format(state.market.spread)} USDT` : '—';
  element('update').textContent = ready ? `Aktualizacja #${state.market.updateId}` : 'Czekamy na świeże dane rynku';
}

function renderState() {
  const wallet = state.wallet;
  element('usdt').textContent = money.format(wallet.usdt);
  const position = wallet.positions?.find((position) => position.symbol === symbol);
  element('btc').textContent = coins.format(position?.quantity ?? wallet.btc);
  element('equity').textContent = wallet.equity === null ? '—' : money.format(wallet.equity);
  element('pnl').textContent = `${wallet.realizedPnl > 0 ? '+' : ''}${profit.format(wallet.realizedPnl)}`;
  element('pnl').className = wallet.realizedPnl > 0 ? 'positive' : wallet.realizedPnl < 0 ? 'negative' : '';
  element('valuation-note').textContent = wallet.equity === null ? 'Brak świeżej ceny posiadanego aktywa' : 'Łączna wycena BTC, ETH, SOL i USDT';
  renderPortfolio(wallet);
  renderConditionalOrders();
  element('conditional-message').textContent = Array.isArray(state.conditionalOrders)
    ? `Dostępne: ${money.format(wallet.availableUsdt)} USDT · zarezerwowane: ${money.format(wallet.reservedUsdt)} USDT. Wybrane aktywo: ${coins.format(position?.available ?? 0)} dostępne, ${coins.format(position?.reserved ?? 0)} zarezerwowane.`
    : 'Reguły wymagają uruchomienia nowej wersji serwera.';
  renderHistory(state.fills);
  renderMarket();
  updateForm();
}

function renderConditionalOrders() {
  const rows = [];
  const labels = { NEW: 'Oczekuje', PARTIALLY_FILLED: 'Częściowo wykonane', FILLED: 'Wykonane', CANCELLED: 'Anulowane' };
  for (const order of [...(state?.conditionalOrders || [])].reverse()) {
    const row = document.createElement('tr');
    const values = [order.id, order.symbol.replace('USDT', '/USDT'), order.type === 'BUY_LIMIT' ? 'Buy Limit' : 'Stop Loss', money.format(order.price), `${coins.format(order.quantity)} / ${coins.format(order.filled)}`, `${labels[order.status]}${order.triggered && order.status !== 'FILLED' ? ' · stop aktywowany' : ''}`];
    for (const value of values) { const cell = document.createElement('td'); cell.textContent = value; row.append(cell); }
    const actions = document.createElement('td');
    if (order.status === 'NEW' || order.status === 'PARTIALLY_FILLED') {
      const button = document.createElement('button');
      button.textContent = 'Anuluj';
      button.disabled = busy || !apiAvailable || performance.now() - lastSuccess >= 3500;
      button.onclick = () => sendConditional(`/api/conditional-orders/${order.id}/cancel`, {});
      actions.append(button);
    }
    row.append(actions); rows.push(row);
  }
  element('conditional-orders').replaceChildren(...rows);
}

async function sendConditional(url, body) {
  if (busy) return;
  busy = true; generation++; updateForm(); renderConditionalOrders();
  try {
    const response = await fetch(url, { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(body), signal: AbortSignal.timeout(8000) });
    const result = await response.json();
    element('conditional-feedback').textContent = result.ok ? (result.id ? `Ustawiono regułę #${result.id}. Sprawdź jej status w tabeli.` : 'Reguła anulowana. Rezerwacja zwolniona.') : `Odrzucono: ${result.error}`;
  } catch {
    element('conditional-feedback').textContent = 'Nie udało się potwierdzić operacji. Sprawdź listę reguł przed ponowieniem.';
  } finally {
    if (refreshPromise) await refreshPromise;
    await refreshState(); busy = false; updateForm(); renderConditionalOrders();
  }
}

element('conditional-form').addEventListener('submit', (event) => {
  event.preventDefault();
  if (!tradingReady() || !Array.isArray(state?.conditionalOrders)) return;
  const quantity = Number(element('conditional-quantity').value.trim().replace(',', '.'));
  const price = Number(element('conditional-price').value.trim().replace(',', '.'));
  if (!Number.isFinite(quantity) || !Number.isFinite(price) || quantity <= 0 || price <= 0) { showMessage('Podaj dodatnią ilość i cenę.', true); return; }
  sendConditional('/api/conditional-orders', { symbol, type: element('conditional-type').value, quantity, price });
});

function renderPortfolio(wallet) {
  const rows = [];
  const positions = [{ symbol: 'USDT', quantity: wallet.usdt, value: wallet.usdt, weight: wallet.cashWeight, realizedPnl: 0 }, ...(wallet.positions || [])];
  for (const position of positions) {
    const row = document.createElement('tr');
    const weight = position.weight == null ? '—' : `${position.weight.toFixed(1)}%`;
    const values = [position.symbol.replace(/USDT$/, '') || 'USDT', coins.format(position.quantity), position.value == null ? '—' : money.format(position.value), weight, position.symbol === 'USDT' ? '—' : profit.format(position.realizedPnl)];
    for (const value of values) { const cell = document.createElement('td'); cell.textContent = value; row.append(cell); }
    rows.push(row);
  }
  element('portfolio').replaceChildren(...rows);
}

function hidePortfolioValuation() {
  if (!state) return;
  renderPortfolio({ ...state.wallet, cashWeight: null,
    positions: state.wallet.positions.map((position) => ({ ...position, value: null, weight: null })) });
}

function setConnection(ready, message) {
  element('connection').textContent = message;
  element('connection').className = `status ${ready ? 'ready' : 'offline'}`;
}

async function refreshState() {
  if (refreshPromise) return refreshPromise;
  const startedGeneration = generation;
  refreshPromise = (async () => {
    try {
      const response = await fetch(`/api/state?symbol=${symbol}`, { cache: 'no-store', signal: AbortSignal.timeout(5000) });
      if (!response.ok) throw new Error('API niedostępne');
      const next = await response.json();
      if (!next.market || !next.wallet || !Array.isArray(next.fills)) throw new Error('Nieprawidłowe dane API');
      // Odczyt rozpoczęty przed wysłaniem zlecenia nie może nadpisać nowego stanu.
      if (startedGeneration !== generation) return;
      if (next.market.symbol !== symbol || !Array.isArray(next.wallet.positions)) throw new Error('Uruchom ponownie serwer — działa starsza wersja');
      state = next;
      apiAvailable = true;
      lastSuccess = performance.now();
      renderState();
      setConnection(tradingReady(), tradingReady() ? 'Rynek połączony' : 'Czekamy na rynek');
    } catch (error) {
      lastSuccess = 0;
      apiAvailable = false;
      setConnection(false, error.message.includes('starsza wersja') ? error.message : 'API niedostępne');
      renderMarket();
      updateForm();
      element('equity').textContent = '—';
      hidePortfolioValuation();
      renderConditionalOrders();
      element('valuation-note').textContent = 'Brak połączenia — saldo pokazuje ostatni odczyt';
    }
  })();
  try { await refreshPromise; } finally { refreshPromise = null; }
}

function showMessage(text, error = false) {
  element('order-message').hidden = false;
  element('order-message').className = `order-message ${error ? 'error' : ''}`;
  element('order-message').textContent = text;
}

function selectSide(nextSide) {
  side = nextSide;
  element('buy').classList.toggle('selected', side === 'BUY');
  element('sell').classList.toggle('selected', side === 'SELL');
  element('buy').setAttribute('aria-pressed', String(side === 'BUY'));
  element('sell').setAttribute('aria-pressed', String(side === 'SELL'));
  updateForm();
}

element('buy').addEventListener('click', () => selectSide('BUY'));
element('sell').addEventListener('click', () => selectSide('SELL'));
element('quantity').addEventListener('input', updateForm);
element('market-select').addEventListener('change', async () => {
  if (busy) return;
  symbol = element('market-select').value;
  generation++;
  lastSuccess = 0;
  element('quantity').value = '';
  element('order-message').hidden = true;
  const asset = assetName();
  for (const icon of document.querySelectorAll('.bitcoin, .bitcoin-small')) icon.textContent = { BTC: '₿', ETH: '◇', SOL: '◎' }[asset];
  for (const label of document.querySelectorAll('[data-asset]')) label.textContent = asset;
  element('asset-title').textContent = { BTC: 'Bitcoin', ETH: 'Ethereum', SOL: 'Solana' }[asset] + ' · Binance Spot';
  element('chart-title').textContent = `${asset} / USDT · wykres live`;
  element('chart-caption').textContent = `Świece 1 min · czas UTC · wolumen w ${asset}`;
  element('price-chart').setAttribute('aria-label', `Wykres ceny i wolumenu ${asset}/USDT`);
  element('chart-legend').textContent = 'Czekamy na świece';
  element('chart-status').textContent = 'Łączenie z Binance…';
  stopChart?.();
  stopChart = startPriceChart(symbol);
  element('candles').setAttribute('aria-pressed', 'true');
  element('line').setAttribute('aria-pressed', 'false');
  element('btc').textContent = '—';
  renderMarket(); updateForm();
  if (refreshPromise) await refreshPromise;
  await refreshState();
});
element('order-form').addEventListener('submit', async (event) => {
  event.preventDefault();
  const quantity = quantityValue();
  if (busy || !tradingReady() || !Number.isFinite(quantity) || quantity <= 0) return;
  busy = true;
  generation++;
  updateForm();
  element('order-message').hidden = true;
  try {
    // POST wysyłamy tylko raz. Timeout nie oznacza, że serwer nie wykonał transakcji.
    const response = await fetch('/api/orders', {
      method: 'POST', headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ side, quantity, symbol }), signal: AbortSignal.timeout(8000),
    });
    const result = await response.json();
    if (!response.ok || !result.ok) {
      showMessage(`Zlecenie odrzucone: ${result.error || 'nieprawidłowa odpowiedź serwera'}.`, true);
    } else {
      const executed = result.filled > 0 ? ` po średniej cenie ${money.format(result.averagePrice)} USDT` : '';
      showMessage(`${side === 'BUY' ? 'Kupiono' : 'Sprzedano'} ${coins.format(result.filled)} ${assetName()}${executed}. Anulowano ${coins.format(result.cancelled)} ${assetName()}.${result.filled > 0 ? ` Slippage: ${money.format(result.slippage)} USDT/${assetName()}.` : ''}`);
    }
  } catch {
    showMessage('Nie udało się potwierdzić wykonania. Sprawdź historię po odzyskaniu połączenia przed ponowieniem zlecenia.', true);
  } finally {
    if (refreshPromise) await refreshPromise;
    await refreshState();
    busy = false;
    updateForm();
  }
});

renderLevels('bids', [], true);
renderLevels('asks', [], false);
renderHistory([]);
updateForm();
// Na tym etapie pobieramy stan HTTP co sekundę. WebSocket jest kolejnym zadaniem planu.
async function poll() { await refreshState(); setTimeout(poll, 1000); }
poll();
setInterval(() => {
  if (state && !tradingReady()) {
    renderMarket();
    updateForm();
    setConnection(false, apiAvailable ? 'Brak świeżych danych' : 'API niedostępne');
    if (!apiAvailable || performance.now() - lastSuccess >= 3500) {
      element('equity').textContent = '—';
      hidePortfolioValuation();
      renderConditionalOrders();
      element('valuation-note').textContent = 'Brak świeżej ceny — saldo pokazuje ostatni odczyt';
    }
  }
}, 1000);
