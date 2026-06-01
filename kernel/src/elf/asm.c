#include <elf/asm.h>

#include <stdint.h>

#include <fs/fs.h>
#include <sched/thread.h>
#include <arch/x86_64/gdt.h>
#include <arch/x86_64/usermode.h>
#include <lib/string.h>

#define NTUX_USERMODE_DISABLE_RING3 1

#define NTUX_ASM_MAX_SRC 32768
#define NTUX_ASM_MAX_CODE 8192
#define NTUX_ASM_MAX_LABELS 128
#define NTUX_ASM_MAX_PRINTS 64
#define NTUX_ASM_MAX_PRINT_STR 120

typedef struct {
    char name[32];
    uint32_t off;
} asm_label_t;

typedef struct {
    uint32_t addr_imm_off;
    uint32_t len_imm_off;
    uint16_t len;
    uint8_t newline;
    char text[NTUX_ASM_MAX_PRINT_STR];
} asm_print_ref_t;

typedef struct __attribute__((packed)) {
    char magic[4];
    uint32_t code_size;
} ntx0_hdr_t;

static uint8_t g_ntx_exec_buf[NTUX_ASM_MAX_CODE];
static uint32_t g_ring3_code_size = 0;
static volatile int g_ring3_ready = 0;

#define NTUX_RING3_CODE_BASE ((uint8_t*)(uintptr_t)0x001A0000u)
#define NTUX_RING3_STACK_TOP ((uint64_t)0x001FF000u)
#define NTUX_RING3_STACK_SLOT_SIZE ((uint64_t)0x00004000u)
#define NTUX_RING3_STACK_LOW_LIMIT ((uint64_t)0x00134000u)

static uint64_t ring3_stack_top_for_current_thread(void) {
    int tid = current_thread_id;
    uint64_t slot = (tid >= 0) ? (uint64_t)((unsigned)tid % MAX_THREADS) : 0;
    uint64_t top = NTUX_RING3_STACK_TOP - (slot * NTUX_RING3_STACK_SLOT_SIZE);
    if (top < NTUX_RING3_STACK_LOW_LIMIT) top = NTUX_RING3_STACK_TOP;
    return (top & ~0xFull) - 8u;
}

static uint64_t read_rsp(void) {
    uint64_t rsp = 0;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));
    return rsp;
}

static void run_entry_in_kernel_context(uint64_t entry) {
    void (*fn)(void) = (void (*)(void))(uintptr_t)entry;
    fn();
    thread_exit_current();
}

static void ring3_thread_entry(void) {
    while (!g_ring3_ready) {
        thread_yield();
        __asm__ volatile("hlt");
    }

    uint8_t* code_base = NTUX_RING3_CODE_BASE;
    uint32_t code_size = g_ring3_code_size;
    uint64_t exit_stub_addr = (uint64_t)(uintptr_t)(code_base + code_size);

    code_base[code_size + 0] = 0x48;
    code_base[code_size + 1] = 0xC7;
    code_base[code_size + 2] = 0xC0;
    code_base[code_size + 3] = 0x02;
    code_base[code_size + 4] = 0x00;
    code_base[code_size + 5] = 0x00;
    code_base[code_size + 6] = 0x00;
    code_base[code_size + 7] = 0xCD;
    code_base[code_size + 8] = 0x80;
    code_base[code_size + 9] = 0xF4;

    uint64_t user_rsp = ring3_stack_top_for_current_thread();
    *((volatile uint64_t*)(uintptr_t)user_rsp) = exit_stub_addr;

#if NTUX_USERMODE_DISABLE_RING3
    run_entry_in_kernel_context((uint64_t)(uintptr_t)code_base);
#else
    gdt_set_kernel_stack(read_rsp());
    enter_user(user_rsp, (uint64_t)(uintptr_t)code_base);
#endif

    for (;;) {
        thread_yield();
        __asm__ volatile("hlt");
    }
}

static void set_status(char* out, size_t cap, const char* msg) {
    if (!out || cap == 0) return;
    strncpy(out, msg, cap - 1);
    out[cap - 1] = '\0';
}

