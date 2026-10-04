// Binance wysyła ceny jako tekst, czas w milisekundach; wykres wymaga sekund.
export function parseCandle(time, open, high, low, close, volume) {
  const values = [open, high, low, close, volume];
  if (values.some((value) => value === '' || value === null || value === undefined)) return null;
  const numbers = values.map(Number);
  if (!Number.isSafeInteger(time) || time <= 0 || time % 60000 !== 0 || numbers.some((value) => !Number.isFinite(value))) return null;
  const [o, h, l, c, v] = numbers;
  if (Math.min(o, h, l, c) <= 0 || v < 0 || h < Math.max(o, c, l) || l > Math.min(o, c)) return null;
  return { time: time / 1000, open: o, high: h, low: l, close: c, volume: v };
}

export function parseHistory(rows) {
  if (!Array.isArray(rows) || rows.length === 0) throw new Error('Brak historii');
  const candles = rows.map((row) => Array.isArray(row) ? parseCandle(...row.slice(0, 6)) : null);
  if (candles.some((candle, index) => !candle || (index > 0 && candle.time <= candles[index - 1].time))) throw new Error('Niepoprawna historia');
  return candles;
}

export function parseLive(message, symbol = 'BTCUSDT') {
  const k = message?.k;
  if (message?.e !== 'kline' || message.s !== symbol || k?.s !== symbol || k.i !== '1m') return null;
  return parseCandle(k.t, k.o, k.h, k.l, k.c, k.v);
}

export function mergeCandles(history, live) {
  const candles = new Map();
  for (const candle of [...history, ...live]) candles.set(candle.time, candle);
  return [...candles.values()].sort((a, b) => a.time - b.time).slice(-500);
}
