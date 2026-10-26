#include "rks_api.h"

__attribute__((section(".text.rks_entry")))
int rks_main(const rks_api *api) {
    api->print("\nRKS executable is running!\n");
    api->print("Format: RKS1  /  Architecture: x86-64\n");
    api->print("Mouse position: ");
    const char *examples = "RKS programs are standalone binaries loaded at runtime.\n";
    for (rks_u32 index = 0; examples[index]; index++) {
        api->put_char(examples[index]);
    }
    api->print("Returning to CKLOS Shell.\n");
    return 0;
}
