/* Calculadora cientifica */
#include "z.h"
static sb_t tape; static zwin *W; static char ex[128]; static int cur; static char res[64] = "0"; static const char *p; static int bad; static double ans;
static double expr(void);
static void sp(void) { while (*p == ' ') p++; }
static double prim(void) {
    sp(); if (*p == '(') { p++; double v = expr(); sp(); if (*p == ')') p++; else bad = 1; return v; } if (*p == '-') { p++; return -prim(); }
    if (isdigit(*p) || *p == '.') { double v = atof(p); while (isdigit(*p) || *p == '.') p++; return v; }
    char n[8]; int k = 0; while (isalpha(*p) && k < 7) n[k++] = *p++; n[k] = 0;
    if (!strcmp(n, "pi")) return 3.14159265358979; if (!strcmp(n, "e")) return 2.71828182845905; if (!strcmp(n, "ans")) return ans;
    if (!k) { bad = 1; return 0; } double a = prim(), r = 3.14159265358979 / 180;
    if (!strcmp(n, "sin")) return z_sin(a * r); if (!strcmp(n, "cos")) return z_cos(a * r); if (!strcmp(n, "tan")) return z_tan(a * r); if (!strcmp(n, "sqrt")) { if (a < 0) bad = 1; return z_sqrt(a); } if (!strcmp(n, "ln")) { if (a <= 0) bad = 1; return z_ln(a); } if (!strcmp(n, "log")) { if (a <= 0) bad = 1; return z_ln(a) / z_ln(10); } if (!strcmp(n, "abs")) return z_fabs(a); if (!strcmp(n, "atan")) return z_atan(a) / r; bad = 1; return 0;
}
static double pw(void) { double a = prim(); sp(); if (*p == '^') { p++; return z_pow(a, pw()); } return a; }
static double term(void) { double a = pw(); for (;;) { sp(); if (*p == '*') { p++; a *= pw(); } else if (*p == '/') { p++; double b = pw(); if (b == 0) bad = 1; else a /= b; } else if (*p == '%') { p++; double b = pw(); if (b == 0) bad = 1; else a -= b * z_floor(a / b); } else return a; } }
static double expr(void) { double a = term(); for (;;) { sp(); if (*p == '+') { p++; a += term(); } else if (*p == '-') { p++; a -= term(); } else return a; } }
static void eval(void) { p = ex; bad = 0; double v = expr(); sp(); if (*p) bad = 1; if (bad) { strcpy(res, _("Error")); return; } ans = v; snprintf(res, 64, "%.8f", v); int l = strlen(res); while (l > 1 && res[l - 1] == '0') res[--l] = 0; if (res[l - 1] == '.') res[l - 1] = 0; char m[100]; snprintf(m, 100, "[prueba] calc %s = %s", ex, res); z_log(m); sb_f(&tape, "%s = %s\n", ex, res); }
static const char *K[] = {"sin","cos","tan","sqrt","C","ln","log","^","(",")","7","8","9","/","<-","4","5","6","*","%","1","2","3","-","pi",".","0","ans","+","="};
#define BW 58
#define BH 38
static void draw(int hot) {
    g_clear(W, 0x1F2937); g_rect(W, 8, 8, W->w - 16, 62, 0x0F172A); g_text(W, 16, 14, ex, 0x9CA3AF); g_round(W, W->w - 92, 12, 78, 20, 4, 0x374151); g_text(W, W->w - 85, 14, _("Imprimir"), 0xE5E7EB); g_text2(W, W->w - 20 - g_textw(res) * 2, 34, res, 0x5EEAD4, 2);
    for (int i = 0; i < 30; i++) { int x = 8 + (i % 5) * (BW + 4), y = 78 + (i / 5) * (BH + 4), c = i < 10 ? 0x374151 : (i % 5 == 3 || i % 5 == 4) ? 0x475569 : 0x4B5563; if (!strcmp(K[i], "=")) c = UI_ACC; if (i == hot) c = 0x14B8A6; g_round(W, x, y, BW, BH, 4, c); g_text(W, x + (BW - g_textw(K[i])) / 2, y + 11, K[i], 0xFFFFFF); }
    zw_present(W);
}
static void put(const char *s) { int l = strlen(ex), n = strlen(s); if (l + n >= 120) return; memmove(ex + cur + n, ex + cur, l - cur + 1); memcpy(ex + cur, s, n); cur += n; }
static void press(const char *k) { if (!strcmp(k, "C")) { ex[0] = 0; cur = 0; strcpy(res, "0"); } else if (!strcmp(k, "<-")) { if (cur) { memmove(ex + cur - 1, ex + cur, strlen(ex + cur) + 1); cur--; } } else if (!strcmp(k, "=")) eval(); else { put(k); if (isalpha(k[0]) && strcmp(k, "pi") && strcmp(k, "ans")) put("("); } }
static void print_(void) { zp_print_text("Calculadora", 0, tape.p && tape.n ? tape.p : _("Todavía no hay operaciones."), 1); }
int main(const char *a) {
    W = zw_new("Calculadora", -1, 0, 5 * (BW + 4) + 12, 78 + 6 * (BH + 4) + 6, WF_DECOR); draw(-1); zevent e;
    for (;;) { if (!z_event(&e, -1)) continue; if (e.type == EV_REDRAW) { draw(-1); continue; } if (e.type == EV_CLOSE) return 0;
        if (e.type == EV_DOWN && e.y < 34 && e.x >= W->w - 92) { print_(); draw(-1); continue; } if (e.type == EV_KEY && e.ch == 16) { print_(); draw(-1); continue; }
        if (e.type == EV_DOWN) { int c = (e.x - 8) / (BW + 4), r = (e.y - 78) / (BH + 4); if (e.y >= 78 && c < 5 && r < 6) { press(K[r * 5 + c]); draw(r * 5 + c); z_sleep(60); } draw(-1); }
        else if (e.type == EV_KEY) { if (e.ch == '\n' || e.ch == '=') press("="); else if (e.ch == 8) press("<-"); else if (e.ch == 27) press("C"); else if (e.key == K_LEFT) { if (cur) cur--; } else if (e.key == K_RIGHT) { if (ex[cur]) cur++; } else if (e.ch >= 32 && e.ch < 127) { char s[2] = {e.ch, 0}; put(s); } draw(-1); } }
}
