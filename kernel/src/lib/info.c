#include <lib/info.h>
#include <interrupt/timer.h>
#include <drivers/framebuffer/kprint.h>
#include <mm/pmm.h>
#include <lib/string.h>




void info_get_cpu_vendor(char vendor[13])
{
    uint32_t ebx, ecx, edx;
    asm volatile("cpuid"
                 : "=b"(ebx), "=c"(ecx), "=d"(edx)
                 : "a"(0));
    ((uint32_t*)vendor)[0] = ebx;
    ((uint32_t*)vendor)[1] = edx;
    ((uint32_t*)vendor)[2] = ecx;
    vendor[12] = '\0';
}

void info_get_cpu_brand(char* out, size_t cap)
{
    if (!out || cap == 0) return;
    char brand[49];
    memset(brand, 0, sizeof(brand));
    uint32_t* p = (uint32_t*)brand;

    for (int i = 0; i < 3; i++) {
        asm volatile("cpuid"
                     : "=a"(p[4*i + 0]), "=b"(p[4*i + 1]),
                       "=c"(p[4*i + 2]), "=d"(p[4*i + 3])
                     : "a"(0x80000002 + i));
    }

    char* start = brand;
    while (*start == ' ') start++;
    char* end = start + strlen(start);
    if (end > start) {
        end--;
        while (end > start && *end == ' ') *end-- = '\0';
    }

    if (start != brand) {
        size_t len = strlen(start);
        if (len + 1 > sizeof(brand)) len = sizeof(brand) - 1;
        for (size_t i = 0; i <= len; ++i) {
            brand[i] = start[i];
        }
    }

    size_t len = strlen(brand);
    if (len + 1 > cap) len = cap - 1;
    memcpy(out, brand, len);
    out[len] = '\0';
}



uint32_t info_cpu_features(void)
{
    uint32_t edx;
    asm volatile("cpuid" : "=d"(edx) : "a"(1));
    return edx;
}




uint32_t info_cpu_features_ext(void)
{
    uint32_t ebx;
    asm volatile("cpuid" : "=b"(ebx) : "a"(7), "c"(0));
    return ebx;
}





int info_has_sse(void)   { return info_cpu_features() & (1 << 25); }
int info_has_sse2(void)  { return info_cpu_features() & (1 << 26); }
int info_has_avx(void)   { return info_cpu_features_ext() & (1 << 28); }
int info_has_avx2(void)   { return info_cpu_features_ext() & (1 << 5);  }




void info_cmd_cpuinfo(void)
{
    char vendor[13];
    info_get_cpu_vendor(vendor);

    char brand[49];
    info_get_cpu_brand(brand, sizeof(brand));

    kprint("CPU Model  : ");
    kprint(brand);
    kprint("\n");

    kprint("Vendor     : ");
    kprint(vendor);
    kprint("\n");

    kprint("Features   :");
    if (info_has_sse())   kprint(" SSE");
    if (info_has_sse2())  kprint(" SSE2");
    if (info_has_avx())   kprint(" AVX");
    if (info_has_avx2())  kprint(" AVX2");
    kprint("\n");
}









void info_cmd_uptime(void)
{
    uint64_t ticks = get_tick_count();        
    uint64_t total_ms  = ticks;               
    uint64_t hours     = total_ms / 3600000;
    uint64_t minutes   = (total_ms % 3600000) / 60000;
    uint64_t seconds   = (total_ms % 60000) / 1000;
    uint64_t ms        = total_ms % 1000;

    kprint("Uptime     : ");
    if (hours)   kprint_uint(hours),   kprint("h ");
    if (hours || minutes) kprint_uint(minutes), kprint("m ");
    kprint_uint(seconds);
    kprint(".");
    if (ms < 100) kprint("0");
    if (ms < 10)  kprint("0");
    kprint_uint(ms);
    kprint("s\n");
}
