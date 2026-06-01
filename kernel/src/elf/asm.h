#ifndef NTUX_EXECUTABLES_ASM_H
#define NTUX_EXECUTABLES_ASM_H

#include <stddef.h>

int ntux_asm_build_file(const char* src_path, const char* out_path, char* status, size_t status_cap);
int ntux_asm_run_file(const char* path, char* status, size_t status_cap);

#endif
