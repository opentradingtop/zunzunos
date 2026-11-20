/* ZunzunOS - cargador UEFI (\EFI\BOOT\BOOTX64.EFI) */
typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;
typedef unsigned long long u64; typedef u64 UINTN; typedef u64 STATUS;
typedef void *HANDLE;
typedef struct { u32 a; u16 b, c; u8 d[8]; } GUID;
typedef struct { u64 sig; u32 rev, size, crc, res; } HDR;

typedef struct { u32 type, pad; u64 phys, virt, pages, attr; } MEMDESC;

typedef struct {
    HDR h;
    void *raise, *restore;
    STATUS (*AllocatePages)(u32 type, u32 memtype, UINTN pages, u64 *addr);
    STATUS (*FreePages)(u64, UINTN);
    STATUS (*GetMemoryMap)(UINTN *size, void *map, UINTN *key, UINTN *dsize, u32 *dver);
    STATUS (*AllocatePool)(u32, UINTN, void **);
    void *FreePool, *ev[6], *pif[3];
    STATUS (*HandleProtocol)(HANDLE, GUID *, void **);
    void *res, *rpn, *lh;
    void *ih, *ict;
    void *load, *start, *exit, *unload;
    STATUS (*ExitBootServices)(HANDLE, UINTN);
    void *mono, *stall;
    STATUS (*SetWatchdogTimer)(UINTN, u64, UINTN, void *);
    void *cc, *dc, *op, *cp, *ope, *pph, *lhb;
    STATUS (*LocateProtocol)(GUID *, void *, void **);
} BOOTSVC;

typedef struct { GUID guid; void *table; } CFGTAB;
typedef struct {
    void *pad; void *Reset; STATUS (*OutputString)(void *, u16 *);
} CONOUT;
typedef struct {
    HDR h; u16 *vendor; u32 rev; HANDLE ci; void *in; HANDLE co; CONOUT *out;
    HANDLE se; void *err; void *rt; BOOTSVC *bs; UINTN ntab; CFGTAB *tab;
} SYSTAB;

typedef struct { u32 rev; HANDLE parent; SYSTAB *st; HANDLE device; } LOADED;
typedef struct FILE_ {
    u64 rev;
    STATUS (*Open)(struct FILE_ *, struct FILE_ **, u16 *, u64, u64);
    STATUS (*Close)(struct FILE_ *);
    void *Delete;
    STATUS (*Read)(struct FILE_ *, UINTN *, void *);
    void *Write;
    STATUS (*GetPos)(struct FILE_ *, u64 *);
    STATUS (*SetPos)(struct FILE_ *, u64);
} FILE;
typedef struct { u64 rev; STATUS (*OpenVolume)(void *, FILE **); } SFS;

typedef struct { u32 ver, w, h, fmt, mask[4], ppl; } GOPINFO;
typedef struct { u32 max, mode; GOPINFO *info; UINTN isz; u64 fb; UINTN fbsz; } GOPMODE;
typedef struct GOP_ {
    STATUS (*Query)(struct GOP_ *, u32, UINTN *, GOPINFO **);
    STATUS (*Set)(struct GOP_ *, u32);
    void *blt;
    GOPMODE *mode;
} GOP;

static GUID g_loaded = {0x5B1B31A1,0x9562,0x11d2,{0x8E,0x3F,0,0xA0,0xC9,0x69,0x72,0x3B}};
static GUID g_sfs = {0x964E5B22,0x6459,0x11d2,{0x8E,0x39,0,0xA0,0xC9,0x69,0x72,0x3B}};
typedef struct { u32 id; u8 rem, pres, lp, ro, wc, p[3]; u32 bs; } BMEDIA;
typedef struct BIO_ { u64 rev; BMEDIA *m; void *reset; STATUS (*Read)(struct BIO_ *, u32, u64, UINTN, void *); } BIO;
static GUID g_bio = {0x964E5B21,0x6459,0x11d2,{0x8E,0x39,0,0xA0,0xC9,0x69,0x72,0x3B}};
static GUID g_gop = {0x9042A9DE,0x23DC,0x4A38,{0x96,0xFB,0x7A,0xDE,0xD0,0x80,0x51,0x6A}};
static GUID g_acpi2 = {0x8868E871,0xE4F1,0x11D3,{0xBC,0x22,0,0x80,0xC7,0x3C,0x88,0x81}};
static GUID g_acpi1 = {0xEB9D2D30,0x2D88,0x11D3,{0x9A,0x16,0,0x90,0x27,0x3F,0xC1,0x4D}};

typedef struct __attribute__((packed)) {
    u64 magic, fb_addr; u32 fb_w, fb_h, fb_pitch, fb_bpp;
    u64 mmap; u32 mmap_count, boot_mode;
    u64 kernel_phys, kernel_size, initrd_phys, initrd_size, rsdp, arena_phys, arena_size;
    u32 fb_rgb, boot_serial;
} BOOTINFO;
typedef struct { u64 base, len; u32 type, acpi; } MMENT;

