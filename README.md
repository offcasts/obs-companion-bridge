# OBS Companion Bridge

Zero-config Bitfocus Companion integration for OBS Studio.

Automatically connects [Bitfocus Companion](https://bitfocus.io/companion) to OBS without
requiring manual WebSocket host/port/password entry.

## How it works

1. This OBS plugin reads your WebSocket server settings and advertises them via mDNS
2. The Companion module discovers the plugin on the network
3. Companion connects automatically — no manual configuration needed

## Building

GitHub Actions builds the plugin automatically on every push.
See the Actions tab for build artifacts.

## Installing (macOS)

Copy `obs-companion-bridge.plugin` to:
```
~/Library/Application Support/obs-studio/plugins/
```

Then restart OBS Studio.
