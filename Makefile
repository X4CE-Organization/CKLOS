SHELL := /bin/sh
.DEFAULT_GOAL := all

BUILD := build
IMAGE := $(BUILD)/cklos.img
DATA_IMAGE := $(BUILD)/cklos-data.img
FS2_IMAGE := $(BUILD)/cklos-fs2.img
UNIFONT_IMAGE := $(BUILD)/UNIFONT.BIN
BEARSSL_LIB := third_party/bearssl/build-cklos/libbearssl.a
TEST_CA := $(BUILD)/test-ca.der
CA_BUNDLE := $(BUILD)/ca-certificates.crt
USER_HELLO_ELF := $(BUILD)/USERHELLO.ELF
USER_FORK_ELF := $(BUILD)/USERFORK.ELF
USER_FAULT_ELF := $(BUILD)/USERFAULT.ELF
USER_EXEC_ELF := $(BUILD)/USEREXEC.ELF
USER_IO_ELF := $(BUILD)/USERIO.ELF
USER_PIPE_ELF := $(BUILD)/USERPIPE.ELF
USER_AGENT_ELF := $(BUILD)/USERAGENT.ELF
MUSL_STAGE := $(BUILD)/musl-stage/usr/local/musl
MUSL_STAMP := $(BUILD)/musl/.cklos-ready
MUSL_ELF := $(BUILD)/MUSLHELLO.ELF
CCK_STAGE := $(BUILD)/cklos-toolchain
CCK_STAMP := $(BUILD)/tinycc/.cklos-ready
USER_ELVES := $(USER_HELLO_ELF) $(USER_FORK_ELF) $(USER_FAULT_ELF) \
	$(USER_EXEC_ELF) $(USER_IO_ELF) $(USER_PIPE_ELF) $(USER_AGENT_ELF) \
	$(MUSL_ELF)
KERNEL_MAX_BYTES := 524288
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
QEMU ?= $(shell command -v qemu-system-x86_64 2>/dev/null)
HOST_OS := $(shell uname -s)
ifeq ($(HOST_OS),Darwin)
QEMU_RUN_DISPLAY ?= cocoa,zoom-to-fit=off,show-cursor=on
endif
QEMU_DISPLAY_ARG := $(if $(QEMU_RUN_DISPLAY),-display $(QEMU_RUN_DISPLAY))
HOST_SSH_PORT ?= 2222
NETWORK_ARGS := -netdev user,id=net0,hostfwd=tcp:127.0.0.1:$(HOST_SSH_PORT)-:22 \
	-device rtl8139,netdev=net0,mac=52:54:00:12:34:56

CFLAGS := --target=x86_64-none-elf -m64 -std=c11 -Oz -Wall -Wextra -Werror \
	-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
	-fno-asynchronous-unwind-tables -fno-unwind-tables -mno-sse -mno-sse2 \
	-mno-mmx -mno-80387 -mno-red-zone -mcmodel=large -nostdlib -MMD -MP \
	-Isrc -I$(BUILD) \
	-Ithird_party/font8x8 \
	-Ithird_party/littlefs \
	-Ithird_party/monocypher/src -Ithird_party/monocypher/src/optional \
	-Ithird_party/bearssl/inc -Ithird_party/bearssl/src \
	-DLFS_NO_MALLOC -DLFS_NO_ASSERT -DLFS_NO_DEBUG -DLFS_NO_WARN -DLFS_NO_ERROR
NASMFLAGS := -f elf64 -Wall -Werror -w-reloc-abs-dword -w-reloc-rel-dword \
	-w-reloc-abs-qword
CRYPTO_CFLAGS := --target=x86_64-none-elf -m64 -std=c11 -Oz \
	-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
	-fno-asynchronous-unwind-tables -fno-unwind-tables -fno-common \
	-mno-sse -mno-sse2 -mno-mmx -mno-80387 \
	-ffunction-sections -fdata-sections \
	-Isrc \
	-Ithird_party/monocypher/src -Ithird_party/monocypher/src/optional \
	-Ithird_party/bearssl/inc -Ithird_party/bearssl/src \
	-Ithird_party/littlefs -DLFS_NO_MALLOC -DLFS_NO_ASSERT \
	-DLFS_NO_DEBUG -DLFS_NO_WARN -DLFS_NO_ERROR

