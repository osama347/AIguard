# Guard++ desktop app

Cross-platform client (Windows, macOS, Linux) for a Guard++ server. It contains
no AI engine and no database: it connects over the LAN to the Jetson that runs
`guard-server` and shows the same UI (`../frontend/`).

## Using it

1. Install the setup file for your OS (`.msi`/`.exe`, `.dmg`, `.deb`/`.AppImage`).
2. Start **Guard++**. It lists Jetsons found on the network (mDNS `_guard._tcp`);
   or type the address, e.g. `http://192.168.1.20:8090`.
3. Sign in. The address is remembered (Settings → Server changes it).

On a new server the app shows the setup screen: community details (name, address,
helpline, logo, ...) and the administrator account. It asks for the **setup code**
printed when the server was installed (`sudo cat /etc/guard/guard.env` on the Jetson).

## Building

Installers for Windows and macOS must be built on those systems; the GitHub
Actions workflow `.github/workflows/desktop.yml` builds all of them.

Locally (needs Node.js 20+ and Rust; on Linux also
`libwebkit2gtk-4.1-dev libsoup-3.0-dev libgtk-3-dev`):

```bash
cd desktop
npm ci
npm run build:linux        # → src-tauri/target/release/bundle/deb/*.deb
npm run build              # all bundle types available on this OS
```

`npm run build` builds `../frontend` first (`beforeBuildCommand`).

## Trying it on a Mac

On the Mac (needs Xcode command line tools, Node.js 20+ and Rust):

```bash
xcode-select --install
brew install node rustup-init && rustup-init -y && source ~/.cargo/env
rustup target add aarch64-apple-darwin x86_64-apple-darwin
cd Guard++/desktop          # copy of this folder plus ../frontend and ../api
npm ci
npm run build -- --target universal-apple-darwin
open src-tauri/target/universal-apple-darwin/release/bundle/dmg
```

The app is unsigned, so macOS blocks the first launch: right-click **Guard++ Desktop** →
*Open* → *Open* (or `xattr -dr com.apple.quarantine "/Applications/Guard++ Desktop.app"`).
Allow the "local network" prompt so the Jetson can be found. The Mac must be on the
same network as the Jetson.
