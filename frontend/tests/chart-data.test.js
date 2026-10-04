import test from 'node:test';
import assert from 'node:assert/strict';
import { parseCandle, parseHistory, parseLive, mergeCandles } from '../src/chart-data.js';

const row = [60000, '100', '105', '98', '102', '0.1234'];
test('Czas w sekundach, OHLC i ułamkowy wolumen', () => {
  assert.deepEqual(parseCandle(...row), { time: 60, open: 100, high: 105, low: 98, close: 102, volume: 0.1234 });
});
test('Odrzucone błędne ceny, wolumen i czas', () => {
  for (const [index, value] of [[0, 60001], [1, ''], [2, '101'], [3, '103'], [4, 'NaN'], [5, '-1'], [5, null]]) {
    const invalid = [...row]; invalid[index] = value;
    assert.equal(parseCandle(...invalid), null);
  }
});
test('Historia musi być uporządkowana i bez duplikatów', () => {
  assert.equal(parseHistory([row, [120000, ...row.slice(1)]]).length, 2);
  assert.throws(() => parseHistory([row, row]));
  assert.throws(() => parseHistory([[120000, ...row.slice(1)], row]));
  assert.throws(() => parseHistory({ code: -1 }));
});
test('WebSocket obsługuje tylko BTCUSDT i minutowe świece', () => {
  const message = { e: 'kline', s: 'BTCUSDT', k: { s: 'BTCUSDT', i: '1m', t: 60000, o: '100', h: '105', l: '98', c: '102', v: '0.1234' } };
  assert.deepEqual(parseLive(message), parseCandle(...row));
  assert.equal(parseLive({ ...message, s: 'ETHUSDT' }), null);
  assert.equal(parseLive({ ...message, k: { ...message.k, i: '5m' } }), null);
  const ethereum = { ...message, s: 'ETHUSDT', k: { ...message.k, s: 'ETHUSDT' } };
  assert.deepEqual(parseLive(ethereum, 'ETHUSDT'), parseCandle(...row));
  assert.equal(parseLive(ethereum, 'SOLUSDT'), null);
});
test('Nowszy WS wygrywa z HTTP, brak duplikatów i naprawa luki', () => {
  const old = parseCandle(...row);
  const live = { ...old, close: 104, volume: 1 };
  const next = { ...live, time: 180 };
  assert.deepEqual(mergeCandles([old, { ...old, time: 120 }], [live, next]), [live, { ...old, time: 120 }, next]);
});