C_SOURCES := src/kernel.c src/console.c src/serial.c src/keyboard.c \
	src/string.c src/memory.c src/vfs.c src/disk.c src/interrupts.c \
	src/network.c src/gui.c src/mouse.c src/rks.c src/fs2.c src/unicode.c \
	src/editor.c src/resolver.c src/http.c src/tls.c src/tasks.c src/shell.c
C_SOURCES := $(C_SOURCES) src/process.c src/rks2.c src/crypto.c src/ssh.c
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
	third_party/bearssl/src/symcipher/aes_ct64_enc.c \
	third_party/littlefs/lfs.c \
	third_party/littlefs/lfs_util.c
VENDOR_OBJECTS := $(patsubst %.c,$(BUILD)/vendor/%.o,$(VENDOR_SOURCES))
FS2_SOURCES := third_party/littlefs/lfs.c third_party/littlefs/lfs_util.c
FS2_OBJECTS := $(patsubst %.c,$(BUILD)/vendor/%.o,$(FS2_SOURCES))
OBJECTS := $(BUILD)/entry.o $(BUILD)/interrupt_stubs.o $(BUILD)/task_switch.o \
	$(BUILD)/process_asm.o $(C_OBJECTS) $(VENDOR_OBJECTS) $(BEARSSL_LIB)
DEPFILES := $(C_OBJECTS:.o=.d)

-include $(DEPFILES)

.PHONY: all image run term test check release clean doctor help crypto font unifont fs2 musl cck bearssl cxx-eval keys

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

$(BUILD)/process_asm.o: src/process.asm | $(BUILD)
	$(NASM) $(NASMFLAGS) $< -o $@

$(BUILD)/hello-program.o: programs/hello.c programs/rks_api.h | $(BUILD)
	$(CLANG) --target=x86_64-none-elf -m64 -std=c11 -O2 -Wall -Wextra -Werror \
		-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
		-fno-asynchronous-unwind-tables -fno-unwind-tables -mno-red-zone \
		-mcmodel=large -nostdlib -Iprograms -c $< -o $@

$(BUILD)/hello.bin: $(BUILD)/hello-program.o programs/rks.ld
	$(LD_LLD) -m elf_x86_64 -T programs/rks.ld --build-id=none \
		--oformat=binary -o $@ $(BUILD)/hello-program.o

$(BUILD)/HELLO.RKS: $(BUILD)/hello.bin tools/rksbuild.py | $(BUILD)
	python3 tools/rksbuild.py $(BUILD)/hello.bin $@

$(BUILD)/user_hello.o: programs/user_hello.c programs/user_syscall.h | $(BUILD)
	$(CLANG) --target=x86_64-none-elf -m64 -std=c11 -O2 -Wall -Wextra -Werror \
		-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
		-fno-asynchronous-unwind-tables -fno-unwind-tables -mno-red-zone \
		-mno-sse -mno-sse2 -mno-mmx -mno-80387 -mcmodel=large -nostdlib -Iprograms \
		-c $< -o $@

$(USER_HELLO_ELF): $(BUILD)/user_hello.o programs/user.ld
	$(LD_LLD) -m elf_x86_64 -T programs/user.ld --build-id=none -o $@ \
		$(BUILD)/user_hello.o

$(BUILD)/user_fork.o: programs/user_fork.c programs/user_syscall.h | $(BUILD)
	$(CLANG) --target=x86_64-none-elf -m64 -std=c11 -O2 -Wall -Wextra -Werror \
		-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
		-fno-asynchronous-unwind-tables -fno-unwind-tables -mno-red-zone \
		-mno-sse -mno-sse2 -mno-mmx -mno-80387 -mcmodel=large -nostdlib -Iprograms \
		-c $< -o $@