#define ARENA_PAGES 1024
#define EFI_OK 0
static SYSTAB *ST; static BOOTSVC *BS;

static void say(const char *s) {
    static u16 b[128]; int i = 0;
    while (*s && i < 126) b[i++] = (u8)*s++;
    b[i++] = '\r'; b[i++] = '\n'; b[i] = 0;
    ST->out->OutputString(ST->out, b);
}
static void die(const char *s) { say(s); for (;;) __asm__("hlt"); }
static int gueq(GUID *a, GUID *b) {
    u8 *x = (u8 *)a, *y = (u8 *)b;
    for (int i = 0; i < 16; i++) if (x[i] != y[i]) return 0;
    return 1;
}
static void zero(void *p, u64 n) { __asm__("rep stosb" : "+D"(p), "+c"(n) : "a"(0)); }
void *memset(void *p, int v, u64 n) { u8 *d = p; while (n--) *d++ = (u8)v; return p; }
void *memcpy(void *a, const void *b, u64 n) { void *r = a; __asm__("rep movsb" : "+D"(a), "+S"(b), "+c"(n)); return r; }

static void *load_file(FILE *root, u16 *name, u64 *size, u64 extra, u64 align) {
    FILE *f; u64 sz = 0, addr = 0;
    if (root->Open(root, &f, name, 1, 0) != EFI_OK) die("ZunzunOS: falta un archivo del sistema / missing system file");
    f->SetPos(f, ~0ULL); f->GetPos(f, &sz); f->SetPos(f, 0);
    u64 total = sz + extra;
    u64 pages = ((total + 4095) >> 12) + (align >> 12);
    if (BS->AllocatePages(0, 2, pages, &addr) != EFI_OK) die("ZunzunOS: sin memoria / out of memory");
    addr = (addr + align - 1) & ~(align - 1);
    zero((void *)addr, total);
    UINTN rd = sz;
    if (f->Read(f, &rd, (void *)addr) != EFI_OK || rd != sz) die("ZunzunOS: error de lectura / read error");
    f->Close(f);
    *size = sz;
    return (void *)addr;
}

static void setup_video(BOOTINFO *bi) {
    GOP *gop = 0;
    if (BS->LocateProtocol(&g_gop, 0, (void **)&gop) != EFI_OK || !gop) return;
    u32 best = gop->mode->mode; u64 score = 0; int keep = 0;
    GOPINFO *cur = gop->mode->info;
    if (cur->w <= 1920 && cur->h <= 1200 && cur->w >= 800 && cur->fmt < 2) keep = 1;
    if (!keep) for (u32 m = 0; m < gop->mode->max; m++) {
        GOPINFO *i; UINTN sz;
        if (gop->Query(gop, m, &sz, &i) != EFI_OK) continue;
        if (i->fmt > 1 || i->w > 1920 || i->h > 1200) continue;
        u64 s = (u64)i->w * i->h;
        if (s > score) { score = s; best = m; }
    }
    if (best != gop->mode->mode) gop->Set(gop, best);
    GOPINFO *i = gop->mode->info;
    bi->fb_addr = gop->mode->fb; bi->fb_w = i->w; bi->fb_h = i->h;
    bi->fb_pitch = i->ppl * 4; bi->fb_bpp = 32; bi->fb_rgb = i->fmt == 0;
}

