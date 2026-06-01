#ifndef MODULE_LOADER_H
#define MODULE_LOADER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    char token[64];
    char path[160];
} ntux_module_info_t;

void module_loader_init(void);
bool module_loader_run_hello(const char **result_text);
bool module_loader_start_hello_ring3(const char **result_text);
bool module_loader_start_module_ring3(const char* token, const char **result_text);
bool module_loader_start_elf_ring3(const char* path, const char **result_text);
int module_loader_last_hello_tid(void);
int module_loader_last_elf_tid(void);
void module_loader_free_user_space(uint64_t pml4_phys, uint64_t start, uint64_t end);
int module_loader_list(ntux_module_info_t* out, uint64_t max_entries, uint64_t* out_count);

#endif
