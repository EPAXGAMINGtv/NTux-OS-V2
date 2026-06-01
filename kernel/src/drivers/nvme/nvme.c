#include <drivers/nvme/nvme.h>

#include <drivers/framebuffer/kprint.h>
#include <drivers/pci/pci.h>
#include <mm/pmm.h>
#include <lib/string.h>

#define NVME_MAX_CONTROLLERS 4
#define NVME_ADMIN_Q_DEPTH 32
#define NVME_IO_Q_DEPTH 64
#define NVME_QUEUE_ID_IO 1
#define NVME_TIMEOUT_SPINS 2000000
#define NVME_PAGE_SIZE 4096u

#define PCI_CLASS_MASS_STORAGE 0x01
#define PCI_SUBCLASS_NVM 0x08
#define PCI_PROGIF_NVME 0x02
#define PCI_COMMAND_REG 0x04
#define PCI_BAR0 0x10

#define NVME_CC_EN (1u << 0)
#define NVME_CSTS_RDY (1u << 0)

#define NVME_OPC_ADMIN_CREATE_IO_SQ 0x01
#define NVME_OPC_ADMIN_CREATE_IO_CQ 0x05
#define NVME_OPC_ADMIN_IDENTIFY 0x06

#define NVME_OPC_NVM_WRITE 0x01
#define NVME_OPC_NVM_READ 0x02

typedef volatile struct {
    uint64_t cap;
    uint32_t vs;
    uint32_t intms;
    uint32_t intmc;
    uint32_t cc;
    uint32_t rsv0;
    uint32_t csts;
    uint32_t nssr;
    uint32_t aqa;
    uint64_t asq;
    uint64_t acq;
} nvme_regs_t;

typedef struct __attribute__((packed)) {
    uint8_t opc;
    uint8_t flags;
    uint16_t cid;
    uint32_t nsid;
    uint32_t rsv0;
    uint32_t rsv1;
    uint64_t mptr;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} nvme_cmd_t;

typedef struct __attribute__((packed)) {
    uint32_t dw0;
    uint32_t dw1;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t cid;
    uint16_t status;
} nvme_cqe_t;

typedef struct {
    nvme_cmd_t* sq;
    nvme_cqe_t* cq;
    uintptr_t sq_phys;
    uintptr_t cq_phys;
    uint16_t q_depth;
    uint16_t sq_tail;
    uint16_t cq_head;
    uint8_t cq_phase;
    volatile uint32_t* sq_db;
    volatile uint32_t* cq_db;
} nvme_queue_t;

typedef struct {
    bool present;
    uint8_t bus;
    uint8_t dev;
    uint8_t func;
    nvme_regs_t* regs;
    uint8_t db_stride;
    uint16_t next_cid;
    nvme_queue_t admin_q;
    nvme_queue_t io_q;
    char model[41];
} nvme_controller_t;

typedef struct {
    bool present;
    uint8_t ctrl_idx;
    uint32_t nsid;
    uint32_t lba_size;
    uint64_t lba_count;
    uint64_t sectors_512;
} nvme_ns_entry_t;

static nvme_controller_t g_ctrls[NVME_MAX_CONTROLLERS];
static size_t g_ctrl_count;
static nvme_ns_entry_t g_namespaces[NVME_MAX_NAMESPACES];
static size_t g_ns_count;

static inline void* nvme_phys_to_virt(uintptr_t phys) {
    return (void*)phys;
}

static volatile uint32_t* nvme_db_ptr(nvme_controller_t* c, uint16_t db_index) {
    uintptr_t base = (uintptr_t)c->regs;
    uintptr_t stride = (uintptr_t)(4u << c->db_stride);
    return (volatile uint32_t*)(base + 0x1000u + ((uintptr_t)db_index * stride));
}

static int nvme_wait_rdy(nvme_controller_t* c, int ready) {
    for (int i = 0; i < NVME_TIMEOUT_SPINS; ++i) {
        int rdy = (c->regs->csts & NVME_CSTS_RDY) ? 1 : 0;
        if (rdy == ready) return 0;
    }
    return -1;
}

