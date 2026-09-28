import { defineConfig } from 'vite';
import plugin from '@vitejs/plugin-react';

// https://vitejs.dev/config/
export default defineConfig({
    plugins: [plugin()],
    server: {
        port: 57721,
        proxy: {
            '/api': {
                target: 'http://localhost:5800',
                changeOrigin: true,
                secure: false,
            },
            // WebSocket proxy for the /ws/state channel (ws:true upgrades the connection)
            '/ws': {
                target: 'ws://localhost:5800',
                ws: true,
                changeOrigin: true,
            }
        }
    }
})
