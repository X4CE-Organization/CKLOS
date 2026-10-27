# CKLOS

CKLOS 是一个独立启动的 x86-64 图形操作系统。它不运行在 Linux、macOS 或 DOS 之上，也不依赖宿主系统运行时。

启动链路：

```text
BIOS -> CKLOS 两阶段引导器 -> x86-64 长模式 -> C 内核 -> CKLOS GUI
```

项目文件夹和系统中的正式名称统一为 `CKLOS`。

## 已实现功能

- 1.44 MB 自启动软盘镜像和自有 x86 引导程序
- x86-64 长模式内核，不使用宿主标准库或运行时
- 256 项 IDT、8259 PIC 中断重映射和 100 Hz PIT 定时器
- CPU 异常捕获，可显示异常类型、错误码和故障地址
- 64 MiB 恒等分页、页帧分配器和 1 MiB 内核堆
- 基于 PIT 的轮转任务调度，最多同时管理 8 个内核任务
- 可停止并回收栈资源的用户内核任务
- VBE 1024×768×32 线性帧缓冲、800×600/VGA 回退、窗口式终端、COM1、PS/2 键盘和鼠标
- 命令历史、上下方向键调用、Ctrl+L 清屏和 Ctrl+C 取消
- `.CKL` 文本命令脚本、文件复制、内容统计和首尾行查看
- RKS1 x86-64 可执行格式、运行时校验和加载以及程序 API
- 可写 12 KiB 内存文件系统，内置只读系统文件
- ATA PIO 数据盘和 CKLOSFS1 持久化格式，用户文件可跨重启保存
- 128 MiB CKLOSFS2 数据盘、目录树、255 字节 UTF-8 文件名和最大 64 MiB 的可变长度文件
- LittleFS 2.11.3 原子元数据更新、启动恢复、目录重命名与 `FSMIGRATE`/`FSCHECK`
- ELF64 静态程序加载、ring 3、每进程 CR3、用户栈和内核/用户内存隔离
- `int 0x80` 系统调用、`fork`/`execve`/`wait4`、管道、文件描述符继承和定时抢占
- 固定版本 musl 静态 libc、标准/POSIX 头文件和 `/usr/lib/libc.a`
- 系统原生 `cck` C 编译器、TinyCC 后端和 C89 到 C23/GNU 模式选择
- 带 SHA-256 校验的 RKS2 静态可执行容器和默认 `a.rks` 输出
- GNU Unifont/WQY Unicode 字体、UTF-8 中文显示和非模态编辑器 `cki`
- RTL8139 IPv4 TCP/UDP 客户端、DNS A/AAAA、HTTP/1.1 和 BearSSL TLS 1.2
- `cwgt`/`curl` 下载、重定向、POST、Basic Auth、请求头、Range 与断点续传
- CMOS 实时时钟、CPU 信息、内存布局和设备诊断
- 整数计算、十六进制查看、颜色主题和 PC 扬声器测试

这是 CKLOS 0.15.0 的开源 x86-64 图形内核版本。它已经有长模式、分页、任务切换、两级持久化存储、ELF64 用户态隔离、静态 musl libc、系统内 C 编译器、VBE 高分辨率图形、UTF-8 编辑器、IPv4/IPv6 TCP、DNS、HTTP、TLS、SSH/SFTP 和可执行文件。0.13.0 同时提供 Clang/LLVM 与 GCC 的 C++ 前端独立评估，但尚未提供 C++ 编译器。系统还不是 Linux 那样的完整通用操作系统：USB、完整桌面、完整 SSH 转发和更完整的 curl/wget 兼容性尚未完成。

## 安装依赖

需要 macOS 的 Homebrew：

```sh
brew install nasm llvm lld qemu
```

## 启动命令

打开“终端”，进入 CKLOS 文件夹，然后编译并启动：

```sh
cd "/Users/wushicheems/Desktop/CKLOS"
make
make run
```

