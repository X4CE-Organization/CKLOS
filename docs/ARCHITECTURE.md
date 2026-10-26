# CKLOS Architecture

## Boot

1. BIOS 从 1.44 MiB 软盘镜像的第一个扇区加载 `boot.asm` 到 `0x7C00`。
2. 第一级引导器读取 32 个扇区，把第二级引导器加载到 `0x00010000`。
3. 第二级引导器从 LBA 33 开始读取最多 200 个内核扇区到 `0x00020000`。
4. 第二级引导器先进入 32 位平坦保护模式，再建立 PML4/PDPT/PD 页表并进入 x86-64 长模式。
5. BIOS E820 内存表写入 `0x00000600`，启动信息位于 `0x00000500`。

## Memory map

| Address | Purpose |
| --- | --- |
| `0x00000500` | Boot information |
| `0x00000600` | E820 memory entries |
| `0x00007C00` | Temporary bootloader |
| `0x00010000` | Second-stage bootloader |
| `0x00020000` | Kernel image |
| `0x00090000` | Initial kernel stack |
| `0x000B8000` | VGA text buffer |
| `0x00200000` | Page directory |
| `0x00400000-0x004FFFFF` | Kernel heap |
| `0x00500000-0x03FFFFFF` | Page-frame allocator |

引导器使用 PML4/PDPT/PD 建立 1 GiB 恒等映射，内核当前使用前 64 MiB。每个用户进程使用私有 PML4 和 PDPT：低地址内核映射保持 supervisor-only，ELF 段映射到 `0x4000000000`，用户栈位于 `0x7FFFFFF000` 以下。

## User mode

`process.asm` 安装包含内核代码/数据、用户代码/数据和 64 位 TSS 的 GDT。用户代码通过 `int 0x80` 进入内核；处理器从 TSS 切换到专用异常栈。系统调用和硬件中断返回前会保存或恢复用户寄存器、SSE/x87 状态、FS base 和 CR3。

进程表支持 8 个隔离进程、父子关系、等待状态、退出状态、文件描述符、私有页表和匿名映射。用户异常只终止当前进程。调度器每 10 个 PIT tick 在内核返回路径切换一次 ring-3 上下文。

## Interrupts

`interrupt_stubs.asm` 提供 32 个 CPU 异常入口和 16 个硬件 IRQ 入口。`interrupts.c` 安装 256 项 IDT，把 8259 PIC 重映射到 `0x20`，并以 100 Hz 配置 PIT。

- IRQ0：递增 tick 并完成 EOI。
- IRQ1：读取 PS/2 扫描码并写入键盘环形队列。
- IRQ12：读取 PS/2 鼠标三字节数据包并更新软件光标。
- CPU 异常：输出名称、错误码和 EIP 后停止系统，避免继续破坏状态。

## Graphics

The second-stage loader queries VBE, selects a 1024x768x32 linear mode and maps
the third and fourth GiB of physical memory through a second page directory so
the framebuffer address is available in long mode. The kernel falls back to
800x600x32 and then VGA mode 13h.

The GUI supports 32-bit, 16-bit and 8-bit framebuffer writes with a dynamic
layout. GNU Unifont/WQY glyph data is loaded from CKLOSFS2 into the kernel heap;
the console and `cki` use the same UTF-8 decoder and glyph renderer.

## Scheduler

调度器支持 8 个 ring-0 内核任务。每个任务拥有 16 KiB 栈，64 位上下文由 `task_switch.asm` 保存 SysV 非易失寄存器和栈指针。

Shell 输入循环和 `sleep` 主动调用 `task_yield` 完成协作式轮转。停止任务会回收栈；停止槽可被后续任务复用。

当前调度器尚未实现优先级、阻塞队列、临界区和 SMP。

## Storage

CKLOSFS2 使用 128 MiB 稀疏 ATA 数据盘和 4 KiB 块，LittleFS 2.11.3 提供目录、可变长度文件和元数据原子更新。超级块位于 LBA 0；文件系统数据从 LBA 8 开始。VFS 路径支持 4096 字节绝对路径和 255 字节 UTF-8 文件名。

旧 CKLOSFS1 继续保留在独立的 2 MiB 数据盘上：

CKLOSFS1 布局：

| LBA | Contents |
| --- | --- |
| 0 | 超级块 |
| 1-24 | 文件元数据 |
| 25-48 | 文件内容 |

FS1 元数据包括文件名、大小、标志和内容扇区。`fsmigrate` 只把 FS1 用户文件复制到 CKLOSFS2 的 `/import/`，不会覆盖已有路径。FS2 使用 `fscheck` 遍历元数据并检查块分配。

## Known constraints

- 已有 ring-3 用户进程，但内核后台任务仍运行在 ring 0。
- 已实现 POSIX 风格系统调用子集；没有动态链接和 Linux ELF 兼容保证。
- 信号与 PTY 语义仍不完整。
- ATA PIO 是同步轮询实现，不支持热插拔、DMA 或多磁盘策略。
- 没有 USB、电源管理或完整图形桌面。
- 恒等映射是当前阶段的过渡设计，不应用于安全隔离。