static void wr_u64_le(uint8_t* p, uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = (uint8_t)((v >> (i * 8)) & 0xFFu);
}

static int parse_u64(const char* s, uint64_t* out) {
    if (!s || !*s || !out) return -1;
    int base = 10;
    size_t i = 0;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        i = 2;
    }
    uint64_t v = 0;
    for (; s[i]; ++i) {
        char c = s[i];
        int d = -1;
        if (c >= '0' && c <= '9') d = c - '0';
        else if (base == 16 && c >= 'a' && c <= 'f') d = 10 + (c - 'a');
        else if (base == 16 && c >= 'A' && c <= 'F') d = 10 + (c - 'A');
        else return -1;
        if (d >= base) return -1;
        v = v * (uint64_t)base + (uint64_t)d;
    }
    *out = v;
    return 0;
}

static void trim(char* s) {
    if (!s) return;
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r' || s[n - 1] == '\n')) {
        s[--n] = '\0';
    }
    size_t st = 0;
    while (s[st] == ' ' || s[st] == '\t') st++;
    if (st > 0) {
        size_t i = 0;
        while (s[st + i]) {
            s[i] = s[st + i];
            i++;
        }
        s[i] = '\0';
    }
}

static void strip_comment(char* s) {
    for (size_t i = 0; s[i]; ++i) {
        if (s[i] == ';' || s[i] == '#') {
            s[i] = '\0';
            return;
        }
    }
}

static int find_label(const asm_label_t* labels, int label_count, const char* name) {
    for (int i = 0; i < label_count; ++i) {
        if (strcmp(labels[i].name, name) == 0) return i;
    }
    return -1;
}

static int emit_u8(uint8_t* code, uint32_t* code_len, uint8_t v) {
    if (*code_len >= NTUX_ASM_MAX_CODE) return -1;
    code[*code_len] = v;
    (*code_len)++;
    return 0;
}

static int emit_u32(uint8_t* code, uint32_t* code_len, uint32_t v) {
    if (*code_len + 4 > NTUX_ASM_MAX_CODE) return -1;
    code[*code_len + 0] = (uint8_t)(v & 0xFF);
    code[*code_len + 1] = (uint8_t)((v >> 8) & 0xFF);
    code[*code_len + 2] = (uint8_t)((v >> 16) & 0xFF);
    code[*code_len + 3] = (uint8_t)((v >> 24) & 0xFF);
    *code_len += 4;
    return 0;
}

static int emit_u64(uint8_t* code, uint32_t* code_len, uint64_t v) {
    if (*code_len + 8 > NTUX_ASM_MAX_CODE) return -1;
    for (int i = 0; i < 8; ++i) code[*code_len + i] = (uint8_t)((v >> (i * 8)) & 0xFF);
    *code_len += 8;
    return 0;
}

static int reg_id(const char* reg) {
    if (strcmp(reg, "rax") == 0) return 0;
    if (strcmp(reg, "rcx") == 0) return 1;
    if (strcmp(reg, "rdx") == 0) return 2;
    if (strcmp(reg, "rbx") == 0) return 3;
    if (strcmp(reg, "rsp") == 0) return 4;
    if (strcmp(reg, "rbp") == 0) return 5;
    if (strcmp(reg, "rsi") == 0) return 6;
    if (strcmp(reg, "rdi") == 0) return 7;
    if (strcmp(reg, "r8") == 0) return 8;
    if (strcmp(reg, "r9") == 0) return 9;
    if (strcmp(reg, "r10") == 0) return 10;
    if (strcmp(reg, "r11") == 0) return 11;
    if (strcmp(reg, "r12") == 0) return 12;
    if (strcmp(reg, "r13") == 0) return 13;
    if (strcmp(reg, "r14") == 0) return 14;
    if (strcmp(reg, "r15") == 0) return 15;
    return -1;
}

