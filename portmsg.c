#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

#include "fonts/portmsg_bold.h"
#include "fonts/portmsg_regular.h"

#define MAX_CP 0x2200
#define AXIS_DEADZONE 9000
#define INPUT_GUARD_MS 300
#define NO_PAD_TIMEOUT 20
#define REPRESENT_MS 100

typedef struct {
    SDL_Texture *tex;
    int w, h, xoff, yoff;
    float adv;
} Glyph;

typedef struct {
    stbtt_fontinfo *info;
    float px, scale;
    float ascent, line;
    Glyph *cache[MAX_CP];
} Face;

typedef struct {
    const char *s;
    int len;
} Line;

typedef struct {
    Line *v;
    int n, cap;
} Lines;

typedef struct {
    Uint8 r, g, b;
} Rgb;

static const Rgb COL_BG_TOP = {0x13, 0x16, 0x1C};
static const Rgb COL_BG_BOT = {0x0B, 0x0C, 0x10};
static const Rgb COL_CARD = {0x17, 0x1A, 0x21};
static const Rgb COL_BORDER = {0x29, 0x2E, 0x3A};
static const Rgb COL_TITLE = {0xF2, 0xF4, 0xF8};
static const Rgb COL_BODY = {0xB4, 0xBA, 0xC6};
static const Rgb COL_MUTED = {0x76, 0x7D, 0x8C};
static const Rgb COL_KEY = {0x2A, 0x2F, 0x3B};

static SDL_Renderer *R;
static stbtt_fontinfo font_regular, font_bold;

/* ---------- text ---------- */

static int utf8_next(const char **p, const char *end) {
    const unsigned char *s = (const unsigned char *)*p;
    int cp, n;
    if (s[0] < 0x80) {
        cp = s[0];
        n = 1;
    } else if ((s[0] & 0xE0) == 0xC0 && end - *p >= 2) {
        cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F);
        n = 2;
    } else if ((s[0] & 0xF0) == 0xE0 && end - *p >= 3) {
        cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F);
        n = 3;
    } else if ((s[0] & 0xF8) == 0xF0 && end - *p >= 4) {
        cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F);
        n = 4;
    } else {
        cp = '?';
        n = 1;
    }
    *p += n;
    return cp;
}

static void face_init(Face *f, stbtt_fontinfo *info, float px) {
    int asc, desc, gap;
    memset(f, 0, sizeof(*f));
    f->info = info;
    f->px = px;
    f->scale = stbtt_ScaleForPixelHeight(info, px);
    stbtt_GetFontVMetrics(info, &asc, &desc, &gap);
    f->ascent = asc * f->scale;
    f->line = (asc - desc) * f->scale;
}

static void face_free(Face *f) {
    for (int i = 0; i < MAX_CP; i++) {
        if (f->cache[i]) {
            if (f->cache[i]->tex) {
                SDL_DestroyTexture(f->cache[i]->tex);
            }
            free(f->cache[i]);
        }
    }
}

static int face_cp(Face *f, int cp) {
    if (cp >= MAX_CP || !stbtt_FindGlyphIndex(f->info, cp)) {
        return '?';
    }
    return cp;
}

static Glyph *face_glyph(Face *f, int cp) {
    cp = face_cp(f, cp);
    if (f->cache[cp]) {
        return f->cache[cp];
    }
    Glyph *g = calloc(1, sizeof(Glyph));
    int adv, lsb;
    stbtt_GetCodepointHMetrics(f->info, cp, &adv, &lsb);
    g->adv = adv * f->scale;
    unsigned char *bmp = stbtt_GetCodepointBitmap(f->info, f->scale, f->scale, cp, &g->w, &g->h, &g->xoff, &g->yoff);
    if (bmp && g->w > 0 && g->h > 0) {
        Uint32 *px = malloc((size_t)g->w * g->h * 4);
        for (int i = 0; i < g->w * g->h; i++) {
            px[i] = ((Uint32)bmp[i] << 24) | 0xFFFFFF;
        }
        g->tex = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, g->w, g->h);
        SDL_UpdateTexture(g->tex, NULL, px, g->w * 4);
        SDL_SetTextureBlendMode(g->tex, SDL_BLENDMODE_BLEND);
        free(px);
    }
    stbtt_FreeBitmap(bmp, NULL);
    f->cache[cp] = g;
    return g;
}

