// Vendored from rcc and libu8ident (same author).
// SPDX-License-Identifier: LGPL-2.1-or-later
/*
  Minor parts derived from slimcc by fuhsnn.
  Optimized by Reini Urban 2026
  Integrated libu8ident TR39 for secure identifier checking.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <inttypes.h>
#include <assert.h>
#include <errno.h>
#if defined(__SSE2__)
#include <emmintrin.h>
#elif defined(__ARM_NEON)
#include <arm_neon.h>
#endif
#include "unicode.h"

// ===== Global state =====
static struct ctx_t ctx[U8ID_CTX_TRESH] = {0}; // pre-allocate 5 contexts
static u8id_ctx_t i_ctx = 0;
static struct ctx_t *ctxp = NULL; // if more than 5 contexts

// ===== Context management =====



/* Changes to the context previously generated with `u8ident_new_ctx`. */
static struct ctx_t *u8ident_ctx(void) {
    return (i_ctx < U8ID_CTX_TRESH) ? &ctx[i_ctx] : &ctxp[i_ctx];
}

// search in linear vector of scripts per ctx
static bool u8ident_has_script_ctx(const uint8_t scr, const struct ctx_t *c) {
    if (!c->count)
        return false;
    const uint8_t *u8p = (c->count > 8) ? c->u8p : c->scr8;
    for (int i = 0; i < c->count; i++) {
        if (scr == u8p[i])
            return true;
    }
    return false;
}

static int u8ident_add_script_ctx(const uint8_t scr, struct ctx_t *c) {
    if (scr < 2 || scr >= FIRST_LIMITED_USE_SCRIPT)
        return -1;
    int i = c->count;
    if (unlikely(i == 8)) {
        uint8_t *p = malloc(16);
        memcpy(p, c->scr8, 8);
        c->u8p = p;
        c->u8p[i] = scr;
    } else if (unlikely(i > 8 && (i & 7) == 7)) {
        c->u8p = realloc(c->u8p, i + 8);
        c->u8p[i] = scr;
    } else {
        if (i > 8) {
            if (!c->u8p) {
                c->u8p = calloc(16, 1);
                memcpy(c->u8p, c->scr8, 8);
            }
            c->u8p[i] = scr;
        } else {
            c->scr8[i] = scr;
        }
    }
    if (scr == SC_Han)
        c->has_han = 1;
    else if (scr == SC_Bopomofo)
        c->is_chinese = 1;
    else if (scr == SC_Katakana || scr == SC_Hiragana)
        c->is_japanese = 1;
    else if (scr == SC_Hangul)
        c->is_korean = 1;
    else if (scr == SC_Hebrew || scr == SC_Arabic)
        c->is_rtl = 1;
    c->count++;
    return 0;
}

// ===== Search utilities =====

static inline bool linear_search(const uint32_t cp,
                                 const struct range_bool *sc_list,
                                 const int len) {
    struct range_bool *s = (struct range_bool *)sc_list;
    for (int i = 0; i < len; i++) {
        assert(s->from <= s->to);
        if ((cp - s->from) <= (s->to - s->from))
            return true;
        if (cp <= s->to) // s is sorted. not found
            return false;
        s++;
    }
    return false;
}

static inline void *binary_search(const uint32_t cp, const char *list,
                                  const size_t len, const size_t size) {
    int n = (int)len;
    const char *p = list;
    while (n > 0) {
        struct range_bool *pos = (struct range_bool *)(p + size * (n / 2));
        // hack: with unsigned wrapping max-cp is always higher, so false
        if ((cp - pos->from) <= (pos->to - pos->from))
            return pos;
        else if (cp < pos->from)
            n /= 2;
        else {
            p = (char *)pos + size;
            n -= (n / 2) + 1;
        }
    }
    return NULL;
}

static inline bool range_bool_search(const uint32_t cp,
                                     const struct range_bool *list,
                                     const size_t len) {
    return binary_search(cp, (char *)list, len, sizeof(*list)) ? true : false;
}