static int emit_mov_imm64(uint8_t* code, uint32_t* code_len, const char* reg, uint64_t imm) {
    int rid = reg_id(reg);
    if (rid < 0) return -1;
    uint8_t rex = (rid >= 8) ? 0x49 : 0x48;
    uint8_t op = (uint8_t)(0xB8 + (rid & 7));
    if (emit_u8(code, code_len, rex) != 0) return -1;
    if (emit_u8(code, code_len, op) != 0) return -1;
    return emit_u64(code, code_len, imm);
}

static int parse_int_imm8(const char* s, uint8_t* out) {
    uint64_t v = 0;
    if (parse_u64(s, &v) != 0 || v > 0xFFu) return -1;
    *out = (uint8_t)v;
    return 0;
}

static int parse_quoted_text(const char* line, char out[NTUX_ASM_MAX_PRINT_STR], uint16_t* out_len) {
    if (!line || !out || !out_len) return -1;
    const char* q = strchr(line, '"');
    if (!q) return -1;
    q++;

    uint16_t n = 0;
    while (*q && *q != '"') {
        char c = *q++;
        if (c == '\\' && *q) {
            char e = *q++;
            if (e == 'n') c = '\n';
            else if (e == 't') c = '\t';
            else if (e == 'r') c = '\r';
            else if (e == '\\') c = '\\';
            else if (e == '"') c = '"';
            else c = e;
        }
        if (n + 1 >= NTUX_ASM_MAX_PRINT_STR) return -1;
        out[n++] = c;
    }
    if (*q != '"') return -1;
    out[n] = '\0';
    *out_len = n;
    return 0;
}

