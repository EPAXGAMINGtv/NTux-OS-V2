# Nuke built-in rules.
.SUFFIXES:

# Target architecture to build for. Default to x86_64.
ARCH := x86_64

# Default user QEMU flags. These are appended to the QEMU command calls.
# Note: For ATA/IDE to work, we need to use the legacy IDE controller syntax
QEMUFLAGS := \
    -enable-kvm \
    -cpu host \
    -machine q35,accel=kvm \
    -m 8G \
    -smp 8,sockets=1,cores=8,threads=1 \
    -serial stdio \
    -boot d \
    -device qemu-xhci \
    -device sdhci-pci \
    -device virtio-rng-pci \
    -device virtio-gpu-pci \
    -netdev user,id=net0 \
    -device e1000,netdev=net0 \
    -drive if=none,id=sdcard,file=disk.img,format=raw,cache=none,aio=native \
    -device sd-card,drive=sdcard
override IMAGE_NAME := NTux-OS-$(ARCH)
# Optional: absolute or relative path to your IWAD (e.g. /path/to/doom1.wad).
# If set, `make create-drives` copies it to drive_fat32.img as /doom1.wad.
DOOM_WAD_PATH ?=
# Auto-detect common local IWAD paths (used when DOOM_WAD_PATH is empty).
DOOM_WAD_AUTO := $(firstword $(wildcard \
	./doom1.wad ./DOOM1.WAD ./doom2.wad ./DOOM2.WAD \
	./wad/doom1.wad ./wad/DOOM1.WAD ./wad/doom2.wad ./wad/DOOM2.WAD \
	./res/doom1.wad ./res/DOOM1.WAD ./res/doom2.wad ./res/DOOM2.WAD))

# Toolchain for building the 'limine' executable for the host.
HOST_CC := cc
HOST_CFLAGS := -g -O2 -pipe
HOST_CPPFLAGS :=
HOST_LDFLAGS :=
HOST_LIBS :=

.PHONY: all
all: $(IMAGE_NAME).iso

.PHONY: all-hdd
all-hdd: $(IMAGE_NAME).hdd

.PHONY: run
run: run-$(ARCH)

.PHONY: run-hdd
run-hdd: run-hdd-$(ARCH)

.PHONY: run-x86_64
run-x86_64: edk2-ovmf $(IMAGE_NAME).iso create-drives
	qemu-system-$(ARCH) \
		-drive if=pflash,unit=0,format=raw,file=edk2-ovmf/ovmf-code-$(ARCH).fd,readonly=on \
		-cdrom $(IMAGE_NAME).iso \
		$(QEMUFLAGS)

.PHONY: run-hdd-x86_64
run-hdd-x86_64: edk2-ovmf $(IMAGE_NAME).hdd
	qemu-system-$(ARCH) \
		-drive if=pflash,unit=0,format=raw,file=edk2-ovmf/ovmf-code-$(ARCH).fd,readonly=on \
		-hda $(IMAGE_NAME).hdd \
		$(QEMUFLAGS)

.PHONY: run-aarch64
run-aarch64: edk2-ovmf $(IMAGE_NAME).iso
	qemu-system-$(ARCH) \
		-M virt \
		-cpu cortex-a72 \
		-device ramfb \
		-drive if=pflash,unit=0,format=raw,file=edk2-ovmf/ovmf-code-$(ARCH).fd,readonly=on \
		-cdrom $(IMAGE_NAME).iso \
		$(QEMUFLAGS)

.PHONY: run-hdd-aarch64
run-hdd-aarch64: edk2-ovmf $(IMAGE_NAME).hdd
	qemu-system-$(ARCH) \
		-M virt \
		-cpu cortex-a72 \
		-device ramfb \
		-drive if=pflash,unit=0,format=raw,file=edk2-ovmf/ovmf-code-$(ARCH).fd,readonly=on \
		-hda $(IMAGE_NAME).hdd \
		$(QEMUFLAGS)

