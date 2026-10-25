SHELL := /bin/sh
.DEFAULT_GOAL := all

BUILD := build
IMAGE := $(BUILD)/cklos.img
DATA_IMAGE := $(BUILD)/cklos-data.img
KERNEL_MAX_BYTES := 51200
VERSION := $(shell grep 'CKLOS_VERSION "' src/version.h | cut -d '"' -f2)

NASM ?= $(shell command -v nasm 2>/dev/null)
BREW := $(shell command -v brew 2>/dev/null)
LLVM_PREFIX ?= $(shell if [ -n "$(BREW)" ]; then "$(BREW)" --prefix llvm 2>/dev/null; fi)
LLD_PREFIX ?= $(shell if [ -n "$(BREW)" ]; then "$(BREW)" --prefix lld 2>/dev/null; fi)
CLANG ?= $(shell if [ -x "$(LLVM_PREFIX)/bin/clang" ]; then \
	echo "$(LLVM_PREFIX)/bin/clang"; else command -v clang 2>/dev/null; fi)
LD_LLD ?= $(shell if [ -x "$(LLD_PREFIX)/bin/ld.lld" ]; then \
	echo "$(LLD_PREFIX)/bin/ld.lld"; else command -v ld.lld 2>/dev/null; fi)
OBJCOPY ?= $(shell if [ -x "$(LLVM_PREFIX)/bin/llvm-objcopy" ]; then \
	echo "$(LLVM_PREFIX)/bin/llvm-objcopy"; \
	elif command -v llvm-objcopy >/dev/null 2>&1; then command -v llvm-objcopy; \
	else command -v objcopy 2>/dev/null; fi)
QEMU ?= $(shell command -v qemu-system-i386 2>/dev/null)
HOST_OS := $(shell uname -s)
ifeq ($(HOST_OS),Darwin)
QEMU_RUN_DISPLAY ?= cocoa,zoom-to-fit=on
endif
QEMU_DISPLAY_ARG := $(if $(QEMU_RUN_DISPLAY),-display $(QEMU_RUN_DISPLAY))
HOST_SSH_PORT ?= 2222
NETWORK_ARGS := -netdev user,id=net0,hostfwd=tcp:127.0.0.1:$(HOST_SSH_PORT)-:22 \
	-device rtl8139,netdev=net0,mac=52:54:00:12:34:56

CFLAGS := --target=i386-none-elf -m32 -std=c11 -O2 -Wall -Wextra -Werror \
	-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
	-fno-asynchronous-unwind-tables -fno-unwind-tables -mno-sse -mno-sse2 \
	-mno-mmx -mno-80387 -nostdlib -MMD -MP \
	-Isrc -I$(BUILD) \
	-Ithird_party/font8x8 \
	-Ithird_party/monocypher/src -Ithird_party/monocypher/src/optional \
	-Ithird_party/bearssl/inc -Ithird_party/bearssl/src
NASMFLAGS := -f elf32 -Wall -Werror -w-reloc-abs-dword -w-reloc-rel-dword
CRYPTO_CFLAGS := --target=i386-none-elf -m32 -std=c11 -O2 \
	-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
	-fno-asynchronous-unwind-tables -fno-unwind-tables -fno-common \
	-ffunction-sections -fdata-sections \
	-Isrc \
	-Ithird_party/monocypher/src -Ithird_party/monocypher/src/optional \
	-Ithird_party/bearssl/inc -Ithird_party/bearssl/src

C_SOURCES := src/kernel.c src/console.c src/serial.c src/keyboard.c \
	src/string.c src/memory.c src/vfs.c src/disk.c src/interrupts.c \
	src/network.c src/gui.c src/tasks.c src/shell.c
C_OBJECTS := $(patsubst src/%.c,$(BUILD)/%.o,$(C_SOURCES))
VENDOR_SOURCES := \
	third_party/monocypher/src/monocypher.c \
	third_party/monocypher/src/optional/monocypher-ed25519.c \
	third_party/bearssl/src/hash/sha2small.c \
	third_party/bearssl/src/codec/ccopy.c \
	third_party/bearssl/src/codec/dec32be.c \
	third_party/bearssl/src/codec/dec32le.c \
	third_party/bearssl/src/codec/enc32be.c \
	third_party/bearssl/src/codec/enc32le.c \
	third_party/bearssl/src/mac/hmac.c \
	third_party/bearssl/src/mac/hmac_ct.c \
	third_party/bearssl/src/rand/hmac_drbg.c \
	third_party/bearssl/src/symcipher/aes_ct64.c \
	third_party/bearssl/src/symcipher/aes_ct64_ctr.c \
	third_party/bearssl/src/symcipher/aes_ct64_dec.c \
	third_party/bearssl/src/symcipher/aes_ct64_enc.c
VENDOR_OBJECTS := $(patsubst %.c,$(BUILD)/vendor/%.o,$(VENDOR_SOURCES))
OBJECTS := $(BUILD)/entry.o $(BUILD)/interrupt_stubs.o $(BUILD)/task_switch.o \
	$(C_OBJECTS)
DEPFILES := $(C_OBJECTS:.o=.d)

-include $(DEPFILES)

.PHONY: all image run term test check release clean doctor help crypto font keys

all: image

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/boot.bin: src/boot.asm | $(BUILD)
	$(NASM) -f bin -Wall -w-reloc-abs-word -w-reloc-abs-dword \
		-w-reloc-rel-dword $< -o $@

