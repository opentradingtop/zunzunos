/* Lector JSON propio y minimo: trabaja sobre el texto sin copiarlo. Para las respuestas de las APIs de IA
   (Forense y su protocolo de Lab). Cada funcion recibe un puntero al inicio de un valor y no pasa nunca de 'end'. */
#include "z.h"

static const char *ws(const char *p, const char *e) { while (p < e && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++; return p; }

/* salta un valor completo; devuelve lo que viene despues o 0 si el JSON esta roto */
const char *zj_skip(const char *p, const char *e) {
    p = ws(p, e); if (p >= e) return 0;
    if (*p == '"') { for (p++; p < e; p++) { if (*p == '\\') p++; else if (*p == '"') return p + 1; } return 0; }
    if (*p == '{' || *p == '[') { int depth = 0; for (; p < e; p++) {
        if (*p == '"') { for (p++; p < e && *p != '"'; p++) if (*p == '\\') p++; if (p >= e) return 0; continue; }
        if (*p == '{' || *p == '[') depth++; else if (*p == '}' || *p == ']') { if (--depth == 0) return p + 1; if (depth < 0) return 0; } } return 0; }
    while (p < e && *p != ',' && *p != '}' && *p != ']' && *p != ' ' && *p != '\n' && *p != '\r' && *p != '\t') p++;
    return p; }

/* valor de la clave key en el objeto que empieza en obj ('{'); 0 si no esta */
const char *zj_get(const char *obj, const char *e, const char *key) {
    const char *p = ws(obj, e); if (p >= e || *p != '{') return 0; p++; int kl = strlen(key);
    for (;;) {
        p = ws(p, e); if (p >= e || *p == '}') return 0; if (*p != '"') return 0;
        const char *k = p + 1, *ke = zj_skip(p, e); if (!ke) return 0;
        p = ws(ke, e); if (p >= e || *p != ':') return 0; p = ws(p + 1, e);
        if (ke - 1 - k == kl && !memcmp(k, key, kl)) return p;
        p = zj_skip(p, e); if (!p) return 0; p = ws(p, e); if (p < e && *p == ',') p++; } }

/* elemento i de la lista que empieza en arr ('['); 0 si no hay */
const char *zj_item(const char *arr, const char *e, int i) {
    const char *p = ws(arr, e); if (p >= e || *p != '[') return 0; p++;
    for (int k = 0;; k++) { p = ws(p, e); if (p >= e || *p == ']') return 0; if (k == i) return p; p = zj_skip(p, e); if (!p) return 0; p = ws(p, e); if (p < e && *p == ',') p++; } }

static int hex4(const char *p, const char *e) { if (p + 4 > e) return -1; int v = 0; for (int i = 0; i < 4; i++) { char c = p[i]; v <<= 4; if (c >= '0' && c <= '9') v |= c - '0'; else if ((c | 32) >= 'a' && (c | 32) <= 'f') v |= (c | 32) - 'a' + 10; else return -1; } return v; }

/* cadena a UTF-8; devuelve su longitud (sin contar lo que no cupo) o -1 si no es una cadena */
int zj_str(const char *v, const char *e, char *o, int cap) {
    const char *p = ws(v, e); if (p >= e || *p != '"') { if (cap) o[0] = 0; return -1; } p++; int k = 0;
    while (p < e && *p != '"') {
        u32 c = (u8)*p++;
        if (c == '\\' && p < e) { char x = *p++; c = x == 'n' ? '\n' : x == 't' ? '\t' : x == 'r' ? '\r' : x == 'b' ? 8 : x == 'f' ? 12 : (u8)x;
            if (x == 'u') { int h = hex4(p, e); if (h < 0) break; p += 4; c = h;
                if (c >= 0xD800 && c < 0xDC00 && p + 6 <= e && p[0] == '\\' && p[1] == 'u') { int l = hex4(p + 2, e); if (l >= 0xDC00 && l < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (l - 0xDC00); p += 6; } } }
            char t[4]; int n = c < 0x80 ? (t[0] = c, 1) : c < 0x800 ? (t[0] = 0xC0 | c >> 6, t[1] = 0x80 | (c & 63), 2) : c < 0x10000 ? (t[0] = 0xE0 | c >> 12, t[1] = 0x80 | (c >> 6 & 63), t[2] = 0x80 | (c & 63), 3) : (t[0] = 0xF0 | c >> 18, t[1] = 0x80 | (c >> 12 & 63), t[2] = 0x80 | (c >> 6 & 63), t[3] = 0x80 | (c & 63), 4);
            if (k + n < cap) { memcpy(o + k, t, n); k += n; } continue; }
        if (k + 1 < cap) o[k++] = c; }
    if (cap) o[k < cap ? k : cap - 1] = 0; return k; }

/* numero: parte entera y milesimas (0.7 -> 700); 0 si no es un numero */
int zj_milli(const char *v, const char *e, long *out) {
    const char *p = ws(v, e); int neg = 0; long a = 0, f = 0, fd = 0; if (p < e && *p == '-') { neg = 1; p++; }
    if (p >= e || *p < '0' || *p > '9') return 0;
    while (p < e && *p >= '0' && *p <= '9') a = a * 10 + (*p++ - '0');
    if (p < e && *p == '.') { p++; while (p < e && *p >= '0' && *p <= '9') { if (fd < 3) { f = f * 10 + (*p - '0'); fd++; } p++; } }
    while (fd < 3) { f *= 10; fd++; }
    *out = (a * 1000 + f) * (neg ? -1 : 1); return 1; }

int zj_true(const char *v, const char *e) { const char *p = ws(v, e); return p + 4 <= e && !memcmp(p, "true", 4); }

/* escribe s como cadena JSON (con comillas) en o; devuelve los bytes escritos (o los que harian falta si cap es 0) */
long zj_escape(const char *s, char *o, long cap) {
    long k = 0; static const char hx[] = "0123456789abcdef";
#define PUT(c) do { if (k < cap) o[k] = (c); k++; } while (0)
    PUT('"');
    for (; *s; s++) { u8 c = *s;
        if (c == '"' || c == '\\') { PUT('\\'); PUT(c); } else if (c == '\n') { PUT('\\'); PUT('n'); } else if (c == '\r') { PUT('\\'); PUT('r'); } else if (c == '\t') { PUT('\\'); PUT('t'); }
        else if (c < 32) { PUT('\\'); PUT('u'); PUT('0'); PUT('0'); PUT(hx[c >> 4]); PUT(hx[c & 15]); } else PUT(c); }
    PUT('"');
#undef PUT
    return k; }