.PHONY: run-riscv64
run-riscv64: edk2-ovmf $(IMAGE_NAME).iso
	qemu-system-$(ARCH) \
		-M virt \
		-cpu rv64 \
		-device ramfb \
		-drive if=pflash,unit=0,format=raw,file=edk2-ovmf/ovmf-code-$(ARCH).fd,readonly=on \
		-cdrom $(IMAGE_NAME).iso \
		$(QEMUFLAGS)

.PHONY: run-hdd-riscv64
run-hdd-riscv64: edk2-ovmf $(IMAGE_NAME).hdd
	qemu-system-$(ARCH) \
		-M virt \
		-cpu rv64 \
		-device ramfb \
		-drive if=pflash,unit=0,format=raw,file=edk2-ovmf/ovmf-code-$(ARCH).fd,readonly=on \
		-hda $(IMAGE_NAME).hdd \
		$(QEMUFLAGS)

.PHONY: run-loongarch64
run-loongarch64: edk2-ovmf $(IMAGE_NAME).iso
	qemu-system-$(ARCH) \
		-M virt \
		-device ramfb \
		-drive if=pflash,unit=0,format=raw,file=edk2-ovmf/ovmf-code-$(ARCH).fd,readonly=on \
		-cdrom $(IMAGE_NAME).iso \
		$(QEMUFLAGS)

.PHONY: run-hdd-loongarch64
run-hdd-loongarch64: edk2-ovmf $(IMAGE_NAME).hdd
	qemu-system-$(ARCH) \
		-M virt \
		-cpu la464 \
		-device ramfb \
		-drive if=pflash,unit=0,format=raw,file=edk2-ovmf/ovmf-code-$(ARCH).fd,readonly=on \
		-hda $(IMAGE_NAME).hdd \
		$(QEMUFLAGS)


.PHONY: run-bios
run-bios: $(IMAGE_NAME).iso
	qemu-system-$(ARCH) \
		-cdrom $(IMAGE_NAME).iso \
		-boot d \
		$(QEMUFLAGS)

.PHONY: run-hdd-bios
run-hdd-bios: $(IMAGE_NAME).hdd
	qemu-system-$(ARCH) \
		-hda $(IMAGE_NAME).hdd \
		$(QEMUFLAGS)

edk2-ovmf:
	curl -L https://github.com/osdev0/edk2-ovmf-nightly/releases/latest/download/edk2-ovmf.tar.gz | gunzip | tar -xf -

limine/limine:
	rm -rf limine
	git clone https://codeberg.org/Limine/Limine.git limine --branch=v10.x-binary --depth=1
	$(MAKE) -C limine \
		CC="$(HOST_CC)" \
		CFLAGS="$(HOST_CFLAGS)" \
		CPPFLAGS="$(HOST_CPPFLAGS)" \
		LDFLAGS="$(HOST_LDFLAGS)" \
		LIBS="$(HOST_LIBS)"

kernel/.deps-obtained:
	./kernel/get-deps
	
.PHONY: kernel
kernel: kernel/.deps-obtained
	$(MAKE) -C kernel

.PHONY: userspace
userspace:
	$(MAKE) -C userspace