STATUS efi_main(HANDLE img, SYSTAB *st) {
    ST = st; BS = st->bs;
    BS->SetWatchdogTimer(0, 0, 0, 0);
    say("ZunzunOS: cargando... / loading...");

    u64 arena = 0;
    if (BS->AllocatePages(0, 2, ARENA_PAGES, &arena) != EFI_OK) die("ZunzunOS: sin memoria / out of memory");
    zero((void *)arena, ARENA_PAGES * 4096ULL);
    BOOTINFO *bi = (BOOTINFO *)arena;
    MMENT *mm = (MMENT *)(arena + 0x1000);
    u8 *rawmap = (u8 *)(arena + 0x20000);
    u64 *pt = (u64 *)(arena + 0x60000);
    int ptn = 0;
#define NEWPT() ((u64 *)((u8 *)pt + 4096ULL * ptn++))

    LOADED *li; SFS *sfs; FILE *root, *dir;
    if (BS->HandleProtocol(img, &g_loaded, (void **)&li) != EFI_OK) die("ZunzunOS: sin LoadedImage / no LoadedImage");
    if (BS->HandleProtocol(li->device, &g_sfs, (void **)&sfs) != EFI_OK) die("ZunzunOS: sin sistema de archivos / no file system");
    if (sfs->OpenVolume(sfs, &root) != EFI_OK) die("ZunzunOS: no se abre el volumen / cannot open the volume");
    if (root->Open(root, &dir, L"\\ZUNZUN", 1, 0) != EFI_OK) die("ZunzunOS: falta la carpeta ZUNZUN / ZUNZUN folder missing");

    u64 ksz, isz;
    /* memsz real en la cabecera: primero leemos con margen de 16 MB para el BSS */
    u8 *k = load_file(dir, L"KERNEL.BIN", &ksz, 16ULL << 20, 2ULL << 20);
    u8 *ird = load_file(dir, L"INITRD.BIN", &isz, 0, 4096);
    if (((u64 *)k)[0] != 0x314E52454B4E555AULL) die("ZunzunOS: kernel no valido / invalid kernel");

    { BIO *b = 0; if (BS->HandleProtocol(li->device, &g_bio, (void **)&b) == EFI_OK && b && b->m->bs <= 4096 && b->Read(b, b->m->id, 0, b->m->bs, rawmap) == EFI_OK) bi->boot_serial = *(u32 *)(rawmap + 67); }
    setup_video(bi);
    bi->magic = 0x31544F424F4E555AULL;
    bi->boot_mode = 2;
    bi->kernel_phys = (u64)k; bi->initrd_phys = (u64)ird; bi->initrd_size = isz;
    bi->arena_phys = arena; bi->arena_size = ARENA_PAGES * 4096ULL;
    bi->mmap = (u64)mm;
    for (UINTN i = 0; i < st->ntab; i++) {
        if (gueq(&st->tab[i].guid, &g_acpi2)) { bi->rsdp = (u64)st->tab[i].table; break; }
        if (gueq(&st->tab[i].guid, &g_acpi1)) bi->rsdp = (u64)st->tab[i].table;
    }

    UINTN msz, key, dsz; u32 dver; STATUS s;
    for (int tries = 0; ; tries++) {
        msz = 0x40000;
        s = BS->GetMemoryMap(&msz, rawmap, &key, &dsz, &dver);
        if (s != EFI_OK) die("ZunzunOS: error en el mapa de memoria / memory map error");
        if (tries == 0) {
            /* identidad hasta el final de la RAM (minimo 4 GB, maximo 64 GB) */
            u64 top = 4ULL << 30;
            for (UINTN o = 0; o < msz; o += dsz) {
                MEMDESC *d = (MEMDESC *)(rawmap + o);
                u64 e = d->phys + (d->pages << 12);
                if (e > top && d->type != 11 && d->type != 12) top = e;
            }
            if (top > (64ULL << 30)) top = 64ULL << 30;
            u64 *pml4 = NEWPT(), *low = NEWPT(), *hi = NEWPT(), *kpd = NEWPT();
            pml4[0] = (u64)low | 3; pml4[511] = (u64)hi | 3;
            hi[510] = (u64)kpd | 3;
            for (int i = 0; i < 16; i++) kpd[i] = ((u64)k + (u64)i * (2ULL << 20)) | 0x83;
            for (u64 g = 0; g * (1ULL << 30) < top; g++) {
                u64 *pd = NEWPT();
                low[g] = (u64)pd | 3;
                for (int j = 0; j < 512; j++)
                    pd[j] = (g * (1ULL << 30) + (u64)j * (2ULL << 20)) | 0x83;
            }
            bi->kernel_size = (u64)pml4;   /* guardado temporal: CR3 */
        }
        int n = 0;
        for (UINTN o = 0; o < msz && n < 4000; o += dsz) {
            MEMDESC *d = (MEMDESC *)(rawmap + o);
            u32 t = 2;
            if (d->type >= 1 && d->type <= 4) t = 1;
            else if (d->type == 7) t = 1;
            else if (d->type == 9) t = 3;
            else if (d->type == 10) t = 4;
            if (n && mm[n-1].type == t && mm[n-1].base + mm[n-1].len == d->phys) {
                mm[n-1].len += d->pages << 12;
                continue;
            }
            mm[n].base = d->phys; mm[n].len = d->pages << 12; mm[n].type = t; mm[n].acpi = 1; n++;
        }
        bi->mmap_count = n;
        if (BS->ExitBootServices(img, key) == EFI_OK) break;
        if (tries > 8) die("ZunzunOS: ExitBootServices fallo / failed");
    }
    u64 cr3 = bi->kernel_size; bi->kernel_size = 0;
    u64 entry = ((u64 *)k)[1];
    __asm__("cli; mov %0, %%cr3; mov %1, %%rcx; mov %2, %%rdi; sub $64, %%rsp; and $-16, %%rsp; jmp *%%rcx"
            :: "r"(cr3), "r"(entry), "r"(bi) : "rcx", "rdi");
    return 0;
}