static float text_width(Face *f, const char *s, int len) {
    const char *p = s, *end = s + len;
    float w = 0;
    int prev = 0;
    while (p < end) {
        int cp = face_cp(f, utf8_next(&p, end));
        if (prev) {
            w += stbtt_GetCodepointKernAdvance(f->info, prev, cp) * f->scale;
        }
        w += face_glyph(f, cp)->adv;
        prev = cp;
    }
    return w;
}

static void draw_text(Face *f, const char *s, int len, float x, float top, Rgb c, Uint8 alpha) {
    const char *p = s, *end = s + len;
    float base = top + f->ascent;
    int prev = 0;
    while (p < end) {
        int cp = face_cp(f, utf8_next(&p, end));
        if (prev) {
            x += stbtt_GetCodepointKernAdvance(f->info, prev, cp) * f->scale;
        }
        Glyph *g = face_glyph(f, cp);
        if (g->tex) {
            SDL_Rect dst = {(int)lroundf(x) + g->xoff, (int)lroundf(base) + g->yoff, g->w, g->h};
            SDL_SetTextureColorMod(g->tex, c.r, c.g, c.b);
            SDL_SetTextureAlphaMod(g->tex, alpha);
            SDL_RenderCopy(R, g->tex, NULL, &dst);
        }
        x += g->adv;
        prev = cp;
    }
}

static void lines_push(Lines *l, const char *s, int len) {
    if (l->n == l->cap) {
        l->cap = l->cap ? l->cap * 2 : 16;
        l->v = realloc(l->v, l->cap * sizeof(Line));
    }
    while (len > 0 && s[len - 1] == ' ') {
        len--;
    }
    l->v[l->n].s = s;
    l->v[l->n].len = len;
    l->n++;
}

/* Greedy word wrap. Paragraphs split on '\n'; overlong words break per character. */
static void wrap(Face *f, const char *text, float maxw, Lines *out) {
    out->n = 0;
    const char *para = text;
    for (;;) {
        const char *pend = strchr(para, '\n');
        if (!pend) {
            pend = para + strlen(para);
        }
        const char *ls = para;
        while (ls < pend && *ls == ' ') {
            ls++;
        }
        if (ls == pend) {
            lines_push(out, ls, 0);
        }
        while (ls < pend) {
            const char *p = ls, *last_fit = NULL;
            while (p < pend) {
                const char *we = p;
                while (we < pend && *we != ' ') {
                    we++;
                }
                if (text_width(f, ls, (int)(we - ls)) <= maxw) {
                    last_fit = we;
                    p = we;
                    while (p < pend && *p == ' ') {
                        p++;
                    }
                } else {
                    break;
                }
            }
            if (!last_fit) {
                const char *q = ls, *fit = NULL;
                while (q < pend) {
                    const char *n = q;
                    utf8_next(&n, pend);
                    if (fit && text_width(f, ls, (int)(n - ls)) > maxw) {
                        break;
                    }
                    fit = n;
                    q = n;
                }
                last_fit = fit;
                p = fit;
            }
            lines_push(out, ls, (int)(last_fit - ls));
            ls = p;
            while (ls < pend && *ls == ' ') {
                ls++;
            }
        }
        if (!*pend) {
            break;
        }
        para = pend + 1;
    }
}

/* ---------- shapes ---------- */

