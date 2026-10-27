#include "console.h"
#include "disk.h"
#include "fs2.h"
#include "gui.h"
#include "interrupts.h"
#include "memory.h"
#include "mouse.h"
#include "network.h"
#include "process.h"
#include "rks.h"
#include "shell.h"
#include "tasks.h"
#include "unicode.h"
#include "vfs.h"
#include "version.h"

void kernel_main(const boot_info *info) {
    gui_configure(info);
    console_init();
    memory_init(info);
    vfs_init();
    rks_init();
    disk_init();
    disk_load_filesystem();
    fs2_init();
    unicode_init("/usr/share/fonts/unifont.bin");
    network_init();
    mouse_init();
    interrupts_init();
    tasks_init();
    process_init();
    interrupts_enable();
    console_set_color(COLOR_LIGHT_GRAY, COLOR_BLACK);
    console_write(
        "\n"
        CKLOS_NAME " " CKLOS_VERSION
        " kernel is running in x86-64 long mode.\n"
        "Boot sequence complete. Starting command shell...\n\n"
    );
    shell_start();
}