// ===== Script and search functions =====
/* Search for list of script indices */
// codeql[cpp/commented-out-code]: deliberately-disabled, kept for reference; its replacement is covered by a separate alert
// REMOVE
//static const struct scx *u8ident_get_scx(const uint32_t cp) {
//    return (const struct scx *)binary_search(
//        cp, (char *)scx_list, ARRAY_SIZE(scx_list), sizeof(*scx_list));
//}

/* Search for TR39 XID entry, in start or cont lists */
static const struct sc_tr39 *u8ident_get_tr39(const uint32_t cp) {
    const struct sc_tr39 *sc = (const struct sc_tr39 *)binary_search(
        cp, (char *)tr39_start_list, ARRAY_SIZE(tr39_start_list),
        sizeof(*tr39_start_list));
    if (sc)
        return sc;
    else
        return (const struct sc_tr39 *)binary_search(cp, (char *)tr39_cont_list,
                                                     ARRAY_SIZE(tr39_cont_list),
                                                     sizeof(*tr39_cont_list));
}

static uint8_t u8ident_get_script(const uint32_t cp) {
    // Search TR39 lists. Characters here already passed XID checks,
    // so they must be in one of these lists.
    const struct sc_tr39 *sc = u8ident_get_tr39(cp);
    if (sc)
        return sc->sc;
    return SC_Unknown;
}


static bool u8ident_is_tr39_MEDIAL(uint32_t cp) {
    return range_bool_search(cp, tr39_medial_list, ARRAY_SIZE(tr39_medial_list));
}
static bool u8ident_is_bidi(const uint32_t cp) {
    return linear_search(cp, bidi_list, ARRAY_SIZE(bidi_list));
}
static const struct sc_tr39 *isTR39_start(const uint32_t cp) {
    return (const struct sc_tr39 *)binary_search(
        cp, (char *)tr39_start_list, ARRAY_SIZE(tr39_start_list),
        sizeof(*tr39_start_list));
}
static const struct sc_tr39 *isTR39_cont(const uint32_t cp) {
    return (const struct sc_tr39 *)binary_search(
        cp, (char *)tr39_cont_list, ARRAY_SIZE(tr39_cont_list),
        sizeof(*tr39_cont_list));
}

static inline int compar32(const void *a, const void *b) {
    const uint32_t ai = *(const uint32_t *)a;
    const uint32_t bi = *(const uint32_t *)b;
    return ai < bi ? -1 : ai == bi ? 0
                                   : 1;
}

static bool u8ident_is_greek_latin_confus(const uint32_t cp) {
    return bsearch(&cp, greek_confus_list, ARRAY_SIZE(greek_confus_list), 4,
                   compar32) != NULL
        ? true
        : false;
}

static const char *u8ident_script_name(const int scr) {
    if (scr < 0 || scr > LAST_SCRIPT)
        return NULL;
    assert(scr >= 0 && scr <= LAST_SCRIPT);
    return all_scripts[scr];
}

static uint32_t u8ident_failed_char(const u8id_ctx_t i) {
    if (i <= i_ctx) {
        const struct ctx_t *c = (i_ctx < U8ID_CTX_TRESH) ? &ctx[i] : &ctxp[i];
        return c->last_cp;
    } else {
        return 0;
    }
}
/* returns the constant script name, which failed in the last check. */
static const char *u8ident_failed_script_name(const u8id_ctx_t i) {
    if (i <= i_ctx) {
        const struct ctx_t *c = (i_ctx < U8ID_CTX_TRESH) ? &ctx[i] : &ctxp[i];
        const uint32_t cp = c->last_cp;
        if (cp > 0)
            return u8ident_script_name(u8ident_get_script(cp));
    }
    return NULL;
}

static int u8ident_free_ctx(u8id_ctx_t i) {
    if (i_ctx < U8ID_CTX_TRESH)
        ctxp = &ctx[0];
    if (i <= i_ctx) {
        if (ctxp[i].count > 8)
            free(ctxp[i].u8p);
        memset(&ctxp[i], 0, sizeof(u8id_ctx_t));
        if (i > 0)
            i_ctx = i - 1; // switch to the previous context
        else
            i_ctx = 0; // deleting 0 will lead to a reset
        return 0;
    } else
        return -1;
}

