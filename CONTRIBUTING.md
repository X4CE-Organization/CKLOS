# Contributing to CKLOS

CKLOS 欢迎错误报告、文档改进、驱动实现和内核功能提交。

## 开发环境

macOS：

```sh
brew install nasm llvm lld qemu
```

Ubuntu / Debian：

```sh
sudo apt-get update
sudo apt-get install -y clang lld nasm qemu-system-x86 make
```

检查工具链并运行完整测试：

```sh
make doctor
make check
```

## 提交要求

- 每个提交只解决一个明确问题。
- 不要提交 `build/`、镜像、`.DS_Store`、编辑器配置或临时备份。
- 新增内核代码必须使用 freestanding C11，不能依赖宿主 libc。
- 修改中断、分页、调度器或文件系统时，必须说明失败路径和资源回收方式。
- 修改 Shell 行为时，同时更新 `README.md` 和自动测试。
- 代码必须通过 `-Wall -Wextra -Werror`。

## 提交信息

推荐使用 Conventional Commits：

```text
feat(vfs): add file copy command
fix(scheduler): reclaim stacks from stopped tasks
docs: describe CKLOSFS1 layout
test: cover persisted file reload
```

## 内核开发约定

- 物理页、任务栈和其他动态资源必须有明确的所有者。
- 不允许在中断处理中执行长时间轮询或阻塞式终端输出。
- 共享状态在进入用户态阶段前必须补充锁和抢占语义。
- 新增硬件驱动应提供初始化、错误返回和 QEMU 回归测试。
- 不得未经讨论引入 GPL 代码；本项目采用 MIT 许可证。

## 报告问题

请附上提交版本、宿主系统、QEMU 命令、完整串口日志和复现步骤。内核异常日志中的 vector、error code、EIP 和 CR2 尤其重要。