$(IMAGE_NAME).iso: limine/limine kernel userspace
	rm -rf iso_root
	mkdir -p iso_root/boot
	mkdir -p iso_root/boot/recovery
	mkdir -p iso_root/boot/modules
	mkdir -p iso_root/boot/tcc
	mkdir -p iso_root/boot/res/backgrounds
	mkdir -p iso_root/boot/res/modules
	mkdir -p iso_root/boot/res/icons
	cp -v kernel/bin-$(ARCH)/kernel iso_root/boot/
	cp -v kernel/bin-$(ARCH)/kernel iso_root/boot/recovery/kernel
	cp -v userspace/bin/hello.elf iso_root/boot/modules/hello.elf
	cp -v userspace/bin/konsole.elf iso_root/boot/modules/konsole.elf
	cp -v userspace/bin/hellokonsole.elf iso_root/boot/modules/hellokonsole.elf
	@if [ -f userspace/bin/cpphello.elf ]; then cp -v userspace/bin/cpphello.elf iso_root/boot/modules/cpphello.elf; else echo "skip: userspace/bin/cpphello.elf (no C++ compiler)"; fi
	cp -v userspace/bin/installer.elf iso_root/boot/modules/installer.elf
	cp -v userspace/bin/installer_term.elf iso_root/boot/modules/installer_term.elf
	cp -v userspace/bin/desktop.elf iso_root/boot/modules/desktop.elf
	cp -v userspace/bin/login.elf iso_root/boot/modules/login.elf
	cp -v userspace/bin/terminal.elf iso_root/boot/modules/terminal.elf
	cp -v userspace/bin/deskdemo.elf iso_root/boot/modules/deskdemo.elf
	cp -v userspace/bin/screensaver.elf iso_root/boot/modules/screensaver.elf
	cp -v userspace/bin/doom.elf iso_root/boot/modules/doom.elf
	cp -v userspace/bin/epaxfetch.elf iso_root/boot/modules/epaxfetch.elf
	cp -v userspace/bin/tetris.elf iso_root/boot/modules/tetris.elf
	cp -v userspace/bin/taskmgr.elf iso_root/boot/modules/taskmgr.elf
	cp -v userspace/bin/deskconsole.elf iso_root/boot/modules/deskconsole.elf
	cp -v userspace/bin/explorer.elf iso_root/boot/modules/explorer.elf
	cp -v userspace/bin/editor.elf iso_root/boot/modules/editor.elf
	cp -v userspace/bin/bench.elf iso_root/boot/modules/bench.elf
	cp -v userspace/bin/imgview.elf iso_root/boot/modules/imgview.elf
	cp -v userspace/bin/objview.elf iso_root/boot/modules/objview.elf
	cp -v userspace/bin/flappy.elf iso_root/boot/modules/flappy.elf
	cp -v userspace/bin/xeyes.elf iso_root/boot/modules/xeyes.elf
	cp -v userspace/bin/settings.elf iso_root/boot/modules/settings.elf
	cp -v userspace/bin/partutil.elf iso_root/boot/modules/partutil.elf
	cp -v userspace/bin/browser.elf iso_root/boot/modules/browser.elf
	cp -v userspace/bin/test.elf iso_root/boot/modules/test.elf
	cp -v userspace/bin/healthcheck.elf iso_root/boot/modules/healthcheck.elf
	cp -v userspace/bin/paint.elf iso_root/boot/modules/paint.elf
	cp -v userspace/bin/calc.elf iso_root/boot/modules/calc.elf
	cp -v userspace/bin/snake.elf iso_root/boot/modules/snake.elf
	cp -v userspace/bin/lua.elf iso_root/boot/modules/lua.elf
	cp -v userspace/bin/tcc.elf iso_root/boot/modules/tcc.elf
	cp -v userspace/src/tinycc/examples/ex1.c iso_root/boot/modules/tcc_example.c
	@if [ -d userspace/bin/tcc ]; then cp -rv userspace/bin/tcc/. iso_root/boot/tcc/; else echo "skip: userspace/bin/tcc (missing)"; fi
	cp -v userspace/src/lua/ntux_tests/autorun.lua iso_root/boot/modules/autorun.lua
	cp -v userspace/src/lua/ntux_tests/hello.lua iso_root/boot/modules/hello.lua
	cp -v userspace/src/lua/ntux_tests/fib.lua iso_root/boot/modules/fib.lua
	cp -v userspace/src/lua/ntux_tests/fs.lua iso_root/boot/modules/fs.lua
	cp -v userspace/src/lua/ntux_tests/tetris.lua iso_root/boot/modules/tetris.lua
	cp -rv wallpapers/. iso_root/boot/res/backgrounds/
	@if [ -d res/icons ]; then cp -rv res/icons/. iso_root/boot/res/icons/; else echo "skip: res/icons (missing)"; fi
	@if [ -f standart.obj ]; then cp -v standart.obj iso_root/boot/res/modules/standart.obj; else echo "skip: standart.obj (missing)"; fi
	@if [ -f doom.wad ]; then \
		echo "Copying doom.wad to ISO root as /doom.wad"; \
		cp -v doom.wad iso_root/doom.wad; \
	else \
		echo "skip: doom.wad not found in project root"; \
	fi
	@WAD_SRC=""; \
	if [ -n "$(DOOM_WAD_PATH)" ]; then WAD_SRC="$(DOOM_WAD_PATH)"; \
	elif [ -n "$(DOOM_WAD_AUTO)" ]; then WAD_SRC="$(DOOM_WAD_AUTO)"; fi; \
	if [ -n "$$WAD_SRC" ]; then \
		echo "Copying IWAD to ISO as /doom1.wad from $$WAD_SRC"; \
		cp -v "$$WAD_SRC" iso_root/doom1.wad; \
	else \
		echo "skip: no IWAD found (set DOOM_WAD_PATH=/path/to/doom1.wad)"; \
	fi
	mkdir -p iso_root/boot/limine
	cp -v limine.conf iso_root/boot/limine/
	mkdir -p iso_root/EFI/BOOT
