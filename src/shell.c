#include "console.h"
#include "disk.h"
#include "fs2.h"
#include "interrupts.h"
#include "io.h"
#include "keyboard.h"
#include "memory.h"
#include "mouse.h"
#include "network.h"
#include "process.h"
#include "rks.h"
#include "shell.h"
#include "string.h"
#include "tasks.h"
#include "vfs.h"
#include "version.h"

#define LINE_SIZE 256
#define MAX_ARGS 32
#define HISTORY_MAX 16

static char line[LINE_SIZE];
static char *arguments[MAX_ARGS];
static char history[HISTORY_MAX][LINE_SIZE];
static uint32_t history_count;
static uint32_t script_depth;

extern uint8_t __bss_start[];
extern uint8_t __bss_end[];

static void normalize_command(char *command);
static int split_arguments(char *command);
static void join_arguments(int first, int argc, char *output, uint32_t capacity);
static void run_command(int argc);

static uint8_t bcd_to_binary(uint8_t value) {
    return (uint8_t)((value & 0x0F) + ((value >> 4) * 10));
}

static uint8_t cmos_read(uint8_t register_index) {
    outb(0x70, register_index);
    io_wait();
    return inb(0x71);
}

static void read_clock(uint8_t *hour, uint8_t *minute, uint8_t *second,
                       uint8_t *day, uint8_t *month, uint16_t *year) {
    while (cmos_read(0x0A) & 0x80) {
        cpu_pause();
    }
    uint8_t status_b = cmos_read(0x0B);
    bool binary = (status_b & 0x04) != 0;
    bool twelve_hour = (status_b & 0x02) == 0;
    uint8_t raw_hour = cmos_read(0x04);
    bool afternoon = (raw_hour & 0x80) != 0;
    raw_hour &= 0x7F;

    *second = cmos_read(0x00);
    *minute = cmos_read(0x02);
    *hour = raw_hour;
    *day = cmos_read(0x07);
    *month = cmos_read(0x08);
    uint8_t raw_year = cmos_read(0x09);
    uint8_t century = cmos_read(0x32);

    if (!binary) {
        *second = bcd_to_binary(*second);
        *minute = bcd_to_binary(*minute);
        *hour = bcd_to_binary(*hour);
        *day = bcd_to_binary(*day);
        *month = bcd_to_binary(*month);
        raw_year = bcd_to_binary(raw_year);
        century = bcd_to_binary(century);
    }
    if (twelve_hour) {
        if (*hour == 12) {
            *hour = 0;
        }
        if (afternoon) {
            *hour = (uint8_t)(*hour + 12);
        }
    }
    *year = century >= 20 ? (uint16_t)(century * 100 + raw_year)
                          : (uint16_t)(2000 + raw_year);
}

static void print_two(uint8_t value) {
    if (value < 10) {
        console_putc('0');
    }
    console_print_uint(value);
}

static void print_banner(void) {
    console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    console_write(
        "\n"
        "   ______ __ __ __        ____  _____\n"
        "  / ____// // // /       / __ \\/ ___/\n"
        " / /    / // // /       / / / /\\__ \\\n"
        "/ /___ / // // /___    / /_/ /___/ /\n"
        "\\____//_//_//_____/    \\____//____/\n"
        "\n"
    );
    console_set_color(COLOR_LIGHT_GRAY, COLOR_BLACK);
    console_write("  " CKLOS_NAME " " CKLOS_VERSION "  |  x86-64 graphical terminal\n");
    console_write("  Type HELP for commands, ABOUT for details.\n\n");
}

static void command_help(void) {
    console_write(
        "CKLOS Shell commands\n"
        "  HELP              Show this command list\n"
        "  ABOUT / VERSION   System and kernel information\n"
        "  UNAME             Print the operating-system name\n"
        "  SYSINFO           CPU and kernel diagnostics\n"
        "  UPTIME / SLEEP N  PIT timer uptime and delay\n"
        "  DATE / TIME       Read the CMOS real-time clock\n"
        "  MEM / DEVICES     Memory map and device inventory\n"
        "  NET / PING IP     Show or test the RTL8139 network\n"
        "  HEAP / MEMTEST    Kernel allocator usage and self-test\n"
        "  TASKS / SPAWN K   Inspect or create kernel tasks\n"
        "  HISTORY [-C]      Show or clear command history\n"
        "  LS / STAT FILE    List or inspect the RAM disk\n"
        "  CAT / HEXDUMP     Print or inspect file contents\n"
        "  CP / WC           Copy files or count lines, words and bytes\n"
        "  HEAD / TAIL FILE  Print the first or last ten lines\n"
        "  TOUCH FILE        Create an empty writable file\n"
        "  WRITE FILE TEXT   Create or overwrite a file\n"
        "  APPEND FILE TEXT  Append text to a writable file\n"
        "  MV OLD NEW / RM   Rename or remove a file\n"
        "  DF                RAM-disk capacity and usage\n"
        "  DISK / DISKSAVE   Inspect or save files to the ATA data disk\n"
        "  DISKLOAD          Reload saved user files from the ATA disk\n"
        "  RUN FILE.CKL      Execute a text command script\n"
        "  EXEC FILE.RKS     Load and execute an RKS binary\n"
        "  ELFEXEC FILE.ELF  Run a static ELF64 program in ring 3\n"
        "  PROCS             Show isolated process count and current PID\n"
        "  FSLS / FSCD       Browse the CKLOSFS2 directory tree\n"
        "  FSWRITE / FSCAT   Write or read CKLOSFS2 files\n"
        "  FSFILL FILE BYTES Create a variable-length test file\n"
        "  FSMIGRATE         Import CKLOSFS1 user files into /import\n"
        "  FSCHECK           Verify CKLOSFS2 metadata and allocations\n"
        "  ECHO TEXT         Print text\n"
        "  CALC A OP B       Integer arithmetic (+ - * / % & | ^ << >>)\n"
        "  COLOR NAME        Theme: cyan green amber blue white red\n"
        "  CLEAR / BANNER    Clear the screen or show the banner\n"
        "  BEEP              Test the PC speaker\n"
        "  PANIC             Test the CPU exception handler (halts)\n"
        "  SHUTDOWN / REBOOT Power commands; HALT stops the CPU\n"
        "\n"
        "Keys: Up/Down recalls history; Ctrl+L clears; Ctrl+C cancels.\n"
    );
}

