import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
  server: {
    port: 5274,
    proxy: {
      '/api': {
        target: 'http://localhost:7432',
        changeOrigin: true,
      },
      '/ws': {
        target: 'ws://localhost:7432',
        ws: true,
      },
    },
  },
})