static int nvme_submit_and_wait(nvme_controller_t* c, nvme_queue_t* q, const nvme_cmd_t* cmd_in) {
    if (!c || !q || !cmd_in || !q->sq || !q->cq || q->q_depth == 0) return -1;

    nvme_cmd_t cmd = *cmd_in;
    cmd.cid = c->next_cid++;
    q->sq[q->sq_tail] = cmd;

    q->sq_tail = (uint16_t)((q->sq_tail + 1u) % q->q_depth);
    *q->sq_db = q->sq_tail;

    for (int i = 0; i < NVME_TIMEOUT_SPINS; ++i) {
        nvme_cqe_t* cqe = &q->cq[q->cq_head];
        uint8_t phase = (uint8_t)(cqe->status & 1u);
        if (phase != q->cq_phase) continue;
        if (cqe->cid != cmd.cid) continue;

        uint16_t status_field = cqe->status;
        q->cq_head = (uint16_t)((q->cq_head + 1u) % q->q_depth);
        if (q->cq_head == 0) q->cq_phase ^= 1u;
        *q->cq_db = q->cq_head;

        uint16_t sc = (uint16_t)((status_field >> 1) & 0xFFu);
        return (sc == 0) ? 0 : -2;
    }

    return -3;
}

static int nvme_identify_controller(nvme_controller_t* c) {
    uintptr_t page_phys = (uintptr_t)pmm_alloc_page();
    if (!page_phys) return -1;
    uint8_t* page = (uint8_t*)nvme_phys_to_virt(page_phys);
    if (!page) {
        pmm_free_page((void*)page_phys);
        return -1;
    }
    memset(page, 0, NVME_PAGE_SIZE);

    nvme_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opc = NVME_OPC_ADMIN_IDENTIFY;
    cmd.prp1 = page_phys;
    cmd.cdw10 = 1u; /* CNS=1 Identify Controller */

    int rc = nvme_submit_and_wait(c, &c->admin_q, &cmd);
    if (rc == 0) {
        memcpy(c->model, &page[24], 40);
        c->model[40] = '\0';
        for (int i = 39; i >= 0; --i) {
            if (c->model[i] == ' ' || c->model[i] == '\0') c->model[i] = '\0';
            else break;
        }
    }

    pmm_free_page((void*)page_phys);
    return rc;
}

static int nvme_identify_namespace(nvme_controller_t* c, uint32_t nsid, uint64_t* out_lba_count, uint32_t* out_lba_size) {
    if (!out_lba_count || !out_lba_size) return -1;

    uintptr_t page_phys = (uintptr_t)pmm_alloc_page();
    if (!page_phys) return -1;
    uint8_t* page = (uint8_t*)nvme_phys_to_virt(page_phys);
    if (!page) {
        pmm_free_page((void*)page_phys);
        return -1;
    }
    memset(page, 0, NVME_PAGE_SIZE);

    nvme_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opc = NVME_OPC_ADMIN_IDENTIFY;
    cmd.nsid = nsid;
    cmd.prp1 = page_phys;
    cmd.cdw10 = 0u; /* CNS=0 Identify Namespace */

    int rc = nvme_submit_and_wait(c, &c->admin_q, &cmd);
    if (rc == 0) {
        uint64_t nsze = *((uint64_t*)(&page[0]));
        uint8_t flbas = page[26];
        uint8_t lba_fmt = (uint8_t)(flbas & 0x0Fu);
        uint32_t lbaf_off = 128u + ((uint32_t)lba_fmt * 4u);
        uint8_t lbads = page[lbaf_off + 2u];
        uint32_t lba_size = (lbads < 32u) ? (1u << lbads) : 0u;

        if (nsze == 0 || lba_size == 0 || lba_size < 512u || (lba_size % 512u) != 0u) {
            rc = -2;
        } else {
            *out_lba_count = nsze;
            *out_lba_size = lba_size;
        }
    }

    pmm_free_page((void*)page_phys);
    return rc;
}

static int nvme_create_io_queues(nvme_controller_t* c) {
    nvme_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opc = NVME_OPC_ADMIN_CREATE_IO_CQ;
    cmd.prp1 = c->io_q.cq_phys;
    cmd.cdw10 = ((uint32_t)(c->io_q.q_depth - 1u) << 16) | NVME_QUEUE_ID_IO;
    cmd.cdw11 = 1u; /* PC=1 contiguous */
    if (nvme_submit_and_wait(c, &c->admin_q, &cmd) != 0) return -1;

    memset(&cmd, 0, sizeof(cmd));
    cmd.opc = NVME_OPC_ADMIN_CREATE_IO_SQ;
    cmd.prp1 = c->io_q.sq_phys;
    cmd.cdw10 = ((uint32_t)(c->io_q.q_depth - 1u) << 16) | NVME_QUEUE_ID_IO;
    cmd.cdw11 = ((uint32_t)NVME_QUEUE_ID_IO << 16) | 1u; /* CQID=1, PC=1 */
    if (nvme_submit_and_wait(c, &c->admin_q, &cmd) != 0) return -1;

    return 0;
}

