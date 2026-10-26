#ifndef CKLOS_SYSCALL_H
#define CKLOS_SYSCALL_H

/* Linux x86-64 numbering for the subset implemented by CKLOS. */
enum {
    SYS_READ = 0,
    SYS_WRITE = 1,
    SYS_OPEN = 2,
    SYS_CLOSE = 3,
    SYS_POLL = 7,
    SYS_MMAP = 9,
    SYS_MUNMAP = 11,
    SYS_BRK = 12,
    SYS_IOCTL = 16,
    SYS_WRITEV = 20,
    SYS_PIPE = 22,
    SYS_GETPID = 39,
    SYS_FORK = 57,
    SYS_EXECVE = 59,
    SYS_EXIT = 60,
    SYS_WAIT4 = 61,
    SYS_KILL = 62,
    SYS_YIELD = 24,
    SYS_DUP = 32,
    SYS_DUP2 = 33,
    SYS_ARCH_PRCTL = 158,
    SYS_SET_TID_ADDRESS = 218,
    SYS_EXIT_GROUP = 231,
    SYS_SET_ROBUST_LIST = 273,
    SYS_RSEQ = 334
};

#define CKLOS_EFAULT 14
#define CKLOS_ENOENT 2
#define CKLOS_EBADF 9
#define CKLOS_ECHILD 10
#define CKLOS_EAGAIN 11
#define CKLOS_ENOMEM 12
#define CKLOS_EINVAL 22
#define CKLOS_EMFILE 24
#define CKLOS_ENOTTY 25
#define CKLOS_ENOSYS 38

#endif