$(USER_FORK_ELF): $(BUILD)/user_fork.o programs/user.ld
	$(LD_LLD) -m elf_x86_64 -T programs/user.ld --build-id=none -o $@ \
		$(BUILD)/user_fork.o

$(BUILD)/user_fault.o: programs/user_fault.c programs/user_syscall.h | $(BUILD)
	$(CLANG) --target=x86_64-none-elf -m64 -std=c11 -O2 -Wall -Wextra -Werror \
		-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
		-fno-asynchronous-unwind-tables -fno-unwind-tables -mno-red-zone \
		-mno-sse -mno-sse2 -mno-mmx -mno-80387 -mcmodel=large -nostdlib -Iprograms \
		-c $< -o $@

$(USER_FAULT_ELF): $(BUILD)/user_fault.o programs/user.ld
	$(LD_LLD) -m elf_x86_64 -T programs/user.ld --build-id=none -o $@ \
		$(BUILD)/user_fault.o

$(BUILD)/user_exec.o: programs/user_exec.c programs/user_syscall.h | $(BUILD)
	$(CLANG) --target=x86_64-none-elf -m64 -std=c11 -O2 -Wall -Wextra -Werror \
		-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
		-fno-asynchronous-unwind-tables -fno-unwind-tables -mno-red-zone \
		-mno-sse -mno-sse2 -mno-mmx -mno-80387 -mcmodel=large -nostdlib -Iprograms \
		-c $< -o $@

$(USER_EXEC_ELF): $(BUILD)/user_exec.o programs/user.ld
	$(LD_LLD) -m elf_x86_64 -T programs/user.ld --build-id=none -o $@ \
		$(BUILD)/user_exec.o

$(BUILD)/user_io.o: programs/user_io.c programs/user_syscall.h | $(BUILD)
	$(CLANG) --target=x86_64-none-elf -m64 -std=c11 -O2 -Wall -Wextra -Werror \
		-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
		-fno-asynchronous-unwind-tables -fno-unwind-tables -mno-red-zone \
		-mno-sse -mno-sse2 -mno-mmx -mno-80387 -mcmodel=large -nostdlib -Iprograms \
		-c $< -o $@

$(USER_IO_ELF): $(BUILD)/user_io.o programs/user.ld
	$(LD_LLD) -m elf_x86_64 -T programs/user.ld --build-id=none -o $@ \
		$(BUILD)/user_io.o

$(BUILD)/user_pipe.o: programs/user_pipe.c programs/user_syscall.h | $(BUILD)
	$(CLANG) --target=x86_64-none-elf -m64 -std=c11 -O2 -Wall -Wextra -Werror \
		-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
		-fno-asynchronous-unwind-tables -fno-unwind-tables -mno-red-zone \
		-mno-sse -mno-sse2 -mno-mmx -mno-80387 -mcmodel=large -nostdlib -Iprograms \
		-c $< -o $@

$(USER_PIPE_ELF): $(BUILD)/user_pipe.o programs/user.ld
	$(LD_LLD) -m elf_x86_64 -T programs/user.ld --build-id=none -o $@ \
		$(BUILD)/user_pipe.o

$(BUILD)/user_agent.o: programs/agent_client.c programs/user_syscall.h | $(BUILD)
	$(CLANG) --target=x86_64-none-elf -m64 -std=c11 -O2 -Wall -Wextra -Werror \
		-ffreestanding -fno-builtin -fno-stack-protector -fno-pic -fno-pie \
		-fno-asynchronous-unwind-tables -fno-unwind-tables -mno-red-zone \
		-mno-sse -mno-sse2 -mno-mmx -mno-80387 -mcmodel=large -nostdlib -Iprograms \
		-c $< -o $@

