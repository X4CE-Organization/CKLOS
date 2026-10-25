# CKLOS Architecture

## Boot

1. BIOS 从 1.44 MiB 软盘镜像的第一个扇区加载 `boot.asm` 到 `0x7C00`。
2. 引导器通过 INT 13h 读取 100 个内核扇区到物理地址 `0x00010000`。
3. BIOS E820 内存表写入 `0x00000600`，启动信息位于 `0x00000500`。
4. 引导器启用 A20 和 32 位平坦段保护模式，并把启动信息指针传给内核。

## Memory map

| Address | Purpose |
| --- | --- |
| `0x00000500` | Boot information |
| `0x00000600` | E820 memory entries |
| `0x00007C00` | Temporary bootloader |
| `0x00010000` | Kernel image |
| `0x00090000` | Initial kernel stack |
| `0x000B8000` | VGA text buffer |
| `0x00200000` | Page directory |
| `0x00400000-0x004FFFFF` | Kernel heap |
| `0x00500000-0x03FFFFFF` | Page-frame allocator |

内核启用 64 MiB 恒等映射。当前使用 4 MiB PSE 页，目的是先建立稳定、可验证的分页环境；用户态隔离阶段会改为带 U/S 位的 4 KiB 页表。

## Interrupts

`interrupt_stubs.asm` 提供 32 个 CPU 异常入口和 16 个硬件 IRQ 入口。`interrupts.c` 安装 256 项 IDT，把 8259 PIC 重映射到 `0x20`，并以 100 Hz 配置 PIT。

- IRQ0：递增 tick，完成 EOI 后调用抢占式调度器。
- IRQ1：读取 PS/2 扫描码并写入键盘环形队列。
- CPU 异常：输出名称、错误码和 EIP 后停止系统，避免继续破坏状态。

## Scheduler

调度器支持 8 个 ring-0 内核任务。每个任务拥有 16 KiB 栈，任务上下文由 `task_switch.asm` 保存 `edi/esi/ebx/ebp` 和栈指针。

PIT 中断执行轮转抢占。任务阻塞时，Shell 的输入循环和 `sleep` 仍会主动调用 `task_yield`。停止任务会回收栈；停止槽可被后续任务复用。

当前调度器尚未实现优先级、阻塞队列、临界区和 SMP。

## Storage

文件系统使用 24 个固定文件槽，每个文件最多 512 字节。RAM 中的可写文件可直接操作；`disksave` 将可写文件写入 ATA PIO 主盘。

CKLOSFS1 布局：

| LBA | Contents |
| --- | --- |
| 0 | 超级块 |
| 1-24 | 文件元数据 |
| 25-48 | 文件内容 |

元数据包括文件名、大小、标志和内容扇区。当前没有目录树、权限、日志或崩溃恢复；磁盘卸载前应执行 `disksave`。

## Known constraints

- 仅有 ring-0 内核任务，没有独立用户地址空间。
- 没有 POSIX 系统调用，也没有 ELF 程序加载器。
- ATA PIO 是同步轮询实现，不支持热插拔、DMA 或多磁盘策略。
- 没有网络协议栈、USB、电源管理或图形桌面。
- 恒等映射是当前阶段的过渡设计，不应用于安全隔离。
