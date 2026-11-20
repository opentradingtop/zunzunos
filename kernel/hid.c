/* Dispositivos HID: raton, tableta, teclados de cualquier protocolo y mandos de juego */
#include "kernel.h"
#include "usb.h"
static u32 getbits(const u8 *b, int off, int n) { u32 v = 0; for (int i = 0; i < n; i++) if (b[(off + i) >> 3] & (1 << ((off + i) & 7))) v |= 1u << i; return v; }
static int sext(u32 v, int n) { return n && n < 32 && (v & (1u << (n - 1))) ? (int)(v | (~0u << n)) : (int)v; }
static int sval(u32 v, int sz) { return sz == 1 ? (i8)v : sz == 2 ? (i16)v : (int)v; }
void hid_parse(hidif_t *d, const u8 *r, int len) {
    u32 up = 0, rsz = 0, rcnt = 0, id = 0; int lmin = 0, lmax = 0; u32 us[16]; int nu = 0; u32 umin = 0, umax = 0; static u16 offs[256]; memset(offs, 0, sizeof offs);
    d->xs = d->ys = 0; d->bc = 0; d->rid = -1; d->joy = 0; d->jxs = d->jys = d->hats = d->jbc = 0; d->kb = 0; d->karrn = d->kbmn[0] = d->kbmn[1] = 0; int app = 0;
    for (int i = 0; i < len;) {
        u8 k = r[i++]; if (k == 0xFE) { i += r[i] + 2; continue; }
        int sz = k & 3; if (sz == 3) sz = 4; u32 v = 0; for (int j = 0; j < sz && i + j < len; j++) v |= (u32)r[i + j] << (8 * j); i += sz;
        int t = (k >> 2) & 3, tag = k >> 4;
        if (t == 1) switch (tag) { case 0: up = v; break; case 1: lmin = sval(v, sz); break; case 2: lmax = sz == 1 && lmin >= 0 ? (int)(u8)v : sz == 2 && lmin >= 0 ? (int)(u16)v : sval(v, sz); break; case 7: rsz = v; break; case 9: rcnt = v; break; case 8: id = v & 255; break; }
        else if (t == 2) switch (tag) { case 0: if (nu < 16) us[nu++] = sz == 4 && v >> 16 ? v : (up << 16 | v); break; case 1: umin = v; break; case 2: umax = v; break; }
        else if (t == 0) {
            if (tag == 10 && v == 1 && !app) { app = nu ? us[0] : (int)(up << 16 | umin); if (app == 0x10004 || app == 0x10005) d->joy = 1; if (app == 0x10006) d->kb = 1; }
            if (tag == 8) {
                int off = offs[id], var = v & 2;
                if (!(v & 1)) {
                    if (up == 7 && !var && !d->karrn && rsz == 8) { d->karr = off; d->karrn = rcnt; d->krid = id; d->kb = 1; }
                    else for (u32 c = 0; c < rcnt; c++) {
                        u32 u = c < (u32)nu ? us[c] : nu ? us[nu - 1] : (up << 16 | (umin + c)); int pos = off + c * rsz;
                        if (umax && !nu) u = up << 16 | (umin + c);
                        if (u >> 16 == 7) { int s = d->kbmn[0] ? 1 : 0; if (var && rsz == 1 && !d->kbmn[s]) { d->kbm[s] = pos; d->kbmmin[s] = u & 0xFFFF; d->kbmn[s] = rcnt - c; d->krid = id; d->kb = 1; } break; }
                        if (d->joy) {
                            if (u == 0x10030) { d->jxb = pos; d->jxs = rsz; d->jxmin = lmin; d->jxmax = lmax; d->jrid = id; }
                            else if (u == 0x10031) { d->jyb = pos; d->jys = rsz; d->jymin = lmin; d->jymax = lmax; }
                            else if (u == 0x10039 && !d->hats) { d->hatb = pos; d->hats = rsz; d->hmin = lmin; d->hmax = lmax; d->jrid = id; }
                            else if (u >> 16 == 9 && !d->jbc) { d->jbb = pos; d->jbc = MIN(rcnt - c, 16U); d->jrid = id; }
                            continue;
                        }
                        if (u == 0x10030 && !d->xs) { d->xb = pos; d->xs = rsz; d->absolute = !(v & 4); d->xmax = lmax; d->rid = id; }
                        else if (u == 0x10031 && !d->ys) { d->yb = pos; d->ys = rsz; d->ymax = lmax; }
                        else if (u == 0x10038 && !d->ws) { d->wb = pos; d->ws = rsz; }
                        else if (u >> 16 == 9 && !d->bc) { d->bb = pos; d->bc = MIN(rcnt, 8U); }
                    }
                }
                offs[id] = off + rcnt * rsz;
            }
            if (tag == 8 || tag == 9 || tag == 11 || tag == 10) { nu = 0; umin = umax = 0; }
        }
    }
    if (d->joy && !d->jxs && !d->hats && !d->jbc) d->joy = 0;
    if (d->kb && !d->karrn && !d->kbmn[0]) d->kb = 0;
    if (d->joy) d->xs = 0;
}
static int axis(u32 raw, int sz, int mn, int mx) { int v = mn < 0 ? sext(raw, sz) : (int)raw; int q = (mx - mn) / 4; if (mx <= mn) return 0; return v <= mn + q ? -1 : v >= mx - q ? 1 : 0; }
void hid_report(hidif_t *d, int len) {
    u8 *r = d->rep;
    if (d->is_kbd) {
        u8 m = r[0] ^ d->last[0];
        for (int i = 0; i < 8; i++) if (m & (1 << i)) input_key(0xE0 + i, r[0] >> i & 1);
        if (r[2] == 1) return;
        for (int i = 2; i < 8; i++) { u8 k = d->last[i]; if (k > 3) { int f = 0; for (int j = 2; j < 8; j++) if (r[j] == k) f = 1; if (!f) input_key(k, 0); } }
        for (int i = 2; i < 8; i++) { u8 k = r[i]; if (k > 3) { int f = 0; for (int j = 2; j < 8; j++) if (d->last[j] == k) f = 1; if (!f) input_key(k, 1); } }
        memcpy(d->last, r, 8); return;
    }
    const u8 *p = r;
    if (d->kb) {
        if (d->krid) { if (p[0] != d->krid) goto other; p++; }
        u8 now[32]; int n = 0;
        for (int i = 0; i < d->karrn && n < 32; i++) { u8 k = getbits(p, d->karr + i * 8, 8); if (k > 3) now[n++] = k; }
        for (int s = 0; s < 2; s++) for (int i = 0; i < d->kbmn[s] && n < 32; i++) if (getbits(p, d->kbm[s] + i, 1)) now[n++] = d->kbmmin[s] + i;
        for (int i = 0; i < d->nkdown; i++) { int f = 0; for (int j = 0; j < n; j++) if (now[j] == d->kdown[i]) f = 1; if (!f) input_key(d->kdown[i], 0); }
        for (int j = 0; j < n; j++) { int f = 0; for (int i = 0; i < d->nkdown; i++) if (now[j] == d->kdown[i]) f = 1; if (!f) input_key(now[j], 1); }
        memcpy(d->kdown, now, n); d->nkdown = n; return;
    }
other:
    p = r;
    if (d->joy) {
        if (d->jrid) { if (p[0] != d->jrid) return; p++; }
        int dx = d->jxs ? axis(getbits(p, d->jxb, d->jxs), d->jxs, d->jxmin, d->jxmax) : 0, dy = d->jys ? axis(getbits(p, d->jyb, d->jys), d->jys, d->jymin, d->jymax) : 0;
        if (d->hats) { int h = (int)getbits(p, d->hatb, d->hats) - d->hmin; static const i8 hx[8] = {0, 1, 1, 1, 0, -1, -1, -1}, hy[8] = {-1, -1, 0, 1, 1, 1, 0, -1}; if (h >= 0 && h < 8) { if (!dx) dx = hx[h]; if (!dy) dy = hy[h]; } }
        u32 b = 0; for (int i = 0; i < d->jbc; i++) if (getbits(p, d->jbb + i, 1)) b |= 1u << i;
        u32 st = b | (u32)(dx + 1) << 24 | (u32)(dy + 1) << 26; if (st != d->jlast) { d->jlast = st; input_joy(b, dx, dy, d->id); } return;
    }
    if (d->rid > 0) { if (p[0] != d->rid) return; p++; }
    if (!d->xs) return;
    int b = 0; for (int i = 0; i < d->bc; i++) if (getbits(p, d->bb + i, 1)) b |= 1 << i;
    u32 x = getbits(p, d->xb, d->xs), y = getbits(p, d->yb, d->ys); int w = d->ws ? sext(getbits(p, d->wb, d->ws), d->ws) : 0;
    if (d->absolute) input_abs(x, y, d->xmax ? d->xmax : 32767, d->ymax ? d->ymax : 32767, b & 7, w);
    else input_mouse(sext(x, d->xs), sext(y, d->ys), b & 7, w);
}
