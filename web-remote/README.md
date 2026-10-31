# ESPer CDP web remote

Small Svelte 5 remote for the firmware's `/api/status` and `/ws` interfaces. The display is drawn from the 1-bit framebuffer sent by the device. The page has remote controls only; preference editing is a later step.

## Develop against a device on the LAN

Requires Node.js 20.19+ or 22.12+.

```sh
npm install
cp .env.example .env.local
# Set DEVICE_HOST in .env.local to the device's IP or hostname.
npm run dev
```

Open the local URL printed by Vite. The dev server proxies `/api` and `/ws` to `DEVICE_HOST`, including the WebSocket Origin expected by the firmware. Alternatively, set `DEVICE_HOST=192.168.1.50 npm run dev` without an env file. The device's **Web remote** setting must be enabled and Wi-Fi connected. The browser should stay on the Vite URL, not the device URL, during development.

## Build for the device

```sh
npm run build:device
```

`dist/device/index.html` is the only generated file. JavaScript and CSS are minified and inlined so it can be embedded in firmware or served as a static page at the device origin. The UI uses same-origin `/api/status` and `/ws` URLs; no development IP is included in this build. This project produces the asset, but the current firmware server does not yet serve it.
