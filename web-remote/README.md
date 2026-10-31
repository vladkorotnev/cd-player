# ESPer CDP web remote

Small Svelte 5 and TypeScript remote for the firmware's `/api/status` and `/ws` interfaces. The display is drawn from the 1-bit framebuffer sent by the device. The page includes remote controls and a Settings editor that follows the device menu. It edits WiFi credentials, CD and metadata options, six radio presets, Bluetooth, and System preferences through `/api/preferences`. Device-only actions such as scanning WiFi networks, clearing cache, updates, reset, and diagnostics remain on the device.

## Develop against a device on the LAN

Requires Node.js 20.19+ or 22.12+.

```sh
npm install
cp .env.example .env.local
# Set DEVICE_HOST in .env.local to the device's IP or hostname.
npm run dev
```

Open the local URL printed by Vite. The dev server proxies `/api` and `/ws` to `DEVICE_HOST`, including the WebSocket Origin expected by the firmware. Alternatively, set `DEVICE_HOST=192.168.1.50 npm run dev` without an env file. The device's **Web remote** setting must be enabled and Wi-Fi connected. The browser should stay on the Vite URL, not the device URL, during development.

Run `npm run check` to type-check the Svelte app and Vite configuration. The device build runs this check too.

## Build for the device

```sh
npm run build:device
cd ../firm
pio run --target buildfs --environment ESPER_OG_8M
```

The PlatformIO prebuild hook compresses `web-remote/dist/device/index.html` into `firm/data/web/index.html.gz` whenever the frontend build exists. It also runs on ordinary firmware builds. If the frontend build is missing, it prints a warning and leaves any existing compressed page alone. The firmware serves the compressed file at `/` and `/index.html` from LittleFS with `Content-Encoding: gzip`; browsers decompress it automatically. The page uses same-origin `/api/status` and `/ws` URLs. Remove any old `firm/data/web/index.html` before uploading a local LittleFS image so it does not occupy space unnecessarily.

CI builds the frontend before `buildfs`; the same hook prepares the compressed page for the LittleFS image and `esper-fs.efs` HTTP update archive.
