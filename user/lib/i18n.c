/* Traducciones: clave en espanol -> {espanol con acentos o NULL, ingles} */
#include "z.h"
#include "i18n_tab.h"
#define NS 4096
static short slot[NS]; static int built;
static u32 hs(const char *s) { u32 h = 2166136261u; while (*s) h = (h ^ (u8)*s++) * 16777619u; return h; }
static void build(void) { for (int i = 0; i < NS; i++) slot[i] = -1; int n = sizeof TRT / sizeof TRT[0]; for (int i = 0; i < n; i++) { u32 h = hs(TRT[i][0]) & (NS - 1); while (slot[h] >= 0) h = (h + 1) & (NS - 1); slot[h] = i; } built = 1; }
extern const char __ro_start[], __ro_end[];
const char *trl(const char *s) { return s >= __ro_start && s < __ro_end ? tr(s) : s; }
const char *tr(const char *s) { if (!s || !*s) return s; if (!built) build(); u32 h = hs(s) & (NS - 1); while (slot[h] >= 0) { const char *const *e = TRT[slot[h]]; if (!strcmp(e[0], s)) return TH.lang ? e[2] : e[1] ? e[1] : e[0]; h = (h + 1) & (NS - 1); } return s; }