static int compile_source(const char* src, uint8_t* code, uint32_t* out_code_len, char* err, size_t err_cap) {
    asm_label_t labels[NTUX_ASM_MAX_LABELS];
    asm_print_ref_t prints[NTUX_ASM_MAX_PRINTS];
    int label_count = 0;
    int print_count = 0;
    uint32_t code_len = 0;
    uint32_t lit_total = 0;
    static char pass_buf[NTUX_ASM_MAX_SRC];

    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 1) {
            code_len = 0;
            print_count = 0;
            lit_total = 0;
        }

        size_t src_len = strlen(src);
        if (src_len + 1 > sizeof(pass_buf)) {
            set_status(err, err_cap, "asm source too large");
            return -1;
        }
        memcpy(pass_buf, src, src_len + 1);

        char* line = pass_buf;
        while (line && *line) {
            char* next = strchr(line, '\n');
            if (next) *next++ = '\0';

            strip_comment(line);
            trim(line);
            if (line[0] == '\0') {
                line = next;
                continue;
            }

            char orig_line[128];
            strncpy(orig_line, line, sizeof(orig_line) - 1);
            orig_line[sizeof(orig_line) - 1] = '\0';

            size_t ll = strlen(line);
            if (line[ll - 1] == ':') {
                line[ll - 1] = '\0';
                trim(line);
                if (line[0] == '\0') {
                    line = next;
                    continue;
                }
                if (pass == 0) {
                    if (label_count >= NTUX_ASM_MAX_LABELS) {
                        set_status(err, err_cap, "too many labels");
                        return -1;
                    }
                    if (find_label(labels, label_count, line) >= 0) {
                        set_status(err, err_cap, "duplicate label");
                        return -1;
                    }
                    strncpy(labels[label_count].name, line, sizeof(labels[label_count].name) - 1);
                    labels[label_count].name[sizeof(labels[label_count].name) - 1] = '\0';
                    labels[label_count].off = code_len;
                    label_count++;
                }
                line = next;
                continue;
            }

            char work[128];
            strncpy(work, line, sizeof(work) - 1);
            work[sizeof(work) - 1] = '\0';
            for (size_t i = 0; work[i]; ++i) {
                if (work[i] >= 'A' && work[i] <= 'Z') work[i] = (char)(work[i] - 'A' + 'a');
            }

            char* a0 = work;
            char* a1 = NULL;
            char* a2 = NULL;
            char* sp = strchr(work, ' ');
            if (sp) {
                *sp++ = '\0';
                while (*sp == ' ' || *sp == '\t') sp++;
                a1 = sp;
                char* comma = strchr(sp, ',');
                if (comma) {
                    *comma++ = '\0';
                    while (*comma == ' ' || *comma == '\t') comma++;
                    a2 = comma;
                }
                trim(a1);
                if (a2) trim(a2);
            }

            if (strcmp(a0, "nop") == 0) {
                if (emit_u8(code, &code_len, 0x90) != 0) goto too_big;
            } else if (strcmp(a0, "ret") == 0) {
                if (emit_u8(code, &code_len, 0xC3) != 0) goto too_big;
            } else if (strcmp(a0, "hlt") == 0) {
                if (emit_u8(code, &code_len, 0xF4) != 0) goto too_big;
            } else if (strcmp(a0, "cli") == 0) {
                if (emit_u8(code, &code_len, 0xFA) != 0) goto too_big;
            } else if (strcmp(a0, "sti") == 0) {
                if (emit_u8(code, &code_len, 0xFB) != 0) goto too_big;
            } else if (strcmp(a0, "int3") == 0) {
                if (emit_u8(code, &code_len, 0xCC) != 0) goto too_big;
            } else if (strcmp(a0, "ud2") == 0) {
                if (emit_u8(code, &code_len, 0x0F) != 0 || emit_u8(code, &code_len, 0x0B) != 0) goto too_big;
            } else if (strcmp(a0, "syscall") == 0) {
                if (emit_u8(code, &code_len, 0x0F) != 0 || emit_u8(code, &code_len, 0x05) != 0) goto too_big;
            } else if (strcmp(a0, "int80") == 0) {
                if (emit_u8(code, &code_len, 0xCD) != 0 || emit_u8(code, &code_len, 0x80) != 0) goto too_big;
            } else if (strcmp(a0, "sleep") == 0 && a1) {
                uint64_t ticks = 0;
                if (parse_u64(a1, &ticks) != 0) {
                    set_status(err, err_cap, "bad sleep imm");
                    return -1;
                }
                if (emit_mov_imm64(code, &code_len, "rax", 5) != 0) goto too_big;
                if (emit_mov_imm64(code, &code_len, "rdi", ticks) != 0) goto too_big;
                if (emit_u8(code, &code_len, 0xCD) != 0 || emit_u8(code, &code_len, 0x80) != 0) goto too_big;
            } else if (strcmp(a0, "exit") == 0) {
                if (emit_mov_imm64(code, &code_len, "rax", 2) != 0) goto too_big;
                if (emit_u8(code, &code_len, 0xCD) != 0 || emit_u8(code, &code_len, 0x80) != 0) goto too_big;
            } else if (strcmp(a0, "print") == 0 || strcmp(a0, "println") == 0) {
                char txt[NTUX_ASM_MAX_PRINT_STR];
                uint16_t txt_len = 0;
                if (parse_quoted_text(orig_line, txt, &txt_len) != 0) {
                    set_status(err, err_cap, "bad print string");
                    return -1;
                }
                uint8_t add_nl = (strcmp(a0, "println") == 0) ? 1u : 0u;
                uint32_t full_len = (uint32_t)txt_len + (uint32_t)add_nl;

                if (pass == 0) {
                    if (lit_total + full_len > NTUX_ASM_MAX_CODE) goto too_big;
                    lit_total += full_len;
                } else {
                    if (print_count >= NTUX_ASM_MAX_PRINTS) {
                        set_status(err, err_cap, "too many print refs");
                        return -1;
                    }
                }

                if (emit_mov_imm64(code, &code_len, "rax", 0) != 0) goto too_big;
                uint32_t rdi_imm_off = code_len + 2;
                if (emit_mov_imm64(code, &code_len, "rdi", 0) != 0) goto too_big;
                uint32_t rsi_imm_off = code_len + 2;
                if (emit_mov_imm64(code, &code_len, "rsi", 0) != 0) goto too_big;
                if (emit_u8(code, &code_len, 0x0F) != 0 || emit_u8(code, &code_len, 0x05) != 0) goto too_big;

                if (pass == 0) {
                    wr_u64_le(&code[rsi_imm_off], (uint64_t)full_len);
                } else {
                    asm_print_ref_t* pr = &prints[print_count++];
                    pr->addr_imm_off = rdi_imm_off;
                    pr->len_imm_off = rsi_imm_off;
                    pr->len = txt_len;
                    pr->newline = add_nl;
                    strncpy(pr->text, txt, sizeof(pr->text) - 1);
                    pr->text[sizeof(pr->text) - 1] = '\0';
                }
            } else if (strcmp(a0, "int") == 0 && a1) {
                uint8_t imm = 0;
                if (parse_int_imm8(a1, &imm) != 0) {
                    set_status(err, err_cap, "bad int imm");
                    return -1;
                }
                if (emit_u8(code, &code_len, 0xCD) != 0 || emit_u8(code, &code_len, imm) != 0) goto too_big;
            } else if (strcmp(a0, "mov") == 0 && a1 && a2) {
                uint64_t imm = 0;
                if (parse_u64(a2, &imm) != 0 || emit_mov_imm64(code, &code_len, a1, imm) != 0) {
                    set_status(err, err_cap, "bad mov");
                    return -1;
                }
            } else if ((strcmp(a0, "jmp") == 0 || strcmp(a0, "call") == 0) && a1) {
                uint8_t op = (strcmp(a0, "jmp") == 0) ? 0xE9 : 0xE8;
                if (emit_u8(code, &code_len, op) != 0) goto too_big;
                uint32_t disp_at = code_len;
                if (emit_u32(code, &code_len, 0) != 0) goto too_big;
                if (pass == 1) {
                    int li = find_label(labels, label_count, a1);
                    if (li < 0) {
                        set_status(err, err_cap, "unknown label");
                        return -1;
                    }
                    int64_t rel = (int64_t)labels[li].off - (int64_t)(disp_at + 4);
                    uint32_t v = (uint32_t)rel;
                    code[disp_at + 0] = (uint8_t)(v & 0xFF);
                    code[disp_at + 1] = (uint8_t)((v >> 8) & 0xFF);
                    code[disp_at + 2] = (uint8_t)((v >> 16) & 0xFF);
                    code[disp_at + 3] = (uint8_t)((v >> 24) & 0xFF);
                }
            } else {
                set_status(err, err_cap, "unsupported asm line");
                return -1;
            }

            line = next;
        }

        if (pass == 0) {
            if (code_len + lit_total > NTUX_ASM_MAX_CODE) goto too_big;
        } else {
            uint32_t lit_off = 0;
            for (int i = 0; i < print_count; ++i) {
                asm_print_ref_t* pr = &prints[i];
                uint32_t full_len = (uint32_t)pr->len + (uint32_t)pr->newline;
                if (code_len + lit_off + full_len > NTUX_ASM_MAX_CODE) goto too_big;
                uint64_t addr = (uint64_t)(uintptr_t)(NTUX_RING3_CODE_BASE + code_len + lit_off);
                wr_u64_le(&code[pr->addr_imm_off], addr);
                wr_u64_le(&code[pr->len_imm_off], full_len);
                memcpy(&code[code_len + lit_off], pr->text, pr->len);
                if (pr->newline) code[code_len + lit_off + pr->len] = '\n';
                lit_off += full_len;
            }
            code_len += lit_off;
        }
    }

    *out_code_len = code_len;
    return 0;

