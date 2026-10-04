import { createChart, CandlestickSeries, LineSeries, HistogramSeries } from 'lightweight-charts';
import { parseHistory, parseLive, mergeCandles } from './chart-data.js';

export function startPriceChart(symbol = 'BTCUSDT') {
  const asset = symbol.replace('USDT', '');
  const element = (id) => document.getElementById(id);
  const status = element('chart-status');
  const chart = createChart(element('price-chart'), {
    autoSize: true,
    layout: { background: { color: '#161d22' }, textColor: '#83929c', attributionLogo: true },
    grid: { vertLines: { color: '#222b31' }, horzLines: { color: '#222b31' } },
    timeScale: { timeVisible: true, secondsVisible: false },
    localization: { locale: 'pl-PL' },
    rightPriceScale: { scaleMargins: { top: 0.08, bottom: 0.25 } },
  });
  const candles = chart.addSeries(CandlestickSeries, { upColor: '#45d3a5', downColor: '#f48382', borderVisible: false, wickUpColor: '#45d3a5', wickDownColor: '#f48382' });
  const line = chart.addSeries(LineSeries, { color: '#45d3a5', lineWidth: 2, visible: false });
  const volume = chart.addSeries(HistogramSeries, { priceScaleId: 'volume', priceFormat: { type: 'volume' }, lastValueVisible: false, priceLineVisible: false });
  volume.priceScale().applyOptions({ scaleMargins: { top: 0.82, bottom: 0 }, visible: false });
  const price = new Intl.NumberFormat('pl-PL', { minimumFractionDigits: 2, maximumFractionDigits: 2 });
  const amount = new Intl.NumberFormat('pl-PL', { maximumFractionDigits: 4 });
  let data = [];
  let socket;
  let lastTick = 0;
  let connectedAt = 0;
  let loading = false;
  let historyAvailable = false;
  let hovering = false;
  let stopped = false;
  let reconnectTimer;

  function showLegend(candle) {
    if (!candle) return;
    const time = new Date(candle.time * 1000).toISOString().slice(11, 16);
    element('chart-legend').textContent = `${time} UTC · Otwarcie ${price.format(candle.open)} · Maksimum ${price.format(candle.high)} · Minimum ${price.format(candle.low)} · Zamknięcie ${price.format(candle.close)} USDT · Wolumen ${amount.format(candle.volume)} ${asset}`;
  }
  function volumePoint(candle) {
    return { time: candle.time, value: candle.volume, color: candle.close >= candle.open ? '#45d3a577' : '#f4838277' };
  }
  function setData() {
    candles.setData(data);
    line.setData(data.map((candle) => ({ time: candle.time, value: candle.close })));
    volume.setData(data.map(volumePoint));
    if (!hovering) showLegend(data.at(-1));
  }
  chart.subscribeCrosshairMove((event) => {
    hovering = Boolean(event.time);
    showLegend(hovering ? data.find((candle) => candle.time === event.time) : data.at(-1));
  });
  for (const id of ['candles', 'line']) {
    element(id).onclick = () => {
      candles.applyOptions({ visible: id === 'candles' });
      line.applyOptions({ visible: id === 'line' });
      element('candles').setAttribute('aria-pressed', String(id === 'candles'));
      element('line').setAttribute('aria-pressed', String(id === 'line'));
    };
  }
  element('chart-reset').onclick = () => chart.timeScale().fitContent();

  async function loadHistory(current) {
    loading = true;
    // Bufor zawiera nowsze aktualizacje WS; nie zastępujemy ich starszą odpowiedzią HTTP.
    const live = [];
    pending = live;
    try {
      const response = await fetch(`/market-data/api/v3/klines?symbol=${symbol}&interval=1m&limit=180`, { signal: AbortSignal.timeout(10000) });
      if (!response.ok) throw new Error('Historia niedostępna');
      const history = parseHistory(await response.json());
      if (stopped || current !== socket) return;
      const firstLoad = data.length === 0;
      data = mergeCandles(history, live);
      historyAvailable = true;
      setData();
      if (firstLoad) chart.timeScale().fitContent();
    } catch {
      if (stopped || current !== socket) return;
      historyAvailable = false;
      data = mergeCandles(data, live);
      setData();
    } finally {
      if (stopped || current !== socket) return;
      loading = false;
      pending = [];
      showStatus();
    }
  }
  let pending = [];
  function showStatus() {
    const fresh = socket?.readyState === WebSocket.OPEN && lastTick > 0 && performance.now() - lastTick < 12000;
    status.textContent = fresh ? `Na żywo · Binance${historyAvailable ? ' · ostatnie 3 godziny przy starcie' : ' · historia niedostępna, tylko odebrane świece'}` : 'Brak świeżych danych · wykres zatrzymany · ponawiam połączenie…';
    status.classList.toggle('buy-text', fresh);
    status.classList.toggle('sell-text', !fresh);
  }
  function connect() {
    if (stopped) return;
    socket = new WebSocket(`wss://data-stream.binance.vision:443/ws/${symbol.toLowerCase()}@kline_1m`);
    connectedAt = performance.now();
    const current = socket;
    lastTick = 0;
    current.onopen = () => { if (current === socket) { connectedAt = performance.now(); loadHistory(current); } };
    current.onmessage = (event) => {
      if (current !== socket) return;
      let candle;
      try { candle = parseLive(JSON.parse(event.data), symbol); } catch { return; }
      if (!candle || (data.length && candle.time < data.at(-1).time)) return;
      lastTick = performance.now();
      if (loading) pending.push(candle);
      else {
        if (data.at(-1)?.time === candle.time) data[data.length - 1] = candle;
        else data.push(candle);
        if (data.length > 500) { data.shift(); setData(); }
        else { candles.update(candle); line.update({ time: candle.time, value: candle.close }); volume.update(volumePoint(candle)); }
        if (!hovering) showLegend(candle);
      }
      showStatus();
    };
    current.onerror = () => current.close();
    current.onclose = () => {
      if (current !== socket || stopped) return;
      showStatus();
      reconnectTimer = setTimeout(connect, 3000);
    };
  }
  connect();
  const watchdog = setInterval(() => {
    showStatus();
    if ([WebSocket.OPEN, WebSocket.CONNECTING].includes(socket?.readyState) && performance.now() - (lastTick || connectedAt) > 12000) socket.close();
  }, 2000);
  function stopChart() {
    if (stopped) return;
    stopped = true;
    clearInterval(watchdog);
    clearTimeout(reconnectTimer);
    socket?.close();
    chart.remove();
    window.removeEventListener('pagehide', stopChart);
  }
  window.addEventListener('pagehide', stopChart, { once: true });
  return stopChart;
}