/* End this library, cleaning up all internal structures. */
static void u8ident_free(void) {
    for (u8id_ctx_t i = 0; i <= i_ctx; i++) {
        u8ident_free_ctx(i);
    }
    if (i_ctx >= U8ID_CTX_TRESH) {
        free(ctxp);
    }
}


// ===== Core identifier checking API =====

static int u8ident_init(void) {
    u8ident_free(); // clear and reset the ctx
    return 0;
}

static bool in_SCX(const enum u8id_sc scr, const char *scx) {
    unsigned char *x = (unsigned char *)scx;
    while (*x) {
        if (*x == (unsigned char)scr)
            return true;
        x++;
    }
    return false;
}

static bool nsm_check(const uint32_t base_cp, const uint32_t cp) {
    if (cp == 0x307 && (base_cp == 'i' || base_cp == 0x131 // dotless i
                        || base_cp == 0x237 // dotless j
                        || base_cp == 0x25F // dotless j with stroke
                        || base_cp == 0x284 // dotless j with stroke and hook
                        || base_cp == 0x1DA1 // dotless j with stroke
                        || base_cp == 0x10798 // dotless i
                        || base_cp == 0x1D6A4 // dotless j
                        || base_cp == 0x1D645)) // dotless j
        return false;
    // Todo: check the 10 different STROKE Mn's: SHORT BAR OVERLAY, LONG BAR
    // OVERLAY, LIGHT CENTRALIZATION STROKE BELOW, STRONG CENTRALIZATION STROKE
    // BELOW, ...

    for (unsigned i = 0; i < ARRAY_SIZE(nsm_letters); i++) {
        const struct nsm_ws *l = &nsm_letters[i];
        if (l->nsm > cp)
            break;
        if (l->nsm != cp)
            continue;
        if (wcschr(l->letters, (wchar_t)base_cp))
            return false;
    }
    return true;
}

static enum u8id_errors u8ident_check_buf(const char *buf, const int bufsz) {
    int ret = U8ID_EOK;
    char *s = (char *)buf;
    const char *e = (char *)&buf[bufsz];
    // codeql[cpp/local-variable-hides-global-variable]: local ptr from accessor u8ident_ctx(), not the global ctx[] pool
    struct ctx_t *ctx = u8ident_ctx();
    enum u8id_sc scr;
    enum u8id_sc basesc = SC_Unknown;
    char *scx = NULL;
    uint32_t prev_cp = 0, base_cp = 0;
    int seq_mn = 0;
    uint32_t cp = dec_utf8(&s);

    // hardcoded TR31 funcs via static functions (inlinable)
    const struct sc_tr39 *tr39 = isTR39_start(cp);
    if (unlikely(!tr39)) {
        ctx->last_cp = cp;
        return U8ID_ERR_XID;
    }
    bool has_latin = u8ident_has_script_ctx(SC_Latin, ctx);

