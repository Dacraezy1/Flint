# ![Flint Logo](flint.png) Flint

**Flint** is a minimal, Linux-native Minecraft Java Edition launcher inspired by the suckless philosophy.

The goal is to create the fastest, cleanest, and most maintainable Minecraft launcher focused on Linux users, avoiding bloated cross-platform structures (such as Electron or heavy Java UI runtimes).

## ⚖️ License
Licensed under **GPL-3.0-only**.

---

## ⚡ Features

### 🎮 Core Launcher Capabilities
- **Native Wayland support:** Integrates environment variables (`GLFW_PLATFORM=wayland`) to render Minecraft natively under Wayland, avoiding XWayland scaling issues and reducing input latency.
- **Asynchronous Portable Java downloader:** Inspects Mojang version requirements and fetches/installs the exact Adoptium Temurin JDK binaries (Java 8, 17, 21, or 25) automatically inside secure user folders.
- **Fast parallel downloader:** Downloads assets, libraries, and client jars concurrently using a thread pool with SHA-1 integrity checks.
- **Secure account manager:** Supporting offline usernames and Microsoft OAuth accounts (headlessly via Xbox Live / Mojang Device flow) with credentials saved to disk with restricted owner-only file permissions (`0600`).

### 📦 Instance Manager
- Create independent profiles matching specific Minecraft versions.
- **Cloning:** Fast local copying.
- **Import/Export:** Bundle entire instance profiles into standard compressed `.zip` archives.

### 🔌 Modding & Custom Loaders
- **Fabric integration:** Installs the latest stable Fabric loader profile dynamically, downloading dependencies, and running KnotClient boot loaders on launch.
- **Mod Manager:** View, delete, and add local mod `.jar` files inside instances.
- **Modrinth Search:** Directly search Modrinth inside the launcher, displaying description details and downloading compatible versions.
- **One-Click Optimization Preset:** Automatically fetches and installs **Sodium, Lithium, and FerriteCore** matching the game version with a single click.

### 🐧 Linux Tweaks
- **Feral GameMode:** Easily wrap execution with `gamemoderun`.
- **MangoHud:** Turn on MangoHud overlay to track frame rate and hardware resource statistics.
- **Vulkan Diagnostics:** Startup checks standard `/usr/share/vulkan` paths and flags alerts if graphics driver icds are missing.

---

## 🛠️ Build Requirements

To compile Flint from source, you need a C++23 compiler and the following dependencies:
- **CMake** (>= 3.22)
- **Qt6 Widgets, Network, and Concurrent**
- **OpenSSL** (for accounts authentication handshakes)
- **libzip** (automatically fetched and statically linked by CMake)
- **zstd**

### On Arch Linux:
```bash
sudo pacman -S git cmake ninja gcc qt6-base openssl zstd
```

### On Fedora:
```bash
sudo dnf install git cmake ninja-build gcc-c++ qt6-qtbase-devel openssl-devel libzstd-devel
```

### On Debian/Ubuntu:
```bash
sudo apt install git cmake ninja-build g++ qt6-base-dev libssl-dev libzstd-dev
```

---

## 🏗️ Compilation & Running

```bash
git clone https://github.com/Dacraezy1/Flint.git
cd Flint
cmake -B build -S . -GNinja -DCMAKE_BUILD_TYPE=Release
cmake --build build
./build/flint
```

---

## 📦 Packaging

This repository includes support for standard packaging across various distros:

### Arch Linux (AUR)
Build the Arch package locally:
```bash
makepkg -si
```

### Debian / Ubuntu (`.deb`)
Compile and package via CPack:
```bash
cd build
cpack -G DEB
```

### Fedora (`.rpm`)
Compile and package via CPack:
```bash
cd build
cpack -G RPM
```

### Generic Tarball (`.tar.gz`)
```bash
cd build
cpack -G TGZ
```

### AppImage
Run the continuous integration script or bundle locally:
```bash
./linuxdeploy-x86_64.AppImage --executable=build/flint --plugin=qt --output=appimage --desktop-file=flint.desktop --icon-file=flint.png
```