$(BUILD)/stage2.bin: src/stage2.asm | $(BUILD)
	$(NASM) -f bin -Wall -w-reloc-abs-word -w-reloc-abs-dword \
		-w-reloc-rel-dword $< -o $@

$(BUILD)/entry.o: src/entry.asm | $(BUILD)
	$(NASM) $(NASMFLAGS) $< -o $@

$(BUILD)/interrupt_stubs.o: src/interrupt_stubs.asm | $(BUILD)
	$(NASM) $(NASMFLAGS) $< -o $@

$(BUILD)/task_switch.o: src/task_switch.asm | $(BUILD)
	$(NASM) $(NASMFLAGS) $< -o $@

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CLANG) $(CFLAGS) -c $< -o $@

crypto:
	@./scripts/fetch-crypto.sh

font:
	@./scripts/fetch-font.sh

keys: | $(BUILD)
	@./scripts/gen-ssh-keys.sh

$(BUILD)/crypto.o: keys
$(BUILD)/ssh.o: keys
$(BUILD)/gui.o: font

$(BUILD)/vendor/%.o: %.c | crypto $(BUILD)
	@mkdir -p $(dir $@)
	$(CLANG) $(CRYPTO_CFLAGS) -c $< -o $@

$(BUILD)/kernel.elf: $(OBJECTS) linker.ld
	$(LD_LLD) -m elf_i386 -T linker.ld --build-id=none --gc-sections \
		-o $@ $(OBJECTS)

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@

image: $(BUILD)/boot.bin $(BUILD)/kernel.bin
	@size=$$(wc -c < $(BUILD)/kernel.bin | tr -d ' '); \
	if [ "$$size" -gt "$(KERNEL_MAX_BYTES)" ]; then \
		echo "Kernel is $$size bytes; boot limit is $(KERNEL_MAX_BYTES)." >&2; \
		exit 1; \
	fi
	dd if=/dev/zero of=$(IMAGE) bs=512 count=2880 2>/dev/null
	dd if=$(BUILD)/boot.bin of=$(IMAGE) conv=notrunc bs=512 seek=0 2>/dev/null
	dd if=$(BUILD)/kernel.bin of=$(IMAGE) conv=notrunc bs=512 seek=1 2>/dev/null
	@if [ ! -f "$(DATA_IMAGE)" ]; then \
		dd if=/dev/zero of=$(DATA_IMAGE) bs=512 count=4096 2>/dev/null; \
	fi
	@echo "Built $(IMAGE) (1.44 MB bootable x86 image)"

run: image
	@test -n "$(QEMU)" || (echo "Install QEMU: brew install qemu" >&2; exit 1)
	@status=0; \
	$(QEMU) -m 64M -drive file=$(IMAGE),format=raw,if=floppy -boot a \
		-drive file=$(DATA_IMAGE),format=raw,if=ide,index=0 \
		$(NETWORK_ARGS) \
		$(QEMU_DISPLAY_ARG) -serial stdio -monitor none \
		-device isa-debug-exit || status=$$?; \
	if [ $$status -ne 0 ] && [ $$status -ne 33 ]; then exit $$status; fi

term: image
	@test -n "$(QEMU)" || (echo "Install QEMU: brew install qemu" >&2; exit 1)
	@status=0; \
	$(QEMU) -m 64M -drive file=$(IMAGE),format=raw,if=floppy -boot a \
		-drive file=$(DATA_IMAGE),format=raw,if=ide,index=0 \
		$(NETWORK_ARGS) \
		-display none -serial stdio -monitor none -device isa-debug-exit || status=$$?; \
	if [ $$status -ne 0 ] && [ $$status -ne 33 ]; then exit $$status; fi

test: image
	@test -n "$(QEMU)" || (echo "Install QEMU: brew install qemu" >&2; exit 1)
	./scripts/test-cklos.sh

doctor:
	@result=0; \
	for entry in \
		"nasm:$(NASM)" \
		"clang:$(CLANG)" \
		"ld.lld:$(LD_LLD)" \
		"objcopy:$(OBJCOPY)" \
		"qemu:$(QEMU)"; do \
		name=$${entry%%:*}; path=$${entry#*:}; \
		if [ -n "$$path" ] && [ -x "$$path" ]; then \
			echo "$$name: $$path"; \
		else \
			echo "$$name: missing" >&2; result=1; \
		fi; \
	done; \
	exit $$result

check: doctor test

release: clean image
	@if command -v shasum >/dev/null 2>&1; then \
		shasum -a 256 $(IMAGE) > $(BUILD)/SHA256SUMS; \
	else \
		sha256sum $(IMAGE) > $(BUILD)/SHA256SUMS; \
	fi
	@echo "CKLOS $(VERSION) release artifact ready in $(BUILD)/"

help:
	@echo "CKLOS $(VERSION) build targets"
	@echo "  make            Build the boot image"
	@echo "  make run        Build and run in a resizable QEMU window"
	@echo "  make term       Build and run using only the serial terminal"
	@echo "  make test       Run automated QEMU integration tests"
	@echo "  make check      Check tools and run the full test suite"
	@echo "  make release    Build a clean image and SHA-256 checksum"
	@echo "  make doctor     Show the selected host toolchain"
	@echo "  make clean      Remove generated files and the test data disk"

clean:
	rm -rf $(BUILD)