    do {
        scr = tr39->sc;
        // disallow Limited Use scripts
        if (unlikely(scr >= FIRST_LIMITED_USE_SCRIPT)) {
            ctx->last_cp = cp;
            return U8ID_ERR_SCRIPT;
        }
        // disallow bidi formatting
        if (unlikely(!ctx->is_rtl && u8ident_is_bidi(cp))) {
            ctx->last_cp = cp;
            return U8ID_ERR_SCRIPT;
        }

        bool is_new = false;
        // check scx on Common or Inherited.
        // TODO Keep list of possible scripts and reduce them.
        if (scr == SC_Common || scr == SC_Inherited) {
            // Almost everybody may mix with latin. This search is mostly false
            const struct sc_tr39 *this_scx = u8ident_get_tr39(cp);
            if (unlikely(this_scx && this_scx->scx)) {
                scx = (char *)this_scx->scx;
                const enum u8id_gc gc = (const enum u8id_gc)this_scx->gc;
                int n = 0;
                if (ctx->count) {
                    // Special-case for runs: only after japanese.
                    // This is the only context dependent Lm case.
                    // All others are Combining Marks.
                    if (!ctx->is_japanese &&
                        ((cp >= 0x30FC && cp <= 0x30FE) || cp == 0xFF70)) {
                        ctx->last_cp = cp;
                        return U8ID_ERR_SCRIPTS;
                    }
                    if (!has_latin) { // 6 cases for Hira Kana
                        if (strEQc(scx, "\x11\x12") && !ctx->is_japanese) {
                            ctx->last_cp = cp;
                            return U8ID_ERR_SCRIPTS;
                        }
                        // any cfk, also 6 cases for Bopo Hang Hani Hira Kana
                        if (strEQc(scx, "\x06\x0e\x0f\x11\x12") && !ctx->is_japanese &&
                            !ctx->has_han && !ctx->is_korean) {
                            ctx->last_cp = cp;
                            return U8ID_ERR_SCRIPTS;
                        }
                    }
                }
                // We have 2 Mc cases, and 30 Mn in SCX. No Me. More of them are in SC
                // though.
                if (gc == GC_Mn || gc == GC_Mc) {
                    if (!ctx->count || basesc == SC_Unknown) {
                        // Disallow combiners without any base char (which does have a
                        // script) This catches only a mark as very first char. We check the
                        // base char for runs at ok:
                        ctx->last_cp = cp;
                        return U8ID_ERR_COMBINE;
                    } else if (!in_SCX(basesc, this_scx->scx)) {
                        // Check combiners against basesc
                        ctx->last_cp = cp;
                        return U8ID_ERR_COMBINE;
                    } else if (cp == prev_cp) {
                        // TR39#5.4 "Forbid sequences of the same nonspacing mark"
                        ctx->last_cp = cp;
                        return U8ID_ERR_COMBINE;
                    } else if (gc == GC_Mn && ++seq_mn > 4) {
                        // TR39#5.4 "Forbid sequences of more than 4 nonspacing marks (gc=Mn
                        // or gc=Me)"
                        ctx->last_cp = cp;
                        return U8ID_ERR_COMBINE;
                    } else if (!nsm_check(base_cp, cp)) {
                        // TR39#5.5 "Forbid sequences of base character + nonspacing mark
                        // that look the same as or confusingly similar to the base
                        // character alone"
                        ctx->last_cp = cp;
                        return U8ID_ERR_COMBINE;
                    }
                } else { // not Mn|Mc
                    seq_mn = 0;
                }
                char *x = scx;
                while (*x) {
                    bool has = u8ident_has_script_ctx(*x, ctx);
                    n += has ? 1 : 0;
                    x++;
                }
                /* We have SCX and none of the SCX occured yet.
           So we have a new one.
           We dont know which yet, but we can set is_new. */
                if (!n) {
                    is_new = true;
                    // scx = (char *)this_scx->scx; // for errors
                }
            }
        } else {
            base_cp = cp;
        }

        // ignore Latin. This is compatible with everything
        if (likely(scr == SC_Latin)) {
            if (!u8ident_has_script_ctx(scr, ctx)) {
                has_latin = true;
                u8ident_add_script_ctx(scr, ctx);
            }
            basesc = scr;
            goto next;
        }

        // if not already have it, add it. EXCLUDED_SCRIPT must already exist
        if (!is_new && !(scr == SC_Common || scr == SC_Inherited))
            is_new = !u8ident_has_script_ctx(scr, ctx);
        if (is_new) {
            // if Limited Use it must have been already manually added
            if (unlikely(scr >= FIRST_LIMITED_USE_SCRIPT)) {
                ctx->last_cp = cp;
                return U8ID_ERR_SCRIPT;
            }
            // allowed is only one, unless it is an allowed combination
            if (ctx->count) {
                // check allowed CJK combinations
                if (scr == SC_Bopomofo) {
                    if (unlikely(!ctx->has_han && !has_latin)) {
                        ctx->last_cp = cp;
                        return U8ID_ERR_SCRIPTS;
                    } else
                        goto ok;
                } else if (scr == SC_Han) {
                    if (unlikely(!(ctx->is_chinese || ctx->is_japanese ||
                                   ctx->is_korean || has_latin))) {
                        ctx->last_cp = cp;
                        return U8ID_ERR_SCRIPTS;
                    } else
                        goto ok;
                } else if (scr == SC_Katakana || scr == SC_Hiragana) {
                    if (unlikely(!(ctx->is_japanese || ctx->has_han || has_latin))) {
                        ctx->last_cp = cp;
                        return U8ID_ERR_SCRIPTS;
                    } else
                        goto ok;
                } else if (scr == SC_Common || scr == SC_Inherited) {
                    // may we now collapse it?
                    goto ok;
                }
                // and disallow all other combinations
                else {
                    if (ctx->count >= 2 || scr == SC_Cyrillic) { // not more than 2
                        ctx->last_cp = cp;
                        return U8ID_ERR_SCRIPTS;
                    }
                    // some Greek may mix with Latin
                    if (scr == SC_Greek && has_latin) {
                        //assert(s_u8id_profile == U8ID_PROFILE_TR39_4);
                        // only not confusables
                        if (u8ident_is_greek_latin_confus(cp)) {
                            ctx->last_cp = cp;
                            return U8ID_ERR_CONFUS;
                        }
                        goto ok;
                    }
                }
            }
        ok:
            basesc = scr;
            if (!u8ident_has_script_ctx(scr, ctx))
                u8ident_add_script_ctx(scr, ctx);
            // not is new, but still a possible greek confusable
        } else if (scr == SC_Greek &&
                   has_latin && u8ident_is_greek_latin_confus(cp)) {
            ctx->last_cp = cp;
            return U8ID_ERR_CONFUS;
        } else if (scr != SC_Common && scr != SC_Inherited) {
            basesc = scr;
            base_cp = cp;
        } else {
            // Check illegal runs.
            // is_MARK(cp) is too slow, and we need the full GC for all cases
            const enum u8id_gc gc = tr39->gc;
            if (gc == GC_Mn || gc == GC_Me) {
                if (cp == prev_cp) {
                    // TR39#5.4 "Forbid sequences of the same nonspacing mark"
                    ctx->last_cp = cp;
                    return U8ID_ERR_COMBINE;
                } else if (++seq_mn > 4) {
                    // TR39#5.4 "Forbid sequences of more than 4 nonspacing marks (gc=Mn
                    // or gc=Me)"
                    ctx->last_cp = cp;
                    return U8ID_ERR_COMBINE;
                } else if (!nsm_check(base_cp, cp)) {
                    // TR39#5.5 "Forbid sequences of base character + nonspacing mark
                    // that look the same as or confusingly similar to the base
                    // character alone"
                    ctx->last_cp = cp;
                    return U8ID_ERR_COMBINE;
                }
            }
            // Allow Sm as first
            if (basesc == SC_Unknown &&
                (gc == GC_Mn || gc == GC_Me || gc == GC_Mc)) {
                // Disallow combiners without any base char (which do have a script)
                ctx->last_cp = cp;
                return U8ID_ERR_COMBINE;
            }
        }

    next:
        prev_cp = cp;
        cp = dec_utf8(&s);
        if (likely(s <= e && cp != 0)) {
            tr39 = isTR39_cont(cp);
            if (unlikely(!tr39))
                tr39 = isTR39_start(cp);
            if (unlikely(!tr39)) {
                ctx->last_cp = cp;
                return U8ID_ERR_XID;
            }
            if (s == e && u8ident_is_tr39_MEDIAL(cp)) {
                ctx->last_cp = cp;
                return U8ID_ERR_XID;
            }
        }
    } while (s <= e);