`make run` 会在 macOS 上打开启用 `zoom-to-fit` 和 `show-cursor` 的 QEMU 窗口，并在当前终端中同步显示串口输出。系统优先进入 1024×768×32 VBE 模式，失败时回退到 800×600 或 VGA 320×200。点击窗口后移动鼠标，CKLOS 会显示并移动自己的 PS/2 软件光标；顶部 `SYS`、`NET`、`CLR` 按钮可以直接点击。

QEMU 捕获鼠标后，按 `Control + Option + G`（QEMU 文档中写作 `Ctrl+Alt+G`）即可释放鼠标和键盘，把控制权交还 macOS。再次按相同快捷键会重新捕获。

默认 SSH 主机转发端口为 `2222`。如果该端口已被占用，可以改用其他端口：

```sh
make run HOST_SSH_PORT=2224
```

如果 QEMU 报告 `Could not set up host forwarding rule`，请关闭之前残留的 QEMU 进程或更换 `HOST_SSH_PORT`。

只使用纯终端模式：

```sh
cd "/Users/wushicheems/Desktop/CKLOS"
make term
```

在 CKLOS Shell 中输入 `shutdown` 关闭虚拟机。`control-C` 可以中止当前的 `make run` 或 `make term` 命令。

如果希望窗口像普通终端一样自由改变宽高，请使用 `make term`。串口控制台会跟随宿主终端尺寸自动换行，因此这是调整命令行窗口大小最方便的方式。也可以执行 `make run QEMU_RUN_DISPLAY=cocoa,zoom-to-fit=off` 关闭自动缩放。

生成的可启动镜像位于 `build/cklos.img`，旧版 2 MiB 数据盘位于 `build/cklos-data.img`，CKLOSFS2 数据盘位于 `build/cklos-fs2.img`。删除数据盘会清除已保存的用户文件。执行自动启动、调度、存储与跨重启测试：

```sh
make test
```

## Shell 命令