static int nvme_controller_init(nvme_controller_t* c) {
    if (!c || !c->regs) return -1;

    uint16_t mqes = (uint16_t)(c->regs->cap & 0xFFFFu);
    uint16_t admin_depth = (uint16_t)((mqes + 1u) < NVME_ADMIN_Q_DEPTH ? (mqes + 1u) : NVME_ADMIN_Q_DEPTH);
    uint16_t io_depth = (uint16_t)((mqes + 1u) < NVME_IO_Q_DEPTH ? (mqes + 1u) : NVME_IO_Q_DEPTH);
    if (admin_depth < 2 || io_depth < 2) return -1;

    c->db_stride = (uint8_t)((c->regs->cap >> 32) & 0x0Fu);
    c->next_cid = 1;

    c->admin_q.q_depth = admin_depth;
    c->io_q.q_depth = io_depth;
    c->admin_q.sq_phys = (uintptr_t)pmm_alloc_page();
    c->admin_q.cq_phys = (uintptr_t)pmm_alloc_page();
    c->io_q.sq_phys = (uintptr_t)pmm_alloc_page();
    c->io_q.cq_phys = (uintptr_t)pmm_alloc_page();
    if (!c->admin_q.sq_phys || !c->admin_q.cq_phys || !c->io_q.sq_phys || !c->io_q.cq_phys) return -1;

    c->admin_q.sq = (nvme_cmd_t*)nvme_phys_to_virt(c->admin_q.sq_phys);
    c->admin_q.cq = (nvme_cqe_t*)nvme_phys_to_virt(c->admin_q.cq_phys);
    c->io_q.sq = (nvme_cmd_t*)nvme_phys_to_virt(c->io_q.sq_phys);
    c->io_q.cq = (nvme_cqe_t*)nvme_phys_to_virt(c->io_q.cq_phys);
    if (!c->admin_q.sq || !c->admin_q.cq || !c->io_q.sq || !c->io_q.cq) return -1;

    memset(c->admin_q.sq, 0, NVME_PAGE_SIZE);
    memset(c->admin_q.cq, 0, NVME_PAGE_SIZE);
    memset(c->io_q.sq, 0, NVME_PAGE_SIZE);
    memset(c->io_q.cq, 0, NVME_PAGE_SIZE);
    c->admin_q.sq_tail = c->admin_q.cq_head = 0;
    c->admin_q.cq_phase = 1;
    c->io_q.sq_tail = c->io_q.cq_head = 0;
    c->io_q.cq_phase = 1;

    c->regs->cc = 0;
    if (nvme_wait_rdy(c, 0) != 0) return -1;

    c->regs->aqa = ((uint32_t)(admin_depth - 1u) << 16) | (uint32_t)(admin_depth - 1u);
    c->regs->asq = c->admin_q.sq_phys;
    c->regs->acq = c->admin_q.cq_phys;
    c->regs->cc = NVME_CC_EN | (4u << 20) | (6u << 16); /* IOCQES=16B, IOSQES=64B */
    if (nvme_wait_rdy(c, 1) != 0) return -1;

    c->admin_q.sq_db = nvme_db_ptr(c, 0);
    c->admin_q.cq_db = nvme_db_ptr(c, 1);
    c->io_q.sq_db = nvme_db_ptr(c, (uint16_t)(2u * NVME_QUEUE_ID_IO));
    c->io_q.cq_db = nvme_db_ptr(c, (uint16_t)(2u * NVME_QUEUE_ID_IO + 1u));

    if (nvme_identify_controller(c) != 0) return -1;
    if (nvme_create_io_queues(c) != 0) return -1;
    return 0;
}