static void command_about(void) {
    console_write(
        CKLOS_NAME " " CKLOS_VERSION "\n"
        "A freestanding graphical operating system for x86-64.\n"
        "Boot path: BIOS -> CKLOS bootloader -> protected-mode kernel -> CKLOS Shell.\n"
        "Core: flat GDT, IDT exception gates, 8259 PIC, 100 Hz PIT timer.\n"
        "Devices: VGA text, COM1 serial, PS/2 keyboard, CMOS RTC, PC speaker.\n"
        "Memory: identity paging, page allocator and a 1 MiB kernel heap.\n"
        "Tasks: cooperative x86-64 kernel scheduler.\n"
        "Storage: writable RAM disk plus persistent ATA data disk.\n"
        "The kernel is compiled from C and assembly with no host runtime.\n"
    );
}

static void command_uname(void) {
    console_write("CKLOS\n");
}

static void print_cpu_feature(const char *label, uint32_t mask) {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(1), "c"(0));
    console_printf("%s%s\n", label, (edx & mask) ? "yes" : "no");
    (void)eax;
    (void)ebx;
    (void)ecx;
}

static void command_sysinfo(void) {
    uint32_t eax;
    uint32_t ebx;
    uint32_t ecx;
    uint32_t edx;
    char vendor[13];

    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                     : "a"(0), "c"(0));
    *(uint32_t *)&vendor[0] = ebx;
    *(uint32_t *)&vendor[4] = edx;
    *(uint32_t *)&vendor[8] = ecx;
    vendor[12] = '\0';

    console_write("OS        " CKLOS_NAME " " CKLOS_VERSION "\n");
    console_write("Arch      x86-64 long mode\n");
    console_printf("CPU       %s\n", vendor);
    console_printf("CPUID     max leaf %u\n", eax);
    console_write("Kernel    loaded at 0x00020000\n");
    console_write("Stack     top at 0x00090000\n");
    console_printf("BSS       %u bytes\n", (uint32_t)(__bss_end - __bss_start));
    console_printf("RAM       %u KiB detected, 64 MiB identity-mapped\n",
                   physical_memory_bytes() / 1024);
    console_printf("Pages     %u total, %u free\n", page_total(), page_free());
    console_printf("Heap      %u / %u bytes used\n", heap_used(), heap_total());
    console_printf("Tasks     %u, %u context switches\n", task_count(), task_switches());
    console_printf("Network   RTL8139 %s\n", network_available() ? "online" : "offline");
    console_printf("Pointer   PS/2 mouse %s\n", mouse_available() ? "online" : "offline");
    console_write("Interrupt IDT, PIC 0x20, PIT 100 Hz, PS/2 mouse IRQ12\n");
    console_printf("Timer     %u ticks\n", timer_ticks());
    console_write("Display   VGA mode 13h 320x200 GUI + COM1 serial\n");
    print_cpu_feature("FPU       ", 1U << 0);
    print_cpu_feature("SSE       ", 1U << 25);
    print_cpu_feature("TSC       ", 1U << 4);
    console_write("Note      FPU/SSE are detected but not used by the kernel.\n");
}

static void command_mem(void) {
    console_write(
        "Address range        Use\n"
        "0x00000000-0x000003FF  Real-mode interrupt vectors\n"
        "0x00007C00-0x00007DFF  Bootloader (temporary)\n"
        "0x00020000-0x0004FFFF  CKLOS kernel image and BSS\n"
        "0x00090000-...         Kernel stack\n"
        "0x000B8000-0x000B8F9F  VGA text buffer\n"
        "0x00100000-...         Extended RAM (available to future code)\n"
    );
    console_printf("RAM disk usage        %u / %u bytes\n",
                   vfs_used_bytes(), vfs_capacity_bytes());
    console_printf("Physical RAM          %u bytes (%u pages free)\n",
                   physical_memory_bytes(), page_free());
    console_printf("Kernel heap           %u / %u bytes used\n",
                   heap_used(), heap_total());
}

static void command_heap(void) {
    console_printf(
        "Kernel heap\n"
        "  Range      0x00400000-0x004FFFFF\n"
        "  Total      %u bytes\n"
        "  Used       %u bytes\n"
        "  Free       %u bytes\n"
        "  Largest    %u bytes\n",
        heap_total(), heap_used(), heap_free(), heap_largest_free()
    );
}

static void format_ipv4(uint32_t address, char *output) {
    uint32_t index = 0;
    for (int shift = 24; shift >= 0; shift -= 8) {
        uint32_t value = (address >> shift) & 0xFF;
        char digits[4];
        uint_to_string(value, digits, 10);
        for (uint32_t digit = 0; digits[digit]; digit++) {
            output[index++] = digits[digit];
        }
        if (shift) {
            output[index++] = '.';
        }
    }
    output[index] = '\0';
}

static bool parse_ipv4(const char *text, uint32_t *address) {
    uint32_t result = 0;
    uint32_t octet = 0;
    uint32_t digits = 0;
    for (const char *cursor = text;; cursor++) {
        if (*cursor >= '0' && *cursor <= '9') {
            octet = octet * 10 + (uint32_t)(*cursor - '0');
            if (++digits > 3 || octet > 255) {
                return false;
            }
        } else if (*cursor == '.' || *cursor == '\0') {
            if (!digits) {
                return false;
            }
            result = result << 8 | octet;
            digits = 0;
            octet = 0;
            if (!*cursor) {
                *address = result;
                return true;
            }
            if (result > 0xFFFFFF) {
                return false;
            }
        } else {
            return false;
        }
    }
}

static void command_net(void) {
    char address[16];
    char gateway[16];
    format_ipv4(network_ip_address(), address);
    format_ipv4(network_gateway_address(), gateway);
    console_printf("Device    RTL8139 %s\n", network_available() ? "online" : "offline");
    const char *hex = "0123456789ABCDEF";
    console_write("MAC       ");
    for (uint32_t index = 0; index < 6; index++) {
        uint8_t value = network_mac_address()[index];
        console_putc(hex[value >> 4]);
        console_putc(hex[value & 0x0F]);
        console_putc(index == 5 ? '\n' : ':');
    }
    console_printf("IPv4      %s/24\n", address);
    console_printf("Gateway   %s\n", gateway);
    console_printf("Traffic   %u received, %u sent\n",
                   network_packets_received(), network_packets_sent());
    console_printf("I/O/TX    port 0x%x, %u errors, status 0x%08x, CBR %u\n",
                   network_io_port(), network_tx_errors(),
                   network_last_tx_status(), network_rx_cursor());
    console_printf("RX state  CR 0x%x, RCR 0x%08x, ISR 0x%x, clock %u\n",
                   network_control_register(), network_receive_config(),
                   network_interrupt_status(), network_clock_state());
    console_printf("TCP       port %u, remote %u, data %u segment(s), %u byte(s)\n",
                   network_tcp_local_port(), network_tcp_remote_port(),
                   network_tcp_data_segments(), network_tcp_data_bytes());
    console_printf("TCP seq   accepted %u, last 0x%x, expected 0x%x\n",
                   network_tcp_accepted_segments(),
                   network_tcp_last_sequence(), network_tcp_expected_sequence());
    console_printf("TCP writes %u attempt(s), %u failure(s), state %u, segment %u, IPv4 %u\n",
                   network_tcp_write_attempts(), network_tcp_write_failures(),
                   network_tcp_last_write_state(), network_tcp_segment_failure(),
                   network_ipv4_send_failure());
    console_write("SSH       not enabled until authenticated SSH-2 is ready\n");
}

