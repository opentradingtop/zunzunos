/* Cliente TLS 1.3 (RFC 8446) con verificacion de certificados X.509.
   Cifrados TLS_AES_128_GCM_SHA256 y TLS_AES_256_GCM_SHA384; intercambio X25519 o P-256;
   firmas ECDSA P-256/P-384, RSA-PSS y RSA PKCS#1. Las raices de confianza son las de Mozilla. */
#include "zcrypto.h"
#include "ztls.h"
static void cpy(void *d, const void *s, long n) { u8 *a = d; const u8 *b = s; while (n-- > 0) *a++ = *b++; }
static int eq(const void *x, const void *y, long n) { const u8 *a = x, *b = y; while (n-- > 0) if (*a++ != *b++) return 0; return 1; }
static int slen(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void serr(ztls *t, const char *m) { int i = 0; while (m[i] && i < 95) { t->err[i] = m[i]; i++; } t->err[i] = 0; }
/* ===================== DER y X.509 ===================== */
static const u8 *der(const u8 *p, const u8 *e, int *tag, long *len) { if (p + 2 > e) return 0; *tag = p[0]; long l = p[1]; p += 2; if (l & 0x80) { int n = l & 0x7f; if (n > 4 || p + n > e) return 0; l = 0; while (n--) l = l << 8 | *p++; } if (p + l > e) return 0; *len = l; return p; }
static const u8 *skip(const u8 *p, const u8 *e) { int t; long l; const u8 *c = der(p, e, &t, &l); return c ? c + l : 0; }
static const u8 OID_RSA[] = {0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01,0x01}, OID_EC[] = {0x2A,0x86,0x48,0xCE,0x3D,0x02,0x01}, OID_P256[] = {0x2A,0x86,0x48,0xCE,0x3D,0x03,0x01,0x07}, OID_P384[] = {0x2B,0x81,0x04,0x00,0x22};
static const u8 OID_SAN[] = {0x55,0x1D,0x11}, OID_BC[] = {0x55,0x1D,0x13};
static int sigalg_of(const u8 *o, long l, int *rsa) { static const u8 r[] = {0x2A,0x86,0x48,0x86,0xF7,0x0D,0x01,0x01}, ec[] = {0x2A,0x86,0x48,0xCE,0x3D,0x04,0x03};
    if (l == 9 && eq(o, r, 8)) { *rsa = 1; return o[8] == 0x0B ? 256 : o[8] == 0x0C ? 384 : o[8] == 0x0D ? 512 : 0; }
    if (l == 8 && eq(o, ec, 7)) { *rsa = 0; return o[7] == 2 ? 256 : o[7] == 3 ? 384 : o[7] == 4 ? 512 : 0; } return 0; }
/* clave publica desde SubjectPublicKeyInfo */
static int parse_spki(const u8 *p, const u8 *e, zkey *k) { int t; long l, ol, bl, cl; const u8 *s = der(p, e, &t, &l); if (!s || t != 0x30) return -1; const u8 *se = s + l;
    const u8 *a = der(s, se, &t, &l); if (!a || t != 0x30) return -1; const u8 *ae = a + l; const u8 *oid = der(a, ae, &t, &ol); if (!oid || t != 6) return -1;
    const u8 *b = der(ae, se, &t, &bl); if (!b || t != 3 || bl < 2) return -1; b++; bl--;
    if (ol == 9 && eq(oid, OID_RSA, 9)) { long ql, nl, xl; const u8 *q = der(b, b + bl, &t, &ql); if (!q) return -1; const u8 *qe = q + ql; const u8 *n = der(q, qe, &t, &nl); if (!n) return -1; const u8 *x = der(n + nl, qe, &t, &xl); if (!x) return -1; k->type = 1; k->a = n; k->al = nl; k->b = x; k->bl = xl; return 0; }
    if (ol == 7 && eq(oid, OID_EC, 7)) { const u8 *c = der(oid + ol, ae, &t, &cl); if (!c || t != 6) return -1; if (cl == 8 && eq(c, OID_P256, 8)) k->type = 256; else if (cl == 5 && eq(c, OID_P384, 5)) k->type = 384; else return -1; k->a = b; k->al = bl; return 0; }
    return -1; }
static long tm(const u8 *p, int t, long l) { /* AAAAMMDDhhmmss como numero */ char d[16]; int n = 0; if (t == 0x17 && l >= 12) { d[n++] = p[0] >= '5' ? '1' : '2'; d[n++] = p[0] >= '5' ? '9' : '0'; for (int i = 0; i < 12; i++) d[n++] = p[i]; } else if (t == 0x18 && l >= 14) for (int i = 0; i < 14; i++) d[n++] = p[i]; else return 0; long v = 0; for (int i = 0; i < n; i++) v = v * 10 + (d[i] - '0'); return v; }
int x509_parse(const u8 *c, long cl, zcert *x) {
    int t; long l; const u8 *e = c + cl, *s = der(c, e, &t, &l); if (!s || t != 0x30) return -1; const u8 *se = s + l;
    const u8 *tb = s, *tbc = der(s, se, &t, &l); if (!tbc || t != 0x30) return -1; x->tbs = tb; x->tbsl = tbc + l - tb; const u8 *te = tbc + l, *p = tbc;
    if (*p == 0xA0) p = skip(p, te); p = skip(p, te); p = skip(p, te); if (!p) return -1;
    x->issuer = p; p = skip(p, te); if (!p) return -1; x->il = p - x->issuer;
    const u8 *v = der(p, te, &t, &l); if (!v) return -1; const u8 *ve = v + l; const u8 *nb = der(v, ve, &t, &l); if (!nb) return -1; x->nb = tm(nb, t, l); const u8 *na = der(nb + l, ve, &t, &l); if (!na) return -1; x->na = tm(na, t, l); p = ve;
    x->subject = p; p = skip(p, te); if (!p) return -1; x->sl = p - x->subject;
    const u8 *sp = p; p = skip(p, te); if (!p) return -1; if (parse_spki(sp, p, &x->key)) x->key.type = 0;
    x->ca = 0; x->san = 0; x->sanl = 0;
    while (p && p < te) { const u8 *q = der(p, te, &t, &l); if (!q) break; if (t == 0xA3) { const u8 *ex = der(q, q + l, &t, &l); const u8 *xe = ex + l;
            while (ex && ex < xe) { long l2; const u8 *one = der(ex, xe, &t, &l2); if (!one) break; const u8 *oe = one + l2; long l3; const u8 *oid = der(one, oe, &t, &l3); const u8 *n2 = oid + l3; if (n2 < oe && *n2 == 1) n2 = skip(n2, oe); long l4; const u8 *val = der(n2, oe, &t, &l4);
                if (val && l3 == 3 && eq(oid, OID_SAN, 3)) { long l5; const u8 *sq = der(val, val + l4, &t, &l5); if (sq) { x->san = sq; x->sanl = l5; } }
                else if (val && l3 == 3 && eq(oid, OID_BC, 3)) { long l5; const u8 *sq = der(val, val + l4, &t, &l5); if (sq && l5 >= 3 && sq[0] == 1 && sq[2]) x->ca = 1; }
                ex = oe; } }
        p = q + l; }
    const u8 *al = der(te, se, &t, &l); if (!al) return -1; long oll; const u8 *oid = der(al, al + l, &t, &oll); if (!oid) return -1; x->halg = sigalg_of(oid, oll, &x->sigrsa); const u8 *sg = der(al + l, se, &t, &l); if (!sg || t != 3 || l < 1) return -1; x->sig = sg + 1; x->sigl = l - 1; return 0; }
static int ecdsa_der(const zkey *k, const u8 *h, int hl, const u8 *sig, long sl) { int t; long l; const u8 *s = der(sig, sig + sl, &t, &l); if (!s) return -1; const u8 *se = s + l; long rl, ql; const u8 *r = der(s, se, &t, &rl); if (!r) return -1; const u8 *q = der(r + rl, se, &t, &ql); if (!q) return -1; return ecdsa_verify(k->type, k->a, k->al, h, hl, r, rl, q, ql); }
static int verify_with(const zcert *c, const zkey *k) { if (!c->halg || !k->type) return -1; u8 h[64]; zc_hash(c->halg, c->tbs, c->tbsl, h);
    if (c->sigrsa) return k->type == 1 ? rsa_verify_pkcs1(k->a, k->al, k->b, k->bl, c->sig, c->sigl, c->halg, h) : -1; return k->type >= 256 ? ecdsa_der(k, h, zc_hlen(c->halg), c->sig, c->sigl) : -1; }
static int host_match(const zcert *c, const char *host) { int hl = slen(host); const u8 *p = c->san, *e = c->san + c->sanl; int t; long l;
    while (p && p < e) { const u8 *v = der(p, e, &t, &l); if (!v) break; if (t == 0x82) { if (l == hl) { int ok = 1; for (int i = 0; i < hl && ok; i++) { int a = v[i] | 32, b = host[i] | 32; ok = a == b; } if (ok) return 1; }
            if (l > 2 && v[0] == '*' && v[1] == '.') { const char *dot = host; while (*dot && *dot != '.') dot++; if (*dot && slen(dot) == l - 1) { int ok = 1; for (int i = 0; i < l - 1 && ok; i++) ok = (v[1 + i] | 32) == (dot[i] | 32); if (ok) return 1; } } }
        p = v + l; } return 0; }
static const u8 *RT; static long RTL; void tls_set_roots(const u8 *d, long n) { RT = d; RTL = n; }
/* busca una raiz cuyo sujeto sea 'name' y que haya firmado c */
static int root_verify(const zcert *c) { if (!RT || RTL < 8 || !eq(RT, "ZRA1", 4)) return -1; long n = (long)RT[4] << 24 | RT[5] << 16 | RT[6] << 8 | RT[7]; const u8 *p = RT + 8, *e = RT + RTL;
    for (long i = 0; i < n && p + 4 <= e; i++) { int sl = p[0] << 8 | p[1]; const u8 *s = p + 2; int kl = s[sl] << 8 | s[sl + 1]; const u8 *k = s + sl + 2; p = k + kl;
        if (sl == c->il && eq(s, c->issuer, sl)) { zkey key; if (!parse_spki(k, k + kl, &key) && !verify_with(c, &key)) return 0; } } return -1; }
int x509_chain(zcert *cs, int n, const char *host, long now) {
    if (!n) return TLS_EUNTRUSTED; if (!host_match(&cs[0], host)) return TLS_EHOST; if (now > 20240101000000L && (now < cs[0].nb || now > cs[0].na)) return TLS_EEXPIRED;
    zcert *cur = &cs[0]; for (int d = 0; d < 8; d++) { if (!root_verify(cur)) return 0; zcert *nx = 0; for (int i = 0; i < n; i++) if (&cs[i] != cur && cs[i].sl == cur->il && eq(cs[i].subject, cur->issuer, cur->il)) { nx = &cs[i]; break; }
        if (!nx || verify_with(cur, &nx->key)) return TLS_EUNTRUSTED; if (now > 20240101000000L && (now < nx->nb || now > nx->na)) return TLS_EEXPIRED; cur = nx; } return TLS_EUNTRUSTED; }
/* ===================== registros ===================== */
static int rd_full(ztls *t, u8 *b, long n) { long o = 0; while (o < n) { long r = t->rcv(t->io, b + o, n - o, 15000); if (r <= 0) return -1; o += r; } return 0; }
static int wr_rec(ztls *t, int type, const u8 *d, long n) { u8 h[5 + 16384 + 256]; h[0] = type; h[1] = 3; h[2] = type == 22 && !t->enc && !t->v12 ? 1 : 3;
    if (t->enc && t->v12) { u8 iv[12], aad[13]; cpy(iv, t->wiv, 4); for (int i = 0; i < 8; i++) { iv[4 + i] = (u8)(t->wseq >> (56 - 8 * i)); aad[i] = iv[4 + i]; } aad[8] = type; aad[9] = 3; aad[10] = 3; aad[11] = n >> 8; aad[12] = n; t->wseq++; long cl = 8 + n + 16; h[3] = cl >> 8; h[4] = cl; cpy(h + 5, iv + 4, 8); gcm_seal(&t->wk, iv, aad, 13, d, n, h + 13, h + 13 + n); return t->snd(t->io, h, 5 + cl) == 5 + cl ? 0 : -1; }
    if (!t->enc) { h[3] = n >> 8; h[4] = n; cpy(h + 5, d, n); return t->snd(t->io, h, 5 + n) == 5 + n ? 0 : -1; }
    long cl = n + 1 + 16; h[0] = 23; h[3] = cl >> 8; h[4] = cl; u8 pt[16384 + 1], iv[12]; cpy(pt, d, n); pt[n] = type; cpy(iv, t->wiv, 12); for (int i = 0; i < 8; i++) iv[4 + i] ^= (u8)(t->wseq >> (56 - 8 * i)); t->wseq++;
    gcm_seal(&t->wk, iv, h, 5, pt, n + 1, h + 5, h + 5 + n + 1); return t->snd(t->io, h, 5 + cl) == 5 + cl ? 0 : -1; }
/* lee un registro y lo descifra; devuelve el tipo interior y deja el contenido en t->rec */
static int rd_rec(ztls *t) { u8 h[5]; if (rd_full(t, h, 5)) return -1; long l = h[3] << 8 | h[4]; if (l > 16384 + 256) return -1; if (rd_full(t, t->rec, l)) return -1;
    if (h[0] == 20) { t->recl = 0; return 20; }
    if (t->renc && t->v12) { if (l < 24) return -1; u8 iv[12], aad[13]; cpy(iv, t->riv, 4); cpy(iv + 4, t->rec, 8); long n = l - 24; for (int i = 0; i < 8; i++) aad[i] = (u8)(t->rseq >> (56 - 8 * i)); aad[8] = h[0]; aad[9] = 3; aad[10] = 3; aad[11] = n >> 8; aad[12] = n; t->rseq++; if (gcm_open(&t->rk, iv, aad, 13, t->rec + 8, n, t->rec, t->rec + 8 + n)) return -2; t->recl = n; return h[0]; }
    if (!t->renc || h[0] != 23) { t->recl = l; return h[0]; }
    if (l < 17) return -1; u8 iv[12]; cpy(iv, t->riv, 12); for (int i = 0; i < 8; i++) iv[4 + i] ^= (u8)(t->rseq >> (56 - 8 * i)); t->rseq++; if (gcm_open(&t->rk, iv, h, 5, t->rec, l - 16, t->rec, t->rec + l - 16)) return -2;
    long n = l - 16; while (n > 0 && !t->rec[n - 1]) n--; if (!n) return -1; t->recl = n - 1; return t->rec[n - 1]; }
/* ===================== programa de claves ===================== */
static void exp_label(int alg, const u8 *sec, const char *label, const u8 *ctx, int cl, u8 *out, int ol) { u8 info[300]; int n = 0, ll = slen(label); info[n++] = ol >> 8; info[n++] = ol; info[n++] = 6 + ll; cpy(info + n, "tls13 ", 6); n += 6; cpy(info + n, label, ll); n += ll; info[n++] = cl; cpy(info + n, ctx, cl); n += cl; zc_hkdf_expand(alg, sec, info, n, out, ol); }
static void derive(int alg, const u8 *sec, const char *label, const u8 *th, u8 *out) { exp_label(alg, sec, label, th, zc_hlen(alg), out, zc_hlen(alg)); }
static void set_keys(ztls *t, const u8 *sec, int write) { u8 k[32], iv[12]; exp_label(t->alg, sec, "key", 0, 0, k, t->keylen); exp_label(t->alg, sec, "iv", 0, 0, iv, 12); if (write) { gcm_init(&t->wk, k, t->keylen); cpy(t->wiv, iv, 12); t->wseq = 0; } else { gcm_init(&t->rk, k, t->keylen); cpy(t->riv, iv, 12); t->rseq = 0; } }
static void th_now(ztls *t, u8 *out) { zhash h; zh_init(&h, t->alg); zh_up(&h, t->tr, t->trl); zh_end(&h, out); }
static void tr_add(ztls *t, const u8 *d, long n) { if (t->trl + n > t->trcap) { long nc = (t->trl + n) * 2; u8 *nb = t->alloc(nc); cpy(nb, t->tr, t->trl); t->free_(t->tr); t->tr = nb; t->trcap = nc; } cpy(t->tr + t->trl, d, n); t->trl += n; }
/* ===================== apreton de manos ===================== */
static void put16(u8 *p, int v) { p[0] = v >> 8; p[1] = v; }
/* ===================== TLS 1.2 (ECDHE + AES-GCM) ===================== */
static void prf(int alg, const u8 *sec, int sl, const char *label, const u8 *seed, int sdl, u8 *out, int ol) { int H = zc_hlen(alg), ll = slen(label); u8 ls[200], A[64], buf[64 + 200], o[64]; cpy(ls, label, ll); cpy(ls + ll, seed, sdl); int lsl = ll + sdl; zc_hmac(alg, sec, sl, ls, lsl, A);
    for (int done = 0; done < ol; ) { cpy(buf, A, H); cpy(buf + H, ls, lsl); zc_hmac(alg, sec, sl, buf, H + lsl, o); int k = ol - done < H ? ol - done : H; cpy(out + done, o, k); done += k; zc_hmac(alg, sec, sl, A, H, A); } }
static int sig_check(zkey *key, int sa, const u8 *data, long dl, const u8 *sg, int sgl) { u8 hh[64];
    if (sa == 0x0403 || sa == 0x0503 || sa == 0x0603) { int a = sa == 0x0403 ? 256 : sa == 0x0503 ? 384 : 512; zc_hash(a, data, dl, hh); return key->type >= 256 ? ecdsa_der(key, hh, zc_hlen(a), sg, sgl) : -1; }
    if (sa >= 0x0804 && sa <= 0x0806) { int a = sa == 0x0804 ? 256 : sa == 0x0805 ? 384 : 512; zc_hash(a, data, dl, hh); return key->type == 1 ? rsa_verify_pss(key->a, key->al, key->b, key->bl, sg, sgl, a, hh) : -1; }
    if (sa == 0x0401 || sa == 0x0501 || sa == 0x0601) { int a = sa == 0x0401 ? 256 : sa == 0x0501 ? 384 : 512; zc_hash(a, data, dl, hh); return key->type == 1 ? rsa_verify_pkcs1(key->a, key->al, key->b, key->bl, sg, sgl, a, hh) : -1; }
    return -1; }
static int hs12(ztls *t, const char *host, long now, int cs, int ems, const u8 *cr, const u8 *sr, const u8 *xpriv, const u8 *ppriv, const u8 *xpub, const u8 *ppub, const u8 *left, long leftl) {
    if (cs == 0xC02B || cs == 0xC02F) { t->alg = 256; t->keylen = 16; } else if (cs == 0xC02C || cs == 0xC030) { t->alg = 384; t->keylen = 32; } else { serr(t, "cifrado TLS 1.2 no admitido"); return -1; }
    t->v12 = 1; u8 *hb = t->alloc(65536 * 4); long hbl = 0; if (leftl > 0) { cpy(hb, left, leftl); hbl = leftl; } zcert *cert = t->alloc(sizeof(zcert) * 8); u8 *certbuf = 0; int ncert = 0, done = 0, grp = 0, r = -1, creq = 0; u8 spub[200]; int spl = 0, H = zc_hlen(t->alg);
    for (int first = 1; !done; first = 0) { if (!first || !hbl) { int ty = rd_rec(t); if (ty == 21) { serr(t, "alerta del servidor (TLS 1.2)"); goto out; } if (ty != 22) { serr(t, "mensaje inesperado (TLS 1.2)"); goto out; }
        if (hbl + t->recl > 65536 * 4) goto out; cpy(hb + hbl, t->rec, t->recl); hbl += t->recl; }
        while (hbl >= 4) { long ml = (hb[1] << 16 | hb[2] << 8 | hb[3]) + 4; if (ml > hbl) break; int mt = hb[0];
            if (mt == 11) { certbuf = t->alloc(ml); cpy(certbuf, hb, ml); long q = 4, le = 7 + (hb[4] << 16 | hb[5] << 8 | hb[6]); q = 7; while (q + 3 <= le && ncert < 8) { long cl = hb[q] << 16 | hb[q + 1] << 8 | hb[q + 2]; q += 3; if (!x509_parse(certbuf + q, cl, &cert[ncert])) ncert++; q += cl; } }
            else if (mt == 12) { const u8 *k = hb + 4; if (k[0] != 3) { serr(t, "intercambio de claves no admitido"); goto out; } grp = k[1] << 8 | k[2]; spl = k[3]; if (spl > 133) goto out; cpy(spub, k + 4, spl); int pl = 4 + spl; int sa = k[pl] << 8 | k[pl + 1], sgl = k[pl + 2] << 8 | k[pl + 3];
                u8 sd[64 + 140]; cpy(sd, cr, 32); cpy(sd + 32, sr, 32); cpy(sd + 64, k, pl); if (!ncert || sig_check(&cert[0].key, sa, sd, 64 + pl, k + pl + 4, sgl)) { serr(t, "la firma del servidor no es valida"); goto out; } }
            else if (mt == 13) creq = 1;   /* pide certificado de cliente: se contesta sin ninguno (es opcional para el servidor) */
            else if (mt == 14) done = 1;
            tr_add(t, hb, ml); for (long i = ml; i < hbl; i++) hb[i - ml] = hb[i]; hbl -= ml; } }
    u8 pms[48], ck[4 + 100]; int ckl, pmsl = 32; if (grp == 0x1d && spl == 32) { x25519(pms, xpriv, spub); ck[4] = 32; cpy(ck + 5, xpub, 32); ckl = 5 + 32; } else if (grp == 0x17 && spl == 65) { if (ecdh_p256(pms, ppriv, spub)) goto out; ck[4] = 65; cpy(ck + 5, ppub, 65); ckl = 5 + 65; } else if (grp == 0x18 && spl == 97) { u8 k3[48]; zc_random(k3, 48); k3[0] &= 0x7F; if (ecdh(384, pms, k3, spub)) goto out; ck[4] = 97; ecdh_pub(384, ck + 5, k3); ckl = 5 + 97; pmsl = 48; } else { serr(t, "grupo de claves no admitido (TLS 1.2)"); goto out; }
    if (creq) { static const u8 nocert[] = {11, 0, 0, 3, 0, 0, 0}; tr_add(t, nocert, 7); if (wr_rec(t, 22, nocert, 7)) goto out; }
    ck[0] = 16; ck[1] = 0; ck[2] = 0; ck[3] = ckl - 4; tr_add(t, ck, ckl); if (wr_rec(t, 22, ck, ckl)) goto out;
    u8 ms[48], seed[64], kb[2 * 32 + 8], th[64]; if (ems) { th_now(t, th); prf(t->alg, pms, pmsl, "extended master secret", th, H, ms, 48); } else { cpy(seed, cr, 32); cpy(seed + 32, sr, 32); prf(t->alg, pms, pmsl, "master secret", seed, 64, ms, 48); }
    cpy(seed, sr, 32); cpy(seed + 32, cr, 32); prf(t->alg, ms, 48, "key expansion", seed, 64, kb, 2 * t->keylen + 8);
    static const u8 ccs[] = {0x14, 3, 3, 0, 1, 1}; t->snd(t->io, ccs, 6); gcm_init(&t->wk, kb, t->keylen); cpy(t->wiv, kb + 2 * t->keylen, 4); t->wseq = 0; t->enc = 1;
    u8 fin[16]; th_now(t, th); fin[0] = 20; fin[1] = 0; fin[2] = 0; fin[3] = 12; prf(t->alg, ms, 48, "client finished", th, H, fin + 4, 12); tr_add(t, fin, 16); if (wr_rec(t, 22, fin, 16)) goto out;
    { int ty = rd_rec(t); if (ty != 20) { char m[80]; int k = 0; const char *b = ty == 21 ? "el servidor rechazo nuestras claves, alerta " : "falta ChangeCipherSpec "; while (*b) m[k++] = *b++; int ad = ty == 21 && t->recl >= 2 ? t->rec[1] : ty; m[k++] = '0' + ad / 100 % 10; m[k++] = '0' + ad / 10 % 10; m[k++] = '0' + ad % 10; m[k] = 0; serr(t, m); goto out; } }
    gcm_init(&t->rk, kb + t->keylen, t->keylen); cpy(t->riv, kb + 2 * t->keylen + 4, 4); t->rseq = 0; t->renc = 1;
    { int ty = rd_rec(t); u8 vd[12]; th_now(t, th); prf(t->alg, ms, 48, "server finished", th, H, vd, 12); if (ty != 22 || t->recl < 16 || t->rec[0] != 20 || !eq(t->rec + 4, vd, 12)) { serr(t, "Finished del servidor no valido (TLS 1.2)"); goto out; } }
    t->cert_status = x509_chain(cert, ncert, host, now); r = 0;
out: t->free_(hb); t->free_(cert); if (certbuf) t->free_(certbuf); t->free_(t->tr); t->tr = 0; t->trl = t->trcap = 0; return r; }
int tls_handshake(ztls *t, const char *host, long now) {
    u8 ch[1024], xpriv[32], xpub[32], ppriv[32], ppub[65]; int n = 0, hl = slen(host); zc_random(xpriv, 32); x25519_base(xpub, xpriv); zc_random(ppriv, 32); ppriv[0] &= 0x7F; ecdh_p256_pub(ppub, ppriv);
    ch[n++] = 1; n += 3; ch[n++] = 3; ch[n++] = 3; zc_random(ch + n, 32); n += 32; ch[n++] = 32; zc_random(ch + n, 32); n += 32; put16(ch + n, 12); n += 2; { static const u16 su[6] = {0x1301, 0x1302, 0xC02B, 0xC02F, 0xC02C, 0xC030}; for (int i = 0; i < 6; i++) { put16(ch + n, su[i]); n += 2; } } ch[n++] = 1; ch[n++] = 0;
    int el = n; n += 2;
    put16(ch + n, 0); put16(ch + n + 2, hl + 5); put16(ch + n + 4, hl + 3); ch[n + 6] = 0; put16(ch + n + 7, hl); cpy(ch + n + 9, host, hl); n += 9 + hl;
    static const u8 fixed[] = {0x00,0x2b,0x00,0x05,0x04,0x03,0x04,0x03,0x03, 0x00,0x0b,0x00,0x02,0x01,0x00, 0x00,0x17,0x00,0x00, 0xff,0x01,0x00,0x01,0x00, 0x00,0x0a,0x00,0x08,0x00,0x06,0x00,0x1d,0x00,0x17,0x00,0x18, 0x00,0x0d,0x00,0x12,0x00,0x10,0x04,0x03,0x05,0x03,0x08,0x04,0x08,0x05,0x08,0x06,0x04,0x01,0x05,0x01,0x06,0x01, 0x00,0x2d,0x00,0x02,0x01,0x01, 0x00,0x10,0x00,0x0b,0x00,0x09,0x08,'h','t','t','p','/','1','.','1'};
    cpy(ch + n, fixed, sizeof fixed); n += sizeof fixed;
    put16(ch + n, 0x33); put16(ch + n + 2, 2 + 4 + 32 + 4 + 65); put16(ch + n + 4, 4 + 32 + 4 + 65); n += 6; put16(ch + n, 0x1d); put16(ch + n + 2, 32); cpy(ch + n + 4, xpub, 32); n += 36; put16(ch + n, 0x17); put16(ch + n + 2, 65); cpy(ch + n + 4, ppub, 65); n += 69;
    put16(ch + el, n - el - 2); ch[1] = (n - 4) >> 16; ch[2] = (n - 4) >> 8; ch[3] = n - 4;
    t->trl = 0; tr_add(t, ch, n); if (wr_rec(t, 22, ch, n)) { serr(t, "no se pudo enviar el saludo"); return -1; }
    /* ServerHello */
    int ty = rd_rec(t); if (ty == 21) { serr(t, "el servidor rechazo la conexion (alerta)"); return -1; } if (ty != 22 || t->recl < 40 || t->rec[0] != 2) { char m[64] = "respuesta no valida del servidor "; int k = 32; m[k++] = '0' + (ty & 255) / 10 % 10; m[k++] = '0' + (ty & 255) % 10; m[k++] = '/'; m[k++] = '0' + t->rec[0] / 10 % 10; m[k++] = '0' + t->rec[0] % 10; m[k] = 0; serr(t, m); return -1; }
    u8 *s = t->rec; long sl = (s[1] << 16 | s[2] << 8 | s[3]) + 4; if (sl > t->recl) { serr(t, "ServerHello partido"); return -1; } tr_add(t, s, sl);
    static const u8 HRR[32] = {0xCF,0x21,0xAD,0x74,0xE5,0x9A,0x61,0x11,0xBE,0x1D,0x8C,0x02,0x1E,0x65,0xB8,0x91,0xC2,0xA2,0x11,0x16,0x7A,0xBB,0x8C,0x5E,0x07,0x9E,0x09,0xE2,0xC8,0xA8,0x33,0x9C}; if (eq(s + 6, HRR, 32)) { serr(t, "el servidor pide otro grupo (HRR)"); return -1; }
    long p = 4 + 2 + 32; p += 1 + s[p]; int cs = s[p] << 8 | s[p + 1]; p += 3; long ee = p + 2 + (s[p] << 8 | s[p + 1]); p += 2; int ver = 0, grp = 0, ems = 0; const u8 *sk = 0; int skl = 0;
    while (p + 4 <= ee) { int et = s[p] << 8 | s[p + 1], ln = s[p + 2] << 8 | s[p + 3]; const u8 *d = s + p + 4; if (et == 0x2b && ln == 2) ver = d[0] << 8 | d[1]; if (et == 0x33 && ln >= 4) { grp = d[0] << 8 | d[1]; skl = d[2] << 8 | d[3]; sk = d + 4; } if (et == 0x17) ems = 1; p += 4 + ln; }
    if (ver != 0x0304) { if (s[4] != 3 || s[5] != 3) { serr(t, "el servidor solo habla TLS 1.1 o anterior (inseguro)"); return -2; } if (eq(s + 6 + 24, "DOWNGRD\x01", 8)) { serr(t, "intento de rebajar la version de TLS"); return -1; } u8 cr[32], sr[32]; cpy(cr, ch + 6, 32); cpy(sr, s + 6, 32); u8 *lf = 0; long lfl = t->recl - sl; if (lfl > 0) { lf = t->alloc(lfl); cpy(lf, t->rec + sl, lfl); } int rr = hs12(t, host, now, cs, ems, cr, sr, xpriv, ppriv, xpub, ppub, lf, lfl); if (lf) t->free_(lf); return rr; }
    if (cs == 0x1301) { t->alg = 256; t->keylen = 16; } else if (cs == 0x1302) { t->alg = 384; t->keylen = 32; } else { serr(t, "cifrado no admitido"); return -1; }
    u8 shared[32]; if (grp == 0x1d && skl == 32) x25519(shared, xpriv, sk); else if (grp == 0x17 && skl == 65) { if (ecdh_p256(shared, ppriv, sk)) { serr(t, "clave P-256 no valida"); return -1; } } else { serr(t, "grupo de claves no admitido"); return -1; }
    int H = zc_hlen(t->alg); u8 zero[64] = {0}, es[64], dr[64], hs[64], eh[64], th[64], chs[64], shs[64], ms[64];
    zc_hkdf_extract(t->alg, 0, 0, zero, H, es); zc_hash(t->alg, 0, 0, eh); derive(t->alg, es, "derived", eh, dr); zc_hkdf_extract(t->alg, dr, H, shared, 32, hs);
    th_now(t, th); derive(t->alg, hs, "c hs traffic", th, chs); derive(t->alg, hs, "s hs traffic", th, shs); set_keys(t, shs, 0); t->renc = 1;
    derive(t->alg, hs, "derived", eh, dr); zc_hkdf_extract(t->alg, dr, H, zero, H, ms);
    /* mensajes cifrados del servidor */
    u8 *hb = t->alloc(65536 * 4), crctx[256]; long hbl = 0; int got_fin = 0, ncert = 0, creq = 0, crl = 0; zcert *cert = t->alloc(sizeof(zcert) * 8); u8 *certbuf = 0; long certl = 0; int r = -1;
    while (!got_fin) { ty = rd_rec(t); if (ty == 20) continue; if (ty == 21) { serr(t, "alerta del servidor durante el saludo"); goto out; } if (ty != 22) { serr(t, ty == -2 ? "fallo al descifrar" : "mensaje inesperado"); goto out; }
        if (hbl + t->recl > 65536 * 4) { serr(t, "saludo demasiado grande"); goto out; } cpy(hb + hbl, t->rec, t->recl); hbl += t->recl;
        while (hbl >= 4) { long ml = (hb[1] << 16 | hb[2] << 8 | hb[3]) + 4; if (ml > hbl) break; int mt = hb[0];
            if (mt == 11) { certbuf = t->alloc(ml); cpy(certbuf, hb, ml); certl = ml; long q = 4 + 1 + hb[4]; long le = q + 3 + (hb[q] << 16 | hb[q + 1] << 8 | hb[q + 2]); q += 3; while (q + 3 <= le && ncert < 8) { long cl = hb[q] << 16 | hb[q + 1] << 8 | hb[q + 2]; q += 3; if (x509_parse(certbuf + q, cl, &cert[ncert]) == 0) ncert++; q += cl; q += 2 + (hb[q] << 8 | hb[q + 1]); } tr_add(t, hb, ml); }
            else if (mt == 15) { if (!ncert) { serr(t, "el servidor no envio certificado"); goto out; } u8 thc[64], ctx[200]; th_now(t, thc); int k = 0; for (int i = 0; i < 64; i++) ctx[k++] = 0x20; cpy(ctx + k, "TLS 1.3, server CertificateVerify", 33); k += 33; ctx[k++] = 0; cpy(ctx + k, thc, H); k += H;
                int sa = hb[4] << 8 | hb[5], sgl = hb[6] << 8 | hb[7]; const u8 *sg = hb + 8; u8 hh[64]; int ok = -1; zkey *key = &cert[0].key;
                if (sa == 0x0403 || sa == 0x0503) { int a = sa == 0x0403 ? 256 : 384; zc_hash(a, ctx, k, hh); ok = ecdsa_der(key, hh, zc_hlen(a), sg, sgl); }
                else if (sa >= 0x0804 && sa <= 0x0806) { int a = sa == 0x0804 ? 256 : sa == 0x0805 ? 384 : 512; zc_hash(a, ctx, k, hh); ok = key->type == 1 ? rsa_verify_pss(key->a, key->al, key->b, key->bl, sg, sgl, a, hh) : -1; }
                else if (sa == 0x0401 || sa == 0x0501 || sa == 0x0601) { int a = sa == 0x0401 ? 256 : sa == 0x0501 ? 384 : 512; zc_hash(a, ctx, k, hh); ok = key->type == 1 ? rsa_verify_pkcs1(key->a, key->al, key->b, key->bl, sg, sgl, a, hh) : -1; }
                if (ok) { serr(t, "la firma del servidor no es valida"); goto out; } tr_add(t, hb, ml); }
            else if (mt == 20) { u8 fk[64], thf[64], vd[64]; exp_label(t->alg, shs, "finished", 0, 0, fk, H); th_now(t, thf); zc_hmac(t->alg, fk, H, thf, H, vd); if (ml != 4 + H || !eq(vd, hb + 4, H)) { serr(t, "Finished del servidor no valido"); goto out; } tr_add(t, hb, ml); got_fin = 1; }
            else if (mt == 13) { creq = 1; crl = hb[4] < 255 && 5 + hb[4] <= ml ? hb[4] : 0; cpy(crctx, hb + 5, crl); tr_add(t, hb, ml); }   /* certificado de cliente: se contestara sin ninguno */
            else tr_add(t, hb, ml);
            for (long i = ml; i < hbl; i++) hb[i - ml] = hb[i]; hbl -= ml; } }
    t->cert_status = x509_chain(cert, ncert, host, now);
    { u8 tha[64], cap[64], sap[64]; th_now(t, tha); derive(t->alg, ms, "c ap traffic", tha, cap); derive(t->alg, ms, "s ap traffic", tha, sap);
      static const u8 ccs[] = {0x14, 3, 3, 0, 1, 1}; t->snd(t->io, ccs, 6); set_keys(t, chs, 1); t->enc = 1;
      if (creq) { u8 cm[4 + 1 + 255 + 3]; int k = 4; cm[k++] = crl; cpy(cm + k, crctx, crl); k += crl; cm[k++] = 0; cm[k++] = 0; cm[k++] = 0; cm[0] = 11; cm[1] = 0; cm[2] = 0; cm[3] = k - 4;
          tr_add(t, cm, k); if (wr_rec(t, 22, cm, k)) { serr(t, "no se pudo enviar el certificado vacio"); goto out; } th_now(t, tha); }   /* Finished cubre tambien este mensaje */
      u8 fk[64], vd[64], fin[4 + 64]; exp_label(t->alg, chs, "finished", 0, 0, fk, H); zc_hmac(t->alg, fk, H, tha, H, vd); fin[0] = 20; fin[1] = 0; fin[2] = 0; fin[3] = H; cpy(fin + 4, vd, H); if (wr_rec(t, 22, fin, 4 + H)) { serr(t, "no se pudo enviar Finished"); goto out; }
      set_keys(t, cap, 1); set_keys(t, sap, 0); cpy(t->sap, sap, H); }
    r = 0;
out: t->free_(hb); t->free_(cert); if (certbuf) t->free_(certbuf); t->free_(t->tr); t->tr = 0; t->trl = t->trcap = 0; return r; }
long tls_write(ztls *t, const void *d, long n) { const u8 *p = d; long o = 0; while (o < n) { long k = n - o > 16000 ? 16000 : n - o; if (wr_rec(t, 23, p + o, k)) return o ? o : -1; o += k; } return n; }
long tls_read(ztls *t, void *d, long n) {
    for (;;) { if (t->aoff < t->alen) { long k = t->alen - t->aoff < n ? t->alen - t->aoff : n; cpy(d, t->rec + t->aoff, k); t->aoff += k; return k; } if (t->closed) return 0;
        int ty = rd_rec(t); if (ty < 0) { t->closed = 1; return ty == -1 ? 0 : -1; } if (ty == 23) { t->aoff = 0; t->alen = t->recl; continue; }
        if (ty == 21) { t->closed = 1; return 0; }
        if (ty == 22 && t->recl >= 5 && t->rec[0] == 24) { int H = zc_hlen(t->alg); u8 ns[64]; exp_label(t->alg, t->sap, "traffic upd", 0, 0, ns, H); cpy(t->sap, ns, H); set_keys(t, ns, 0); } } }
