/* Adaptacion del motor HTML de MiOS a las aplicaciones de ZunzunOS */
#define CTRL(c) ((c) & 31)
#pragma once
#include "z.h"
#define strcasecmp web_strcasecmp
#define strncasecmp web_strncasecmp
#define strdup web_strdup
#define kmalloc malloc
#define kfree free
#define ksnprintf snprintf
typedef signed char i8;
enum { KEY_UP = 0x100, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_HOME, KEY_END, KEY_PGUP, KEY_PGDN, KEY_DEL };
void strlcpy(char *, const char *, size_t); int strcasecmp(const char *, const char *); int strncasecmp(const char *, const char *, size_t); char *strdup(const char *);
void gfx_fill(int, int, int, int, u32); void gfx_char(int, int, u8, u32); void gfx_image(int, int, int, int, const u32 *, int, int); void gfx_text_big(int, int, const char *, u32, int);
const char *clip_get(int *);
u8 uni_to_cp437(u32); int utf8_decode(const u8 **, const u8 *); int utf8_encode(u32, char *); char *cp437_to_utf8(const char *, u32, u32 *);
typedef int (*calc_lookup_fn)(void *, const char *, int, const char **, int *);
int calc_eval(const char *, calc_lookup_fn, void *, int, char *, int); int calc_truth(const char *, calc_lookup_fn, void *);
struct html_doc;
struct html_item { int type, x, y, w, h; u32 color, bg; const char *str; int len, scale, bold, underline; const u32 *px; int iw, ih; };
void html_walk(struct html_doc *, void (*)(void *, const struct html_item *), void *);
const char *html_base(struct html_doc *); const char *html_refresh(struct html_doc *, int *secs);
struct html_doc; struct html_doc *html_load(const char *, u32, int); void html_free(struct html_doc *); int html_height(struct html_doc *); u32 html_background(struct html_doc *); const char *html_title(struct html_doc *); void html_draw(struct html_doc *, int, int, int, int, int); const char *html_link_at(struct html_doc *, int, int); int html_anchor_y(struct html_doc *, const char *); int html_has_script(struct html_doc *); int html_click(struct html_doc *, int, int); int html_key(struct html_doc *, int); int html_has_focus(struct html_doc *); void html_relayout(struct html_doc *); int html_focus_y(struct html_doc *); const char *html_focus_text(struct html_doc *);
const char *html_take_submit(struct html_doc *, int *post); int html_nimages(struct html_doc *); const char *html_image_src(struct html_doc *, int); extern int (*html_img_get)(const char *src, u32 **px, int *w, int *h);