static void command_mouse(void) {
    console_printf("PS/2 mouse: %s\nX: %d  Y: %d  Left: %s\n",
                   mouse_available() ? "online" : "offline",
                   mouse_x(), mouse_y(), mouse_left_down() ? "down" : "up");
}

static void command_ping(int argc) {
    if (argc != 2) {
        console_write("Usage: PING IPV4_ADDRESS\n");
        return;
    }
    uint32_t address;
    if (!parse_ipv4(arguments[1], &address)) {
        console_write("PING: invalid IPv4 address.\n");
        return;
    }
    if (!network_available()) {
        console_write("PING: RTL8139 is not available.\n");
        return;
    }
    console_write("Resolving ARP and sending ICMP echo...\n");
    if (network_ping(address, 1000)) {
        console_write("Reply received.\n");
    } else {
        console_write("PING: no reply within 1000 ms.\n");
    }
}

static void tcp_diagnostic_handler(uint8_t event, const uint8_t *data, uint32_t length) {
    if (event == TCP_EVENT_CONNECTED) {
        const char *banner = "CKLOS TCP diagnostic ready\r\n";
        network_tcp_write((const uint8_t *)banner, (uint32_t)strlen(banner));
    } else if (event == TCP_EVENT_DATA) {
        network_tcp_write(data, length);
    }
}

static void command_tcp_diagnostic(void) {
    if (network_tcp_listen(2323, tcp_diagnostic_handler)) {
        console_write("TCP diagnostic echo listening on port 2323.\n");
    } else {
        console_write("TCP diagnostic: network unavailable.\n");
    }
}

static void command_memtest(void) {
    bool heap_ok = heap_self_test();
    void *pages[4] = {0, 0, 0, 0};
    bool pages_ok = true;
    for (uint32_t index = 0; index < 4; index++) {
        pages[index] = page_alloc();
        if (!pages[index]) {
            pages_ok = false;
            break;
        }
        memset(pages[index], (int)(0x31 + index), 4096);
    }
    for (uint32_t index = 0; index < 4 && pages[index]; index++) {
        uint8_t *page = (uint8_t *)pages[index];
        if (page[0] != (uint8_t)(0x31 + index) || page[4095] != (uint8_t)(0x31 + index)) {
            pages_ok = false;
        }
        page_free_address(pages[index]);
    }
    console_printf("Heap allocator: %s\n", heap_ok ? "PASS" : "FAIL");
    console_printf("Page allocator: %s\n", pages_ok ? "PASS" : "FAIL");
}

static void command_tasks(void) {
    console_printf("Tasks: %u  Context switches: %u\n", task_count(), task_switches());
    console_write("ID  STATE     NAME             YIELDS  WORK\n");
    for (uint32_t index = 0; index < task_count(); index++) {
        const kernel_task *task = task_at(index);
        console_printf("%u   %s", index, task_state_name(task->state));
        uint32_t state_length = (uint32_t)strlen(task_state_name(task->state));
        for (uint32_t space = state_length; space < 10; space++) {
            console_putc(' ');
        }
        console_write(task->name);
        uint32_t name_length = (uint32_t)strlen(task->name);
        for (uint32_t space = name_length; space < 17; space++) {
            console_putc(' ');
        }
        console_printf("%u     %u\n", task->yields, task->work);
    }
}

static void command_spawn(int argc) {
    if (argc != 2) {
        console_write("Usage: SPAWN idle|worker\n");
        return;
    }
    for (char *cursor = arguments[1]; *cursor; cursor++) {
        if (*cursor >= 'A' && *cursor <= 'Z') {
            *cursor = (char)(*cursor - 'A' + 'a');
        }
    }
    int result = tasks_spawn(arguments[1]);
    if (result == 0) {
        console_printf("Spawned %s task.\n", arguments[1]);
    } else if (result == -2) {
        console_write("SPAWN: supported task types are idle and worker.\n");
    } else if (result == -1) {
        console_write("SPAWN: no task slots or heap memory available.\n");
    } else {
        console_write("SPAWN: unknown scheduler error.\n");
    }
}

static void command_kill(int argc) {
    if (argc != 2) {
        console_write("Usage: KILL TASK_ID\n");
        return;
    }
    bool ok;
    int32_t id = parse_int(arguments[1], &ok);
    if (!ok || id < 0) {
        console_write("KILL: invalid task ID.\n");
        return;
    }
    int result = tasks_stop((uint32_t)id);
    if (result == 0) {
        console_printf("Stopped task %d.\n", id);
    } else if (result == -1) {
        console_write("KILL: task ID does not exist or is the shell.\n");
    } else if (result == -2) {
        console_write("KILL: task is already stopped.\n");
    } else {
        console_write("KILL: current task cannot stop itself.\n");
    }
}

static void command_killall(void) {
    uint32_t stopped = 0;
    for (uint32_t id = 2; id < task_count(); id++) {
        if (tasks_stop(id) == 0) {
            stopped++;
        }
    }
    console_printf("Stopped %u user task(s).\n", stopped);
}