ifeq ($(ARCH),x86_64)
	cp -v limine/limine-bios.sys limine/limine-bios-cd.bin limine/limine-uefi-cd.bin iso_root/boot/limine/
	cp -v limine/BOOTX64.EFI iso_root/EFI/BOOT/
	cp -v limine/BOOTIA32.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table -hfsplus \
		-apm-block-size 2048 --efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(IMAGE_NAME).iso
	./limine/limine bios-install $(IMAGE_NAME).iso
endif
ifeq ($(ARCH),aarch64)
	cp -v limine/limine-uefi-cd.bin iso_root/boot/limine/
	cp -v limine/BOOTAA64.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(IMAGE_NAME).iso
endif
ifeq ($(ARCH),riscv64)
	cp -v limine/limine-uefi-cd.bin iso_root/boot/limine/
	cp -v limine/BOOTRISCV64.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(IMAGE_NAME).iso
endif
ifeq ($(ARCH),loongarch64)
	cp -v limine/limine-uefi-cd.bin iso_root/boot/limine/
	cp -v limine/BOOTLOONGARCH64.EFI iso_root/EFI/BOOT/
	xorriso -as mkisofs -R -r -J \
		-hfsplus -apm-block-size 2048 \
		--efi-boot boot/limine/limine-uefi-cd.bin \
		-efi-boot-part --efi-boot-image --protective-msdos-label \
		iso_root -o $(IMAGE_NAME).iso
endif
	rm -rf iso_root

$(IMAGE_NAME).hdd: limine/limine kernel userspace
	rm -f $(IMAGE_NAME).hdd
	dd if=/dev/zero bs=1M count=0 seek=64 of=$(IMAGE_NAME).hdd
ifeq ($(ARCH),x86_64)
	PATH=$$PATH:/usr/sbin:/sbin sgdisk $(IMAGE_NAME).hdd -n 1:2048 -t 1:ef00 -m 1
	./limine/limine bios-install $(IMAGE_NAME).hdd
else
	PATH=$$PATH:/usr/sbin:/sbin sgdisk $(IMAGE_NAME).hdd -n 1:2048 -t 1:ef00