static int nvme_rw_internal(const nvme_ns_entry_t* ns, uint64_t lba_512, uint32_t sector_count_512, void* buffer, int write) {
    if (!ns || !ns->present || !buffer || sector_count_512 == 0) return -1;
    if (ns->ctrl_idx >= g_ctrl_count) return -1;
    nvme_controller_t* c = &g_ctrls[ns->ctrl_idx];

    uint32_t factor = ns->lba_size / 512u;
    if (factor == 0 || (lba_512 % factor) != 0u || (sector_count_512 % factor) != 0u) return -2;
    if (lba_512 + (uint64_t)sector_count_512 > ns->sectors_512) return -3;

    uint64_t lba_native = lba_512 / factor;
    uint32_t left_native = sector_count_512 / factor;
    uint8_t* io = (uint8_t*)buffer;

    uintptr_t dma_phys = (uintptr_t)pmm_alloc_page();
    if (!dma_phys) return -4;
    uint8_t* dma = (uint8_t*)nvme_phys_to_virt(dma_phys);
    if (!dma) {
        pmm_free_page((void*)dma_phys);
        return -4;
    }

    uint32_t max_native = NVME_PAGE_SIZE / ns->lba_size;
    if (max_native == 0) {
        pmm_free_page((void*)dma_phys);
        return -5;
    }

    while (left_native > 0) {
        uint32_t n = (left_native > max_native) ? max_native : left_native;
        uint32_t bytes = n * ns->lba_size;

        if (write) memcpy(dma, io, bytes);

        nvme_cmd_t cmd;
        memset(&cmd, 0, sizeof(cmd));
        cmd.opc = write ? NVME_OPC_NVM_WRITE : NVME_OPC_NVM_READ;
        cmd.nsid = ns->nsid;
        cmd.prp1 = dma_phys;
        cmd.cdw10 = (uint32_t)(lba_native & 0xFFFFFFFFu);
        cmd.cdw11 = (uint32_t)((lba_native >> 32) & 0xFFFFFFFFu);
        cmd.cdw12 = (n - 1u) & 0xFFFFu;

        if (nvme_submit_and_wait(c, &c->io_q, &cmd) != 0) {
            pmm_free_page((void*)dma_phys);
            return -6;
        }

        if (!write) memcpy(io, dma, bytes);

        io += bytes;
        lba_native += n;
        left_native -= n;
    }

    pmm_free_page((void*)dma_phys);
    return 0;
}

static void nvme_add_namespaces(nvme_controller_t* c, uint8_t ctrl_idx) {
    uintptr_t page_phys = (uintptr_t)pmm_alloc_page();
    if (!page_phys) return;
    uint8_t* page = (uint8_t*)nvme_phys_to_virt(page_phys);
    if (!page) {
        pmm_free_page((void*)page_phys);
        return;
    }
    memset(page, 0, NVME_PAGE_SIZE);

    nvme_cmd_t cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.opc = NVME_OPC_ADMIN_IDENTIFY;
    cmd.prp1 = page_phys;
    cmd.cdw10 = 2u; /* CNS=2 active namespace list */
    if (nvme_submit_and_wait(c, &c->admin_q, &cmd) != 0) {
        pmm_free_page((void*)page_phys);
        return;
    }

    uint32_t* nsids = (uint32_t*)page;
    for (size_t i = 0; i < (NVME_PAGE_SIZE / sizeof(uint32_t)); ++i) {
        uint32_t nsid = nsids[i];
        if (nsid == 0) break;
        if (g_ns_count >= NVME_MAX_NAMESPACES) break;

        uint64_t lba_count = 0;
        uint32_t lba_size = 0;
        if (nvme_identify_namespace(c, nsid, &lba_count, &lba_size) != 0) continue;

        nvme_ns_entry_t* ns = &g_namespaces[g_ns_count++];
        memset(ns, 0, sizeof(*ns));
        ns->present = true;
        ns->ctrl_idx = ctrl_idx;
        ns->nsid = nsid;
        ns->lba_size = lba_size;
        ns->lba_count = lba_count;
        ns->sectors_512 = lba_count * (uint64_t)(lba_size / 512u);
    }

    pmm_free_page((void*)page_phys);
}