static void command_date(void) {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    static const char *months[] = {
        "", "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    read_clock(&hour, &minute, &second, &day, &month, &year);
    if (month >= 1 && month <= 12) {
        console_printf("%s %u, %u  ", months[month], day, year);
    } else {
        console_printf("%u/%u/%u  ", month, day, year);
    }
    print_two(hour);
    console_putc(':');
    print_two(minute);
    console_putc(':');
    print_two(second);
    console_write(" (RTC local time)\n");
}

static void command_time(void) {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    read_clock(&hour, &minute, &second, &day, &month, &year);
    print_two(hour);
    console_putc(':');
    print_two(minute);
    console_putc(':');
    print_two(second);
    console_putc('\n');
}

static void command_uptime(void) {
    uint32_t all_seconds = timer_ticks() / 100;
    uint32_t days = all_seconds / 86400;
    uint32_t hours = all_seconds / 3600 % 24;
    uint32_t minutes = all_seconds / 60 % 60;
    uint32_t seconds = all_seconds % 60;
    console_printf("Uptime   %u day(s), ", days);
    print_two((uint8_t)hours);
    console_putc(':');
    print_two((uint8_t)minutes);
    console_putc(':');
    print_two((uint8_t)seconds);
    console_printf("  (%u ticks at 100 Hz)\n", timer_ticks());
}

static void command_sleep(int argc) {
    if (argc != 2) {
        console_write("Usage: SLEEP SECONDS (0-3600)\n");
        return;
    }
    bool ok;
    int32_t seconds = parse_int(arguments[1], &ok);
    if (!ok || seconds < 0 || seconds > 3600) {
        console_write("SLEEP: expected an integer from 0 to 3600.\n");
        return;
    }
    console_printf("Sleeping for %d second(s)...\n", seconds);
    timer_sleep_ms((uint32_t)seconds * 1000);
    console_write("Timer wake-up complete.\n");
}

static void command_calc(int argc) {
    if (argc != 4) {
        console_write("Usage: CALC A OP B, for example CALC 42 '*' 7\n");
        return;
    }
    bool left_ok;
    bool right_ok;
    int32_t left = parse_int(arguments[1], &left_ok);
    int32_t right = parse_int(arguments[3], &right_ok);
    if (!left_ok || !right_ok) {
        console_write("CALC: operands must be decimal or 0x-prefixed integers.\n");
        return;
    }

    const char *operator = arguments[2];
    int32_t result;
    if (strcmp(operator, "+") == 0) {
        result = left + right;
    } else if (strcmp(operator, "-") == 0) {
        result = left - right;
    } else if (strcmp(operator, "*") == 0) {
        result = left * right;
    } else if (strcmp(operator, "/") == 0) {
        if (right == 0) {
            console_write("CALC: division by zero.\n");
            return;
        }
        result = left / right;
    } else if (strcmp(operator, "%") == 0) {
        if (right == 0) {
            console_write("CALC: division by zero.\n");
            return;
        }
        result = left % right;
    } else if (strcmp(operator, "&") == 0) {
        result = left & right;
    } else if (strcmp(operator, "|") == 0) {
        result = left | right;
    } else if (strcmp(operator, "^") == 0) {
        result = left ^ right;
    } else if (strcmp(operator, "<<") == 0 || strcmp(operator, ">>") == 0) {
        if (right < 0 || right > 31) {
            console_write("CALC: shift count must be from 0 to 31.\n");
            return;
        }
        result = operator[0] == '<' ? left << right : left >> right;
    } else {
        console_write("CALC: supported operators are + - * / % & | ^ << >>.\n");
        return;
    }
    console_print_int(result);
    console_putc('\n');
}

static void command_color(int argc) {
    if (argc != 2) {
        console_write("Usage: COLOR cyan|green|amber|blue|white|red\n");
        return;
    }
    for (char *cursor = arguments[1]; *cursor; cursor++) {
        if (*cursor >= 'A' && *cursor <= 'Z') {
            *cursor = (char)(*cursor - 'A' + 'a');
        }
    }
    if (strcmp(arguments[1], "cyan") == 0) {
        console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
    } else if (strcmp(arguments[1], "green") == 0) {
        console_set_color(COLOR_LIGHT_GREEN, COLOR_BLACK);
    } else if (strcmp(arguments[1], "amber") == 0) {
        console_set_color(COLOR_YELLOW, COLOR_BLACK);
    } else if (strcmp(arguments[1], "blue") == 0) {
        console_set_color(COLOR_LIGHT_BLUE, COLOR_BLACK);
    } else if (strcmp(arguments[1], "white") == 0) {
        console_set_color(COLOR_WHITE, COLOR_BLACK);
    } else if (strcmp(arguments[1], "red") == 0) {
        console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
    } else {
        console_write("COLOR: unknown theme.\n");
        return;
    }
    console_printf("Color theme changed to %s.\n", arguments[1]);
}

static void print_vfs_error(const char *action, int result) {
    console_write(action);
    console_write(": ");
    switch (result) {
        case VFS_NOT_FOUND: console_write("file not found"); break;
        case VFS_INVALID_NAME: console_write("invalid name"); break;
        case VFS_NO_SPACE: console_write("RAM disk is full"); break;
        case VFS_READ_ONLY: console_write("file is read-only"); break;
        case VFS_NAME_EXISTS: console_write("name already exists"); break;
        case VFS_CONTENT_TOO_LARGE: console_write("file would exceed 512 bytes"); break;
        default: console_write("unknown storage error"); break;
    }
    console_putc('\n');
}

static void command_ls(void) {
    console_write("CKLOS:/  <writable RAM disk>\n");
    if (vfs_count() == 0) {
        console_write("  <empty>\n");
        return;
    }
    for (uint32_t index = 0; index < vfs_count(); index++) {
        const virtual_file *file = vfs_at(index);
        console_write("  ");
        console_write(file->name);
        console_write("  ");
        console_print_uint(file->size);
        console_write(file->read_only ? " bytes  [RO]\n" : " bytes  [RW]\n");
    }
}

static void command_cat(int argc) {
    if (argc != 2) {
        console_write("Usage: CAT FILE. Try LS first.\n");
        return;
    }
    const virtual_file *file = vfs_find(arguments[1]);
    if (!file) {
        print_vfs_error("CAT", VFS_NOT_FOUND);
        return;
    }
    console_write(file->content);
    if (file->size && file->content[file->size - 1] != '\n') {
        console_putc('\n');
    }
}

static void command_stat(int argc) {
    if (argc != 2) {
        console_write("Usage: STAT FILE\n");
        return;
    }
    const virtual_file *file = vfs_find(arguments[1]);
    if (!file) {
        print_vfs_error("STAT", VFS_NOT_FOUND);
        return;
    }
    console_printf("Name      %s\nSize      %u bytes\nMode      %s\n",
                   file->name, file->size, file->read_only ? "read-only" : "read/write");
}

static void command_df(void) {
    uint32_t count = vfs_count();
    console_printf("RAM disk  %u / %u bytes\nFiles     %u / %u\n",
                   vfs_used_bytes(), vfs_capacity_bytes(), count, VFS_MAX_FILES);
}

static void command_copy(int argc) {
    if (argc != 3) {
        console_write("Usage: CP SOURCE DESTINATION\n");
        return;
    }
    const virtual_file *source = vfs_find(arguments[1]);
    if (!source) {
        print_vfs_error("CP", VFS_NOT_FOUND);
        return;
    }
    int result = vfs_write(arguments[2], source->content, false);
    if (result != VFS_OK) {
        print_vfs_error("CP", result);
        return;
    }
    console_printf("Copied %s to %s.\n", source->name, arguments[2]);
}

static void command_wc(int argc) {
    if (argc != 2) {
        console_write("Usage: WC FILE\n");
        return;
    }
    const virtual_file *file = vfs_find(arguments[1]);
    if (!file) {
        print_vfs_error("WC", VFS_NOT_FOUND);
        return;
    }
    uint32_t lines = 0;
    uint32_t words = 0;
    bool in_word = false;
    for (uint32_t index = 0; index < file->size; index++) {
        char character = file->content[index];
        if (character == '\n') {
            lines++;
        }
        bool whitespace = character == ' ' || character == '\t' || character == '\n';
        if (!whitespace && !in_word) {
            words++;
        }
        in_word = !whitespace;
    }
    if (file->size && file->content[file->size - 1] != '\n') {
        lines++;
    }
    console_printf("%u lines  %u words  %u bytes  %s\n",
                   lines, words, file->size, file->name);
}

static void command_file_lines(int argc, bool tail) {
    if (argc < 2 || argc > 3) {
        console_write(tail ? "Usage: TAIL FILE [LINES]\n" : "Usage: HEAD FILE [LINES]\n");
        return;
    }
    const virtual_file *file = vfs_find(arguments[1]);
    if (!file) {
        print_vfs_error(tail ? "TAIL" : "HEAD", VFS_NOT_FOUND);
        return;
    }
    uint32_t requested = 10;
    if (argc == 3) {
        bool ok;
        int32_t parsed = parse_int(arguments[2], &ok);
        if (!ok || parsed < 1 || parsed > 100) {
            console_write("Line count must be between 1 and 100.\n");
            return;
        }
        requested = (uint32_t)parsed;
    }

    uint32_t total_lines = 0;
    for (uint32_t index = 0; index < file->size; index++) {
        if (file->content[index] == '\n') {
            total_lines++;
        }
    }
    if (file->size && file->content[file->size - 1] != '\n') {
        total_lines++;
    }

    uint32_t offset = 0;
    uint32_t start = 0;
    uint32_t end = file->size;
    if (tail) {
        uint32_t skip = total_lines > requested ? total_lines - requested : 0;
        for (uint32_t line = 0; line < skip; line++) {
            while (offset < file->size && file->content[offset] != '\n') {
                offset++;
            }
            if (offset < file->size) {
                offset++;
            }
        }
        start = offset;
    } else if (total_lines > requested) {
        uint32_t lines_seen = 0;
        for (offset = 0; offset < file->size; offset++) {
            if (file->content[offset] == '\n' && ++lines_seen == requested) {
                end = offset + 1;
                break;
            }
        }
    }
    for (uint32_t index = start; index < end; index++) {
        console_putc(file->content[index]);
    }
    if (end > start && file->content[end - 1] != '\n') {
        console_putc('\n');
    }
}

static void command_disk(void) {
    console_printf("Device    ATA PIO primary master\n"
                   "Present   %s\n"
                   "Capacity  %u bytes\n"
                   "Saved     %u user file(s)\n",
                   disk_available() ? "yes" : "no",
                   disk_capacity_bytes(), disk_saved_file_count());
}

static void command_disksave(void) {
    if (!disk_available()) {
        console_write("DISKSAVE: ATA data disk is not available.\n");
        return;
    }
    if (!disk_save_filesystem()) {
        console_write("DISKSAVE: write failed.\n");
        return;
    }
    console_printf("Saved %u user file(s) to the ATA data disk.\n",
                   disk_saved_file_count());
}

static void command_diskload(void) {
    if (!disk_available()) {
        console_write("DISKLOAD: ATA data disk is not available.\n");
        return;
    }
    if (!disk_load_filesystem()) {
        console_write("DISKLOAD: no valid CKLOSFS1 signature found.\n");
        return;
    }
    console_printf("Loaded %u user file(s) from the ATA data disk.\n",
                   disk_saved_file_count());
}

static void command_run(int argc) {
    if (argc != 2) {
        console_write("Usage: RUN FILE.CKL\n");
        return;
    }
    if (script_depth >= 4) {
        console_write("RUN: script recursion limit reached.\n");
        return;
    }
    const virtual_file *file = vfs_find(arguments[1]);
    if (!file) {
        print_vfs_error("RUN", VFS_NOT_FOUND);
        return;
    }
    char script[VFS_CONTENT_CAPACITY + 1];
    strcpy(script, file->content);
    console_printf("Running %s...\n", file->name);
    script_depth++;

    char *cursor = script;
    while (*cursor) {
        char *line_end = cursor;
        while (*line_end && *line_end != '\n') {
            line_end++;
        }
        bool has_more = *line_end == '\n';
        if (has_more) {
            *line_end = '\0';
        }
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
        if (*cursor && *cursor != '#') {
            int count = split_arguments(cursor);
            if (count) {
                normalize_command(arguments[0]);
                run_command(count);
            }
        }
        if (!has_more) {
            break;
        }
        cursor = line_end + 1;
    }

    script_depth--;
    console_printf("Finished %s.\n", file->name);
}

static void command_exec(int argc) {
    if (argc != 2) {
        console_write("Usage: EXEC FILE.RKS\n");
        return;
    }
    rks_run(arguments[1]);
}

static void command_elfexec(int argc) {
    if (argc != 2) {
        console_write("Usage: ELFEXEC FILE.ELF\n");
        return;
    }
    process_run_elf(arguments[1]);
}

static void command_processes(void) {
    console_printf("Processes %u  Current PID %u\n",
                   process_count(), process_current_pid());
}

static void command_rks(void) {
    console_printf("RKS1 executable runtime\nInstalled images: %u\n"
                   "Example: EXEC HELLO.RKS\n",
                   rks_installed_count());
}

static void fs2_list_item(const char *name, uint32_t size, uint8_t type) {
    console_write(type == FS2_TYPE_DIRECTORY ? "  [DIR] " : "  [FILE] ");
    console_write(name);
    if (type != FS2_TYPE_DIRECTORY) {
        console_printf("  %u bytes", size);
    }
    console_putc('\n');
}

static void command_fs2(int argc) {
    const char *path = argc > 1 ? arguments[1] : ".";
    if (!fs2_available()) {
        console_write("CKLOSFS2 is not mounted.\n");
        return;
    }
    console_printf("CKLOSFS2 %s\n", fs2_get_cwd());
    if (fs2_list(path, fs2_list_item) != 0) {
        console_printf("FSLS: cannot open %s.\n", path);
    }
}

static void command_fscd(int argc) {
    if (argc != 2) {
        console_write("Usage: FSCD PATH\n");
        return;
    }
    if (fs2_set_cwd(arguments[1]) != 0) {
        console_write("FSCD: directory not found.\n");
    }
}

static void command_fspwd(void) {
    console_write(fs2_get_cwd());
    console_putc('\n');
}

static void command_fsmkdir(int argc) {
    if (argc != 2) {
        console_write("Usage: FSMKDIR PATH\n");
        return;
    }
    console_printf(fs2_mkdir(arguments[1]) == 0 ? "Directory created.\n"
                                                : "FSMKDIR: failed.\n");
}

static void command_fswrite(int argc) {
    if (argc < 2) {
        console_write("Usage: FSWRITE FILE TEXT\n");
        return;
    }
    char text[VFS_CONTENT_CAPACITY + 1];
    join_arguments(2, argc, text, sizeof(text));
    if (fs2_write(arguments[1], text, (uint32_t)strlen(text), false) != 0) {
        console_write("FSWRITE: failed.\n");
        return;
    }
    console_write("File written.\n");
}

static void command_fscat(int argc) {
    if (argc != 2) {
        console_write("Usage: FSCAT FILE\n");
        return;
    }
    uint32_t size;
    if (fs2_stat(arguments[1], &size, 0) != 0) {
        console_write("FSCAT: file not found.\n");
        return;
    }
    uint8_t *buffer = kmalloc(size + 1);
    if (!buffer) {
        console_write("FSCAT: not enough memory.\n");
        return;
    }
    uint32_t read;
    if (fs2_read(arguments[1], buffer, size, &read) == 0) {
        buffer[read] = '\0';
        console_write((char *)buffer);
        if (read && buffer[read - 1] != '\n') console_putc('\n');
    } else {
        console_write("FSCAT: read failed.\n");
    }
    kfree(buffer);
}

static void command_fsrm(int argc) {
    if (argc != 2) {
        console_write("Usage: FSRM PATH\n");
        return;
    }
    console_printf(fs2_remove(arguments[1]) == 0 ? "Removed.\n" : "FSRM: failed.\n");
}

static void command_fsmv(int argc) {
    if (argc != 3) {
        console_write("Usage: FSMV OLD NEW\n");
        return;
    }
    console_printf(fs2_rename(arguments[1], arguments[2]) == 0 ? "Renamed.\n"
                                                              : "FSMV: failed.\n");
}

static void command_fsfill(int argc) {
    if (argc != 3) {
        console_write("Usage: FSFILL FILE BYTES\n");
        return;
    }
    bool valid;
    int32_t requested = parse_int(arguments[2], &valid);
    if (!valid || requested < 0 || requested > 64 * 1024 * 1024) {
        console_write("FSFILL: byte count must be 0..67108864.\n");
        return;
    }
    uint8_t *chunk = kmalloc(4096);
    if (!chunk) {
        console_write("FSFILL: not enough memory.\n");
        return;
    }
    for (uint32_t index = 0; index < 4096; index++) {
        chunk[index] = "CKLOSFS2\n"[(index / 9) % 9];
    }
    uint32_t remaining = (uint32_t)requested;
    uint32_t first = remaining < 4096 ? remaining : 4096;
    int result = fs2_write(arguments[1], chunk, first, false);
    remaining -= first;
    while (result == 0 && remaining) {
        uint32_t count = remaining < 4096 ? remaining : 4096;
        result = fs2_write(arguments[1], chunk, count, true);
        remaining -= count;
    }
    kfree(chunk);
    if (result == 0) {
        console_printf("FSFILL: created %s with %d bytes.\n",
                       arguments[1], (int)requested);
    } else {
        console_write("FSFILL: failed.\n");
    }
}

static void command_fsmigrate(void) {
    uint32_t imported;
    uint32_t skipped;
    int result = fs2_migrate_legacy(&imported, &skipped);
    if (result == 0) {
        console_printf("FSMIGRATE: imported %u, skipped %u existing file(s).\n",
                       imported, skipped);
    } else {
        console_write("FSMIGRATE: migration failed; existing files were preserved.\n");
    }
}

static void command_fscheck(void) {
    uint32_t used;
    uint32_t total;
    int result = fs2_fsck(&used, &total);
    if (result == 0) {
        console_printf("FSCHECK: metadata valid, %u / %u data blocks used.\n",
                       used, total);
    } else {
        console_printf("FSCHECK: errors found (%d).\n", result);
    }
}

static void join_arguments(int first, int argc, char *output, uint32_t capacity) {
    uint32_t length = 0;
    output[0] = '\0';
    for (int index = first; index < argc; index++) {
        if (index > first) {
            if (length + 1 >= capacity) {
                break;
            }
            output[length++] = ' ';
        }
        const char *word = arguments[index];
        while (*word && length + 1 < capacity) {
            output[length++] = *word++;
        }
    }
    output[length] = '\0';
}

static void command_write(int argc, bool append) {
    if (argc < 2) {
        console_write(append ? "Usage: APPEND FILE TEXT\n" : "Usage: WRITE FILE TEXT\n");
        return;
    }
    char text[VFS_CONTENT_CAPACITY + 1];
    join_arguments(2, argc, text, sizeof(text));
    int result = vfs_write(arguments[1], text, append);
    if (result != VFS_OK) {
        print_vfs_error(append ? "APPEND" : "WRITE", result);
        return;
    }
    const virtual_file *file = vfs_find(arguments[1]);
    console_printf("%s: %s now contains %u bytes.\n",
                   append ? "APPEND" : "WRITE", file ? file->name : "file",
                   file ? file->size : 0);
}

static void command_touch(int argc) {
    if (argc != 2) {
        console_write("Usage: TOUCH FILE\n");
        return;
    }
    int result = vfs_touch(arguments[1]);
    if (result != VFS_OK) {
        print_vfs_error("TOUCH", result);
        return;
    }
    console_printf("Created %s.\n", arguments[1]);
}

static void command_remove(int argc) {
    if (argc != 2) {
        console_write("Usage: RM FILE\n");
        return;
    }
    int result = vfs_remove(arguments[1]);
    if (result != VFS_OK) {
        print_vfs_error("RM", result);
        return;
    }
    console_printf("Removed %s.\n", arguments[1]);
}

static void command_rename(int argc) {
    if (argc != 3) {
        console_write("Usage: MV OLD NEW\n");
        return;
    }
    int result = vfs_rename(arguments[1], arguments[2]);
    if (result != VFS_OK) {
        print_vfs_error("MV", result);
        return;
    }
    console_printf("Renamed %s to %s.\n", arguments[1], arguments[2]);
}

static void print_hex_byte(uint8_t value) {
    const char *digits = "0123456789ABCDEF";
    console_putc(digits[value >> 4]);
    console_putc(digits[value & 0x0F]);
}

static void command_hexdump(int argc) {
    if (argc != 2) {
        console_write("Usage: HEXDUMP FILE\n");
        return;
    }
    const virtual_file *file = vfs_find(arguments[1]);
    if (!file) {
        print_vfs_error("HEXDUMP", VFS_NOT_FOUND);
        return;
    }
    for (uint32_t offset = 0; offset < file->size; offset += 16) {
        console_print_hex(offset);
        console_putc(' ');
        for (uint32_t column = 0; column < 16; column++) {
            if (offset + column < file->size) {
                print_hex_byte((uint8_t)file->content[offset + column]);
            } else {
                console_write("  ");
            }
            console_putc(' ');
        }
        console_putc('|');
        for (uint32_t column = 0; column < 16 && offset + column < file->size; column++) {
            char character = file->content[offset + column];
            console_putc(character >= 32 && character <= 126 ? character : '.');
        }
        console_write("|\n");
    }
}

static void history_add(void) {
    if (!line[0]) {
        return;
    }
    if (history_count && strcmp(history[history_count - 1], line) == 0) {
        return;
    }
    if (history_count == HISTORY_MAX) {
        memmove(history[0], history[1], sizeof(history[0]) * (HISTORY_MAX - 1));
        history_count--;
    }
    strcpy(history[history_count++], line);
}

static void command_history(int argc) {
    if (argc == 2 && strcmp(arguments[1], "-c") == 0) {
        memset(history, 0, sizeof(history));
        history_count = 0;
        console_write("Command history cleared.\n");
        return;
    }
    if (!history_count) {
        console_write("Command history is empty.\n");
        return;
    }
    for (uint32_t index = 0; index < history_count; index++) {
        console_printf("%u  %s\n", index + 1, history[index]);
    }
}

static void beep(void) {
    uint32_t divisor = 1193182 / 880;
    outb(0x43, 0xB6);
    outb(0x42, (uint8_t)divisor);
    outb(0x42, (uint8_t)(divisor >> 8));
    outb(0x61, (uint8_t)(inb(0x61) | 3));
    timer_sleep_ms(150);
    outb(0x61, (uint8_t)(inb(0x61) & 0xFC));
}

static void command_panic(void) {
    console_write("Triggering an intentional divide-by-zero exception...\n");
    __asm__ volatile(
        "xorl %%ecx, %%ecx\n"
        "movl $1, %%eax\n"
        "cltd\n"
        "idivl %%ecx\n"
        :
        :
        : "eax", "ecx", "edx"
    );
}

static void shutdown_system(void) {
    console_set_color(COLOR_LIGHT_RED, COLOR_BLACK);
    console_write("Shutting down CKLOS...\n");

    /* QEMU isa-debug-exit makes automated tests deterministic. */
    outb(0x501, 0x10);
    /* Common ACPI PM1a sleep-control ports used by virtual machines. */
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);
    console_write("Poweroff is not supported by this firmware; halting.\n");
    cpu_halt();
}