endif
	mformat -i $(IMAGE_NAME).hdd@@1M
	mmd -i $(IMAGE_NAME).hdd@@1M ::/EFI ::/EFI/BOOT ::/boot ::/boot/limine ::/boot/modules ::/boot/tcc ::/boot/res ::/boot/res/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M kernel/bin-$(ARCH)/kernel ::/boot
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/hello.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/konsole.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/hellokonsole.elf ::/boot/modules
	@if [ -f userspace/bin/cpphello.elf ]; then mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/cpphello.elf ::/boot/modules; else echo "skip: userspace/bin/cpphello.elf (no C++ compiler)"; fi
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/installer.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/installer_term.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/desktop.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/login.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/terminal.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/deskdemo.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/screensaver.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/doom.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/tetris.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/taskmgr.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/deskconsole.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/explorer.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/editor.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/bench.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/imgview.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/snake.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/objview.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/healthcheck.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/paint.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/calc.elf ::/boot/modules
	@if [ -d res/icons ]; then mcopy -i $(IMAGE_NAME).hdd@@1M -s res/icons ::/boot/res/icons; else echo "skip: res/icons (missing)"; fi
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/flappy.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/xeyes.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/test.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/settings.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/browser.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/lua.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/bin/tcc.elf ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/src/tinycc/examples/ex1.c ::/boot/modules/tcc_example.c
	@if [ -d userspace/bin/tcc ]; then mcopy -s -i $(IMAGE_NAME).hdd@@1M userspace/bin/tcc ::/boot/tcc; else echo "skip: userspace/bin/tcc (missing)"; fi
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/src/lua/ntux_tests/autorun.lua ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/src/lua/ntux_tests/hello.lua ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/src/lua/ntux_tests/fib.lua ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/src/lua/ntux_tests/fs.lua ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M userspace/src/lua/ntux_tests/tetris.lua ::/boot/modules
	mcopy -i $(IMAGE_NAME).hdd@@1M limine.conf ::/boot/limine
ifeq ($(ARCH),x86_64)
	mcopy -i $(IMAGE_NAME).hdd@@1M limine/limine-bios.sys ::/boot/limine
	@if [ -f standart.obj ]; then mcopy -i $(IMAGE_NAME).hdd@@1M standart.obj ::/boot/res/modules; else echo "skip: standart.obj (missing)"; fi
	mcopy -i $(IMAGE_NAME).hdd@@1M limine/BOOTX64.EFI ::/EFI/BOOT
	mcopy -i $(IMAGE_NAME).hdd@@1M limine/BOOTIA32.EFI ::/EFI/BOOT
endif
ifeq ($(ARCH),aarch64)
	mcopy -i $(IMAGE_NAME).hdd@@1M limine/BOOTAA64.EFI ::/EFI/BOOT
endif
ifeq ($(ARCH),riscv64)
	mcopy -i $(IMAGE_NAME).hdd@@1M limine/BOOTRISCV64.EFI ::/EFI/BOOT
endif
ifeq ($(ARCH),loongarch64)
	mcopy -i $(IMAGE_NAME).hdd@@1M limine/BOOTLOONGARCH64.EFI ::/EFI/BOOT
endif

.PHONY: clean
clean:
	$(MAKE) -C kernel clean
	$(MAKE) -C userspace clean
	rm -rf iso_root $(IMAGE_NAME).iso $(IMAGE_NAME).hdd
	rm -f drive_fat32.img drive_ext2.img drive_fat16.img