$(USER_AGENT_ELF): $(BUILD)/user_agent.o programs/user.ld
	$(LD_LLD) -m elf_x86_64 -T programs/user.ld --build-id=none -o $@ \
		$(BUILD)/user_agent.o

$(MUSL_STAMP): scripts/fetch-musl.sh scripts/build-musl.sh | $(BUILD)
	./scripts/build-musl.sh

$(BUILD)/musl_hello.o: programs/musl_hello.c $(MUSL_STAMP) | $(BUILD)
	$(CLANG) --target=x86_64-none-elf -m64 -mcmodel=large -std=c11 -O2 \
		-Wall -Wextra -Werror -fno-pic -fno-pie -fno-stack-protector \
		-ffreestanding -mno-red-zone -fno-asynchronous-unwind-tables \
		-fno-unwind-tables -nostdinc -isystem $(MUSL_STAGE)/include \
		-c $< -o $@

$(MUSL_ELF): $(BUILD)/musl_hello.o $(MUSL_STAMP) programs/user.ld
	$(LD_LLD) -m elf_x86_64 -T programs/user.ld --build-id=none \
		--gc-sections -o $@ $(MUSL_STAGE)/lib/crt1.o \
		$(BUILD)/musl_hello.o $(MUSL_STAGE)/lib/libc.a

$(CCK_STAMP): scripts/fetch-tinycc.sh scripts/build-cck.sh \
	scripts/build-musl.sh programs/user.ld | $(BUILD)
	./scripts/build-cck.sh

$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CLANG) $(CFLAGS) -c $< -o $@

$(BUILD)/keyboard.o $(BUILD)/editor.o: CFLAGS += -O1

crypto:
	@./scripts/fetch-crypto.sh
	@./scripts/fetch-littlefs.sh

font:
	@./scripts/fetch-font.sh

unifont:
	@./scripts/fetch-unifont.sh

$(UNIFONT_IMAGE): scripts/fetch-unifont.sh tools/make-unifont.py | $(BUILD)
	@./scripts/fetch-unifont.sh
	python3 tools/make-unifont.py $@ third_party/unifont/unifont-base.hex \
		third_party/unifont/wqy.hex

fs2:
	@./scripts/fetch-littlefs.sh

musl:
	@./scripts/build-musl.sh

cck:
	@./scripts/build-cck.sh

bearssl:
	@./scripts/build-bearssl.sh

cxx-eval:
	@./scripts/evaluate-cxx-frontends.sh

$(BEARSSL_LIB): scripts/build-bearssl.sh scripts/fetch-crypto.sh | $(BUILD)
	./scripts/build-bearssl.sh

$(TEST_CA): scripts/make-test-cert.sh | $(BUILD)
	./scripts/make-test-cert.sh

$(CA_BUNDLE): scripts/fetch-ca-bundle.sh | $(BUILD)
	./scripts/fetch-ca-bundle.sh

keys: | $(BUILD)
	@./scripts/gen-ssh-keys.sh

$(BUILD)/crypto.o: keys
$(BUILD)/ssh.o: keys
$(BUILD)/gui.o: font
$(BUILD)/fs2.o: fs2

$(BUILD)/vendor/%.o: %.c | crypto $(BUILD)
	@mkdir -p $(dir $@)
	$(CLANG) $(CRYPTO_CFLAGS) -c $< -o $@

$(BUILD)/kernel.elf: $(OBJECTS) linker.ld
	$(LD_LLD) -m elf_x86_64 -T linker.ld --build-id=none --gc-sections \
		-o $@ $(OBJECTS)

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/fs2-root/.stamp: $(BUILD)/HELLO.RKS $(USER_ELVES) \
	tools/make-fs2-root.py $(MUSL_STAMP) $(CCK_STAMP) \
	$(UNIFONT_IMAGE) $(TEST_CA) $(CA_BUNDLE) | $(BUILD)
	python3 tools/make-fs2-root.py $(BUILD)/fs2-root $(BUILD)/HELLO.RKS \
		$(USER_ELVES) --musl $(MUSL_STAGE) \
		--toolchain $(CCK_STAGE) --sample programs/cck_hello.c \
		--sdk include --license third_party/musl/COPYRIGHT \
		--tinycc-license third_party/tinycc/COPYING \
		--font $(UNIFONT_IMAGE) --unifont-license third_party/unifont/COPYING \
		--trust-anchor $(TEST_CA) --ca-bundle $(CA_BUNDLE)
	touch $@