static void run_command(int argc) {
    if (argc == 0) {
        return;
    }
    if (strcmp(arguments[0], "help") == 0) {
        command_help();
    } else if (strcmp(arguments[0], "about") == 0) {
        command_about();
    } else if (strcmp(arguments[0], "version") == 0) {
        console_write(CKLOS_NAME " " CKLOS_VERSION " (x86, freestanding kernel)\n");
    } else if (strcmp(arguments[0], "uname") == 0) {
        command_uname();
    } else if (strcmp(arguments[0], "sysinfo") == 0) {
        command_sysinfo();
    } else if (strcmp(arguments[0], "mem") == 0) {
        command_mem();
    } else if (strcmp(arguments[0], "net") == 0) {
        command_net();
    } else if (strcmp(arguments[0], "mouse") == 0) {
        command_mouse();
    } else if (strcmp(arguments[0], "ping") == 0) {
        command_ping(argc);
    } else if (strcmp(arguments[0], "tcptest") == 0) {
        command_tcp_diagnostic();
    } else if (strcmp(arguments[0], "heap") == 0) {
        command_heap();
    } else if (strcmp(arguments[0], "memtest") == 0) {
        command_memtest();
    } else if (strcmp(arguments[0], "tasks") == 0) {
        command_tasks();
    } else if (strcmp(arguments[0], "spawn") == 0) {
        command_spawn(argc);
    } else if (strcmp(arguments[0], "kill") == 0) {
        command_kill(argc);
    } else if (strcmp(arguments[0], "killall") == 0) {
        command_killall();
    } else if (strcmp(arguments[0], "date") == 0) {
        command_date();
    } else if (strcmp(arguments[0], "time") == 0) {
        command_time();
    } else if (strcmp(arguments[0], "uptime") == 0) {
        command_uptime();
    } else if (strcmp(arguments[0], "sleep") == 0) {
        command_sleep(argc);
    } else if (strcmp(arguments[0], "ls") == 0) {
        command_ls();
    } else if (strcmp(arguments[0], "cat") == 0) {
        command_cat(argc);
    } else if (strcmp(arguments[0], "stat") == 0) {
        command_stat(argc);
    } else if (strcmp(arguments[0], "df") == 0) {
        command_df();
    } else if (strcmp(arguments[0], "cp") == 0) {
        command_copy(argc);
    } else if (strcmp(arguments[0], "wc") == 0) {
        command_wc(argc);
    } else if (strcmp(arguments[0], "head") == 0) {
        command_file_lines(argc, false);
    } else if (strcmp(arguments[0], "tail") == 0) {
        command_file_lines(argc, true);
    } else if (strcmp(arguments[0], "disk") == 0) {
        command_disk();
    } else if (strcmp(arguments[0], "disksave") == 0) {
        command_disksave();
    } else if (strcmp(arguments[0], "diskload") == 0) {
        command_diskload();
    } else if (strcmp(arguments[0], "run") == 0) {
        command_run(argc);
    } else if (strcmp(arguments[0], "exec") == 0) {
        command_exec(argc);
    } else if (strcmp(arguments[0], "elfexec") == 0) {
        command_elfexec(argc);
    } else if (strcmp(arguments[0], "procs") == 0) {
        command_processes();
    } else if (strcmp(arguments[0], "rks") == 0) {
        command_rks();
    } else if (strcmp(arguments[0], "fsls") == 0) {
        command_fs2(argc);
    } else if (strcmp(arguments[0], "fscd") == 0) {
        command_fscd(argc);
    } else if (strcmp(arguments[0], "fspwd") == 0) {
        command_fspwd();
    } else if (strcmp(arguments[0], "fsmkdir") == 0) {
        command_fsmkdir(argc);
    } else if (strcmp(arguments[0], "fswrite") == 0) {
        command_fswrite(argc);
    } else if (strcmp(arguments[0], "fscat") == 0) {
        command_fscat(argc);
    } else if (strcmp(arguments[0], "fsrm") == 0) {
        command_fsrm(argc);
    } else if (strcmp(arguments[0], "fsmv") == 0) {
        command_fsmv(argc);
    } else if (strcmp(arguments[0], "fsfill") == 0) {
        command_fsfill(argc);
    } else if (strcmp(arguments[0], "fsmigrate") == 0) {
        command_fsmigrate();
    } else if (strcmp(arguments[0], "fscheck") == 0) {
        command_fscheck();
    } else if (strcmp(arguments[0], "write") == 0) {
        command_write(argc, false);
    } else if (strcmp(arguments[0], "append") == 0) {
        command_write(argc, true);
    } else if (strcmp(arguments[0], "touch") == 0) {
        command_touch(argc);
    } else if (strcmp(arguments[0], "rm") == 0) {
        command_remove(argc);
    } else if (strcmp(arguments[0], "mv") == 0) {
        command_rename(argc);
    } else if (strcmp(arguments[0], "hexdump") == 0) {
        command_hexdump(argc);
    } else if (strcmp(arguments[0], "history") == 0) {
        command_history(argc);
    } else if (strcmp(arguments[0], "echo") == 0) {
        for (int index = 1; index < argc; index++) {
            if (index > 1) {
                console_putc(' ');
            }
            console_write(arguments[index]);
        }
        console_putc('\n');
    } else if (strcmp(arguments[0], "calc") == 0) {
        command_calc(argc);
    } else if (strcmp(arguments[0], "color") == 0) {
        command_color(argc);
    } else if (strcmp(arguments[0], "clear") == 0) {
        console_clear();
    } else if (strcmp(arguments[0], "banner") == 0) {
        print_banner();
    } else if (strcmp(arguments[0], "devices") == 0) {
        console_write(
            "vga0     80x25 color text display\n"
            "com1     16550-compatible serial terminal\n"
            "kbd0     PS/2 keyboard, scan code set 1\n"
            "pic0     8259 interrupt controller, remapped to 0x20\n"
            "pit0     Programmable interval timer, 100 Hz\n"
            "net0     RTL8139 Ethernet, 10.0.2.15/24\n"
            "tcp0     IPv4 TCP listener for the SSH transport\n"
            "rtc0     CMOS real-time clock\n"
            "ram0     Writable RAM disk, 12 KiB\n"
            "ata0     ATA PIO data disk, 2 MiB persistent CKLOSFS1 volume\n"
            "ata1     ATA PIO 128 MiB CKLOSFS2 directory volume\n"
            "pcspk0   Programmable interval timer speaker\n"
        );
    } else if (strcmp(arguments[0], "beep") == 0) {
        beep();
    } else if (strcmp(arguments[0], "panic") == 0) {
        command_panic();
    } else if (strcmp(arguments[0], "reboot") == 0) {
        console_write("Rebooting...\n");
        cpu_reboot();
    } else if (strcmp(arguments[0], "shutdown") == 0) {
        shutdown_system();
    } else if (strcmp(arguments[0], "halt") == 0) {
        console_write("CPU halted.\n");
        cpu_halt();
    } else {
        console_printf("CKLOS: %s: command not found. Type HELP.\n", arguments[0]);
    }
}

