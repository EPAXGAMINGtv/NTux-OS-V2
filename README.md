# NTux-OS

**NTux-OS** is a hobby operating system built from scratch for x86-64
with support for both **UEFI and legacy BIOS**. The name combines
"NT" (Windows NT) and "Tux" (Linux penguin), reflecting its goal of
merging concepts from both worlds.

The system boots via [Limine](https://github.com/limine-bootloader/limine),
features a full GUI desktop environment with a taskbar
and window manager, and can run Doom, Tetris, Snake, Flappy Bird,
Lua scripts, and even compile C programs on the fly via TinyCC.

> **License:** GNU General Public License v3 — see [LICENSE](LICENSE)

---

## Quick Start

```bash
chmod +x kernel/get-deps   # first time only
make                        # build kernel + userspace + ISO
make run                    # launch in QEMU
```

Detailed build instructions: [BUILD.md](BUILD.md)

---

## Features

### Kernel & Core
- **Architecture:** x86_64 only (no other architectures planned)
- **Bootloader:** Limine (UEFI + BIOS)
- **Memory Management:** HHDM, PMM, VMM (paging), kmalloc, umalloc
- **Preemptive Multitasking:** Threads, context switching, SMP-ready
- **Syscalls:** `int 0x80` + `syscall`/`sysret`, ~80 syscalls
- **Linux Compatibility:** Linux syscall wrappers (`kernel/src/syscall/linux_syscall.c`)
- **Userspace:** Freestanding ELF modules loaded by the kernel
- **ELF Loader:** Full ELF64 loading with relocations
- **FPU/SSE:** Floating-point and SIMD support

### Filesystems
- **FAT12/16/32** — read/write/format (`kernel/src/fs/fat.c`)
- **ext2** — read/write/format (`kernel/src/fs/ext2.c`)
- **ext4** — read/write/format (`kernel/src/fs/ext4.c`)
- **ISO 9660** — read-only (`kernel/src/fs/iso.c`)
- **RAM FS** — in-memory filesystem (`kernel/src/fs/ramfs.c`)
- **devfs** — device filesystem (`kernel/src/fs/devfs.c`)
- **VFS Layer** — unified interface with mount points (`kernel/src/fs/vfs.c`)

### Drivers
- **Framebuffer** — Limine framebuffer, double-buffering, fonts
- **PS/2 Keyboard & Mouse** — full input drivers
- **ATA/SATA (AHCI)** — PATA and SATA
- **NVMe** — SSD support
- **SD/MMC** — SD card and MMC
- **CMOS/RTC** — real-time clock
- **ACPI** — power management
- **APIC** — advanced interrupt controller (SMP)
- **PCI** — bus enumeration
- **GPU** — virtio-gpu via IOCTL interface (`kernel/src/drivers/gpu/`)
- **Virtio-RNG** — random number generator

### Network Drivers (ported from [BoredOS](https://github.com/ir0nheart/BoredOS))
- **e1000** — Intel Gigabit Ethernet (`kernel/src/net/nic/e1000.c`)
- **rtl8139** — Realtek Fast Ethernet (`kernel/src/net/nic/rtl8139.c`)
- **rtl8111** — Realtek Gigabit Ethernet (`kernel/src/net/nic/rtl8111.c`)
- **virtio-net** — paravirtualized NIC (`kernel/src/net/nic/virtio_net.c`)

### Networking Stack
- Ethernet, ARP, IPv4, UDP, TCP (lwIP), ICMP (ping)
- DHCP (automatic IP), DNS (name resolution)
- HTTP (`sys_net_http_get`)
- Full socket API (open/read/write/close/ioctl)

Source: `kernel/src/net/`

### GUI & Windowing
- Desktop environment with taskbar, window management, desktop icons
- Window API: create/close/move/resize/draw
- Title bars with drag, minimize, maximize, close
- Mouse and keyboard event dispatch to windows
- GUI widgets: buttons, scrollbars, dropdowns, text rendering
- Double-buffered compositing, desktop IPC (DeskAPI)
- Native file picker, message boxes, notifications
- Runtime theming (`window_set_theme`)
- GDI-style drawing (rect, line, text, button, scrollbar)

Source: `userspace/src/desktop/`

### Apps & Games

| App | Description | Source |
|---|---|---|
| **Desktop** | GUI desktop with taskbar | `userspace/src/desktop/` |
| **Login** | Login screen / session manager | `userspace/src/login/` |
| **Terminal** | Desktop terminal emulator | `userspace/src/terminal/` |
| **Konsole** | System console (kernel log) | `userspace/src/konsole/` |
| **Explorer** | File manager | `userspace/src/explorer/` |
| **Editor** | Text editor | `userspace/src/editor/` |
| **Settings** | System settings GUI | `userspace/src/settings/` |
| **PartUtil** | Partitioning tool | `userspace/src/partutil/` |
| **Task Manager** | Process list, kill, stats | `userspace/src/taskmanager/` |
| **Calculator** | GUI calculator | `userspace/src/calc/` |
| **Paint** | Paint program | `userspace/src/paint/` |
| **Image Viewer** | BMP/PNG/JPG viewer | `userspace/src/imgview/` |
| **EpaxFetch** | System info (neofetch-like) | `userspace/src/epaxfetch/` |
| **Bench** | CPU benchmark | `userspace/src/bench/` |
| **Health Check** | System diagnostics | `userspace/src/healthcheck/` |
| **Browser** | Web browser (HTTP GET) | `userspace/src/browser/` |
| **Doom** | via doomgeneric | `userspace/src/doom/` |
| **Tetris** | Tetris clone | `userspace/src/tetris/` |
| **Snake** | Snake game | `userspace/src/snake/` |
| **Flappy Bird** | Flappy Bird clone | `userspace/src/flappy/` |
| **Xeyes** | Eyes follow mouse cursor | `userspace/src/xeyes/` |
| **DeskDemo** | Desktop effects demo | `userspace/src/deskdemo/` |
| **Screen Saver** | Screen saver | `userspace/src/screensaver/` |
| **C++ Hello** | C++ userspace demo | `userspace/src/cpphello/` |

### Self-Hosting & Development
- **Lua 5.4** — full interpreter with NTux bindings (`userspace/src/lua/`)
- **TinyCC (TCC)** — C compiler running natively on the OS (`userspace/src/tinycc/`)
- **C++ Support** — limited runtime (no exceptions/RTTI)

### Installer & Recovery
- GUI installer (`userspace/src/installer/`)
- Terminal-based installer
- Recovery mode (separate boot entry)
- **GPT** and **MBR** partitioning
- `mkfs.ext2`, `mkfs.ext4`, `mkfs.fat`

---

## AI Policy

[AI-Policy.md](AI-Policy.md) — rules for AI-assisted development in this project.

---

## Project Structure

```
NTux-OS/
├── GNUmakefile          # Top-level build file
├── limine.conf          # Limine bootloader config
├── kernel/              # Kernel source (arch, drivers, fs, net, mm, sched, syscall)
├── userspace/           # Userspace programs (desktop, terminal, games, tools)
├── res/                 # Icons, backgrounds, 3D modules
├── wallpapers/          # Wallpaper images
├── AI-Policy.md         # AI usage policy
├── BUILD.md             # Build & run instructions
└── LICENSE              # GPL v3
```

---

## Acknowledgements

- [Limine Bootloader](https://github.com/limine-bootloader/limine)
- [lwIP](https://savannah.nongnu.org/projects/lwip/) — TCP/IP stack
- [BoredOS](https://github.com/ir0nheart/BoredOS) — network drivers
- [doomgeneric](https://github.com/ozkl/doomgeneric) — Doom port
- [Lua](https://www.lua.org/) — 5.4 scripting language
- [TinyCC](https://repo.or.cz/tinycc.git) — C compiler
- [stb_image](https://github.com/nothings/stb) — image decoding
- [edk2-ovmf-nightly](https://github.com/osdev0/edk2-ovmf-nightly) — UEFI firmware