# Create disk images with filesystems
.PHONY: create-drives
create-drives: userspace
	@echo "Creating disk images with filesystems..."
	# Create 100MB drive images
	dd if=/dev/zero of=drive_fat32.img bs=1M count=100 status=none
	dd if=/dev/zero of=drive_ext2.img bs=1M count=100 status=none
	dd if=/dev/zero of=drive_ext4.img bs=1M count=100 status=none
	@DISK_SZ_BYTES=$$((128*1024*1024)); \
	if [ ! -f disk.img ] || [ $$(stat -c%s disk.img 2>/dev/null || echo 0) -ne $$DISK_SZ_BYTES ]; then \
		dd if=/dev/zero of=disk.img bs=1M count=128 status=none; \
		mkfs.fat -F 32 disk.img; \
		mmd -i disk.img ::/usb; \
		mcopy -i disk.img LICENSE ::/usb/LICENSE; \
	fi
	# Create FAT32 filesystem (drive 1 - primary slave)
	mkfs.fat -F 32 drive_fat32.img
	# Put project LICENSE into FAT32 to verify read/write path
	# Put userspace modules into FAT32 for /fat0 runtime testing
	mmd -i drive_fat32.img ::/boot
	mmd -i drive_fat32.img ::/boot/modules
	mmd -i drive_fat32.img ::/boot/tcc
	mmd -i drive_fat32.img ::/boot/res
	mmd -i drive_fat32.img ::/boot/res/modules
	mcopy -i drive_fat32.img userspace/bin/hello.elf ::/boot/modules/hello.elf
	mcopy -i drive_fat32.img userspace/bin/hellokonsole.elf ::/boot/modules/hellokonsole.elf
	@if [ -f userspace/bin/cpphello.elf ]; then mcopy -i drive_fat32.img userspace/bin/cpphello.elf ::/boot/modules/cpphello.elf; else echo "skip: userspace/bin/cpphello.elf (no C++ compiler)"; fi
	mcopy -i drive_fat32.img userspace/bin/installer.elf ::/boot/modules/installer.elf
	mcopy -i drive_fat32.img userspace/bin/installer_term.elf ::/boot/modules/installer_term.elf
	mcopy -i drive_fat32.img userspace/bin/desktop.elf ::/boot/modules/desktop.elf
	mcopy -i drive_fat32.img userspace/bin/login.elf ::/boot/modules/login.elf
	mcopy -i drive_fat32.img userspace/bin/terminal.elf ::/boot/modules/terminal.elf
	mcopy -i drive_fat32.img userspace/bin/deskdemo.elf ::/boot/modules/deskdemo.elf
	mcopy -i drive_fat32.img userspace/bin/screensaver.elf ::/boot/modules/screensaver.elf
	mcopy -i drive_fat32.img userspace/bin/doom.elf ::/boot/modules/doom.elf
	mcopy -i drive_fat32.img userspace/bin/epaxfetch.elf ::/boot/modules/epaxfetch.elf
	mcopy -i drive_fat32.img userspace/bin/tetris.elf ::/boot/modules/tetris.elf
	mcopy -i drive_fat32.img userspace/bin/taskmgr.elf ::/boot/modules/taskmgr.elf
	mcopy -i drive_fat32.img userspace/bin/deskconsole.elf ::/boot/modules/deskconsole.elf
	mcopy -i drive_fat32.img userspace/bin/explorer.elf ::/boot/modules/explorer.elf
	mcopy -i drive_fat32.img userspace/bin/editor.elf ::/boot/modules/editor.elf
	mcopy -i drive_fat32.img userspace/bin/bench.elf ::/boot/modules/bench.elf
	mcopy -i drive_fat32.img userspace/bin/imgview.elf ::/boot/modules/imgview.elf
	mcopy -i drive_fat32.img userspace/bin/snake.elf ::/boot/modules/snake.elf
	mcopy -i drive_fat32.img userspace/bin/objview.elf ::/boot/modules/objview.elf
	mcopy -i drive_fat32.img userspace/bin/healthcheck.elf ::/boot/modules/healthcheck.elf
	mcopy -i drive_fat32.img userspace/bin/paint.elf ::/boot/modules/paint.elf
	mcopy -i drive_fat32.img userspace/bin/calc.elf ::/boot/modules/calc.elf
	@if [ -d res/icons ]; then mcopy -i drive_fat32.img -s res/icons ::/boot/res/icons; else echo "skip: res/icons (missing)"; fi
	mcopy -i drive_fat32.img userspace/bin/flappy.elf ::/boot/modules/flappy.elf
	mcopy -i drive_fat32.img userspace/bin/xeyes.elf ::/boot/modules/xeyes.elf
	mcopy -i drive_fat32.img userspace/bin/settings.elf ::/boot/modules/settings.elf
	mcopy -i drive_fat32.img userspace/bin/partutil.elf ::/boot/modules/partutil.elf
	mcopy -i drive_fat32.img userspace/bin/browser.elf ::/boot/modules/browser.elf
	mcopy -i drive_fat32.img userspace/bin/test.elf ::/boot/modules/test.elf
	mcopy -i drive_fat32.img userspace/bin/lua.elf ::/boot/modules/lua.elf
	mcopy -i drive_fat32.img userspace/bin/tcc.elf ::/boot/modules/tcc.elf
	mcopy -i drive_fat32.img userspace/src/tinycc/examples/ex1.c ::/boot/modules/tcc_example.c
	@if [ -d userspace/bin/tcc ]; then mcopy -s -i drive_fat32.img userspace/bin/tcc ::/boot/tcc; else echo "skip: userspace/bin/tcc (missing)"; fi
	mcopy -i drive_fat32.img userspace/src/lua/ntux_tests/autorun.lua ::/boot/modules/autorun.lua
	mcopy -i drive_fat32.img userspace/src/lua/ntux_tests/hello.lua ::/boot/modules/hello.lua
	mcopy -i drive_fat32.img userspace/src/lua/ntux_tests/fib.lua ::/boot/modules/fib.lua
	mcopy -i drive_fat32.img userspace/src/lua/ntux_tests/fs.lua ::/boot/modules/fs.lua
	mcopy -i drive_fat32.img userspace/src/lua/ntux_tests/tetris.lua ::/boot/modules/tetris.lua
	@if [ -f standart.obj ]; then mcopy -i drive_fat32.img standart.obj ::/boot/res/modules/standart.obj; else echo "skip: standart.obj (missing)"; fi
	@WAD_SRC="$(if $(strip $(DOOM_WAD_PATH)),$(DOOM_WAD_PATH),$(DOOM_WAD_AUTO))"; \
	if [ -n "$$WAD_SRC" ] && [ -f "$$WAD_SRC" ]; then \
		echo "Copying IWAD from $$WAD_SRC to /doom1.wad"; \
		mcopy -i drive_fat32.img "$$WAD_SRC" ::/doom1.wad; \
	else \
		echo "ERROR: no IWAD found."; \
		echo "Set DOOM_WAD_PATH, e.g.:"; \
		echo "  make create-drives DOOM_WAD_PATH=/path/to/doom1.wad"; \
		exit 1; \
	fi
	# Put wallpaper.jpg into FAT32 under /wallpaper/
	mmd -i drive_fat32.img ::/wallpaper
	mcopy -i drive_fat32.img wallpaper.jpg ::/wallpaper/wallpaper.jpg
	mcopy -i drive_fat32.img wallpaper.bmp ::/wallpaper/wallpaper.bmp
	mcopy -i  drive_fat32.img res/backgrounds/background.bmp ::/wallpaper/background.bmp
	# Create EXT2 filesystem (drive 2 - secondary master)
	mkfs.ext2 -F drive_ext2.img
	# Put project LICENSE into EXT2 to verify ext2 read path
	debugfs -w -R "write LICENSE LICENSE" drive_ext2.img
	# Put wallpaper.jpg into EXT2 under /wallpaper/
	debugfs -w -R "mkdir /wallpaper" drive_ext2.img
	debugfs -w -R "write wallpaper.jpg /wallpaper/wallpaper.jpg" drive_ext2.img
	debugfs -w -R "write res/backgrounds/background.bmp /wallpaper/wallpaper.bmp" drive_ext2.img
	mmd -i drive_fat32.img ::/images
	mkfs.ext4 -F drive_ext4.img
	debugfs -w -R "mkdir /wallpaper" drive_ext4.img
	debugfs -w -R "write res/backgrounds/background.bmp  /wallpaper/wallpaper.bmp" drive_ext4.img
	@echo "Disk images created successfully!"
	@echo "  drive_fat32.img - FAT32 (100MB) - primary slave"
	@echo "  drive_ext2.img - EXT2 (100MB) - secondary master"

.PHONY: distclean
distclean:
	$(MAKE) -C kernel distclean
	rm -rf iso_root *.iso *.hdd kernel-deps limine edk2-ovmf