static void nvme_scan_cb(uint32_t bus, uint32_t device, uint32_t function, uint16_t vendor, uint16_t device_id, void* extra) {
    (void)vendor;
    (void)device_id;
    (void)extra;

    if (g_ctrl_count >= NVME_MAX_CONTROLLERS) return;

    uint8_t class_code = (uint8_t)pci_read_field(bus, device, function, 0x0B, 1);
    uint8_t subclass = (uint8_t)pci_read_field(bus, device, function, 0x0A, 1);
    uint8_t prog_if = (uint8_t)pci_read_field(bus, device, function, 0x09, 1);
    if (class_code != PCI_CLASS_MASS_STORAGE || subclass != PCI_SUBCLASS_NVM || prog_if != PCI_PROGIF_NVME) return;

    uint16_t cmd = (uint16_t)pci_read_field(bus, device, function, PCI_COMMAND_REG, 2);
    cmd |= (1u << 1) | (1u << 2);
    pci_write_field(bus, device, function, PCI_COMMAND_REG, 2, cmd);

    uint32_t bar0_lo = pci_read_field(bus, device, function, PCI_BAR0, 4);
    uint32_t bar0_hi = pci_read_field(bus, device, function, PCI_BAR0 + 4, 4);
    if ((bar0_lo & 0x1u) != 0) return;

    uint64_t mmio_phys = ((uint64_t)bar0_lo & ~0xFu);
    if ((bar0_lo & 0x6u) == 0x4u) mmio_phys |= ((uint64_t)bar0_hi << 32);
    if ((bar0_lo & 0x6u) == 0x4u && bar0_hi != 0u) return;
    if (mmio_phys == 0) return;

    nvme_controller_t* c = &g_ctrls[g_ctrl_count];
    memset(c, 0, sizeof(*c));
    c->bus = (uint8_t)bus;
    c->dev = (uint8_t)device;
    c->func = (uint8_t)function;
    c->regs = (nvme_regs_t*)nvme_phys_to_virt((uintptr_t)mmio_phys);
    if (!c->regs) return;
    if (nvme_controller_init(c) != 0) return;

    c->present = true;
    uint8_t ctrl_idx = (uint8_t)g_ctrl_count;
    g_ctrl_count++;
    nvme_add_namespaces(c, ctrl_idx);
}

void nvme_rescan(bool verbose) {
    memset(g_ctrls, 0, sizeof(g_ctrls));
    memset(g_namespaces, 0, sizeof(g_namespaces));
    g_ctrl_count = 0;
    g_ns_count = 0;

    pci_scan_ex(nvme_scan_cb, NULL);

    if (verbose) {
        kprint("[NVMe] controllers: ");
        kprint_int((int)g_ctrl_count);
        kprint(", namespaces: ");
        kprint_int((int)g_ns_count);
        kprint("\n");
        for (size_t i = 0; i < g_ns_count; ++i) {
            const nvme_ns_entry_t* ns = &g_namespaces[i];
            if (!ns->present) continue;
            const nvme_controller_t* c = &g_ctrls[ns->ctrl_idx];
            kprint("[NVMe] ns#");
            kprint_int((int)i);
            kprint(" model=");
            kprint(c->model[0] ? c->model : "NVMe SSD");
            kprint(" lba=");
            kprint_int((int)ns->lba_size);
            kprint("B\n");
        }
    }
}

void nvme_init(void) {
    nvme_rescan(true);
}

size_t nvme_namespace_count(void) {
    return g_ns_count;
}

const nvme_namespace_t* nvme_get_namespace(uint8_t index) {
    static nvme_namespace_t out;
    if (index >= g_ns_count) return NULL;
    const nvme_ns_entry_t* ns = &g_namespaces[index];
    if (!ns->present || ns->ctrl_idx >= g_ctrl_count) return NULL;

    const nvme_controller_t* c = &g_ctrls[ns->ctrl_idx];
    memset(&out, 0, sizeof(out));
    out.present = true;
    out.controller_index = ns->ctrl_idx;
    out.nsid = ns->nsid;
    out.lba_size = ns->lba_size;
    out.lba_count = ns->lba_count;
    out.sectors_512 = ns->sectors_512;
    strncpy(out.model, c->model[0] ? c->model : "NVMe SSD", sizeof(out.model) - 1);
    out.model[sizeof(out.model) - 1] = '\0';
    return &out;
}

int nvme_read_sectors(uint8_t ns_index, uint64_t lba_512, uint32_t sector_count_512, void* out_buffer) {
    if (ns_index >= g_ns_count) return -1;
    return nvme_rw_internal(&g_namespaces[ns_index], lba_512, sector_count_512, out_buffer, 0);
}

int nvme_write_sectors(uint8_t ns_index, uint64_t lba_512, uint32_t sector_count_512, const void* in_buffer) {
    if (ns_index >= g_ns_count) return -1;
    return nvme_rw_internal(&g_namespaces[ns_index], lba_512, sector_count_512, (void*)in_buffer, 1);
}