    return ret;
}

// ===== UTF-8 normalization =====


#define _UNICODE_MAX 0x10ffff

// UTF-8 helpers

/* from https://rosettacode.org/wiki/UTF-8_encode_and_decode#C
   taken from the safeclib
 */
typedef struct {
    uint8_t mask; /* char data will be bitwise AND with this */
    uint8_t lead; /* start bytes of current char in utf-8 encoded character */
    uint32_t beg; /* beginning of codepoint range */
    uint32_t end; /* end of codepoint range */
    int bits_stored; /* number of bits from the codepoint that fits in char */
} _utf_t;

static const _utf_t *utf[] = {
    // clang-format off
  /*             mask                 lead                beg      end    bits */
  [0] = &(_utf_t){0x3f/*0b00111111*/, 0x80/*0b10000000*/, 0,       0,        6},
  [1] = &(_utf_t){0x7f/*0b01111111*/, 0x00/*0b00000000*/, 0000,    0177,     7},
  [2] = &(_utf_t){0x1f/*0b00011111*/, 0xc0/*0b11000000*/, 0200,    03777,    5},
  [3] = &(_utf_t){0x0f/*0b00001111*/, 0xe0/*0b11100000*/, 04000,   0177777,  4},
  [4] = &(_utf_t){0x07/*0b00000111*/, 0xf0/*0b11110000*/, 0200000, 04177777, 3},
  &(_utf_t){0},
    // clang-format on
};

