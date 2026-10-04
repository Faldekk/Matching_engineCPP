import { defineConfig } from 'vite';

// Przegladarka komunikuje sie z /api pod tym samym adresem co interfejs.
// Vite przekazuje te zapytania do serwera C++, bez zmieniania CORS w Crow.
const proxy = {
  '/api': { target: process.env.MATCHING_API_TARGET || 'http://127.0.0.1:18080' },
  '/market-data': {
    target: 'https://data-api.binance.vision', changeOrigin: true,
    rewrite: (path) => path.replace(/^\/market-data/, ''),
  },
};
export default defineConfig({
  server: { host: '127.0.0.1', port: 5173, strictPort: true, proxy },
  preview: { host: '127.0.0.1', port: 5173, strictPort: true, proxy },
});