too_big:
    set_status(err, err_cap, "code too large");
    return -1;
}

static void split_parent_name(const char* full, char parent[256], char name[64]) {
    const char* slash = strrchr(full, '/');
    if (!slash || !slash[1]) {
        parent[0] = '/';
        parent[1] = '\0';
        name[0] = '\0';
        return;
    }
    strncpy(name, slash + 1, 63);
    name[63] = '\0';
    if (slash == full) {
        parent[0] = '/';
        parent[1] = '\0';
    } else {
        size_t pl = (size_t)(slash - full);
        if (pl > 255) pl = 255;
        memcpy(parent, full, pl);
        parent[pl] = '\0';
    }
}

static int write_or_create_file(const char* full_path, const void* data, size_t len) {
    if (fs_write_file(full_path, data, len) == 0) return 0;
    char parent[256];
    char name[64];
    split_parent_name(full_path, parent, name);
    if (name[0] == '\0') return -1;
    return fs_create_file(parent, name, data, len);
}

int ntux_asm_build_file(const char* src_path, const char* out_path, char* status, size_t status_cap) {
    size_t src_len = 0;
    if (fs_read_file(src_path, NULL, 0, &src_len) != 0 || src_len == 0 || src_len >= NTUX_ASM_MAX_SRC) {
        set_status(status, status_cap, "asm read failed");
        return -1;
    }

    static char src_buf[NTUX_ASM_MAX_SRC];
    size_t got = 0;
    if (fs_read_file(src_path, src_buf, src_len, &got) != 0 || got == 0) {
        set_status(status, status_cap, "asm read failed");
        return -1;
    }
    if (got >= sizeof(src_buf)) got = sizeof(src_buf) - 1;
    src_buf[got] = '\0';

    uint8_t code[NTUX_ASM_MAX_CODE];
    uint32_t code_len = 0;
    char err[64];
    if (compile_source(src_buf, code, &code_len, err, sizeof(err)) != 0) {
        set_status(status, status_cap, err);
        return -1;
    }

    uint8_t out_bin[sizeof(ntx0_hdr_t) + NTUX_ASM_MAX_CODE];
    ntx0_hdr_t hdr;
    hdr.magic[0] = 'N';
    hdr.magic[1] = 'T';
    hdr.magic[2] = 'X';
    hdr.magic[3] = '0';
    hdr.code_size = code_len;
    memcpy(out_bin, &hdr, sizeof(hdr));
    memcpy(out_bin + sizeof(hdr), code, code_len);

    if (write_or_create_file(out_path, out_bin, sizeof(hdr) + code_len) != 0) {
        set_status(status, status_cap, "asm write failed");
        return -1;
    }

    set_status(status, status_cap, "asm ok");
    return 0;
}

