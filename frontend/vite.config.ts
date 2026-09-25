import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// base "./" keeps asset URLs relative, so the same build works when served by
// guard-core and when bundled into the desktop app.
export default defineConfig({
  base: "./",
  plugins: [react()],
  server: {
    proxy: { "/api": { target: "http://127.0.0.1:8090", changeOrigin: false } },
  },
  build: { outDir: "dist", sourcemap: false, chunkSizeWarningLimit: 800 },
});