/* Anti-aliased rounded rectangle mask (white, coverage in alpha). feather > 1 gives a soft shadow. */
static SDL_Texture *make_rrect(int w, int h, float rad, float feather) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    Uint32 *px = malloc((size_t)w * h * 4);
    float hx = w * 0.5f, hy = h * 0.5f;
    float in = feather > 1 ? feather : 0;
    if (rad > hx - in) rad = hx - in;
    if (rad > hy - in) rad = hy - in;
    if (rad < 0) rad = 0;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            float qx = fabsf(x + 0.5f - hx) - (hx - in - rad);
            float qy = fabsf(y + 0.5f - hy) - (hy - in - rad);
            float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
            float d = sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - rad;
            float a;
            if (feather > 1) {
                float t = 1.0f - (d + feather) / (2 * feather);
                t = t < 0 ? 0 : t > 1 ? 1 : t;
                a = t * t * (3 - 2 * t);
            } else {
                a = 0.5f - d;
                a = a < 0 ? 0 : a > 1 ? 1 : a;
            }
            px[y * w + x] = ((Uint32)(a * 255 + 0.5f) << 24) | 0xFFFFFF;
        }
    }
    SDL_Texture *t = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    SDL_UpdateTexture(t, NULL, px, w * 4);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    free(px);
    return t;
}