| 命令 | 作用 |
| --- | --- |
| `help` | 显示完整命令列表 |
| `about` / `version` / `uname` | 查看 CKLOS 信息 |
| `sysinfo` | 查看 CPU、内核、BSS、中断和定时器状态 |
| `uptime` | 显示 PIT 定时器运行时间 |
| `sleep 2` | 使用定时器延时指定秒数 |
| `mem` / `devices` | 查看内存布局和设备 |
| `heap` | 查看 1 MiB 内核堆使用情况 |
| `memtest` | 实测内核堆和物理页分配器 |
| `tasks` | 查看任务、状态、让出次数和工作计数 |
| `spawn worker` / `spawn idle` | 创建协作式内核任务 |
| `kill ID` / `killall` | 停止用户内核任务并回收任务栈 |
| `history` | 查看命令历史 |
| `history -c` | 清空命令历史 |
| `ls` / `stat FILE` | 列出文件或查看文件属性 |
| `cat FILE` / `hexdump FILE` | 输出文本或十六进制内容 |
| `cp SRC DST` | 复制文件 |
| `wc FILE` | 统计行数、单词数和字节数 |
| `head FILE N` / `tail FILE N` | 输出前 N 行或后 N 行 |
| `write FILE TEXT` | 创建或覆盖可写文件 |
| `append FILE TEXT` | 向可写文件追加文本 |
| `touch FILE` | 创建空文件 |
| `mv OLD NEW` / `rm FILE` | 重命名或删除文件 |
| `df` | 查看内存文件系统容量 |
| `disk` | 查看 ATA 数据盘和已保存文件数 |
| `disksave` | 把所有可写用户文件保存到 ATA 数据盘 |
| `diskload` | 从 ATA 数据盘重新载入用户文件 |
| `fsls PATH` / `fscd PATH` | 浏览 CKLOSFS2 目录树或切换目录 |
| `fspwd` | 显示 CKLOSFS2 当前目录 |
| `fsmkdir PATH` | 创建 CKLOSFS2 目录 |
| `fswrite FILE TEXT` / `fscat FILE` | 写入或读取可变长度文件 |
| `fsfill FILE BYTES` | 创建指定长度的文件，用于容量和持久化测试 |
| `fsmv OLD NEW` / `fsrm PATH` | 原子重命名或删除 CKLOSFS2 路径 |
| `fsmigrate` | 把 CKLOSFS1 用户文件导入 `/import`，不覆盖已有文件 |
| `fscheck` | 遍历 CKLOSFS2 元数据并检查块分配 |
| `run DEMO.CKL` | 执行内置演示脚本 |
| `run FILE.CKL` | 执行用户命令脚本，支持最多四层嵌套 |
| `exec HELLO.RKS` | 校验并执行 RKS1 可执行文件 |
| `elfexec FILE.ELF` | 在 ring 3 装载并执行静态 ELF64 程序 |
| `procs` | 显示活动进程数和当前 PID |
| `cck FILE.c` | 使用 TinyCC 后端编译静态 ELF64，并默认生成 `a.rks` |
| `cki FILE` | 打开支持中文、鼠标定位、查找、保存和撤销的全屏编辑器 |
| `dns HOST` | 查询 A 和 AAAA 记录 |
| `cwgt URL` | 下载 HTTP/HTTPS 内容，支持重定向、POST、请求头和断点续传 |
| `curl URL` | 支持 GET/HEAD/自定义方法、输出、认证、Cookie、Range 和重定向 |
| `rks` | 显示 RKS 运行时和已安装程序数量 |
| `calc 42 '*' 7` | 支持加减乘除、取余、位运算和移位 |
| `color green` | 切换终端主题 |
| `date` / `time` | 读取 CMOS 实时时钟 |
| `panic` | 主动触发除零异常，验证异常保护后停机 |
| `shutdown` / `reboot` / `halt` | 关机、重启或停止 CPU |

方向键上下可以调用历史命令。命令和文件名不区分大小写；`cat`、`write` 等命令会保留文本参数中的大小写。

## RKS 可执行文件

`HELLO.RKS` 是真正的外部可执行文件，存放在 CKLOSFS 数据盘，不编入内核。运行时加载器会验证 `RKS1` 魔数、版本、入口、大小和校验和，再把程序装载到 `0x00300000` 执行。

```text
CKLOS> ls
HELLO.RKS  313 bytes  [RW]
CKLOS> exec HELLO.RKS
RKS executable is running!
```

示例源码位于 `programs/hello.c`，ABI 在 `programs/rks_api.h`。`RKS1` 是兼容的旧式 ring-0 程序格式，只用于现有演示。新的隔离程序使用 `ELFEXEC`，运行在 ring 3；示例包括 `USERHELLO.ELF`、`USERFORK.ELF`、`USERFAULT.ELF`、`USEREXEC.ELF`、`USERIO.ELF`、`USERPIPE.ELF` 和链接 musl 的 `MUSLHELLO.ELF`。

`cck` 使用固定提交的 TinyCC 作为 C 后端。它接受 C89/C90、C99、C11、C17、C23 和对应 GNU 模式，并明确拒绝不存在的 `-std=c98`。`-o app.elf` 生成静态 ELF64；`-o app.rks` 或无 `-o` 生成经 BearSSL SHA-256 校验的 RKS2。C++ 不由 `cck` 处理，具体限制见 [docs/CCK.md](docs/CCK.md)。

C++ 前端只完成了独立评估：`make cxx-eval` 会实际编译一个 Clang/LLVM
freestanding C++23 目标文件；GCC 与标准库移植的依赖、许可证、风险和
工作量见 [docs/CXX_FRONTENDS.md](docs/CXX_FRONTENDS.md)。这不等于 CKLOS
已经拥有 `g++` 或 C++ 运行时。