int ntux_asm_run_file(const char* full_path, char* status, size_t status_cap) {
    uint8_t file_buf[sizeof(ntx0_hdr_t) + NTUX_ASM_MAX_CODE];
    size_t out_len = 0;
    if (fs_read_file(full_path, file_buf, sizeof(file_buf), &out_len) != 0 || out_len < sizeof(ntx0_hdr_t)) {
        set_status(status, status_cap, "run read failed");
        return -1;
    }

    ntx0_hdr_t hdr;
    memcpy(&hdr, file_buf, sizeof(hdr));
    if (hdr.magic[0] != 'N' || hdr.magic[1] != 'T' || hdr.magic[2] != 'X' || hdr.magic[3] != '0') {
        set_status(status, status_cap, "bad format");
        return -1;
    }
    if (hdr.code_size == 0 || hdr.code_size > NTUX_ASM_MAX_CODE || sizeof(hdr) + hdr.code_size > out_len) {
        set_status(status, status_cap, "bad code size");
        return -1;
    }

    if (hdr.code_size + 16 > 0x4000u) {
        set_status(status, status_cap, "ring3 code too large");
        return -1;
    }

    memcpy(g_ntx_exec_buf, file_buf + sizeof(hdr), hdr.code_size);
    memcpy(NTUX_RING3_CODE_BASE, g_ntx_exec_buf, hdr.code_size);
    g_ring3_code_size = hdr.code_size;
    g_ring3_ready = 1;

    if (thread_create(ring3_thread_entry) < 0) {
        set_status(status, status_cap, "failed to create ring3 thread");
        return -1;
    }
    set_status(status, status_cap, "ring3 thread started");
    return 0;
}
