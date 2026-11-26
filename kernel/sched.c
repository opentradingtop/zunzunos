#include "kernel.h"

extern void switch_context(u64 *, u64); extern void kthread_tramp(void); extern void isr_return(void);
task_t *cur; static task_t *head, *idle; static u32 next_tid = 1; static int next_pid = 1;
#define MAXPROC 64
static proc_t *procs[MAXPROC]; static int quantum; static task_t *zombies_t; static u64 cur_cr3;
#define KSTACK 32768

static task_t *task_new(const char *name) {
    task_t *t = kzalloc(sizeof *t);
    if (!t) return 0;
    t->kstack = kmalloc(KSTACK);
    if (!t->kstack) { kfree(t); return 0; }
    t->kstack_top = ((u64)t->kstack + KSTACK) & ~15UL;
    t->fx = (u8 *)ALIGN((u64)t->fx_raw, 16); fpu_init_state(t->fx);
    strncpy0(t->name, name, sizeof t->name);
    t->tid = next_tid++;
    return t;
}
static void task_link(task_t *t) {
    u64 f = irq_save();
    t->next = head->next; head->next = t;
    irq_restore(f);
}
void schedule(void) {
    u64 f = irq_save();
    task_t *n = cur->next, *s = n;
    for (;;) {
        if (n->state == T_READY && n != idle) break;
        n = n->next;
        if (n == s) { n = (cur->state == T_READY && cur != idle) ? cur : idle; break; }
    }
    quantum = 10;
    if (n != cur) {
        task_t *p = cur;
        fpu_save(p->fx);
        tss_set_rsp0(n->kstack_top);
        u64 c = n->proc ? n->proc->cr3 : kernel_cr3;
        if (c != cur_cr3) { write_cr3(c); cur_cr3 = c; }
        cur = n;
        fpu_load(n->fx);
        switch_context(&p->rsp, n->rsp);
    }
    irq_restore(f);
}
void yield(void) { schedule(); }
void timer_tick(void) {
    ps2_poll();
    if (!cur) return;
    cur->cpu_ticks++;
    task_t *t = head;
    do { if (t->state == T_BLOCKED && t->wake_at && ticks >= t->wake_at) { t->wake_at = 0; t->state = T_READY; } t = t->next; } while (t != head);
    if (--quantum <= 0) schedule();
}
void task_block(u64 ms) {
    u64 f = irq_save();
    cur->wake_at = ms ? ticks + ms * HZ / 1000 + 1 : 0; cur->state = T_BLOCKED;
    schedule();
    irq_restore(f);
}
void msleep(u64 ms) { if (cur) task_block(ms ? ms : 1); else mdelay_busy(ms); }
void task_wake(task_t *t) { u64 f = irq_save(); if (t->state == T_BLOCKED) { t->state = T_READY; t->wake_at = 0; } irq_restore(f); }

void mutex_lock(mutex_t *m) {
    for (;;) {
        u64 f = irq_save();
        if (!m->owner || m->owner == cur) { m->owner = cur; m->depth++; irq_restore(f); return; }
        irq_restore(f);
        yield();
    }
}
void mutex_unlock(mutex_t *m) { u64 f = irq_save(); if (--m->depth == 0) m->owner = 0; irq_restore(f); }

task_t *kthread(const char *name, void (*fn)(void *), void *arg) {
    task_t *t = task_new(name);
#ifdef __arm__
    arch_kthread_init(t, fn, arg);
#else
    u64 *s = (u64 *)t->kstack_top;
    *--s = 0; *--s = (u64)kthread_tramp; *--s = 0; *--s = 0; *--s = (u64)fn; *--s = (u64)arg; *--s = 0; *--s = 0;
    /* orden de pop: r15 r14 r13 r12 rbx rbp, ret */
    t->rsp = (u64)s;
#endif
    task_link(t);
    return t;
}
static void reaper(void *a) {
    for (;;) {
        msleep(100);
        for (;;) {
            u64 f = irq_save(); task_t *z = zombies_t, *prev = 0, *t = head->next, *pv = head;
            (void)z; (void)prev;
            while (t != head && !(t->state == T_ZOMBIE && t != cur)) { pv = t; t = t->next; }
            if (t == head) { irq_restore(f); break; }
            pv->next = t->next; irq_restore(f);
            proc_t *p = t->proc;
            if (p) {
                u64 fl = irq_save(); int last = --p->nthreads == 0; irq_restore(fl);
                if (last) { vm_free_space(p->cr3); kfree(p); }
            }
            kfree(t->kstack); kfree(t);
        }
    }
}
static void idle_fn(void *a) { for (;;) { cpu_idle(); schedule(); } }
void sched_init(void) {
    task_t *t = task_new("kernel"); t->next = t; head = t; cur = t;
    cur_cr3 = kernel_cr3; tss_set_rsp0(t->kstack_top); quantum = 10;
    idle = kthread("idle", idle_fn, 0);
    kthread("reaper", reaper, 0);
}