`cki` 使用 VBE 线性帧缓冲和 GNU Unifont/WQY 中文点阵字体。`Ctrl+S` 保存，`Ctrl+O` 打开，`Ctrl+F` 查找，`Ctrl+Z` 撤销，`Ctrl+Q` 退出；鼠标点击可定位光标。

## SSH

当前稳定构建已经包含 RTL8139、ARP、IPv4、ICMP 和 TCP，并配置了 QEMU 主机端口 `2222` 到客户机端口 `22` 的转发。`ping 10.0.2.2` 和主机 TCP 连接均已验证。

SSH-2 服务端使用 Curve25519、Ed25519、AES-128-CTR 和 HMAC-SHA256，
支持 `cklos` 公钥登录、PTY、Shell 和单次命令。首次构建会生成
`keys/cklos_user`，连接方法见 [docs/SSH.md](docs/SSH.md)。当前限制是单客户端、
仅公钥认证，尚未实现 SFTP、端口转发和完整窗口流控。

## 开源项目

CKLOS 使用 [MIT License](LICENSE)，允许使用、修改、分发和商业集成，但必须保留版权与许可声明。

- [CONTRIBUTING.md](CONTRIBUTING.md)：开发环境、代码约定和提交流程
- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)：内存布局、中断、调度器和存储设计
- [ROADMAP.md](ROADMAP.md)：用户态、驱动、网络和 GUI 的分阶段计划
- [CHANGELOG.md](CHANGELOG.md)：版本变更记录
- [SECURITY.md](SECURITY.md)：当前安全边界和漏洞报告方式
- [CODE_OF_CONDUCT.md](CODE_OF_CONDUCT.md)：社区协作规范

## 代码结构

```text
src/boot.asm            第一级引导扇区
src/stage2.asm          第二级引导、内核加载、分页和长模式切换
src/entry.asm           x86-64 内核入口和 BSS 初始化
src/interrupt_stubs.asm 异常与硬件中断入口
src/interrupts.c        IDT、PIC、PIT 和异常处理
src/memory.c            E820 内存、分页、页帧分配器和内核堆
src/tasks.c             协作式轮转任务调度器
src/task_switch.asm     任务上下文切换
src/disk.c              ATA PIO 驱动与 CKLOSFS1 持久化
src/fs2.c               CKLOSFS2、目录路径、迁移和一致性检查
src/process.c           ELF64、ring 3、进程、系统和文件描述符
src/process.asm         GDT/TSS、用户态入口和返回内核上下文
src/syscall.h           CKLOS/Linux x86-64 兼容系统调用编号
src/rks2.c              RKS2 容器、SHA-256 和 ELF64 载荷校验
src/unicode.c           UTF-8 编解码与 Unifont 字形查询
src/editor.c            `cki` 非模态全屏编辑器
src/kernel.c            内核初始化
src/gui.c               VGA 帧缓冲、窗口、按钮、面板和软件光标
src/mouse.c             PS/2 鼠标 IRQ12 驱动
src/console.c           图形与串口终端路由
src/keyboard.c          PS/2、串口输入和历史按键
src/vfs.c               可写内存文件系统
src/shell.c             CKLOS Shell
src/version.h           集中式版本信息
linker.ld               内核链接布局
```

内核入口位于 `0x00020000`，临时栈顶位于 `0x00090000`。第一级引导器加载第二级引导器；第二级通过 BIOS 读取内核并把内核加载到低端内存。内核随后建立恒等分页、中断表、内核堆、任务调度器和持久化存储，再进入命令循环。

## 网络与后续路线

HTTP/1.1、HTTPS、DNS A/AAAA 查询和 IPv4/IPv6 客户端工具已经可用，并同时
验证本地测试 CA 与固定 Mozilla CA bundle。SSH 已可用，HTTP/2/3、完整代理和
更完整的 curl/wget 参数仍在后续批次。