static int utf8_byte_len(const unsigned char ch) {
    int len = 0;
    for (_utf_t **u = (_utf_t **)utf; *u; ++u) {
        if ((ch & ~(*u)->mask) == (*u)->lead) {
            break;
        }
        ++len;
    }
#if 0 /* error handled in caller */
    if (len > 4) { /* Malformed leading byte */
        // "illegal UTF-8 character" EILSEQ
    }
#endif
    return len;
}


/* convert utf8 to unicode codepoint (to_cp) */
static uint32_t dec_utf8(char **strp) {
    const unsigned char *str = (const unsigned char *)*strp;
    int bytes = utf8_byte_len(*str);
    int shift;
    uint32_t cp;

    if (bytes > 4) {
        errno = EILSEQ;
        return 0;
    }
    shift = utf[0]->bits_stored * (bytes - 1);
    assert(shift >= 0);
    cp = (*str++ & utf[bytes]->mask) << shift;
    for (int i = 1; i < bytes; ++i, ++str) {
        shift -= utf[0]->bits_stored;
        assert(shift >= 0);
        cp |= (*str & utf[0]->mask) << shift;
    }
    *strp = (char *)str;
    return cp;
}

/* convert unicode codepoint to utf8 (to_utf8) */

/* size of array for combining characters */
/* enough as an initial value? */
#define CC_SEQ_SIZE 10
#define CC_SEQ_STEP 5

#define ERR_ILSEQ -3
#define ERR_NOSPACE -2
#define ERR_INVAL -1
#define EOK 0


// ===== Compiler-facing Unicode API =====

#define MASK(bits) (uint8_t)((1 << (bits)) - 1)
#define MASK2(bits, shift) (uint8_t)(MASK(bits) << (shift))

typedef struct {
    uint32_t first;
    uint32_t last;
} UTF32Range;

// clang-format off
// Math symbols allowed in C identifiers (universal character names)
// These augment the TR39 script-based ranges.

// clang-format on



// Read a UTF-8-encoded Unicode code point from a source file.
// We assume that source files are always in UTF-8.
static uint32_t decode_utf8(char **new_pos, char *p) {
    if ((unsigned char)*p < 128) {
        *new_pos = p + 1;
        return *p;
    }

    int len = 0;
    uint32_t c = 0;

    if ((unsigned char)*p >= MASK2(4, 4)) {
        len = 4;
        c = *p & MASK(3);
    } else if ((unsigned char)*p >= MASK2(3, 5)) {
        len = 3;
        c = *p & MASK(4);
    } else if ((unsigned char)*p >= MASK2(2, 6)) {
        len = 2;
        c = *p & MASK(5);
    } else {
        { *new_pos = p + 1; return 0xFFFFFFFFu; } /* invalid */
    }

    for (int i = 1; i < len; i++) {
        if ((unsigned char)p[i] >> 6 != MASK2(1, 1))
            { *new_pos = p + 1; return 0xFFFFFFFFu; } /* invalid */
        c = (c << 6) | (p[i] & MASK(6));
    }

    *new_pos = p + len;
    return c;
}

static int compare_range(const void *key, const void *elem) {
    uint32_t c = *(const uint32_t *)key;
    const UTF32Range *r = elem;
    if (c < r->first)
        return -1;
    if (c > r->last)
        return 1;
    return 0;
}




static bool u8ident_initialized = false;

static void ensure_u8ident_init(void) {
    if (!u8ident_initialized) {
        u8ident_init();
        u8ident_initialized = true;
    }
}



