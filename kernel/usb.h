/* Partes comunes de los controladores USB */
#pragma once
typedef struct {
    int dci, ifn, rep_len, is_kbd; u8 *rep; u8 last[8]; int rid, xb, xs, yb, ys, wb, ws, bb, bc, absolute, xmax, ymax;
    u32 id; int joy, jrid, jxb, jxs, jyb, jys, jxmin, jxmax, jymin, jymax, hatb, hats, hmin, hmax, jbb, jbc; u32 jlast;
    int kb, krid, karr, karrn, karrs, kbm[2], kbmmin[2], kbmn[2]; u8 kdown[32]; int nkdown; u32 ledgen;
} hidif_t;
void hid_parse(hidif_t *, const u8 *, int);
void hid_report(hidif_t *, int);
