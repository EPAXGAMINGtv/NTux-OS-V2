# BUILD.md — Build & Run NTux-OS

## Prerequisites

### Debian / Ubuntu

```bash
sudo apt install build-essential nasm mtools xorriso \
                 gdisk dosfstools e2fsprogs git curl qemu-system-x86
```

### Arch Linux

```bash
sudo pacman -S base-devel nasm mtools xorriso gptfdisk \
               dosfstools e2fsprogs git curl qemu-system-x86
```

### Fedora

```bash
sudo dnf install make gcc nasm mtools xorriso gdisk \
               dosfstools e2fsprogs git curl qemu-system-x86
```

### Optional

- **clang / lld** — alternative compiler/linker (use via `TOOLCHAIN=clang`)
- **Doom IWAD** (`doom1.wad` / `doom2.wad`) — for Doom support

---

## 1. Clone

```bash
git clone https://github.com/epaxgaming/NTux-OS.git
cd NTux-OS
```

---

## 2. Build

```bash
chmod +x kernel/get-deps   # make the dep script executable (first time only)
make                        # builds kernel + userspace + ISO image
```

Produces: `NTux-OS-x86_64.iso`

The build automatically fetches all needed dependencies (Limine bootloader,
freestanding C headers, etc.).

### Individual Targets

```bash
make kernel          # kernel only
make userspace       # userspace programs only
make all-hdd         # HDD image instead of ISO
make create-drives   # test drives (FAT32, ext2, ext4)
```



---

## 3. Run in QEMU

```bash
make run
```

This automatically:
1. Downloads OVMF (UEFI firmware) if missing
2. Builds the ISO if out of date
3. Creates test drives (`drive_fat32.img`, `drive_ext2.img`, `disk.img`)
4. Launches QEMU with 8 GB RAM, 8 CPU cores, KVM, Q35 chipset,
   e1000 NIC, HDA audio, SD card, USB (xHCI)

### Variants

| Command | Description |
|---|---|
| `make run` | UEFI + ISO (default) |
| `make run-hdd` | UEFI + HDD image |
| `make run-bios` | Legacy BIOS + ISO |
| `make run-hdd-bios` | Legacy BIOS + HDD image |
| `make run-aarch64` | ARM64 (UEFI) |
| `make run-riscv64` | RISC-V 64 (UEFI) |
| `make run-loongarch64` | LoongArch 64 (UEFI) |

### Manual

```bash
qemu-system-x86_64 \
    -enable-kvm -cpu host -machine q35,accel=kvm \
    -m 8G -smp 8 -serial stdio \
    -cdrom NTux-OS-x86_64.iso \
    -drive if=pflash,unit=0,format=raw,file=edk2-ovmf/ovmf-code-x86_64.fd,readonly=on
```

---

## 4. Inside the System

After booting you see the **login screen**. Log in to enter the
**desktop environment** with taskbar.

| Shortcut | Action |
|---|---|
| `Alt + F1` | Task Manager |
| `Alt + F2` | Terminal |
| `Alt + F4` | Close window |
| `F12` | Emergency console |

Apps are in the taskbar or on the desktop. Run Lua scripts with the
Lua interpreter, or C code with TinyCC (tcc).

---

## 5. Doom (Optional)

Place `doom1.wad` or `doom2.wad` in the project root, then rebuild:

```bash
make
make run
```

Or explicitly:

```bash
make create-drives DOOM_WAD_PATH=/path/to/doom1.wad
```

---

## 6. Clean

```bash
make clean       # remove build artifacts
make distclean   # remove everything, including downloaded deps
```

---

## 7. Known Issues

- **KVM unavailable**: `QEMUFLAGS="-accel tcg -m 2G -smp 2"`
- **OVMF won't load**: `rm -rf edk2-ovmf` and retry
- **No sound**: newer QEMU needs an explicit `-audiodev` flag
- **Keyboard unresponsive**: click the QEMU window
- **Cross-compiler**: use `TOOLCHAIN=clang` or `TOOLCHAIN_PREFIX=x86_64-elf-`