static void blit(SDL_Texture *t, int x, int y, Rgb c, Uint8 alpha) {
    int w, h;
    SDL_QueryTexture(t, NULL, NULL, &w, &h);
    SDL_Rect dst = {x, y, w, h};
    SDL_SetTextureColorMod(t, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(t, alpha);
    SDL_RenderCopy(R, t, NULL, &dst);
}

static void fill(int x, int y, int w, int h, Rgb c, Uint8 alpha) {
    SDL_Rect r = {x, y, w, h};
    SDL_SetRenderDrawBlendMode(R, alpha == 255 ? SDL_BLENDMODE_NONE : SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(R, c.r, c.g, c.b, alpha);
    SDL_RenderFillRect(R, &r);
}

static Uint8 mul8(Uint8 a, float f) {
    return (Uint8)(a * f + 0.5f);
}

/* ---------- layout ---------- */

typedef struct {
    int W, H;
    Face body, small, title, badge, key;
    Lines title_lines, body_lines;
    float B, pad, line_h, title_line_h;
    int card_x, card_y, card_w, card_h;
    int badge_d, header_h, body_y, view_h, content_h, footer_y;
    int max_scroll;
    Rgb accent;
    const char *icon;
    SDL_Texture *t_shadow, *t_border, *t_card, *t_badge, *t_key, *t_glow, *t_fade;
    int shadow_pad, glow_d, thumb_w;
    int has_header, has_footer;
} Ui;

static void ui_layout(Ui *u, const char *title, const char *msg) {
    float m = (float)(u->W < u->H ? u->W : u->H);
    float B = m * 0.047f;
    B = B < 14 ? 14 : B > 40 ? 40 : B;
    u->B = B;
    face_init(&u->body, &font_regular, B);
    face_init(&u->small, &font_regular, B * 0.82f);
    face_init(&u->title, &font_bold, B * 1.32f);
    face_init(&u->badge, &font_bold, B * 1.45f);
    face_init(&u->key, &font_bold, B * 0.62f);

    u->pad = roundf(B * 1.15f);
    u->line_h = roundf(B * 1.45f);
    u->title_line_h = roundf(u->title.px * 1.2f);
    u->badge_d = (int)roundf(B * 2.1f);

    float cw = u->W * 0.9f;
    if (cw > B * 30) cw = B * 30;
    u->card_w = (int)cw;
    float inner = u->card_w - 2 * u->pad;
    float title_w = inner - u->badge_d - B * 0.75f;

    if (u->has_header) {
        wrap(&u->title, title, title_w, &u->title_lines);
    }
    wrap(&u->body, msg, inner, &u->body_lines);

    float th = u->title_lines.n * u->title_line_h;
    u->header_h = u->has_header ? (int)fmaxf(th, (float)u->badge_d) : 0;
    int div_gap = (int)roundf(B * 0.95f);
    int footer_h = (int)roundf(B * 1.5f);
    int head_part = u->has_header ? u->header_h + 2 * div_gap : 0;
    int foot_part = u->has_footer ? div_gap + footer_h : 0;
    u->content_h = (int)(u->body_lines.n * u->line_h - (u->line_h - u->body.line));

    int fixed = (int)(2 * u->pad) + head_part + foot_part;
    int max_h = (int)(u->H * 0.88f);
    u->view_h = u->content_h;
    if (fixed + u->view_h > max_h) {
        u->view_h = max_h - fixed;
        if (u->view_h < u->line_h) u->view_h = (int)u->line_h;
    }
    u->max_scroll = u->content_h - u->view_h;
    if (u->max_scroll < 0) u->max_scroll = 0;

    u->card_h = fixed + u->view_h;
    u->card_x = (u->W - u->card_w) / 2;
    u->card_y = (u->H - u->card_h) / 2;
    u->body_y = (int)u->pad + head_part;
    u->footer_y = u->body_y + u->view_h + div_gap;

    float rad = B * 0.9f;
    int bw = (int)fmaxf(1, roundf(B / 22));
    u->shadow_pad = (int)roundf(B * 1.6f);
    u->t_shadow = make_rrect(u->card_w + 2 * u->shadow_pad, u->card_h + 2 * u->shadow_pad, rad + u->shadow_pad * 0.5f, u->shadow_pad * 0.5f);
    u->t_border = make_rrect(u->card_w, u->card_h, rad, 1);
    u->t_card = make_rrect(u->card_w - 2 * bw, u->card_h - 2 * bw, rad - bw, 1);
    u->t_badge = make_rrect(u->badge_d, u->badge_d, u->badge_d * 0.5f, 1);
    int kd = (int)roundf(B * 1.15f);
    u->t_key = make_rrect(kd, kd, kd * 0.5f, 1);
    u->glow_d = (int)(m * 1.1f);
    u->t_glow = make_rrect(u->glow_d, u->glow_d, u->glow_d * 0.5f, u->glow_d * 0.5f);
    u->thumb_w = (int)fmaxf(3, roundf(B * 0.2f));

    int fh = (int)roundf(B * 1.6f);
    Uint32 *px = malloc(fh * 4);
    for (int i = 0; i < fh; i++) {
        float t = (float)i / (fh - 1);
        px[i] = ((Uint32)(t * t * 255) << 24) | 0xFFFFFF;
    }
    u->t_fade = SDL_CreateTexture(R, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 1, fh);
    SDL_UpdateTexture(u->t_fade, NULL, px, 4);
    SDL_SetTextureBlendMode(u->t_fade, SDL_BLENDMODE_BLEND);
    free(px);
}

static void ui_free(Ui *u) {
    SDL_Texture *ts[] = {u->t_shadow, u->t_border, u->t_card, u->t_badge, u->t_key, u->t_glow, u->t_fade};
    for (size_t i = 0; i < sizeof(ts) / sizeof(ts[0]); i++) {
        if (ts[i]) SDL_DestroyTexture(ts[i]);
    }
    Face *fs[] = {&u->body, &u->small, &u->title, &u->badge, &u->key};
    for (size_t i = 0; i < sizeof(fs) / sizeof(fs[0]); i++) {
        face_free(fs[i]);
    }
    free(u->title_lines.v);
    free(u->body_lines.v);
}

static void ui_draw(Ui *u, float appear, int scroll, int countdown) {
    for (int y = 0; y < u->H; y++) {
        float t = (float)y / u->H;
        SDL_SetRenderDrawColor(R,
            (Uint8)(COL_BG_TOP.r + (COL_BG_BOT.r - COL_BG_TOP.r) * t),
            (Uint8)(COL_BG_TOP.g + (COL_BG_BOT.g - COL_BG_TOP.g) * t),
            (Uint8)(COL_BG_TOP.b + (COL_BG_BOT.b - COL_BG_TOP.b) * t), 255);
        SDL_RenderDrawLine(R, 0, y, u->W - 1, y);
    }

    float e = 1 - (1 - appear) * (1 - appear) * (1 - appear);
    Uint8 A = (Uint8)(255 * e);
    int cx = u->card_x;
    int cy = u->card_y + (int)roundf((1 - e) * u->B * 0.6f);

    blit(u->t_glow, (u->W - u->glow_d) / 2, cy - u->glow_d / 2, u->accent, mul8(A, 0.07f));
    blit(u->t_shadow, cx - u->shadow_pad, cy - u->shadow_pad + (int)(u->B * 0.35f), (Rgb){0, 0, 0}, mul8(A, 0.55f));
    blit(u->t_border, cx, cy, COL_BORDER, A);
    int cw;
    SDL_QueryTexture(u->t_card, NULL, NULL, &cw, NULL);
    int bw = (u->card_w - cw) / 2;
    blit(u->t_card, cx + bw, cy + bw, COL_CARD, A);

    int px = cx + (int)u->pad, py = cy + (int)u->pad;

    int inner = u->card_w - 2 * (int)u->pad;

    /* header: tinted badge + glyph, title next to it, divider with accent lead */
    if (u->has_header) {
    int by = py + (u->header_h - u->badge_d) / 2;
    if (u->title_lines.n > 1) by = py;
    blit(u->t_badge, px, by, u->accent, mul8(A, 0.18f));
    float iw = text_width(&u->badge, u->icon, (int)strlen(u->icon));
    draw_text(&u->badge, u->icon, (int)strlen(u->icon), px + (u->badge_d - iw) / 2,
        by + (u->badge_d - u->badge.ascent) / 2 - u->badge.px * 0.02f, u->accent, A);

    float tx = px + u->badge_d + u->B * 0.75f;
    float tblock = u->title_lines.n * u->title_line_h;
    float ty = py + (u->header_h - tblock) / 2 + (u->title_line_h - u->title.line) / 2;
    for (int i = 0; i < u->title_lines.n; i++) {
        draw_text(&u->title, u->title_lines.v[i].s, u->title_lines.v[i].len, tx, ty + i * u->title_line_h, COL_TITLE, A);
    }

    int dy = py + u->header_h + (int)roundf(u->B * 0.95f);
    int dh = (int)fmaxf(1, roundf(u->B / 22));
    fill(px, dy, inner, dh, COL_BORDER, A);
    fill(px, dy, (int)(u->badge_d * 1.6f), (int)fmaxf(2, dh * 2), u->accent, A);
    }

    /* body */
    int vy = cy + u->body_y;
    SDL_Rect clip = {px, vy, inner + (int)u->pad / 2, u->view_h};
    SDL_RenderSetClipRect(R, &clip);
    int first = (int)(scroll / u->line_h);
    for (int i = first; i < u->body_lines.n; i++) {
        float ly = vy + i * u->line_h - scroll;
        if (ly > vy + u->view_h) break;
        draw_text(&u->body, u->body_lines.v[i].s, u->body_lines.v[i].len, px, ly, COL_BODY, A);
    }
    SDL_RenderSetClipRect(R, NULL);

    if (u->max_scroll > 0) {
        int fw, fh;
        SDL_QueryTexture(u->t_fade, NULL, NULL, &fw, &fh);
        SDL_SetTextureColorMod(u->t_fade, COL_CARD.r, COL_CARD.g, COL_CARD.b);
        SDL_SetTextureAlphaMod(u->t_fade, A);
        if (scroll < u->max_scroll) {
            SDL_Rect d = {px, vy + u->view_h - fh, inner, fh};
            SDL_RenderCopy(R, u->t_fade, NULL, &d);
        }
        if (scroll > 0) {
            SDL_Rect d = {px, vy, inner, fh};
            SDL_RenderCopyEx(R, u->t_fade, NULL, &d, 0, NULL, SDL_FLIP_VERTICAL);
        }
        int track_x = cx + u->card_w - (int)(u->pad * 0.5f) - u->thumb_w / 2;
        int th = (int)((float)u->view_h * u->view_h / u->content_h);
        if (th < u->B) th = (int)u->B;
        int ty2 = vy + (int)((float)(u->view_h - th) * scroll / u->max_scroll);
        fill(track_x, vy, u->thumb_w, u->view_h, COL_BORDER, mul8(A, 0.6f));
        fill(track_x, ty2, u->thumb_w, th, COL_MUTED, A);
    }

    /* footer: hints left, A key + Close right */
    if (!u->has_footer) {
        return;
    }
    int fy = cy + u->footer_y;
    int fline = (int)roundf(u->B * 1.5f);
    int kd;
    SDL_QueryTexture(u->t_key, NULL, NULL, &kd, NULL);
    const char *close = "Close";
    float cwid = text_width(&u->small, close, (int)strlen(close));
    float right = px + inner;
    float ctx = right - cwid;
    float stop = fy + (fline - u->small.line) / 2;
    draw_text(&u->small, close, (int)strlen(close), ctx, stop, COL_MUTED, A);
    int kx = (int)(ctx - u->B * 0.45f - kd);
    int ky = fy + (fline - kd) / 2;
    blit(u->t_key, kx, ky, COL_KEY, A);
    float aw = text_width(&u->key, "A", 1);
    draw_text(&u->key, "A", 1, kx + (kd - aw) / 2, ky + (kd - u->key.ascent) / 2 - u->key.px * 0.03f, COL_TITLE, A);

    char hint[96];
    hint[0] = 0;
    if (u->max_scroll > 0) {
        strcat(hint, "\xE2\x86\x91\xE2\x86\x93 Scroll");
    }
    if (countdown > 0) {
        char buf[48];
        snprintf(buf, sizeof(buf), "%sCloses in %ds", hint[0] ? "   \xE2\x80\xA2   " : "", countdown);
        strcat(hint, buf);
    }
    if (hint[0]) {
        draw_text(&u->small, hint, (int)strlen(hint), px, stop, COL_MUTED, mul8(A, 0.85f));
    }
}

/* ---------- main ---------- */

static char *unescape(const char *s) {
    char *o = malloc(strlen(s) + 1), *d = o;
    while (*s) {
        if (s[0] == '\\' && s[1] == 'n') {
            *d++ = '\n';
            s += 2;
        } else if (s[0] == '\\' && s[1] == 't') {
            *d++ = ' ';
            s += 2;
        } else {
            *d++ = *s++;
        }
    }
    *d = 0;
    return o;
}

static void usage(const char *argv0) {
    fprintf(stderr,
        "usage: %s [--type error|warning|info] [--title TITLE] [--timeout SECONDS] [--wait] [--text] MESSAGE\n"
        "  --title=TITLE and --text=MESSAGE also work. Without a title the card shows only the text.\n"
        "  MESSAGE may contain \\n for line breaks (blank line = new paragraph).\n"
        "  Closes on A/B/Start/Select (Enter/Esc/Space). Exit status 0, or 2 if no display.\n"
        "  --wait: no Close button, cannot be dismissed, no timeout; stays until killed.\n",
        argv0);
}

int main(int argc, char **argv) {
    Uint64 t_launch = SDL_GetPerformanceCounter();
    const char *type = "error", *title = NULL, *msg = NULL, *snapshot = NULL;
    int timeout = 0, wait = 0, snap_w = 640, snap_h = 480, snap_scroll = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--type") && i + 1 < argc) {
            type = argv[++i];
        } else if (!strcmp(argv[i], "--title") && i + 1 < argc) {
            title = argv[++i];
        } else if (!strncmp(argv[i], "--title=", 8)) {
            title = argv[i] + 8;
        } else if (!strcmp(argv[i], "--text") && i + 1 < argc) {
            msg = argv[++i];
        } else if (!strncmp(argv[i], "--text=", 7)) {
            msg = argv[i] + 7;
        } else if (!strcmp(argv[i], "--wait")) {
            wait = 1;
        } else if (!strcmp(argv[i], "--timeout") && i + 1 < argc) {
            timeout = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "--snapshot") && i + 1 < argc) {
            snapshot = argv[++i];
        } else if (!strcmp(argv[i], "--size") && i + 1 < argc) {
            sscanf(argv[++i], "%dx%d", &snap_w, &snap_h);
        } else if (!strcmp(argv[i], "--snapshot-scroll") && i + 1 < argc) {
            snap_scroll = atoi(argv[++i]);
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        } else if (!msg) {
            msg = argv[i];
        } else {
            usage(argv[0]);
            return 2;
        }
    }
    if (!msg) {
        usage(argv[0]);
        return 2;
    }

    Ui u;
    memset(&u, 0, sizeof(u));
    if (!strcmp(type, "warning")) {
        u.accent = (Rgb){0xF5, 0xB0, 0x41};
        u.icon = "!";
    } else if (!strcmp(type, "info")) {
        u.accent = (Rgb){0x5B, 0x9C, 0xF8};
        u.icon = "i";
    } else {
        u.accent = (Rgb){0xF2, 0x5C, 0x5C};
        u.icon = "!";
    }
    u.has_header = title && *title;
    u.has_footer = !wait;
    char *text = unescape(msg);
    char *ttext = unescape(u.has_header ? title : "");
    if (u.has_header) {
        fprintf(stderr, "%s: %s\n", ttext, text);
    } else {
        fprintf(stderr, "%s\n", text);
    }

    stbtt_InitFont(&font_regular, portmsg_regular_ttf, 0);
    stbtt_InitFont(&font_bold, portmsg_bold_ttf, 0);

    if (snapshot) {
        SDL_Surface *surf = SDL_CreateRGBSurfaceWithFormat(0, snap_w, snap_h, 32, SDL_PIXELFORMAT_ARGB8888);
        R = SDL_CreateSoftwareRenderer(surf);
        if (!R) {
            fprintf(stderr, "snapshot renderer failed: %s\n", SDL_GetError());
            return 2;
        }
        u.W = snap_w;
        u.H = snap_h;
        ui_layout(&u, ttext, text);
        int sc = snap_scroll < 0 ? u.max_scroll : (snap_scroll > u.max_scroll ? u.max_scroll : snap_scroll);
        ui_draw(&u, 1.0f, sc, timeout);
        SDL_RenderPresent(R);
        SDL_SaveBMP(surf, snapshot);
        ui_free(&u);
        SDL_DestroyRenderer(R);
        SDL_FreeSurface(surf);
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 2;
    }
    SDL_DisplayMode dm;
    if (SDL_GetCurrentDisplayMode(0, &dm) != 0) {
        dm.w = 640;
        dm.h = 480;
    }
    SDL_Window *win = SDL_CreateWindow("portmsg", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
        dm.w, dm.h, SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_SHOWN);
    if (!win) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 2;
    }
    R = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!R) {
        fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(win);
        SDL_Quit();
        return 2;
    }
    SDL_ShowCursor(SDL_DISABLE);
    if (getenv("PORTMSG_DEBUG")) {
        fprintf(stderr, "portmsg: SDL init + window after %.0f ms\n",
            (SDL_GetPerformanceCounter() - t_launch) * 1000.0 / SDL_GetPerformanceFrequency());
    }
    SDL_GetRendererOutputSize(R, &u.W, &u.H);
    ui_layout(&u, ttext, text);

    SDL_GameController *pad = NULL;
    for (int i = 0; i < SDL_NumJoysticks() && !pad; i++) {
        if (SDL_IsGameController(i)) pad = SDL_GameControllerOpen(i);
    }

    Uint32 start = SDL_GetTicks();
    Uint32 last = start;
    float scroll = 0, axis = 0;
    int running = 1, dirty = 1, last_cd = -1, armed = 0, frames = 0;
    Uint32 last_present = 0;
    if (getenv("PORTMSG_DEBUG")) {
        fprintf(stderr, "portmsg: ready after %.0f ms\n",
            (SDL_GetPerformanceCounter() - t_launch) * 1000.0 / SDL_GetPerformanceFrequency());
    }

    while (running) {
        SDL_Event ev;
        int got = SDL_WaitEventTimeout(&ev, 16);
        Uint32 now = SDL_GetTicks();
        int guard = now - start < INPUT_GUARD_MS;
        while (got) {
            switch (ev.type) {
            case SDL_QUIT:
                running = 0;
                break;
            case SDL_CONTROLLERDEVICEADDED:
                if (!pad && SDL_IsGameController(ev.cdevice.which)) {
                    pad = SDL_GameControllerOpen(ev.cdevice.which);
                    dirty = 1;
                }
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                switch (ev.cbutton.button) {
                case SDL_CONTROLLER_BUTTON_A:
                case SDL_CONTROLLER_BUTTON_B:
                case SDL_CONTROLLER_BUTTON_START:
                case SDL_CONTROLLER_BUTTON_BACK:
                    if (!guard && !wait) armed = 1;
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_UP:
                    scroll -= u.line_h;
                    break;
                case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                    scroll += u.line_h;
                    break;
                case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
                    scroll -= u.view_h - u.line_h;
                    break;
                case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
                    scroll += u.view_h - u.line_h;
                    break;
                }
                dirty = 1;
                break;
            case SDL_CONTROLLERBUTTONUP:
                if (armed && (ev.cbutton.button == SDL_CONTROLLER_BUTTON_A || ev.cbutton.button == SDL_CONTROLLER_BUTTON_B ||
                        ev.cbutton.button == SDL_CONTROLLER_BUTTON_START || ev.cbutton.button == SDL_CONTROLLER_BUTTON_BACK)) {
                    running = 0;
                }
                break;
            case SDL_CONTROLLERAXISMOTION:
                if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTY || ev.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY) {
                    axis = abs(ev.caxis.value) > AXIS_DEADZONE ? ev.caxis.value / 32767.0f : 0;
                }
                break;
            case SDL_KEYDOWN:
                switch (ev.key.keysym.sym) {
                case SDLK_RETURN:
                case SDLK_ESCAPE:
                case SDLK_SPACE:
                    if (!guard && !wait && !ev.key.repeat) armed = 1;
                    break;
                case SDLK_UP:
                    scroll -= u.line_h;
                    break;
                case SDLK_DOWN:
                    scroll += u.line_h;
                    break;
                case SDLK_PAGEUP:
                    scroll -= u.view_h - u.line_h;
                    break;
                case SDLK_PAGEDOWN:
                    scroll += u.view_h - u.line_h;
                    break;
                }
                dirty = 1;
                break;
            case SDL_KEYUP:
                if (armed && (ev.key.keysym.sym == SDLK_RETURN || ev.key.keysym.sym == SDLK_ESCAPE || ev.key.keysym.sym == SDLK_SPACE)) {
                    running = 0;
                }
                break;
            case SDL_WINDOWEVENT:
                dirty = 1;
                break;
            }
            got = SDL_PollEvent(&ev);
        }

        float dt = (now - last) / 1000.0f;
        last = now;
        if (axis != 0) {
            scroll += axis * u.B * 22 * dt;
            dirty = 1;
        }
        if (scroll < 0) scroll = 0;
        if (scroll > u.max_scroll) scroll = (float)u.max_scroll;

        int limit = wait ? 0 : timeout > 0 ? timeout : (pad ? 0 : NO_PAD_TIMEOUT);
        int cd = 0;
        if (limit > 0) {
            int left = limit - (int)((now - start) / 1000);
            if (left <= 0) running = 0;
            cd = left;
        }
        if (cd != last_cd) {
            last_cd = cd;
            dirty = 1;
        }

        float appear = (now - start) / 220.0f;
        if (appear < 1) dirty = 1;
        if (appear > 1) appear = 1;

        if (now - last_present >= REPRESENT_MS) {
            dirty = 1;
        }
        if (dirty && running) {
            last_present = now;
            ui_draw(&u, appear, (int)scroll, cd);
            SDL_RenderPresent(R);
            dirty = 0;
            if (frames++ == 0 && getenv("PORTMSG_DEBUG")) {
                fprintf(stderr, "portmsg: first frame after %.0f ms\n",
                    (SDL_GetPerformanceCounter() - t_launch) * 1000.0 / SDL_GetPerformanceFrequency());
            }
        }
    }

    if (pad) SDL_GameControllerClose(pad);
    ui_free(&u);
    SDL_DestroyRenderer(R);
    SDL_DestroyWindow(win);
    SDL_Quit();
    free(text);
    free(ttext);
    return 0;
}