static const char *u8ident_check_ident_align16(const char *name, int len) {
    // Fast path: pure ASCII identifiers are always valid (no homoglyph risk).
    // Scan using SIMD if available; fall back to scalar.
    bool has_non_ascii = false;
#if defined(__SSE2__)
    {
        const unsigned char *p = (const unsigned char *)name;
        int n = len;
        __m128i hi = _mm_set1_epi8((char)0x80);
        for (; n >= 16; p += 16, n -= 16) {
            __m128i v = _mm_loadu_si128((const __m128i *)p);
            if (_mm_movemask_epi8(_mm_and_si128(v, hi))) {
                has_non_ascii = true;
                break;
            }
        }
        if (!has_non_ascii)
            for (; n > 0; p++, n--)
                if (*p & 0x80) {
                    has_non_ascii = true;
                    break;
                }
    }
#elif defined(__ARM_NEON)
    {
        const unsigned char *p = (const unsigned char *)name;
        int n = len;
        for (; n >= 16; p += 16, n -= 16) {
            if (vmaxvq_u8(vld1q_u8(p)) >= 0x80) {
                has_non_ascii = true;
                break;
            }
        }
        if (!has_non_ascii)
            for (; n > 0; p++, n--)
                if (*p & 0x80) {
                    has_non_ascii = true;
                    break;
                }
    }
#else
    {
        const unsigned char *p = (const unsigned char *)name;
        for (int i = 0; i < len; i++)
            if (p[i] & 0x80) {
                has_non_ascii = true;
                break;
            }
    }
#endif
    if (!has_non_ascii) {
        // Pure ASCII: register Latin in context so later Cyrillic/etc. triggers
        // a script-mixing warning, then skip the full scan.
        ensure_u8ident_init();
        // codeql[cpp/local-variable-hides-global-variable]: local ptr from accessor u8ident_ctx(), not the global ctx[] pool
        struct ctx_t *ctx = u8ident_ctx();
        if (!u8ident_has_script_ctx(SC_Latin, ctx))
            u8ident_add_script_ctx(SC_Latin, ctx);
        return NULL;
    }
    ensure_u8ident_init();
    enum u8id_errors ret = u8ident_check_buf(name, len);
    switch (ret) {
    case U8ID_ERR_SCRIPT:
    case U8ID_ERR_SCRIPTS: {
        static char msg[128];
        const char *sc = u8ident_failed_script_name(0);
        uint32_t cp = u8ident_failed_char(0);
        if (sc)
            snprintf(msg, sizeof(msg), "disallowed script %s (U+%04X) in identifier",
                     sc, cp);
        else
            snprintf(msg, sizeof(msg), "identifier mixes scripts in a confusable way");
        return msg;
    }
    case U8ID_ERR_COMBINE: return "identifier has invalid combining mark sequence";
    case U8ID_ERR_CONFUS: return "identifier contains confusable characters";
    case U8ID_EOK_WARN_CONFUS:
        return "identifier may contain confusable characters";
    default: return NULL;
    }
}


#include "unicode_xid.h"
/* Length of the valid UTF-8 encoded Unicode identifier character at s (n bytes left), else 0.
 * first: XID_Start (plus the math start characters), else XID_Continue.
 * Used by the grammars (syntax-p5.y, syntax-p6.y) for IDFIRST/utfw. */
int potion_uni_ident_len(const char *s, long n, int first) {
    unsigned char c = (unsigned char)*s;
    char *np;
    uint32_t cp;
    int len = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : c >= 0xC0 ? 2 : 0;
    if (!len || len > n || c > 0xF4) return 0;
    cp = decode_utf8(&np, (char *)s);
    if (cp == 0xFFFFFFFFu || cp < 0x80 || np - s != len) return 0;
    {
        const UTF32Range *r = first ? xid_start : xid_cont;
        int nr = first ? (int)(sizeof xid_start / sizeof *xid_start) : (int)(sizeof xid_cont / sizeof *xid_cont);
        return bsearch(&cp, r, nr, sizeof(UTF32Range), compare_range) ? len : 0;
    }
}

/* NULL if the identifier is fine, else a message (mixed scripts, confusables, bad marks) */
const char *potion_uni_ident_check(const char *name, int len) {
    return u8ident_check_ident_align16(name, len);
}