/* --- procesos --- */
static proc_t *proc_find(int pid) { for (int i = 0; i < MAXPROC; i++) if (procs[i] && procs[i]->pid == pid) return procs[i]; return 0; }
static task_t *user_task(proc_t *p, const char *name, u64 rip, u64 rsp, u64 rdi) {
    task_t *t = task_new(name);
    if (!t) return 0;
    t->proc = p;
#ifdef __arm__
    arch_user_init(t, rip, rsp, rdi);
#else
    regs_t *r = (regs_t *)(t->kstack_top - sizeof(regs_t));
    memset(r, 0, sizeof *r);
    r->rip = rip; r->cs = 0x2B; r->ss = 0x23; r->rsp = rsp; r->rflags = 0x202; r->rdi = rdi;
    u64 *s = (u64 *)r;
    *--s = (u64)isr_return; for (int i = 0; i < 6; i++) *--s = 0;
    t->rsp = (u64)s;
#endif
    return t;
}
typedef struct PACKED { char magic[8]; u64 entry, memsz; } apphdr_t;
int proc_spawn(const char *path, const char *args) {
    u64 size; u8 *img = vfs_slurp(path, &size);
    if (!img) return E_NOENT;
    apphdr_t *h = (apphdr_t *)img;
    if (size < sizeof *h || memcmp(h->magic, "ZUNAPP01", 8) || h->memsz < size || h->memsz > (1UL << 30)) { kfree(img); return E_INVAL; }
    int slot = -1;
    for (int i = 0; i < MAXPROC; i++) if (!procs[i]) { slot = i; break; }
    if (slot < 0) { kfree(img); return E_NOMEM; }
    proc_t *p = kzalloc(sizeof *p);
    if (!p || !(p->cr3 = vm_new_space())) { kfree(p); kfree(img); return E_NOMEM; }
    const char *n = strrchr(path, '/'); n = n ? n + 1 : path;
    strncpy0(p->name, n, sizeof p->name); if (args) strncpy0(p->args, args, sizeof p->args);
    u64 pages = ALIGN(h->memsz, PAGE) >> 12;
    for (u64 i = 0; i < pages; i++) {
        u64 pa = pmm_alloc();
        if (!pa) { vm_free_space(p->cr3); kfree(p); kfree(img); return E_NOMEM; }
        u8 *d = P2V(pa); memset(d, 0, PAGE);
        u64 o = i * PAGE; if (o < size) memcpy(d, img + o, MIN(PAGE, size - o));
        vm_map(p->cr3, APP_BASE + o, pa, PTE_U | PTE_W);
#ifdef __arm__
        if (o < size) arch_code_sync(d, PAGE);
#endif
    }
    p->mem_pages = pages;
    p->brk_base = p->brk = APP_BASE + pages * PAGE + PAGE;
    p->stack_hi = STACK_TOP; p->stack_lo = STACK_TOP - STACK_SIZE;
    u64 entry = h->entry; kfree(img);
    task_t *t = user_task(p, p->name, entry, STACK_TOP - 64, 0);
    if (!t) { vm_free_space(p->cr3); kfree(p); return E_NOMEM; }
    p->nthreads = 1;
    u64 f = irq_save(); p->pid = next_pid++; procs[slot] = p; task_link(t); irq_restore(f);
    kprintf("proceso %d: %s\n", p->pid, path);
    return p->pid;
}
int thread_create(u64 entry, u64 stack, u64 arg) {
    proc_t *p = cur->proc; task_t *t = user_task(p, p->name, entry, stack, arg);
    if (!t) return E_NOMEM;
    u64 f = irq_save(); p->nthreads++; task_link(t); irq_restore(f);
    return t->tid;
}
static int live_threads(proc_t *p) { int n = 0; u64 f = irq_save(); task_t *t = head; do { if (t->proc == p && t->state != T_ZOMBIE) n++; t = t->next; } while (t != head); irq_restore(f); return n; }
void task_exit(void) { irq_off(); cur->state = T_ZOMBIE; for (;;) { schedule(); cpu_idle(); irq_off(); } }
void proc_exit(int code) {
    proc_t *p = cur->proc;
    p->killed = 1; p->exit_code = code;
    u64 f = irq_save(); task_t *t = head; do { if (t->proc == p && t != cur) task_wake(t); t = t->next; } while (t != head); irq_restore(f);
    if (live_threads(p) == 1) {
        wm_proc_gone(p);
        for (int i = 0; i < MAXFD; i++) if (p->fds[i]) { vfs_close(p->fds[i]); p->fds[i] = 0; }
        f = irq_save(); for (int i = 0; i < MAXPROC; i++) if (procs[i] == p) procs[i] = 0; irq_restore(f);
        kprintf("proceso %d (%s) termino: %d\n", p->pid, p->name, code);
    }
    task_exit();
}
int proc_kill(int pid) {
    proc_t *p = proc_find(pid);
    if (!p) return E_NOENT;
    p->killed = 1;
    u64 f = irq_save(); task_t *t = head; do { if (t->proc == p) task_wake(t); t = t->next; } while (t != head); irq_restore(f);
    return 0;
}
int proc_list(zproc_t *o, int max) {
    int n = 0;
    u64 f = irq_save();
    for (int i = 0; i < MAXPROC && n < max; i++) {
        proc_t *p = procs[i]; if (!p) continue;
        o[n].pid = p->pid; o[n].threads = p->nthreads; memcpy(o[n].name, p->name, 32); o[n].mem_kb = p->mem_pages * 4; o[n].cpu = 0;
        task_t *t = head; do { if (t->proc == p) o[n].cpu += t->cpu_ticks; t = t->next; } while (t != head);
        n++;
    }
    irq_restore(f);
    return n;
}
int vm_fault(u64 a) {
    if (!cur || !cur->proc || a >= USER_TOP) return 0;
    proc_t *p = cur->proc;
    if ((a >= p->brk_base && a < p->brk) || (a >= p->stack_lo && a < p->stack_hi)) {
        u64 pa = pmm_alloc();
        if (!pa) return 0;
        memset(P2V(pa), 0, PAGE);
        vm_map(p->cr3, a & ~(PAGE - 1), pa, PTE_U | PTE_W); p->mem_pages++;
        return 1;
    }
    return 0;
}
int user_ok(const void *v, u64 n) {
    u64 a = (u64)v; proc_t *p = cur->proc;
    if (!p || a + n < a || a + n > USER_TOP) return 0;
    if (!n) return 1;
    for (u64 pg = a & ~(PAGE - 1); pg < a + n; pg += PAGE) {
        u64 fl = 0;
        if (vm_lookup(p->cr3, pg, &fl) && (fl & PTE_U)) continue;
        if ((pg >= p->brk_base && pg < p->brk) || (pg >= p->stack_lo && pg < p->stack_hi)) continue;
        return 0;
    }
    return 1;
}
int user_str(const char *s, char *o, int max) {
    for (int i = 0; i < max; i++) {
        if (!user_ok(s + i, 1)) return -1;
        o[i] = s[i];
        if (!o[i]) return i;
    }
    o[max - 1] = 0;
    return max - 1;
}
void ev_push(task_t *t, const zevent_t *e) {
    u64 f = irq_save(); u32 n = (t->evt + 1) % EVQ;
    if (e->type == EV_MOVE && t->evt != t->evh) {
        zevent_t *l = &t->evq[(t->evt + EVQ - 1) % EVQ];
        if (l->type == EV_MOVE && l->win == e->win) { *l = *e; irq_restore(f); return; }
    }
    if (n != t->evh) { t->evq[t->evt] = *e; t->evt = n; }
    if (t->ev_wait) task_wake(t);
    irq_restore(f);
}
int ev_pop(zevent_t *e, i64 ms) {
    task_t *t = cur; u64 end = ms > 0 ? ticks + ms * HZ / 1000 : 0;
    for (;;) {
        u64 f = irq_save();
        if (t->evh != t->evt) { *e = t->evq[t->evh]; t->evh = (t->evh + 1) % EVQ; irq_restore(f); return 1; }
        if (ms == 0 || (t->proc && t->proc->killed)) { irq_restore(f); return 0; }
        u64 left = 0;
        if (ms > 0) { if (ticks >= end) { irq_restore(f); return 0; } left = (end - ticks) * 1000 / HZ + 1; }
        t->ev_wait = 1; task_block(left); t->ev_wait = 0;
        irq_restore(f);
    }
}