static void normalize_command(char *command) {
    for (uint32_t index = 0; command[index]; index++) {
        if (command[index] >= 'A' && command[index] <= 'Z') {
            command[index] = (char)(command[index] - 'A' + 'a');
        }
    }
}

static int split_arguments(char *command) {
    int count = 0;
    char *cursor = command;
    while (*cursor) {
        while (*cursor == ' ') {
            *cursor++ = '\0';
        }
        if (!*cursor) {
            break;
        }
        if (count < MAX_ARGS) {
            arguments[count++] = cursor;
        }
        while (*cursor && *cursor != ' ') {
            cursor++;
        }
    }
    return count;
}

static uint32_t replace_input_line(uint32_t old_length, const char *replacement) {
    while (old_length--) {
        console_putc('\b');
    }
    strcpy(line, replacement);
    uint32_t length = (uint32_t)strlen(line);
    console_write(line);
    return length;
}

static void read_line(void) {
    uint32_t length = 0;
    uint32_t history_cursor = history_count;
    line[0] = '\0';

    while (1) {
        uint16_t key = input_getchar();
        if (key == '\n') {
            line[length] = '\0';
            console_putc('\n');
            history_add();
            return;
        }
        if (key == 3) {
            while (length--) {
                console_putc('\b');
            }
            line[0] = '\0';
            console_write("^C\n");
            return;
        }
        if (key == KEY_UP) {
            if (history_cursor) {
                history_cursor--;
                length = replace_input_line(length, history[history_cursor]);
            }
            continue;
        }
        if (key == KEY_DOWN) {
            if (history_cursor < history_count) {
                history_cursor++;
            }
            const char *replacement = history_cursor < history_count
                                    ? history[history_cursor] : "";
            length = replace_input_line(length, replacement);
            continue;
        }
        if (key == '\b') {
            if (length) {
                length--;
                line[length] = '\0';
                console_putc('\b');
                history_cursor = history_count;
            }
            continue;
        }
        if (key == 12) {
            console_clear();
            console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
            console_write("CKLOS> ");
            console_set_color(COLOR_LIGHT_GRAY, COLOR_BLACK);
            line[length] = '\0';
            console_write(line);
            continue;
        }
        if (key >= 32 && key <= 0xFF && length + 1 < LINE_SIZE) {
            line[length++] = (char)key;
            line[length] = '\0';
            console_putc((char)key);
            history_cursor = history_count;
        }
    }
}

void shell_start(void) {
    while (1) {
        console_set_color(COLOR_LIGHT_CYAN, COLOR_BLACK);
        console_write("CKLOS> ");
        console_set_color(COLOR_LIGHT_GRAY, COLOR_BLACK);
        read_line();

        char command[LINE_SIZE];
        strcpy(command, line);
        int count = split_arguments(command);
        if (count) {
            normalize_command(arguments[0]);
        }
        run_command(count);
    }
}