$(BUILD)/mkfs2: tools/mkfs2.c third_party/littlefs/lfs.c third_party/littlefs/lfs_util.c | fs2 $(BUILD)
	$(CLANG) -O2 -Ithird_party/littlefs tools/mkfs2.c \
		third_party/littlefs/lfs.c third_party/littlefs/lfs_util.c -o $@

$(FS2_IMAGE): $(BUILD)/mkfs2 $(BUILD)/fs2-root/.stamp
	$(BUILD)/mkfs2 $@ $(BUILD)/fs2-root

image: $(BUILD)/boot.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin \
	$(BUILD)/HELLO.RKS $(USER_ELVES) $(FS2_IMAGE)
	@size=$$(wc -c < $(BUILD)/kernel.bin | tr -d ' '); \
	if [ "$$size" -gt "$(KERNEL_MAX_BYTES)" ]; then \
		echo "Kernel is $$size bytes; boot limit is $(KERNEL_MAX_BYTES)." >&2; \
		exit 1; \
	fi
	dd if=/dev/zero of=$(IMAGE) bs=512 count=2880 2>/dev/null
	dd if=$(BUILD)/boot.bin of=$(IMAGE) conv=notrunc bs=512 seek=0 2>/dev/null
	dd if=$(BUILD)/stage2.bin of=$(IMAGE) conv=notrunc bs=512 seek=1 2>/dev/null
	dd if=$(BUILD)/kernel.bin of=$(IMAGE) conv=notrunc bs=512 seek=33 2>/dev/null
	@if [ ! -f "$(DATA_IMAGE)" ]; then \
		dd if=/dev/zero of=$(DATA_IMAGE) bs=512 count=4096 2>/dev/null; \
	fi
	python3 tools/install-rks-disk.py $(DATA_IMAGE) $(BUILD)/HELLO.RKS HELLO.RKS
	@echo "Built $(IMAGE) (1.44 MB bootable x86 image)"

run: image
	@test -n "$(QEMU)" || (echo "Install QEMU: brew install qemu" >&2; exit 1)
	@status=0; \
	$(QEMU) -m 64M -drive file=$(IMAGE),format=raw,if=floppy -boot a \
		-cpu max \
		-drive file=$(DATA_IMAGE),format=raw,if=ide,index=0 \
		-drive file=$(FS2_IMAGE),format=raw,if=ide,index=1 \
		$(NETWORK_ARGS) \
		$(QEMU_DISPLAY_ARG) -serial stdio -monitor none \
		-device isa-debug-exit || status=$$?; \
	if [ $$status -ne 0 ] && [ $$status -ne 33 ]; then exit $$status; fi

term: image
	@test -n "$(QEMU)" || (echo "Install QEMU: brew install qemu" >&2; exit 1)
	@status=0; \
	$(QEMU) -m 64M -drive file=$(IMAGE),format=raw,if=floppy -boot a \
		-cpu max \
		-drive file=$(DATA_IMAGE),format=raw,if=ide,index=0 \
		-drive file=$(FS2_IMAGE),format=raw,if=ide,index=1 \
		$(NETWORK_ARGS) \
		-display none -serial stdio -monitor none -device isa-debug-exit || status=$$?; \
	if [ $$status -ne 0 ] && [ $$status -ne 33 ]; then exit $$status; fi

test: $(BUILD)/boot.bin $(BUILD)/stage2.bin $(BUILD)/kernel.bin \
	$(BUILD)/HELLO.RKS $(USER_ELVES) $(FS2_IMAGE)
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
