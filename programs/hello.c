#include "rks_api.h"

__attribute__((section(".text.rks_entry")))
int rks_main(const rks_api *api) {
    api->print("Hello from HELLO.RKS.\n");
    return 0;
}
