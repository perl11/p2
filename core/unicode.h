// SPDX-License-Identifier: LGPL-2.1-or-later
// Unicode identifier checking via libu8ident TR39
#define U8ID_TR31 3
#include <stdint.h>
#include <stdbool.h>
#include <wchar.h>

#if __GNUC__ >= 3
#ifndef likely
#define likely(expr) __builtin_expect((long)((expr) != 0), 1)
#define unlikely(expr) __builtin_expect((long)((expr) != 0), 0)
#endif
#else
#ifndef likely
#define likely(expr) (expr)
#define unlikely(expr) (expr)
#endif
#endif

#define STDCHAR char
#define TRUE true
#define FALSE false
#define ARRAY_SIZE(x) sizeof(x) / sizeof(*x)
#define strEQc(s1, s2) !strcmp((s1), s2 "")

#define U8ID_UNICODE_MAJOR 17
#define U8ID_UNICODE_MINOR 0

#define U8ID_CTX_TRESH 5
#define U8ID_SCR_TRESH 8
struct ctx_t {
    uint8_t count;
    uint8_t has_han : 1;
    uint8_t is_japanese : 1;
    uint8_t is_chinese : 1;
    uint8_t is_korean : 1;
    uint8_t is_rtl : 1; // Hebrew or Arabic
    uint32_t last_cp; // only set on errors
    union {
        uint64_t scr64; // room for 8 scripts
        uint8_t scr8[U8ID_SCR_TRESH];
        // we need more than 8 only with insecure
        // profiles, or when we manually add extra scripts.
        uint8_t *u8p; // or if count > 8
    };
};

// clang-format off
#if (defined(__GNUC__) && ((__GNUC__ * 100) + __GNUC_MINOR__) >= 480)
#  define GCC_DIAG_PRAGMA(x) _Pragma (#x)
#  define GCC_DIAG_IGNORE(x)                                                  \
    GCC_DIAG_PRAGMA (GCC diagnostic ignored #x)
#else
#  define GCC_DIAG_IGNORE(w)
#endif

// from u8idnorm.c
static uint32_t dec_utf8(char **strp);
typedef unsigned u8id_ctx_t;

/* Initialize the library for a tr39 profile, normalization */
static int u8ident_init();

/* maxlength of an identifier. Default: 1024. Beware that such longs
   identifiers, are not really identifiable anymore, and keep them under 80 or
   even less. Some filesystems do allow now 32K identifiers, which is a glaring
   security hole, waiting to be exploited */
/* Generates a new identifier document/context/directory, which
   initializes a new list of seen scripts. Contexts are optional, by
   default all checks are done in the same context 0. With compilers
   and interpreters a context is a source file, with filesystems a directory,
   with usernames you may choose if you need to support different languages at
   once.
   I cannot think of any such usage, so better avoid contexts with usernames to
   avoid mixups. */

/* Optionally adds a script to the context, if it's known or declared
   beforehand. Such as `use utf8 "Greek";` in cperl.

   All http://www.unicode.org/reports/tr31/#Table_Recommended_Scripts
   need not to be declared beforehand.

   Common Inherited Arabic Armenian Bengali Bopomofo Cyrillic
   Devanagari Ethiopic Georgian Greek Gujarati Gurmukhi Hangul Han Hebrew
   Hiragana Katakana Kannada Khmer Lao Latin Malayalam Myanmar Oriya
   Sinhala Tamil Telugu Thaana Thai Tibetan

   All http://www.unicode.org/reports/tr31/#Table_Limited_Use_Scripts need
   to be or are disallowed by profile:

   Adlam Balinese Bamum Batak Canadian_Aboriginal Chakma Cham Cherokee
   Hanifi_Rohingya Javanese Kayah_Li Lepcha Limbu Lisu Mandaic
   Meetei_Mayek Miao New_Tai_Lue Newa Nko Nyiakeng_Puachue_Hmong Ol_Chiki
   Osage Saurashtra Sundanese Syloti_Nagri Syriac Tai_Le Tai_Tham
   Tai_Viet Tifinagh Vai Wancho Yi Unknown

 */

/* Deletes the context generated with `u8ident_new_ctx`. This is
   optional, all remaining contexts are deleted by `u8ident_free` */
static int u8ident_free_ctx(u8id_ctx_t ctx);

/* End this library, cleaning up all internal structures. */
static void u8ident_free(void);

enum u8id_errors {
    U8ID_EOK = 0, /* valid */
    U8ID_EOK_WARN_CONFUS = 2, /* warn about confusable */
    U8ID_ERR_XID = -1, /* invalid xid, disallowed via IdentifierStatus.txt */
    U8ID_ERR_SCRIPT = -2, /* invalid script */
    U8ID_ERR_SCRIPTS = -3, /* invalid mixed scripts */
    U8ID_ERR_ENCODING = -4, /* invalid encoding */
    U8ID_ERR_COMBINE = -5, /* invalid combination of codepoints */
    U8ID_ERR_CONFUS = -6, /* invalid because confusable */
};

/* Two variants to check if this identifier is valid. With u8ident_check_buf
   buf does not need to be zero-terminated.

   Return values (enum u8id_errors):
    * 0   - valid
    * 2   - warn about confusable
    * -1  - invalid xid, disallowed via IdentifierStatus.txt
    * -2  - invalid script
    * -3  - invalid mixed scripts
    * -4  - invalid encoding
    * -5  - invalid combination of codepoints
    * -6  - invalid because confusable

   Note that in the check we explicitly allow the Latin confusables: 0 1 I `
   i.e. U+30, U+31, U+49, U+60
*/
static enum u8id_errors u8ident_check_buf(const char *buf, const int len);

/* returns the failing codepoint, which failed in the last check. */
static uint32_t u8ident_failed_char(const u8id_ctx_t ctx);
/* returns the constant script name, which failed in the last check. */
static const char *u8ident_failed_script_name(const u8id_ctx_t ctx);

// More Function prototypes
static bool u8ident_has_script_ctx(const uint8_t scr, const struct ctx_t *ctx);
static int u8ident_add_script_ctx(const uint8_t scr, struct ctx_t *ctx);
static struct ctx_t *u8ident_ctx(void);
// member or bidi formatting characters for reordering attacks.
// Only valid with RTL scripts, such as Hebrew and Arabic.
static bool u8ident_is_bidi(const uint32_t cp);
// Greek letters confusable with Latin
static bool u8ident_is_greek_latin_confus(const uint32_t cp);
static const char *u8ident_script_name(const int scr);

enum u8id_sc {
// clang-format off
#define FIRST_RECOMMENDED_SCRIPT 0
  SC_Common     = 0,
  SC_Inherited  = 1,
  SC_Latin      = 2,
  SC_Arabic     = 3,
  SC_Armenian   = 4,
  SC_Bengali    = 5,
  SC_Bopomofo   = 6,
  SC_Cyrillic   = 7,
  SC_Devanagari = 8,
  SC_Ethiopic   = 9,
  SC_Georgian   = 10,
  SC_Greek      = 11,
  SC_Gujarati   = 12,
  SC_Gurmukhi   = 13,
  SC_Hangul     = 14,
  SC_Han        = 15,
  SC_Hebrew     = 16,
  SC_Hiragana   = 17,
  SC_Katakana   = 18,
  SC_Kannada    = 19,
  SC_Khmer      = 20,
  SC_Lao        = 21,
  SC_Malayalam  = 22,
  SC_Myanmar    = 23,
  SC_Oriya      = 24,
  SC_Sinhala    = 25,
  SC_Tamil      = 26,
  SC_Telugu     = 27,
  SC_Thaana     = 28,
  SC_Thai       = 29,
  SC_Tibetan    = 30,
#define FIRST_EXCLUDED_SCRIPT 31
  SC_Ahom       = 31,
  SC_Anatolian_Hieroglyphs = 32,
  SC_Avestan    = 33,
  SC_Bassa_Vah  = 34,
  SC_Beria_Erfe = 35,
  SC_Bhaiksuki  = 36,
  SC_Brahmi     = 37,
  SC_Braille    = 38,
  SC_Buginese   = 39,
  SC_Buhid      = 40,
  SC_Carian     = 41,
  SC_Caucasian_Albanian = 42,
  SC_Chorasmian = 43,
  SC_Coptic     = 44,
  SC_Cuneiform  = 45,
  SC_Cypriot    = 46,
  SC_Cypro_Minoan = 47,
  SC_Deseret    = 48,
  SC_Dives_Akuru = 49,
  SC_Dogra      = 50,
  SC_Duployan   = 51,
  SC_Egyptian_Hieroglyphs = 52,
  SC_Elbasan    = 53,
  SC_Elymaic    = 54,
  SC_Garay      = 55,
  SC_Glagolitic = 56,
  SC_Gothic     = 57,
  SC_Grantha    = 58,
  SC_Gunjala_Gondi = 59,
  SC_Gurung_Khema = 60,
  SC_Hanunoo    = 61,
  SC_Hatran     = 62,
  SC_Imperial_Aramaic = 63,
  SC_Inscriptional_Pahlavi = 64,
  SC_Inscriptional_Parthian = 65,
  SC_Kaithi     = 66,
  SC_Kawi       = 67,
  SC_Kharoshthi = 68,
  SC_Khitan_Small_Script = 69,
  SC_Khojki     = 70,
  SC_Khudawadi  = 71,
  SC_Kirat_Rai  = 72,
  SC_Linear_A   = 73,
  SC_Linear_B   = 74,
  SC_Lycian     = 75,
  SC_Lydian     = 76,
  SC_Mahajani   = 77,
  SC_Makasar    = 78,
  SC_Manichaean = 79,
  SC_Marchen    = 80,
  SC_Masaram_Gondi = 81,
  SC_Medefaidrin = 82,
  SC_Mende_Kikakui = 83,
  SC_Meroitic_Cursive = 84,
  SC_Meroitic_Hieroglyphs = 85,
  SC_Modi       = 86,
  SC_Mongolian  = 87,
  SC_Mro        = 88,
  SC_Multani    = 89,
  SC_Nabataean  = 90,
  SC_Nag_Mundari = 91,
  SC_Nandinagari = 92,
  SC_Nushu      = 93,
  SC_Ogham      = 94,
  SC_Ol_Onal    = 95,
  SC_Old_Hungarian = 96,
  SC_Old_Italic = 97,
  SC_Old_North_Arabian = 98,
  SC_Old_Permic = 99,
  SC_Old_Persian = 100,
  SC_Old_Sogdian = 101,
  SC_Old_South_Arabian = 102,
  SC_Old_Turkic = 103,
  SC_Old_Uyghur = 104,
  SC_Osmanya    = 105,
  SC_Pahawh_Hmong = 106,
  SC_Palmyrene  = 107,
  SC_Pau_Cin_Hau = 108,
  SC_Phags_Pa   = 109,
  SC_Phoenician = 110,
  SC_Psalter_Pahlavi = 111,
  SC_Rejang     = 112,
  SC_Runic      = 113,
  SC_Samaritan  = 114,
  SC_Sharada    = 115,
  SC_Shavian    = 116,
  SC_Siddham    = 117,
  SC_Sidetic    = 118,
  SC_SignWriting = 119,
  SC_Sogdian    = 120,
  SC_Sora_Sompeng = 121,
  SC_Soyombo    = 122,
  SC_Sunuwar    = 123,
  SC_Tagalog    = 124,
  SC_Tagbanwa   = 125,
  SC_Tai_Yo     = 126,
  SC_Takri      = 127,
  SC_Tangsa     = 128,
  SC_Tangut     = 129,
  SC_Tirhuta    = 130,
  SC_Todhri     = 131,
  SC_Tolong_Siki = 132,
  SC_Toto       = 133,
  SC_Tulu_Tigalari = 134,
  SC_Ugaritic   = 135,
  SC_Vithkuqi   = 136,
  SC_Warang_Citi = 137,
  SC_Yezidi     = 138,
  SC_Zanabazar_Square = 139,
#define FIRST_LIMITED_USE_SCRIPT 140
  SC_Adlam      = 140,
  SC_Balinese   = 141,
  SC_Bamum      = 142,
  SC_Batak      = 143,
  SC_Canadian_Aboriginal = 144,
  SC_Chakma     = 145,
  SC_Cham       = 146,
  SC_Cherokee   = 147,
  SC_Hanifi_Rohingya = 148,
  SC_Javanese   = 149,
  SC_Kayah_Li   = 150,
  SC_Lepcha     = 151,
  SC_Limbu      = 152,
  SC_Lisu       = 153,
  SC_Mandaic    = 154,
  SC_Meetei_Mayek = 155,
  SC_Miao       = 156,
  SC_New_Tai_Lue = 157,
  SC_Newa       = 158,
  SC_Nko        = 159,
  SC_Nyiakeng_Puachue_Hmong = 160,
  SC_Ol_Chiki   = 161,
  SC_Osage      = 162,
  SC_Saurashtra = 163,
  SC_Sundanese  = 164,
  SC_Syloti_Nagri = 165,
  SC_Syriac     = 166,
  SC_Tai_Le     = 167,
  SC_Tai_Tham   = 168,
  SC_Tai_Viet   = 169,
  SC_Tifinagh   = 170,
  SC_Vai        = 171,
  SC_Wancho     = 172,
  SC_Yi         = 173,
  SC_Unknown    = 174,
#define LAST_SCRIPT 174
    // clang-format on
};

struct range_bool {
    uint32_t from;
    uint32_t to;
};

struct range_short {
    uint32_t from;
    uint32_t to;
    uint16_t types;
};

struct nsm_ws {
    uint32_t nsm;
    wchar_t *letters;
};

/* All Combining Marks, sorted */
static const struct range_bool mark_list[] = {
    // clang-format off
    { 0x300, 0x36F },
    { 0x483, 0x489 },
    { 0x591, 0x5BD },
    { 0x5BF, 0x5BF },
    { 0x5C1, 0x5C2 },
    { 0x5C4, 0x5C5 },
    { 0x5C7, 0x5C7 },
    { 0x610, 0x61A },
    { 0x64B, 0x65F },
    { 0x670, 0x670 },
    { 0x6D6, 0x6DC },
    { 0x6DF, 0x6E4 },
    { 0x6E7, 0x6E8 },
    { 0x6EA, 0x6ED },
    { 0x711, 0x711 },
    { 0x730, 0x74A },
    { 0x7A6, 0x7B0 },
    { 0x7EB, 0x7F3 },
    { 0x7FD, 0x7FD },
    { 0x816, 0x819 },
    { 0x81B, 0x823 },
    { 0x825, 0x827 },
    { 0x829, 0x82D },
    { 0x859, 0x85B },
    { 0x897, 0x89F },
    { 0x8CA, 0x8E1 },
    { 0x8E3, 0x903 },
    { 0x93A, 0x93C },
    { 0x93E, 0x94F },
    { 0x951, 0x957 },
    { 0x962, 0x963 },
    { 0x981, 0x983 },
    { 0x9BC, 0x9BC },
    { 0x9BE, 0x9C4 },
    { 0x9C7, 0x9C8 },
    { 0x9CB, 0x9CD },
    { 0x9D7, 0x9D7 },
    { 0x9E2, 0x9E3 },
    { 0x9FE, 0x9FE },
    { 0xA01, 0xA03 },
    { 0xA3C, 0xA3C },
    { 0xA3E, 0xA42 },
    { 0xA47, 0xA48 },
    { 0xA4B, 0xA4D },
    { 0xA51, 0xA51 },
    { 0xA70, 0xA71 },
    { 0xA75, 0xA75 },
    { 0xA81, 0xA83 },
    { 0xABC, 0xABC },
    { 0xABE, 0xAC5 },
    { 0xAC7, 0xAC9 },
    { 0xACB, 0xACD },
    { 0xAE2, 0xAE3 },
    { 0xAFA, 0xAFF },
    { 0xB01, 0xB03 },
    { 0xB3C, 0xB3C },
    { 0xB3E, 0xB44 },
    { 0xB47, 0xB48 },
    { 0xB4B, 0xB4D },
    { 0xB55, 0xB57 },
    { 0xB62, 0xB63 },
    { 0xB82, 0xB82 },
    { 0xBBE, 0xBC2 },
    { 0xBC6, 0xBC8 },
    { 0xBCA, 0xBCD },
    { 0xBD7, 0xBD7 },
    { 0xC00, 0xC04 },
    { 0xC3C, 0xC3C },
    { 0xC3E, 0xC44 },
    { 0xC46, 0xC48 },
    { 0xC4A, 0xC4D },
    { 0xC55, 0xC56 },
    { 0xC62, 0xC63 },
    { 0xC81, 0xC83 },
    { 0xCBC, 0xCBC },
    { 0xCBE, 0xCC4 },
    { 0xCC6, 0xCC8 },
    { 0xCCA, 0xCCD },
    { 0xCD5, 0xCD6 },
    { 0xCE2, 0xCE3 },
    { 0xCF3, 0xCF3 },
    { 0xD00, 0xD03 },
    { 0xD3B, 0xD3C },
    { 0xD3E, 0xD44 },
    { 0xD46, 0xD48 },
    { 0xD4A, 0xD4D },
    { 0xD57, 0xD57 },
    { 0xD62, 0xD63 },
    { 0xD81, 0xD83 },
    { 0xDCA, 0xDCA },
    { 0xDCF, 0xDD4 },
    { 0xDD6, 0xDD6 },
    { 0xDD8, 0xDDF },
    { 0xDF2, 0xDF3 },
    { 0xE31, 0xE31 },
    { 0xE34, 0xE3A },
    { 0xE47, 0xE4E },
    { 0xEB1, 0xEB1 },
    { 0xEB4, 0xEBC },
    { 0xEC8, 0xECE },
    { 0xF18, 0xF19 },
    { 0xF35, 0xF35 },
    { 0xF37, 0xF37 },
    { 0xF39, 0xF39 },
    { 0xF3E, 0xF3F },
    { 0xF71, 0xF84 },
    { 0xF86, 0xF87 },
    { 0xF8D, 0xF97 },
    { 0xF99, 0xFBC },
    { 0xFC6, 0xFC6 },
    { 0x102B, 0x103E },
    { 0x1056, 0x1059 },
    { 0x105E, 0x1060 },
    { 0x1062, 0x1064 },
    { 0x1067, 0x106D },
    { 0x1071, 0x1074 },
    { 0x1082, 0x108D },
    { 0x108F, 0x108F },
    { 0x109A, 0x109D },
    { 0x135D, 0x135F },
    { 0x1712, 0x1715 },
    { 0x1732, 0x1734 },
    { 0x1752, 0x1753 },
    { 0x1772, 0x1773 },
    { 0x17B4, 0x17D3 },
    { 0x17DD, 0x17DD },
    { 0x180B, 0x180D },
    { 0x180F, 0x180F },
    { 0x1885, 0x1886 },
    { 0x18A9, 0x18A9 },
    { 0x1920, 0x192B },
    { 0x1930, 0x193B },
    { 0x1A17, 0x1A1B },
    { 0x1A55, 0x1A5E },
    { 0x1A60, 0x1A7C },
    { 0x1A7F, 0x1A7F },
    { 0x1AB0, 0x1ADD },
    { 0x1AE0, 0x1AEB },
    { 0x1B00, 0x1B04 },
    { 0x1B34, 0x1B44 },
    { 0x1B6B, 0x1B73 },
    { 0x1B80, 0x1B82 },
    { 0x1BA1, 0x1BAD },
    { 0x1BE6, 0x1BF3 },
    { 0x1C24, 0x1C37 },
    { 0x1CD0, 0x1CD2 },
    { 0x1CD4, 0x1CE8 },
    { 0x1CED, 0x1CED },
    { 0x1CF4, 0x1CF4 },
    { 0x1CF7, 0x1CF9 },
    { 0x1DC0, 0x1DFF },
    { 0x20D0, 0x20F0 },
    { 0x2CEF, 0x2CF1 },
    { 0x2D7F, 0x2D7F },
    { 0x2DE0, 0x2DFF },
    { 0x302A, 0x302F },
    { 0x3099, 0x309A },
    { 0xA66F, 0xA672 },
    { 0xA674, 0xA67D },
    { 0xA69E, 0xA69F },
    { 0xA6F0, 0xA6F1 },
    { 0xA802, 0xA802 },
    { 0xA806, 0xA806 },
    { 0xA80B, 0xA80B },
    { 0xA823, 0xA827 },
    { 0xA82C, 0xA82C },
    { 0xA880, 0xA881 },
    { 0xA8B4, 0xA8C5 },
    { 0xA8E0, 0xA8F1 },
    { 0xA8FF, 0xA8FF },
    { 0xA926, 0xA92D },
    { 0xA947, 0xA953 },
    { 0xA980, 0xA983 },
    { 0xA9B3, 0xA9C0 },
    { 0xA9E5, 0xA9E5 },
    { 0xAA29, 0xAA36 },
    { 0xAA43, 0xAA43 },
    { 0xAA4C, 0xAA4D },
    { 0xAA7B, 0xAA7D },
    { 0xAAB0, 0xAAB0 },
    { 0xAAB2, 0xAAB4 },
    { 0xAAB7, 0xAAB8 },
    { 0xAABE, 0xAABF },
    { 0xAAC1, 0xAAC1 },
    { 0xAAEB, 0xAAEF },
    { 0xAAF5, 0xAAF6 },
    { 0xABE3, 0xABEA },
    { 0xABEC, 0xABED },
    { 0xFB1E, 0xFB1E },
    { 0xFE00, 0xFE0F },
    { 0xFE20, 0xFE2F },
    { 0x101FD, 0x101FD },
    { 0x102E0, 0x102E0 },
    { 0x10376, 0x1037A },
    { 0x10A01, 0x10A03 },
    { 0x10A05, 0x10A06 },
    { 0x10A0C, 0x10A0F },
    { 0x10A38, 0x10A3A },
    { 0x10A3F, 0x10A3F },
    { 0x10AE5, 0x10AE6 },
    { 0x10D24, 0x10D27 },
    { 0x10D69, 0x10D6D },
    { 0x10EAB, 0x10EAC },
    { 0x10EFA, 0x10EFF },
    { 0x10F46, 0x10F50 },
    { 0x10F82, 0x10F85 },
    { 0x11000, 0x11002 },
    { 0x11038, 0x11046 },
    { 0x11070, 0x11070 },
    { 0x11073, 0x11074 },
    { 0x1107F, 0x11082 },
    { 0x110B0, 0x110BA },
    { 0x110C2, 0x110C2 },
    { 0x11100, 0x11102 },
    { 0x11127, 0x11134 },
    { 0x11145, 0x11146 },
    { 0x11173, 0x11173 },
    { 0x11180, 0x11182 },
    { 0x111B3, 0x111C0 },
    { 0x111C9, 0x111CC },
    { 0x111CE, 0x111CF },
    { 0x1122C, 0x11237 },
    { 0x1123E, 0x1123E },
    { 0x11241, 0x11241 },
    { 0x112DF, 0x112EA },
    { 0x11300, 0x11303 },
    { 0x1133B, 0x1133C },
    { 0x1133E, 0x11344 },
    { 0x11347, 0x11348 },
    { 0x1134B, 0x1134D },
    { 0x11357, 0x11357 },
    { 0x11362, 0x11363 },
    { 0x11366, 0x1136C },
    { 0x11370, 0x11374 },
    { 0x113B8, 0x113C0 },
    { 0x113C2, 0x113C2 },
    { 0x113C5, 0x113C5 },
    { 0x113C7, 0x113CA },
    { 0x113CC, 0x113D0 },
    { 0x113D2, 0x113D2 },
    { 0x113E1, 0x113E2 },
    { 0x11435, 0x11446 },
    { 0x1145E, 0x1145E },
    { 0x114B0, 0x114C3 },
    { 0x115AF, 0x115B5 },
    { 0x115B8, 0x115C0 },
    { 0x115DC, 0x115DD },
    { 0x11630, 0x11640 },
    { 0x116AB, 0x116B7 },
    { 0x1171D, 0x1172B },
    { 0x1182C, 0x1183A },
    { 0x11930, 0x11935 },
    { 0x11937, 0x11938 },
    { 0x1193B, 0x1193E },
    { 0x11940, 0x11940 },
    { 0x11942, 0x11943 },
    { 0x119D1, 0x119D7 },
    { 0x119DA, 0x119E0 },
    { 0x119E4, 0x119E4 },
    { 0x11A01, 0x11A0A },
    { 0x11A33, 0x11A39 },
    { 0x11A3B, 0x11A3E },
    { 0x11A47, 0x11A47 },
    { 0x11A51, 0x11A5B },
    { 0x11A8A, 0x11A99 },
    { 0x11B60, 0x11B67 },
    { 0x11C2F, 0x11C36 },
    { 0x11C38, 0x11C3F },
    { 0x11C92, 0x11CA7 },
    { 0x11CA9, 0x11CB6 },
    { 0x11D31, 0x11D36 },
    { 0x11D3A, 0x11D3A },
    { 0x11D3C, 0x11D3D },
    { 0x11D3F, 0x11D45 },
    { 0x11D47, 0x11D47 },
    { 0x11D8A, 0x11D8E },
    { 0x11D90, 0x11D91 },
    { 0x11D93, 0x11D97 },
    { 0x11EF3, 0x11EF6 },
    { 0x11F00, 0x11F01 },
    { 0x11F03, 0x11F03 },
    { 0x11F34, 0x11F3A },
    { 0x11F3E, 0x11F42 },
    { 0x11F5A, 0x11F5A },
    { 0x13440, 0x13440 },
    { 0x13447, 0x13455 },
    { 0x1611E, 0x1612F },
    { 0x16AF0, 0x16AF4 },
    { 0x16B30, 0x16B36 },
    { 0x16F4F, 0x16F4F },
    { 0x16F51, 0x16F87 },
    { 0x16F8F, 0x16F92 },
    { 0x16FE4, 0x16FE4 },
    { 0x16FF0, 0x16FF1 },
    { 0x1BC9D, 0x1BC9E },
    { 0x1CF00, 0x1CF2D },
    { 0x1CF30, 0x1CF46 },
    { 0x1D165, 0x1D169 },
    { 0x1D16D, 0x1D172 },
    { 0x1D17B, 0x1D182 },
    { 0x1D185, 0x1D18B },
    { 0x1D1AA, 0x1D1AD },
    { 0x1D242, 0x1D244 },
    { 0x1DA00, 0x1DA36 },
    { 0x1DA3B, 0x1DA6C },
    { 0x1DA75, 0x1DA75 },
    { 0x1DA84, 0x1DA84 },
    { 0x1DA9B, 0x1DA9F },
    { 0x1DAA1, 0x1DAAF },
    { 0x1E000, 0x1E006 },
    { 0x1E008, 0x1E018 },
    { 0x1E01B, 0x1E021 },
    { 0x1E023, 0x1E024 },
    { 0x1E026, 0x1E02A },
    { 0x1E08F, 0x1E08F },
    { 0x1E130, 0x1E136 },
    { 0x1E2AE, 0x1E2AE },
    { 0x1E2EC, 0x1E2EF },
    { 0x1E4EC, 0x1E4EF },
    { 0x1E5EE, 0x1E5EF },
    { 0x1E6E3, 0x1E6E3 },
    { 0x1E6E6, 0x1E6E6 },
    { 0x1E6EE, 0x1E6EF },
    { 0x1E6F5, 0x1E6F5 },
    { 0x1E8D0, 0x1E8D6 },
    { 0x1E944, 0x1E94A },
    { 0xE0100, 0xE01EF },
    // clang-format on
};

/* All letters with non-spacing combining marks, sorted.
   The first entry is the NSM, if letters exist.
 */
static const struct nsm_ws nsm_letters[] = {
    // clang-format off
    { 0x0300,  /* NSM: GRAVE 300 */
      L"\u00C0\u00C8\u00CC\u00D2\u00D9\u00E0\u00E8\u00EC\u00F2\u00F9\u01DB\u01DC\u01F8\u01F9\u0400\u040D\u0450\u045D\u1E14\u1E15\u1E50\u1E51\u1E80\u1E81\u1EA6\u1EA7\u1EB0\u1EB1\u1EC0\u1EC1\u1ED2\u1ED3\u1EDC\u1EDD\u1EEA\u1EEB\u1EF2\u1EF3\u1F02\u1F03\u1F0A\u1F0B\u1F12\u1F13\u1F1A\u1F1B\u1F22\u1F23\u1F2A\u1F2B\u1F32\u1F33\u1F3A\u1F3B\u1F42\u1F43\u1F4A\u1F4B\u1F52\u1F53\u1F5B\u1F62\u1F63\u1F6A\u1F6B\u1F70\u1F72\u1F74\u1F76\u1F78\u1F7A\u1F7C\u1FBA\u1FC8\u1FCA\u1FD2\u1FDA\u1FE2\u1FEA\u1FF8\u1FFA" },
      /* ÀÈÌÒÙàèìòùǛǜǸǹЀЍѐѝḔḕṐṑẀẁẦầẰằỀềỒồỜờỪừỲỳἂἃἊἋἒἓἚἛἢἣἪἫἲἳἺἻὂὃὊὋὒὓὛὢὣὪὫὰὲὴὶὸὺὼᾺῈῊῒῚῢῪῸῺ */
    { 0x0301,  /* NSM: ACUTE 301 */
      L"\u00C1\u00C9\u00CD\u00D3\u00DA\u00DD\u00E1\u00E9\u00ED\u00F3\u00FA\u00FD\u0106\u0107\u0139\u013A\u0143\u0144\u0154\u0155\u015A\u015B\u0179\u017A\u01D7\u01D8\u01F4\u01F5\u01FA\u01FB\u01FC\u01FD\u01FE\u01FF\u0386\u0388\u0389\u038A\u038C\u038E\u038F\u0390\u03AC\u03AD\u03AE\u03AF\u03B0\u03CC\u03CD\u03CE\u03D3\u0403\u040C\u0453\u045C\u1E08\u1E09\u1E16\u1E17\u1E2E\u1E2F\u1E30\u1E31\u1E3E\u1E3F\u1E4C\u1E4D\u1E52\u1E53\u1E54\u1E55\u1E78\u1E79\u1E82\u1E83\u1EA4\u1EA5\u1EAE\u1EAF\u1EBE\u1EBF\u1ED0\u1ED1\u1EDA\u1EDB\u1EE8\u1EE9\u1F04\u1F05\u1F0C\u1F0D\u1F14\u1F15\u1F1C\u1F1D\u1F24\u1F25\u1F2C\u1F2D\u1F34\u1F35\u1F3C\u1F3D\u1F44\u1F45\u1F4C\u1F4D\u1F54\u1F55\u1F5D\u1F64\u1F65\u1F6C\u1F6D" },
      /* ÁÉÍÓÚÝáéíóúýĆćĹĺŃńŔŕŚśŹźǗǘǴǵǺǻǼǽǾǿΆΈΉΊΌΎΏΐάέήίΰόύώϓЃЌѓќḈḉḖḗḮḯḰḱḾḿṌṍṒṓṔṕṸṹẂẃẤấẮắẾếỐốỚớỨứἄἅἌἍἔἕἜἝἤἥἬἭἴἵἼἽὄὅὌὍὔὕὝὤὥὬὭ */
    { 0x0302,  /* NSM: CIRCUMFLEX 302 */
      L"\u00C2\u00CA\u00CE\u00D4\u00DB\u00E2\u00EA\u00EE\u00F4\u00FB\u0108\u0109\u011C\u011D\u0124\u0125\u0134\u0135\u015C\u015D\u0174\u0175\u0176\u0177\u1E90\u1E91\u1EAC\u1EAD\u1EC6\u1EC7\u1ED8\u1ED9" },
      /* ÂÊÎÔÛâêîôûĈĉĜĝĤĥĴĵŜŝŴŵŶŷẐẑẬậỆệỘộ */
    { 0x0303,  /* NSM: TILDE 303 */
      L"\u00C3\u00D1\u00D5\u00E3\u00F1\u00F5\u0128\u0129\u0168\u0169\u1E7C\u1E7D\u1EAA\u1EAB\u1EB4\u1EB5\u1EBC\u1EBD\u1EC4\u1EC5\u1ED6\u1ED7\u1EE0\u1EE1\u1EEE\u1EEF\u1EF8\u1EF9" },
      /* ÃÑÕãñõĨĩŨũṼṽẪẫẴẵẼẽỄễỖỗỠỡỮữỸỹ */
    { 0x0304,  /* NSM: MACRON 304 */
      L"\u0100\u0101\u0112\u0113\u012A\u012B\u014C\u014D\u016A\u016B\u01D5\u01D6\u01DE\u01DF\u01E0\u01E1\u01E2\u01E3\u01EC\u01ED\u022A\u022B\u022C\u022D\u0230\u0231\u0232\u0233\u04E2\u04E3\u04EE\u04EF\u1E20\u1E21\u1E38\u1E39\u1E5C\u1E5D\u1FB1\u1FB9\u1FD1\u1FD9\u1FE1\u1FE9" },
      /* ĀāĒēĪīŌōŪūǕǖǞǟǠǡǢǣǬǭȪȫȬȭȰȱȲȳӢӣӮӯḠḡḸḹṜṝᾱᾹῑῙῡῩ */
    { 0x0306,  /* NSM: BREVE 306 */
      L"\u0102\u0103\u0114\u0115\u011E\u011F\u012C\u012D\u014E\u014F\u016C\u016D\u040E\u0419\u0439\u045E\u04C1\u04C2\u04D0\u04D1\u04D6\u04D7\u1E1C\u1E1D\u1EB6\u1EB7\u1FB0\u1FB8\u1FD0\u1FD8\u1FE0\u1FE8" },
      /* ĂăĔĕĞğĬĭŎŏŬŭЎЙйўӁӂӐӑӖӗḜḝẶặᾰᾸῐῘῠῨ */
    { 0x0307,  /* NSM: DOT ABOVE 307 */
      L"\u010A\u010B\u0116\u0117\u0120\u0121\u0130\u017B\u017C\u0226\u0227\u022E\u022F\u06A7\u06AC\u06B6\u06BF\u06CF\u0762\u0765\u087A\u1DA1\u1E02\u1E03\u1E0A\u1E0B\u1E1E\u1E1F\u1E22\u1E23\u1E40\u1E41\u1E44\u1E45\u1E56\u1E57\u1E58\u1E59\u1E60\u1E61\u1E64\u1E65\u1E66\u1E67\u1E68\u1E69\u1E6A\u1E6B\u1E86\u1E87\u1E8A\u1E8B\u1E8E\u1E8F\u1E9B\u312E\U000105C9\U000105E4\U00010798\U00010EB0" },
      /* ĊċĖėĠġİŻżȦȧȮȯڧڬڶڿۏݢݥࡺᶡḂḃḊḋḞḟḢḣṀṁṄṅṖṗṘṙṠṡṤṥṦṧṨṩṪṫẆẇẊẋẎẏẛㄮ𐗉𐗤𐞘𐺰 */
    { 0x0308,  /* NSM: DIAERESIS 308 */
      L"\u00C4\u00CB\u00CF\u00D6\u00DC\u00E4\u00EB\u00EF\u00F6\u00FC\u00FF\u0178\u03AA\u03AB\u03CA\u03CB\u03D4\u0401\u0407\u0451\u0457\u04D2\u04D3\u04DA\u04DB\u04DC\u04DD\u04DE\u04DF\u04E4\u04E5\u04E6\u04E7\u04EA\u04EB\u04EC\u04ED\u04F0\u04F1\u04F4\u04F5\u04F8\u04F9\u1DF2\u1DF3\u1DF4\u1E26\u1E27\u1E4E\u1E4F\u1E7A\u1E7B\u1E84\u1E85\u1E8C\u1E8D\u1E97" },
      /* ÄËÏÖÜäëïöüÿŸΪΫϊϋϔЁЇёїӒӓӚӛӜӝӞӟӤӥӦӧӪӫӬӭӰӱӴӵӸӹᷲᷳᷴḦḧṎṏṺṻẄẅẌẍẗ */
    { 0x0309,  /* NSM: HOOK ABOVE 309 */
      L"\u1EA2\u1EA3\u1EA8\u1EA9\u1EB2\u1EB3\u1EBA\u1EBB\u1EC2\u1EC3\u1EC8\u1EC9\u1ECE\u1ECF\u1ED4\u1ED5\u1EDE\u1EDF\u1EE6\u1EE7\u1EEC\u1EED\u1EF6\u1EF7" },
      /* ẢảẨẩẲẳẺẻỂểỈỉỎỏỔổỞởỦủỬửỶỷ */
    { 0x030A,  /* NSM: RING ABOVE 30A */
      L"\u00C5\u00E5\u016E\u016F\u088F\u1E98\u1E99" },
      /* ÅåŮů࢏ẘẙ */
    { 0x030B,  /* NSM: DOUBLE ACUTE 30B */
      L"\u0150\u0151\u0170\u0171\u04F2\u04F3" },
      /* ŐőŰűӲӳ */
    { 0x030C,  /* NSM: HACEK 30C */
      L"\u010C\u010D\u010E\u010F\u011A\u011B\u013D\u013E\u0147\u0148\u0158\u0159\u0160\u0161\u0164\u0165\u017D\u017E\u01CD\u01CE\u01CF\u01D0\u01D1\u01D2\u01D3\u01D4\u01D9\u01DA\u01E6\u01E7\u01E8\u01E9\u01EE\u01EF\u01F0\u021E\u021F" },
      /* ČčĎďĚěĽľŇňŘřŠšŤťŽžǍǎǏǐǑǒǓǔǙǚǦǧǨǩǮǯǰȞȟ */
    { 0x030F,  /* NSM: DOUBLE GRAVE 30F */
      L"\u0200\u0201\u0204\u0205\u0208\u0209\u020C\u020D\u0210\u0211\u0214\u0215\u0476\u0477" },
      /* ȀȁȄȅȈȉȌȍȐȑȔȕѶѷ */
    { 0x0311,  /* NSM: INVERTED BREVE 311 */
      L"\u0202\u0203\u0206\u0207\u020A\u020B\u020E\u020F\u0212\u0213\u0216\u0217" },
      /* ȂȃȆȇȊȋȎȏȒȓȖȗ */
    { 0x0313,  /* NSM: COMMA ABOVE 313 */
      L"\u1F00\u1F08\u1F10\u1F18\u1F20\u1F28\u1F30\u1F38\u1F40\u1F48\u1F50\u1F60\u1F68\u1FE4" },
      /* ἀἈἐἘἠἨἰἸὀὈὐὠὨῤ */
    { 0x0314,  /* NSM: REVERSED COMMA ABOVE 314 */
      L"\u1F01\u1F09\u1F11\u1F19\u1F21\u1F29\u1F31\u1F39\u1F41\u1F49\u1F51\u1F59\u1F61\u1F69\u1FE5\u1FEC" },
      /* ἁἉἑἙἡἩἱἹὁὉὑὙὡὩῥῬ */
    { 0x031B,  /* NSM: HORN 31B */
      L"\u01A0\u01A1\u01AF\u01B0" },
      /* ƠơƯư */
    { 0x0323,  /* NSM: DOT BELOW 323 */
      L"\u068A\u0694\u06A3\u06B9\u06FA\u06FB\u06FC\u0766\u088B\u08A5\u08B4\u1E04\u1E05\u1E0C\u1E0D\u1E24\u1E25\u1E32\u1E33\u1E36\u1E37\u1E42\u1E43\u1E46\u1E47\u1E5A\u1E5B\u1E62\u1E63\u1E6C\u1E6D\u1E7E\u1E7F\u1E88\u1E89\u1E92\u1E93\u1EA0\u1EA1\u1EB8\u1EB9\u1ECA\u1ECB\u1ECC\u1ECD\u1EE2\u1EE3\u1EE4\u1EE5\u1EF0\u1EF1\u1EF4\u1EF5\U0001BC26" },
      /* ڊڔڣڹۺۻۼݦࢋࢥࢴḄḅḌḍḤḥḲḳḶḷṂṃṆṇṚṛṢṣṬṭṾṿẈẉẒẓẠạẸẹỊịỌọỢợỤụỰựỴỵ𛰦 */
    { 0x0324,  /* NSM: DOUBLE DOT BELOW 324 */
      L"\u1E72\u1E73" },
      /* Ṳṳ */
    { 0x0325,  /* NSM: RING BELOW 325 */
      L"\u1E00\u1E01" },
      /* Ḁḁ */
    { 0x0326,  /* NSM: COMMA BELOW 326 */
      L"\u0218\u0219\u021A\u021B" },
      /* ȘșȚț */
    { 0x0327,  /* NSM: CEDILLA 327 */
      L"\u00C7\u00E7\u0122\u0123\u0136\u0137\u013B\u013C\u0145\u0146\u0156\u0157\u015E\u015F\u0162\u0163\u0228\u0229\u1E10\u1E11\u1E28\u1E29" },
      /* ÇçĢģĶķĻļŅņŖŗŞşŢţȨȩḐḑḨḩ */
    { 0x0328,  /* NSM: OGONEK 328 */
      L"\u0104\u0105\u0118\u0119\u012E\u012F\u0172\u0173\u01EA\u01EB" },
      /* ĄąĘęĮįŲųǪǫ */
    { 0x032D,  /* NSM: CIRCUMFLEX BELOW 32D */
      L"\u1E12\u1E13\u1E18\u1E19\u1E3C\u1E3D\u1E4A\u1E4B\u1E70\u1E71\u1E76\u1E77" },
      /* ḒḓḘḙḼḽṊṋṰṱṶṷ */
    { 0x032E,  /* NSM: BREVE BELOW 32E */
      L"\u1E2A\u1E2B" },
      /* Ḫḫ */
    { 0x0330,  /* NSM: TILDE BELOW 330 */
      L"\u1E1A\u1E1B\u1E2C\u1E2D\u1E74\u1E75" },
      /* ḚḛḬḭṴṵ */
    { 0x0331,  /* NSM: MACRON BELOW 331 */
      L"\u1E06\u1E07\u1E0E\u1E0F\u1E34\u1E35\u1E3A\u1E3B\u1E48\u1E49\u1E5E\u1E5F\u1E6E\u1E6F\u1E94\u1E95\u1E96" },
      /* ḆḇḎḏḴḵḺḻṈṉṞṟṮṯẔẕẖ */
    { 0x20DB,  /* NSM: THREE DOTS ABOVE 20DB */
      L"\u063F\u0685\u069E\u069F\u06A0\u06A8\u06B4\u06B7\u06BD\u0763\u08A7\u08C3\u08C4\u08C5" },
      /* ؿڅڞڟڠڨڴڷڽݣࢧࣃࣄࣅ */
    { 0x20DC,  /* NSM: FOUR DOTS ABOVE 20DC */
      L"\u0690\u0699\u075C" },
      /* ڐڙݜ */
    { 0x3099,  /* NSM: KATAKANA-HIRAGANA VOICED SOUND MARK 3099 */
      L"\u304C\u304E\u3050\u3052\u3054\u3056\u3058\u305A\u305C\u305E\u3060\u3062\u3065\u3067\u3069\u3070\u3073\u3076\u3079\u307C\u3094\u309E\u30AC\u30AE\u30B0\u30B2\u30B4\u30B6\u30B8\u30BA\u30BC\u30BE\u30C0\u30C2\u30C5\u30C7\u30C9\u30D0\u30D3\u30D6\u30D9\u30DC\u30F4\u30F7\u30F8\u30F9\u30FA\u30FE\uFF9E" },
      /* がぎぐげござじずぜぞだぢづでどばびぶべぼゔゞガギグゲゴザジズゼゾダヂヅデドバビブベボヴヷヸヹヺヾﾞ */
    { 0x309A,  /* NSM: KATAKANA-HIRAGANA SEMI-VOICED SOUND MARK 309A */
      L"\u3071\u3074\u3077\u307A\u307D\u30D1\u30D4\u30D7\u30DA\u30DD\uFF9F" },
      /* ぱぴぷぺぽパピプペポﾟ */
    // clang-format on
};

enum u8id_gc {
    GC_Cc,
    GC_Cf,
    GC_Co,
    GC_Cs,
    GC_Ll,
    GC_Lm,
    GC_Lo,
    GC_Lt,
    GC_Lu,
    GC_Mc,
    GC_Me,
    GC_Mn,
    GC_Nd,
    GC_Nl,
    GC_No,
    GC_Pc,
    GC_Pd,
    GC_Pe,
    GC_Pf,
    GC_Pi,
    GC_Po,
    GC_Ps,
    GC_Sc,
    GC_Sk,
    GC_Sm,
    GC_So,
    GC_Zl,
    GC_Zp,
    GC_Zs,
    GC_L, // is L&, all letters. (for unitr39.h only)
    GC_V, // is varying, eg L or S. (for unitr39.h only),
    GC_INVALID,
};

struct sc_tr39 {
    uint32_t from;
    uint32_t to;
    enum u8id_sc sc;
    enum u8id_gc gc;
    // maxsize: Beng Deva Dogr Gong Gonm Gran Gujr Guru Knda Limb
    //          Mahj Mlym Nand Orya Sind Sinh Sylo Takr Taml Telu Tirh
    const char *scx;
};

// Filtering allowed scripts, XID_Start, Skipped Ids, !MEDIAL and NFC.
// Ranges split on GC and SCX changes
static const struct sc_tr39 tr39_start_list[] = {
#ifdef ALLOW_DOLLAR
    {'$', '$', SC_Latin, GC_Sc, NULL},
#endif
    {'A', 'Z', SC_Latin, GC_Lu, NULL},
    {'_', '_', SC_Latin, GC_Pc, NULL},
    {'a', 'z', SC_Latin, GC_Ll, NULL},
    {0xC0, 0xD6, SC_Latin, GC_Lu, NULL}, //  À..Ö
    {0xD8, 0xF6, SC_Latin, GC_L, NULL}, //  Ø..ö
    {0xF8, 0x113, SC_Latin, GC_L, NULL}, //  ø..ē
    {0x116, 0x12B, SC_Latin, GC_L, NULL}, //  Ė..ī
    {0x12E, 0x131, SC_Latin, GC_L, NULL}, //  Į..ı
    {0x134, 0x137, SC_Latin, GC_L, NULL}, //  Ĵ..ķ
    {0x139, 0x13E, SC_Latin, GC_L, NULL}, //  Ĺ..ľ
    {0x141, 0x148, SC_Latin, GC_L, NULL}, //  Ł..ň
    {0x14A, 0x14D, SC_Latin, GC_L, NULL}, //  Ŋ..ō
    {0x150, 0x155, SC_Latin, GC_L, NULL}, //  Ő..ŕ
    {0x158, 0x161, SC_Latin, GC_L, NULL}, //  Ř..š
    {0x164, 0x17E, SC_Latin, GC_L, NULL}, //  Ť..ž
    {0x180, 0x181, SC_Latin, GC_L, NULL}, //  ƀ..Ɓ
    {0x186, 0x186, SC_Latin, GC_Lu, NULL}, //  Ɔ
    {0x189, 0x18A, SC_Latin, GC_Lu, NULL}, //  Ɖ..Ɗ
    {0x18E, 0x192, SC_Latin, GC_L, NULL}, //  Ǝ..ƒ
    {0x194, 0x194, SC_Latin, GC_Lu, NULL}, //  Ɣ
    {0x196, 0x199, SC_Latin, GC_L, NULL}, //  Ɩ..ƙ
    {0x19D, 0x19D, SC_Latin, GC_Lu, NULL}, //  Ɲ
    {0x1A0, 0x1A1, SC_Latin, GC_L, NULL}, //  Ơ..ơ
    {0x1AF, 0x1B0, SC_Latin, GC_L, NULL}, //  Ư..ư
    {0x1B2, 0x1B4, SC_Latin, GC_L, NULL}, //  Ʋ..ƴ
    {0x1B7, 0x1B7, SC_Latin, GC_Lu, NULL}, //  Ʒ
    {0x1CD, 0x1D4, SC_Latin, GC_L, NULL}, //  Ǎ..ǔ
    {0x1DD, 0x1DD, SC_Latin, GC_Ll, NULL}, //  ǝ
    {0x1E6, 0x1E9, SC_Latin, GC_L, NULL}, //  Ǧ..ǩ
    {0x1EE, 0x1EF, SC_Latin, GC_L, NULL}, //  Ǯ..ǯ
    {0x1F8, 0x1F9, SC_Latin, GC_L, NULL}, //  Ǹ..ǹ
    {0x200, 0x21B, SC_Latin, GC_L, NULL}, //  Ȁ..ț
    {0x234, 0x236, SC_Latin, GC_Ll, NULL}, //  ȴ..ȶ
    {0x244, 0x244, SC_Latin, GC_Lu, NULL}, //  Ʉ
    {0x24C, 0x24D, SC_Latin, GC_L, NULL}, //  Ɍ..ɍ
    {0x250, 0x276, SC_Latin, GC_Ll, NULL}, //  ɐ..ɶ
    {0x278, 0x27B, SC_Latin, GC_Ll, NULL}, //  ɸ..ɻ
    {0x27D, 0x29D, SC_Latin, GC_L, NULL}, //  ɽ..ʝ
    {0x29F, 0x2AF, SC_Latin, GC_Ll, NULL}, //  ʟ..ʯ
    // SPLIT on SCX (prev to U+2C1)
    {0x2B9, 0x2BB, SC_Common, GC_Lm, NULL}, //  ʹ..ʻ
    // SPLIT on SCX (prev to U+2C1)
    {0x2BC, 0x2BC, SC_Common, GC_Lm, "\x05\x07\x08\x02\x99\x1d\x85"}, //Bengali,Cyrillic,Devanagari,Latin,Lisu,Thai,Toto //  ʼ
    {0x2BD, 0x2C1, SC_Common, GC_Lm, NULL}, //  ʽ..ˁ
    // SPLIT on SCX (prev to U+2D1)
    {0x2C6, 0x2C6, SC_Common, GC_Lm, NULL}, //  ˆ
    // SPLIT on SCX (prev to U+2D1)
    {0x2C7, 0x2C7, SC_Common, GC_Lm, "\x06\x02"}, //Bopomofo,Latin //  ˇ
    // SPLIT on SCX (prev to U+2D1)
    {0x2C8, 0x2C8, SC_Common, GC_Lm, NULL}, //  ˈ
    // SPLIT on SCX (prev to U+2D1)
    {0x2C9, 0x2CB, SC_Common, GC_Lm, "\x06\x02"}, //Bopomofo,Latin //  ˉ..ˋ
    // SPLIT on SCX (prev to U+2D1)
    {0x2CC, 0x2CC, SC_Common, GC_Lm, NULL}, //  ˌ
    // SPLIT on SCX (prev to U+2D1)
    {0x2CD, 0x2CD, SC_Common, GC_Lm, "\x02\x99"}, //Latin,Lisu //  ˍ
    {0x2CE, 0x2D1, SC_Common, GC_Lm, NULL}, //  ˎ..ˑ
    {0x2EC, 0x2EC, SC_Common, GC_Lm, NULL}, //  ˬ
    {0x2EE, 0x2EE, SC_Common, GC_Lm, NULL}, //  ˮ
    {0x386, 0x386, SC_Greek, GC_Lu, NULL}, //  Ά
    {0x388, 0x38A, SC_Greek, GC_Lu, NULL}, //  Έ..Ί
    {0x38C, 0x38C, SC_Greek, GC_Lu, NULL}, //  Ό
    {0x38E, 0x3A1, SC_Greek, GC_L, NULL}, //  Ύ..Ρ
    {0x3A3, 0x3CF, SC_Greek, GC_L, NULL}, //  Σ..Ϗ
    {0x3D7, 0x3D7, SC_Greek, GC_Ll, NULL}, //  ϗ
    {0x401, 0x40C, SC_Cyrillic, GC_Lu, NULL}, //  Ё..Ќ
    {0x40E, 0x44F, SC_Cyrillic, GC_L, NULL}, //  Ў..я
    {0x451, 0x45C, SC_Cyrillic, GC_Ll, NULL}, //  ё..ќ
    {0x45E, 0x45F, SC_Cyrillic, GC_Ll, NULL}, //  ў..џ
    {0x490, 0x49B, SC_Cyrillic, GC_L, NULL}, //  Ґ..қ
    {0x49E, 0x4A5, SC_Cyrillic, GC_L, NULL}, //  Ҟ..ҥ
    {0x4A8, 0x4B7, SC_Cyrillic, GC_L, NULL}, //  Ҩ..ҷ
    {0x4BA, 0x4C0, SC_Cyrillic, GC_L, NULL}, //  Һ..Ӏ
    {0x4CF, 0x4D9, SC_Cyrillic, GC_L, NULL}, //  ӏ..ә
    {0x4DC, 0x4E9, SC_Cyrillic, GC_L, NULL}, //  Ӝ..ө
    {0x4EE, 0x4F5, SC_Cyrillic, GC_L, NULL}, //  Ӯ..ӵ
    {0x4F8, 0x4F9, SC_Cyrillic, GC_L, NULL}, //  Ӹ..ӹ
    {0x524, 0x525, SC_Cyrillic, GC_L, NULL}, //  Ԥ..ԥ
    {0x531, 0x556, SC_Armenian, GC_Lu, NULL}, //  Ա..Ֆ
    {0x559, 0x559, SC_Armenian, GC_Lm, NULL}, //  ՙ
    {0x560, 0x586, SC_Armenian, GC_Ll, NULL}, //  ՠ..ֆ
    {0x588, 0x588, SC_Armenian, GC_Ll, NULL}, //  ֈ
    {0x5D0, 0x5EA, SC_Hebrew, GC_Lo, NULL}, //  א..ת
    {0x620, 0x63A, SC_Arabic, GC_Lo, NULL}, //  ؠ..غ
    {0x63D, 0x63D, SC_Arabic, GC_Lo, NULL}, //  ؽ
    {0x641, 0x64A, SC_Arabic, GC_Lo, NULL}, //  ف..ي
    {0x671, 0x672, SC_Arabic, GC_Lo, NULL}, //  ٱ..ٲ
    {0x674, 0x674, SC_Arabic, GC_Lo, NULL}, //  ٴ
    {0x679, 0x68F, SC_Arabic, GC_Lo, NULL}, //  ٹ..ڏ
    {0x691, 0x69A, SC_Arabic, GC_Lo, NULL}, //  ڑ..ښ
    {0x69F, 0x6A0, SC_Arabic, GC_Lo, NULL}, //  ڟ..ڠ
    {0x6A2, 0x6A2, SC_Arabic, GC_Lo, NULL}, //  ڢ
    {0x6A4, 0x6AB, SC_Arabic, GC_Lo, NULL}, //  ڤ..ګ
    {0x6AD, 0x6B1, SC_Arabic, GC_Lo, NULL}, //  ڭ..ڱ
    {0x6B3, 0x6B3, SC_Arabic, GC_Lo, NULL}, //  ڳ
    {0x6B5, 0x6B7, SC_Arabic, GC_Lo, NULL}, //  ڵ..ڷ
    {0x6BA, 0x6BE, SC_Arabic, GC_Lo, NULL}, //  ں..ھ
    {0x6C0, 0x6D3, SC_Arabic, GC_Lo, NULL}, //  ۀ..ۓ
    {0x6D5, 0x6D5, SC_Arabic, GC_Lo, NULL}, //  ە
    {0x6E5, 0x6E6, SC_Arabic, GC_Lm, NULL}, //  ۥ..ۦ
    {0x6EE, 0x6EF, SC_Arabic, GC_Lo, NULL}, //  ۮ..ۯ
    {0x6FF, 0x6FF, SC_Arabic, GC_Lo, NULL}, //  ۿ
    {0x751, 0x752, SC_Arabic, GC_Lo, NULL}, //  ݑ..ݒ
    {0x756, 0x756, SC_Arabic, GC_Lo, NULL}, //  ݖ
    {0x760, 0x760, SC_Arabic, GC_Lo, NULL}, //  ݠ
    {0x762, 0x763, SC_Arabic, GC_Lo, NULL}, //  ݢ..ݣ
    {0x766, 0x768, SC_Arabic, GC_Lo, NULL}, //  ݦ..ݨ
    {0x76A, 0x76A, SC_Arabic, GC_Lo, NULL}, //  ݪ
    {0x76E, 0x771, SC_Arabic, GC_Lo, NULL}, //  ݮ..ݱ
    {0x780, 0x7A5, SC_Thaana, GC_Lo, NULL}, //  ހ..ޥ
    {0x7B1, 0x7B1, SC_Thaana, GC_Lo, NULL}, //  ޱ
    {0x870, 0x887, SC_Arabic, GC_Lo, NULL}, //  ࡰ..ࢇ
    {0x88F, 0x88F, SC_Arabic, GC_Lo, NULL}, //  ࢏
    {0x8A0, 0x8A0, SC_Arabic, GC_Lo, NULL}, //  ࢠ
    {0x8A2, 0x8A9, SC_Arabic, GC_Lo, NULL}, //  ࢢ..ࢩ
    {0x8BB, 0x8C2, SC_Arabic, GC_Lo, NULL}, //  ࢻ..ࣂ
    {0x8C7, 0x8C7, SC_Arabic, GC_Lo, NULL}, //  ࣇ
    {0x8C9, 0x8C9, SC_Arabic, GC_Lm, NULL}, //  ࣉ
    {0x905, 0x90B, SC_Devanagari, GC_Lo, NULL}, //  अ..ऋ
    {0x90D, 0x928, SC_Devanagari, GC_Lo, NULL}, //  ऍ..न
    {0x92A, 0x933, SC_Devanagari, GC_Lo, NULL}, //  प..ळ
    {0x935, 0x939, SC_Devanagari, GC_Lo, NULL}, //  व..ह
    {0x950, 0x950, SC_Devanagari, GC_Lo, NULL}, //  ॐ
    {0x972, 0x977, SC_Devanagari, GC_Lo, NULL}, //  ॲ..ॷ
    {0x97B, 0x97F, SC_Devanagari, GC_Lo, NULL}, //  ॻ..ॿ
    {0x985, 0x98B, SC_Bengali, GC_Lo, NULL}, //  অ..ঋ
    {0x98F, 0x990, SC_Bengali, GC_Lo, NULL}, //  এ..ঐ
    {0x993, 0x9A8, SC_Bengali, GC_Lo, NULL}, //  ও..ন
    {0x9AA, 0x9B0, SC_Bengali, GC_Lo, NULL}, //  প..র
    {0x9B2, 0x9B2, SC_Bengali, GC_Lo, NULL}, //  ল
    {0x9B6, 0x9B9, SC_Bengali, GC_Lo, NULL}, //  শ..হ
    {0x9CE, 0x9CE, SC_Bengali, GC_Lo, NULL}, //  ৎ
    {0x9F0, 0x9F1, SC_Bengali, GC_Lo, NULL}, //  ৰ..ৱ
    {0xA05, 0xA0A, SC_Gurmukhi, GC_Lo, NULL}, //  ਅ..ਊ
    {0xA0F, 0xA10, SC_Gurmukhi, GC_Lo, NULL}, //  ਏ..ਐ
    {0xA13, 0xA28, SC_Gurmukhi, GC_Lo, NULL}, //  ਓ..ਨ
    {0xA2A, 0xA30, SC_Gurmukhi, GC_Lo, NULL}, //  ਪ..ਰ
    {0xA32, 0xA32, SC_Gurmukhi, GC_Lo, NULL}, //  ਲ
    {0xA35, 0xA35, SC_Gurmukhi, GC_Lo, NULL}, //  ਵ
    {0xA38, 0xA39, SC_Gurmukhi, GC_Lo, NULL}, //  ਸ..ਹ
    {0xA5C, 0xA5C, SC_Gurmukhi, GC_Lo, NULL}, //  ੜ
    {0xA74, 0xA74, SC_Gurmukhi, GC_Lo, NULL}, //  ੴ
    {0xA85, 0xA8D, SC_Gujarati, GC_Lo, NULL}, //  અ..ઍ
    {0xA8F, 0xA91, SC_Gujarati, GC_Lo, NULL}, //  એ..ઑ
    {0xA93, 0xAA8, SC_Gujarati, GC_Lo, NULL}, //  ઓ..ન
    {0xAAA, 0xAB0, SC_Gujarati, GC_Lo, NULL}, //  પ..ર
    {0xAB2, 0xAB3, SC_Gujarati, GC_Lo, NULL}, //  લ..ળ
    {0xAB5, 0xAB9, SC_Gujarati, GC_Lo, NULL}, //  વ..હ
    {0xAD0, 0xAD0, SC_Gujarati, GC_Lo, NULL}, //  ૐ
    {0xB05, 0xB0B, SC_Oriya, GC_Lo, NULL}, //  ଅ..ଋ
    {0xB0F, 0xB10, SC_Oriya, GC_Lo, NULL}, //  ଏ..ଐ
    {0xB13, 0xB28, SC_Oriya, GC_Lo, NULL}, //  ଓ..ନ
    {0xB2A, 0xB30, SC_Oriya, GC_Lo, NULL}, //  ପ..ର
    {0xB32, 0xB33, SC_Oriya, GC_Lo, NULL}, //  ଲ..ଳ
    {0xB36, 0xB39, SC_Oriya, GC_Lo, NULL}, //  ଶ..ହ
    {0xB5F, 0xB5F, SC_Oriya, GC_Lo, NULL}, //  ୟ
    {0xB71, 0xB71, SC_Oriya, GC_Lo, NULL}, //  ୱ
    {0xB83, 0xB83, SC_Tamil, GC_Lo, NULL}, //  ஃ
    {0xB85, 0xB8A, SC_Tamil, GC_Lo, NULL}, //  அ..ஊ
    {0xB8E, 0xB90, SC_Tamil, GC_Lo, NULL}, //  எ..ஐ
    {0xB92, 0xB95, SC_Tamil, GC_Lo, NULL}, //  ஒ..க
    {0xB99, 0xB9A, SC_Tamil, GC_Lo, NULL}, //  ங..ச
    {0xB9C, 0xB9C, SC_Tamil, GC_Lo, NULL}, //  ஜ
    {0xB9E, 0xB9F, SC_Tamil, GC_Lo, NULL}, //  ஞ..ட
    {0xBA3, 0xBA4, SC_Tamil, GC_Lo, NULL}, //  ண..த
    {0xBA8, 0xBAA, SC_Tamil, GC_Lo, NULL}, //  ந..ப
    {0xBAE, 0xBB9, SC_Tamil, GC_Lo, NULL}, //  ம..ஹ
    {0xBD0, 0xBD0, SC_Tamil, GC_Lo, NULL}, //  ௐ
    {0xC05, 0xC0B, SC_Telugu, GC_Lo, NULL}, //  అ..ఋ
    {0xC0E, 0xC10, SC_Telugu, GC_Lo, NULL}, //  ఎ..ఐ
    {0xC12, 0xC28, SC_Telugu, GC_Lo, NULL}, //  ఒ..న
    {0xC2A, 0xC30, SC_Telugu, GC_Lo, NULL}, //  ప..ర
    {0xC32, 0xC33, SC_Telugu, GC_Lo, NULL}, //  ల..ళ
    {0xC35, 0xC39, SC_Telugu, GC_Lo, NULL}, //  వ..హ
    {0xC85, 0xC8B, SC_Kannada, GC_Lo, NULL}, //  ಅ..ಋ
    {0xC8E, 0xC90, SC_Kannada, GC_Lo, NULL}, //  ಎ..ಐ
    {0xC92, 0xCA8, SC_Kannada, GC_Lo, NULL}, //  ಒ..ನ
    {0xCAA, 0xCB0, SC_Kannada, GC_Lo, NULL}, //  ಪ..ರ
    {0xCB2, 0xCB3, SC_Kannada, GC_Lo, NULL}, //  ಲ..ಳ
    {0xCB5, 0xCB9, SC_Kannada, GC_Lo, NULL}, //  ವ..ಹ
    {0xD05, 0xD0B, SC_Malayalam, GC_Lo, NULL}, //  അ..ഋ
    {0xD0E, 0xD10, SC_Malayalam, GC_Lo, NULL}, //  എ..ഐ
    {0xD12, 0xD28, SC_Malayalam, GC_Lo, NULL}, //  ഒ..ന
    {0xD2A, 0xD39, SC_Malayalam, GC_Lo, NULL}, //  പ..ഹ
    {0xD7A, 0xD7F, SC_Malayalam, GC_Lo, NULL}, //  ൺ..ൿ
    {0xD85, 0xD8D, SC_Sinhala, GC_Lo, NULL}, //  අ..ඍ
    {0xD91, 0xD96, SC_Sinhala, GC_Lo, NULL}, //  එ..ඖ
    {0xD9A, 0xD9D, SC_Sinhala, GC_Lo, NULL}, //  ක..ඝ
    {0xD9F, 0xDB1, SC_Sinhala, GC_Lo, NULL}, //  ඟ..න
    {0xDB3, 0xDBB, SC_Sinhala, GC_Lo, NULL}, //  ඳ..ර
    {0xDBD, 0xDBD, SC_Sinhala, GC_Lo, NULL}, //  ල
    {0xDC0, 0xDC6, SC_Sinhala, GC_Lo, NULL}, //  ව..ෆ
    {0xE01, 0xE30, SC_Thai, GC_Lo, NULL}, //  ก..ะ
    {0xE32, 0xE32, SC_Thai, GC_Lo, NULL}, //  า
    {0xE40, 0xE46, SC_Thai, GC_L, NULL}, //  เ..ๆ
    {0xE81, 0xE82, SC_Lao, GC_Lo, NULL}, //  ກ..ຂ
    {0xE84, 0xE84, SC_Lao, GC_Lo, NULL}, //  ຄ
    {0xE87, 0xE88, SC_Lao, GC_Lo, NULL}, //  ງ..ຈ
    {0xE8A, 0xE8A, SC_Lao, GC_Lo, NULL}, //  ຊ
    {0xE8D, 0xE8D, SC_Lao, GC_Lo, NULL}, //  ຍ
    {0xE94, 0xE97, SC_Lao, GC_Lo, NULL}, //  ດ..ທ
    {0xE99, 0xE9F, SC_Lao, GC_Lo, NULL}, //  ນ..ຟ
    {0xEA1, 0xEA3, SC_Lao, GC_Lo, NULL}, //  ມ..ຣ
    {0xEA5, 0xEA5, SC_Lao, GC_Lo, NULL}, //  ລ
    {0xEA7, 0xEA7, SC_Lao, GC_Lo, NULL}, //  ວ
    {0xEAA, 0xEAB, SC_Lao, GC_Lo, NULL}, //  ສ..ຫ
    {0xEAD, 0xEB0, SC_Lao, GC_Lo, NULL}, //  ອ..ະ
    {0xEB2, 0xEB2, SC_Lao, GC_Lo, NULL}, //  າ
    {0xEBD, 0xEBD, SC_Lao, GC_Lo, NULL}, //  ຽ
    {0xEC0, 0xEC4, SC_Lao, GC_Lo, NULL}, //  ເ..ໄ
    {0xEC6, 0xEC6, SC_Lao, GC_Lm, NULL}, //  ໆ
    {0xF00, 0xF00, SC_Tibetan, GC_Lo, NULL}, //  ༀ
    {0xF40, 0xF42, SC_Tibetan, GC_Lo, NULL}, //  ཀ..ག
    {0xF44, 0xF47, SC_Tibetan, GC_Lo, NULL}, //  ང..ཇ
    {0xF49, 0xF4C, SC_Tibetan, GC_Lo, NULL}, //  ཉ..ཌ
    {0xF4E, 0xF51, SC_Tibetan, GC_Lo, NULL}, //  ཎ..ད
    {0xF53, 0xF56, SC_Tibetan, GC_Lo, NULL}, //  ན..བ
    {0xF58, 0xF5B, SC_Tibetan, GC_Lo, NULL}, //  མ..ཛ
    {0xF5D, 0xF68, SC_Tibetan, GC_Lo, NULL}, //  ཝ..ཨ
    {0x1000, 0x102A, SC_Myanmar, GC_Lo, NULL}, //  က..ဪ
    {0x103F, 0x103F, SC_Myanmar, GC_Lo, NULL}, //  ဿ
    {0x105A, 0x105D, SC_Myanmar, GC_Lo, NULL}, //  ၚ..ၝ
    {0x1061, 0x1061, SC_Myanmar, GC_Lo, NULL}, //  ၡ
    {0x1075, 0x1081, SC_Myanmar, GC_Lo, NULL}, //  ၵ..ႁ
    {0x10C7, 0x10C7, SC_Georgian, GC_Lu, NULL}, //  Ⴧ
    {0x10CD, 0x10CD, SC_Georgian, GC_Lu, NULL}, //  Ⴭ
    {0x10D0, 0x10F0, SC_Georgian, GC_Ll, NULL}, //  ა..ჰ
    {0x1200, 0x1206, SC_Ethiopic, GC_Lo, NULL}, //  ሀ..ሆ
    {0x1208, 0x1248, SC_Ethiopic, GC_Lo, NULL}, //  ለ..ቈ
    {0x124A, 0x124D, SC_Ethiopic, GC_Lo, NULL}, //  ቊ..ቍ
    {0x1250, 0x1256, SC_Ethiopic, GC_Lo, NULL}, //  ቐ..ቖ
    {0x1258, 0x1258, SC_Ethiopic, GC_Lo, NULL}, //  ቘ
    {0x125A, 0x125D, SC_Ethiopic, GC_Lo, NULL}, //  ቚ..ቝ
    {0x1260, 0x1286, SC_Ethiopic, GC_Lo, NULL}, //  በ..ኆ
    {0x1288, 0x1288, SC_Ethiopic, GC_Lo, NULL}, //  ኈ
    {0x128A, 0x128D, SC_Ethiopic, GC_Lo, NULL}, //  ኊ..ኍ
    {0x1290, 0x12AE, SC_Ethiopic, GC_Lo, NULL}, //  ነ..ኮ
    {0x12B0, 0x12B0, SC_Ethiopic, GC_Lo, NULL}, //  ኰ
    {0x12B2, 0x12B5, SC_Ethiopic, GC_Lo, NULL}, //  ኲ..ኵ
    {0x12B8, 0x12BE, SC_Ethiopic, GC_Lo, NULL}, //  ኸ..ኾ
    {0x12C0, 0x12C0, SC_Ethiopic, GC_Lo, NULL}, //  ዀ
    {0x12C2, 0x12C5, SC_Ethiopic, GC_Lo, NULL}, //  ዂ..ዅ
    {0x12C8, 0x12D6, SC_Ethiopic, GC_Lo, NULL}, //  ወ..ዖ
    {0x12D8, 0x12F7, SC_Ethiopic, GC_Lo, NULL}, //  ዘ..ዷ
    {0x1300, 0x130E, SC_Ethiopic, GC_Lo, NULL}, //  ጀ..ጎ
    {0x1310, 0x1310, SC_Ethiopic, GC_Lo, NULL}, //  ጐ
    {0x1312, 0x1315, SC_Ethiopic, GC_Lo, NULL}, //  ጒ..ጕ
    {0x1318, 0x131E, SC_Ethiopic, GC_Lo, NULL}, //  ጘ..ጞ
    {0x1320, 0x1346, SC_Ethiopic, GC_Lo, NULL}, //  ጠ..ፆ
    {0x1348, 0x1359, SC_Ethiopic, GC_Lo, NULL}, //  ፈ..ፙ
    {0x1780, 0x179C, SC_Khmer, GC_Lo, NULL}, //  ក..វ
    {0x179F, 0x17A2, SC_Khmer, GC_Lo, NULL}, //  ស..អ
    {0x17A5, 0x17A7, SC_Khmer, GC_Lo, NULL}, //  ឥ..ឧ
    {0x17AA, 0x17B3, SC_Khmer, GC_Lo, NULL}, //  ឪ..ឳ
    {0x1C90, 0x1CBA, SC_Georgian, GC_Lu, NULL}, //  Ა..Ჺ
    {0x1CBD, 0x1CBF, SC_Georgian, GC_Lu, NULL}, //  Ჽ..Ჿ
    {0x1D00, 0x1D25, SC_Latin, GC_Ll, NULL}, //  ᴀ..ᴥ
    {0x1D27, 0x1D2A, SC_Greek, GC_Ll, NULL}, //  ᴧ..ᴪ
    {0x1D2F, 0x1D2F, SC_Latin, GC_Lm, NULL}, //  ᴯ
    {0x1D3B, 0x1D3B, SC_Latin, GC_Lm, NULL}, //  ᴻ
    {0x1D4E, 0x1D4E, SC_Latin, GC_Lm, NULL}, //  ᵎ
    {0x1D6B, 0x1D77, SC_Latin, GC_Ll, NULL}, //  ᵫ..ᵷ
    {0x1D79, 0x1D9A, SC_Latin, GC_Ll, NULL}, //  ᵹ..ᶚ
    {0x1E00, 0x1E01, SC_Latin, GC_L, NULL}, //  Ḁ..ḁ
    {0x1E0C, 0x1E0D, SC_Latin, GC_L, NULL}, //  Ḍ..ḍ
    {0x1E12, 0x1E13, SC_Latin, GC_L, NULL}, //  Ḓ..ḓ
    {0x1E18, 0x1E1B, SC_Latin, GC_L, NULL}, //  Ḙ..ḛ
    {0x1E20, 0x1E21, SC_Latin, GC_L, NULL}, //  Ḡ..ḡ
    {0x1E24, 0x1E25, SC_Latin, GC_L, NULL}, //  Ḥ..ḥ
    {0x1E2A, 0x1E2D, SC_Latin, GC_L, NULL}, //  Ḫ..ḭ
    {0x1E36, 0x1E37, SC_Latin, GC_L, NULL}, //  Ḷ..ḷ
    {0x1E3C, 0x1E3F, SC_Latin, GC_L, NULL}, //  Ḽ..ḿ
    {0x1E42, 0x1E4B, SC_Latin, GC_L, NULL}, //  Ṃ..ṋ
    {0x1E5A, 0x1E5B, SC_Latin, GC_L, NULL}, //  Ṛ..ṛ
    {0x1E62, 0x1E63, SC_Latin, GC_L, NULL}, //  Ṣ..ṣ
    {0x1E6C, 0x1E6D, SC_Latin, GC_L, NULL}, //  Ṭ..ṭ
    {0x1E70, 0x1E77, SC_Latin, GC_L, NULL}, //  Ṱ..ṷ
    {0x1E8C, 0x1E8D, SC_Latin, GC_L, NULL}, //  Ẍ..ẍ
    {0x1E92, 0x1E93, SC_Latin, GC_L, NULL}, //  Ẓ..ẓ
    {0x1E9C, 0x1EFF, SC_Latin, GC_L, NULL}, //  ẜ..ỿ
    {0x1FA0, 0x1FAF, SC_Greek, GC_L, NULL}, //  ᾠ..ᾯ
    {0x1FB2, 0x1FB4, SC_Greek, GC_Ll, NULL}, //  ᾲ..ᾴ
    {0x1FEC, 0x1FEC, SC_Greek, GC_Lu, NULL}, //  Ῥ
    {0x2118, 0x2118, SC_Common, GC_Sm, NULL}, //  ℘
    {0x212E, 0x212E, SC_Common, GC_So, NULL}, //  ℮
    {0x2C60, 0x2C67, SC_Latin, GC_L, NULL}, //  Ⱡ..Ⱨ
    {0x2C77, 0x2C7B, SC_Latin, GC_Ll, NULL}, //  ⱷ..ⱻ
    {0x2D27, 0x2D27, SC_Georgian, GC_Ll, NULL}, //  ⴧ
    {0x2D2D, 0x2D2D, SC_Georgian, GC_Ll, NULL}, //  ⴭ
    {0x3005, 0x3005, SC_Han, GC_Lm, NULL}, //  々
    {0x3007, 0x3007, SC_Han, GC_Nl, NULL}, //  〇
    {0x3021, 0x3029, SC_Han, GC_Nl, NULL}, //  〡..〩
    {0x3031, 0x3035, SC_Common, GC_Lm, "\x11\x12"}, //Hiragana,Katakana //  〱..〵
    {0x303B, 0x303B, SC_Han, GC_Lm, NULL}, //  〻
    {0x3041, 0x3096, SC_Hiragana, GC_Lo, NULL}, //  ぁ..ゖ
    {0x309D, 0x309E, SC_Hiragana, GC_Lm, NULL}, //  ゝ..ゞ
    {0x30A1, 0x30FA, SC_Katakana, GC_Lo, NULL}, //  ァ..ヺ
    {0x30FC, 0x30FC, SC_Common, GC_Lm, "\x11\x12"}, //Hiragana,Katakana //  ー
    {0x30FE, 0x30FE, SC_Katakana, GC_Lm, NULL}, //  ヾ
    {0x3447, 0x3447, SC_Han, GC_Lo, NULL}, //  㑇
    {0x3473, 0x3473, SC_Han, GC_Lo, NULL}, //  㑳
    {0x34E4, 0x34E4, SC_Han, GC_Lo, NULL}, //  㓤
    {0x3577, 0x3577, SC_Han, GC_Lo, NULL}, //  㕷
    {0x359E, 0x359E, SC_Han, GC_Lo, NULL}, //  㖞
    {0x35A1, 0x35A1, SC_Han, GC_Lo, NULL}, //  㖡
    {0x35AD, 0x35AD, SC_Han, GC_Lo, NULL}, //  㖭
    {0x35BF, 0x35BF, SC_Han, GC_Lo, NULL}, //  㖿
    {0x35CE, 0x35CE, SC_Han, GC_Lo, NULL}, //  㗎
    {0x35F3, 0x35F3, SC_Han, GC_Lo, NULL}, //  㗳
    {0x35FE, 0x35FE, SC_Han, GC_Lo, NULL}, //  㗾
    {0x360E, 0x360E, SC_Han, GC_Lo, NULL}, //  㘎
    {0x361A, 0x361A, SC_Han, GC_Lo, NULL}, //  㘚
    {0x3918, 0x3918, SC_Han, GC_Lo, NULL}, //  㤘
    {0x3960, 0x3960, SC_Han, GC_Lo, NULL}, //  㥠
    {0x396E, 0x396E, SC_Han, GC_Lo, NULL}, //  㥮
    {0x39CF, 0x39D0, SC_Han, GC_Lo, NULL}, //  㧏..㧐
    {0x39DB, 0x39DB, SC_Han, GC_Lo, NULL}, //  㧛
    {0x39DF, 0x39DF, SC_Han, GC_Lo, NULL}, //  㧟
    {0x39F8, 0x39F8, SC_Han, GC_Lo, NULL}, //  㧸
    {0x39FE, 0x39FE, SC_Han, GC_Lo, NULL}, //  㧾
    {0x3A18, 0x3A18, SC_Han, GC_Lo, NULL}, //  㨘
    {0x3A52, 0x3A52, SC_Han, GC_Lo, NULL}, //  㩒
    {0x3A5C, 0x3A5C, SC_Han, GC_Lo, NULL}, //  㩜
    {0x3A67, 0x3A67, SC_Han, GC_Lo, NULL}, //  㩧
    {0x3A73, 0x3A73, SC_Han, GC_Lo, NULL}, //  㩳
    {0x3B39, 0x3B39, SC_Han, GC_Lo, NULL}, //  㬹
    {0x3B4E, 0x3B4E, SC_Han, GC_Lo, NULL}, //  㭎
    {0x3BA3, 0x3BA3, SC_Han, GC_Lo, NULL}, //  㮣
    {0x3C6E, 0x3C6E, SC_Han, GC_Lo, NULL}, //  㱮
    {0x3CE0, 0x3CE0, SC_Han, GC_Lo, NULL}, //  㳠
    {0x3DE7, 0x3DE7, SC_Han, GC_Lo, NULL}, //  㷧
    {0x3DEB, 0x3DEB, SC_Han, GC_Lo, NULL}, //  㷫
    {0x3E74, 0x3E74, SC_Han, GC_Lo, NULL}, //  㹴
    {0x3ED0, 0x3ED0, SC_Han, GC_Lo, NULL}, //  㻐
    {0x4056, 0x4056, SC_Han, GC_Lo, NULL}, //  䁖
    {0x4065, 0x4065, SC_Han, GC_Lo, NULL}, //  䁥
    {0x406A, 0x406A, SC_Han, GC_Lo, NULL}, //  䁪
    {0x40BB, 0x40BB, SC_Han, GC_Lo, NULL}, //  䂻
    {0x40DF, 0x40DF, SC_Han, GC_Lo, NULL}, //  䃟
    {0x4137, 0x4137, SC_Han, GC_Lo, NULL}, //  䄷
    {0x415F, 0x415F, SC_Han, GC_Lo, NULL}, //  䅟
    {0x4337, 0x4337, SC_Han, GC_Lo, NULL}, //  䌷
    {0x43AC, 0x43AC, SC_Han, GC_Lo, NULL}, //  䎬
    {0x43B1, 0x43B1, SC_Han, GC_Lo, NULL}, //  䎱
    {0x43D3, 0x43D3, SC_Han, GC_Lo, NULL}, //  䏓
    {0x43DD, 0x43DD, SC_Han, GC_Lo, NULL}, //  䏝
    {0x4443, 0x4443, SC_Han, GC_Lo, NULL}, //  䑃
    {0x44D6, 0x44D6, SC_Han, GC_Lo, NULL}, //  䓖
    {0x44EA, 0x44EA, SC_Han, GC_Lo, NULL}, //  䓪
    {0x4606, 0x4606, SC_Han, GC_Lo, NULL}, //  䘆
    {0x464C, 0x464C, SC_Han, GC_Lo, NULL}, //  䙌
    {0x4661, 0x4661, SC_Han, GC_Lo, NULL}, //  䙡
    {0x4723, 0x4723, SC_Han, GC_Lo, NULL}, //  䜣
    {0x4729, 0x4729, SC_Han, GC_Lo, NULL}, //  䜩
    {0x477C, 0x477C, SC_Han, GC_Lo, NULL}, //  䝼
    {0x478D, 0x478D, SC_Han, GC_Lo, NULL}, //  䞍
    {0x47F4, 0x47F4, SC_Han, GC_Lo, NULL}, //  䟴
    {0x4882, 0x4882, SC_Han, GC_Lo, NULL}, //  䢂
    {0x4947, 0x4947, SC_Han, GC_Lo, NULL}, //  䥇
    {0x497A, 0x497A, SC_Han, GC_Lo, NULL}, //  䥺
    {0x497D, 0x497D, SC_Han, GC_Lo, NULL}, //  䥽
    {0x4982, 0x4983, SC_Han, GC_Lo, NULL}, //  䦂..䦃
    {0x4985, 0x4986, SC_Han, GC_Lo, NULL}, //  䦅..䦆
    {0x499B, 0x499B, SC_Han, GC_Lo, NULL}, //  䦛
    {0x499F, 0x499F, SC_Han, GC_Lo, NULL}, //  䦟
    {0x49B6, 0x49B7, SC_Han, GC_Lo, NULL}, //  䦶..䦷
    {0x4A12, 0x4A12, SC_Han, GC_Lo, NULL}, //  䨒
    {0x4AB8, 0x4AB8, SC_Han, GC_Lo, NULL}, //  䪸
    {0x4C77, 0x4C77, SC_Han, GC_Lo, NULL}, //  䱷
    {0x4C7D, 0x4C7D, SC_Han, GC_Lo, NULL}, //  䱽
    {0x4C81, 0x4C81, SC_Han, GC_Lo, NULL}, //  䲁
    {0x4C85, 0x4C85, SC_Han, GC_Lo, NULL}, //  䲅
    {0x4C9D, 0x4CA3, SC_Han, GC_Lo, NULL}, //  䲝..䲣
    {0x4D13, 0x4D19, SC_Han, GC_Lo, NULL}, //  䴓..䴙
    {0x4DAE, 0x4DAE, SC_Han, GC_Lo, NULL}, //  䶮
    {0x4E00, 0x4E11, SC_Han, GC_Lo, NULL}, //  一..丑
    {0x4E13, 0x4E28, SC_Han, GC_Lo, NULL}, //  专..丨
    {0x4E2A, 0x4E67, SC_Han, GC_Lo, NULL}, //  个..乧
    {0x4E69, 0x4E78, SC_Han, GC_Lo, NULL}, //  乩..乸
    {0x4E7A, 0x4E95, SC_Han, GC_Lo, NULL}, //  乺..井
    {0x4E97, 0x4EA2, SC_Han, GC_Lo, NULL}, //  亗..亢
    {0x4EA4, 0x4EBB, SC_Han, GC_Lo, NULL}, //  交..亻
    {0x4EBD, 0x4ECB, SC_Han, GC_Lo, NULL}, //  亽..介
    {0x4ECD, 0x4EE6, SC_Han, GC_Lo, NULL}, //  仍..仦
    {0x4EE8, 0x4EF7, SC_Han, GC_Lo, NULL}, //  仨..价
    {0x4EFB, 0x4EFB, SC_Han, GC_Lo, NULL}, //  任
    {0x4EFD, 0x4EFD, SC_Han, GC_Lo, NULL}, //  份
    {0x4EFF, 0x4F06, SC_Han, GC_Lo, NULL}, //  仿..伆
    {0x4F08, 0x4F15, SC_Han, GC_Lo, NULL}, //  伈..伕
    {0x4F17, 0x4F27, SC_Han, GC_Lo, NULL}, //  众..伧
    {0x4F29, 0x4F30, SC_Han, GC_Lo, NULL}, //  伩..估
    {0x4F32, 0x4F34, SC_Han, GC_Lo, NULL}, //  伲..伴
    {0x4F36, 0x4F36, SC_Han, GC_Lo, NULL}, //  伶
    {0x4F38, 0x4F3F, SC_Han, GC_Lo, NULL}, //  伸..伿
    {0x4F41, 0x4F43, SC_Han, GC_Lo, NULL}, //  佁..佃
    {0x4F45, 0x4F70, SC_Han, GC_Lo, NULL}, //  佅..佰
    {0x4F72, 0x4F8B, SC_Han, GC_Lo, NULL}, //  佲..例
    {0x4F8D, 0x4F8D, SC_Han, GC_Lo, NULL}, //  侍
    {0x4F8F, 0x4FA1, SC_Han, GC_Lo, NULL}, //  侏..価
    {0x4FA3, 0x4FBC, SC_Han, GC_Lo, NULL}, //  侣..侼
    {0x4FBE, 0x4FC5, SC_Han, GC_Lo, NULL}, //  侾..俅
    {0x4FC7, 0x4FC7, SC_Han, GC_Lo, NULL}, //  俇
    {0x4FC9, 0x4FCB, SC_Han, GC_Lo, NULL}, //  俉..俋
    {0x4FCD, 0x4FE1, SC_Han, GC_Lo, NULL}, //  俍..信
    {0x4FE3, 0x4FFB, SC_Han, GC_Lo, NULL}, //  俣..俻
    {0x4FFE, 0x500F, SC_Han, GC_Lo, NULL}, //  俾..倏
    {0x5011, 0x5033, SC_Han, GC_Lo, NULL}, //  們..倳
    {0x5035, 0x5037, SC_Han, GC_Lo, NULL}, //  倵..倷
    {0x5039, 0x503C, SC_Han, GC_Lo, NULL}, //  倹..值
    {0x503E, 0x5041, SC_Han, GC_Lo, NULL}, //  倾..偁
    {0x5043, 0x5051, SC_Han, GC_Lo, NULL}, //  偃..偑
    {0x5053, 0x5057, SC_Han, GC_Lo, NULL}, //  偓..偗
    {0x5059, 0x507B, SC_Han, GC_Lo, NULL}, //  偙..偻
    {0x507D, 0x5080, SC_Han, GC_Lo, NULL}, //  偽..傀
    {0x5082, 0x5092, SC_Han, GC_Lo, NULL}, //  傂..傒
    {0x5094, 0x5096, SC_Han, GC_Lo, NULL}, //  傔..傖
    {0x5098, 0x509E, SC_Han, GC_Lo, NULL}, //  傘..傞
    {0x50A2, 0x50B8, SC_Han, GC_Lo, NULL}, //  傢..傸
    {0x50BA, 0x50C2, SC_Han, GC_Lo, NULL}, //  傺..僂
    {0x50C4, 0x50D7, SC_Han, GC_Lo, NULL}, //  僄..僗
    {0x50D9, 0x50DE, SC_Han, GC_Lo, NULL}, //  僙..僞
    {0x50E0, 0x50E0, SC_Han, GC_Lo, NULL}, //  僠
    {0x50E3, 0x50EA, SC_Han, GC_Lo, NULL}, //  僣..僪
    {0x50EC, 0x50F3, SC_Han, GC_Lo, NULL}, //  僬..僳
    {0x50F5, 0x50F6, SC_Han, GC_Lo, NULL}, //  僵..僶
    {0x50F8, 0x511A, SC_Han, GC_Lo, NULL}, //  僸..儚
    {0x511C, 0x5127, SC_Han, GC_Lo, NULL}, //  儜..儧
    {0x5129, 0x512A, SC_Han, GC_Lo, NULL}, //  儩..優
    {0x512C, 0x5141, SC_Han, GC_Lo, NULL}, //  儬..允
    {0x5143, 0x5149, SC_Han, GC_Lo, NULL}, //  元..光
    {0x514B, 0x514E, SC_Han, GC_Lo, NULL}, //  克..兎
    {0x5150, 0x5152, SC_Han, GC_Lo, NULL}, //  児..兒
    {0x5154, 0x5157, SC_Han, GC_Lo, NULL}, //  兔..兗
    {0x5159, 0x515F, SC_Han, GC_Lo, NULL}, //  兙..兟
    {0x5161, 0x5163, SC_Han, GC_Lo, NULL}, //  兡..兣
    {0x5165, 0x5171, SC_Han, GC_Lo, NULL}, //  入..共
    {0x5173, 0x517D, SC_Han, GC_Lo, NULL}, //  关..兽
    {0x517F, 0x5182, SC_Han, GC_Lo, NULL}, //  兿..冂
    {0x5185, 0x518D, SC_Han, GC_Lo, NULL}, //  内..再
    {0x518F, 0x51A0, SC_Han, GC_Lo, NULL}, //  冏..冠
    {0x51A2, 0x51A2, SC_Han, GC_Lo, NULL}, //  冢
    {0x51A4, 0x51AC, SC_Han, GC_Lo, NULL}, //  冤..冬
    {0x51AE, 0x51B7, SC_Han, GC_Lo, NULL}, //  冮..冷
    {0x51B9, 0x51B9, SC_Han, GC_Lo, NULL}, //  冹
    {0x51BB, 0x51C1, SC_Han, GC_Lo, NULL}, //  冻..凁
    {0x51C3, 0x51D1, SC_Han, GC_Lo, NULL}, //  凃..凑
    {0x51D4, 0x51DE, SC_Han, GC_Lo, NULL}, //  凔..凞
    {0x51E0, 0x51EB, SC_Han, GC_Lo, NULL}, //  几..凫
    {0x51ED, 0x51ED, SC_Han, GC_Lo, NULL}, //  凭
    {0x51EF, 0x51F1, SC_Han, GC_Lo, NULL}, //  凯..凱
    {0x51F3, 0x5252, SC_Han, GC_Lo, NULL}, //  凳..剒
    {0x5254, 0x5265, SC_Han, GC_Lo, NULL}, //  剔..剥
    {0x5267, 0x5278, SC_Han, GC_Lo, NULL}, //  剧..剸
    {0x527A, 0x5284, SC_Han, GC_Lo, NULL}, //  剺..劄
    {0x5286, 0x528D, SC_Han, GC_Lo, NULL}, //  劆..劍
    {0x528F, 0x52C3, SC_Han, GC_Lo, NULL}, //  劏..勃
    {0x52C5, 0x52C7, SC_Han, GC_Lo, NULL}, //  勅..勇
    {0x52C9, 0x52CB, SC_Han, GC_Lo, NULL}, //  勉..勋
    {0x52CD, 0x52CD, SC_Han, GC_Lo, NULL}, //  勍
    {0x52CF, 0x52D0, SC_Han, GC_Lo, NULL}, //  勏..勐
    {0x52D2, 0x52D3, SC_Han, GC_Lo, NULL}, //  勒..勓
    {0x52D5, 0x52E0, SC_Han, GC_Lo, NULL}, //  動..勠
    {0x52E2, 0x52E4, SC_Han, GC_Lo, NULL}, //  勢..勤
    {0x52E6, 0x52ED, SC_Han, GC_Lo, NULL}, //  勦..勭
    {0x52EF, 0x5302, SC_Han, GC_Lo, NULL}, //  勯..匂
    {0x5305, 0x5317, SC_Han, GC_Lo, NULL}, //  包..北
    {0x5319, 0x531A, SC_Han, GC_Lo, NULL}, //  匙..匚
    {0x531C, 0x531D, SC_Han, GC_Lo, NULL}, //  匜..匝
    {0x531F, 0x5326, SC_Han, GC_Lo, NULL}, //  匟..匦
    {0x5328, 0x5328, SC_Han, GC_Lo, NULL}, //  匨
    {0x532A, 0x5331, SC_Han, GC_Lo, NULL}, //  匪..匱
    {0x5333, 0x5334, SC_Han, GC_Lo, NULL}, //  匳..匴
    {0x5337, 0x5341, SC_Han, GC_Lo, NULL}, //  匷..十
    {0x5343, 0x535A, SC_Han, GC_Lo, NULL}, //  千..博
    {0x535C, 0x535C, SC_Han, GC_Lo, NULL}, //  卜
    {0x535E, 0x5369, SC_Han, GC_Lo, NULL}, //  卞..卩
    {0x536B, 0x536C, SC_Han, GC_Lo, NULL}, //  卫..卬
    {0x536E, 0x537F, SC_Han, GC_Lo, NULL}, //  卮..卿
    {0x5381, 0x53A0, SC_Han, GC_Lo, NULL}, //  厁..厠
    {0x53A2, 0x53A9, SC_Han, GC_Lo, NULL}, //  厢..厩
    {0x53AC, 0x53AE, SC_Han, GC_Lo, NULL}, //  厬..厮
    {0x53B0, 0x53B9, SC_Han, GC_Lo, NULL}, //  厰..厹
    {0x53BB, 0x53C4, SC_Han, GC_Lo, NULL}, //  去..叄
    {0x53C6, 0x53CE, SC_Han, GC_Lo, NULL}, //  叆..収
    {0x53D0, 0x53DC, SC_Han, GC_Lo, NULL}, //  叐..叜
    {0x53DF, 0x53E6, SC_Han, GC_Lo, NULL}, //  叟..另
    {0x53E8, 0x53FE, SC_Han, GC_Lo, NULL}, //  叨..叾
    {0x5401, 0x5419, SC_Han, GC_Lo, NULL}, //  吁..吙
    {0x541B, 0x5421, SC_Han, GC_Lo, NULL}, //  君..吡
    {0x5423, 0x544B, SC_Han, GC_Lo, NULL}, //  吣..呋
    {0x544D, 0x545C, SC_Han, GC_Lo, NULL}, //  呍..呜
    {0x545E, 0x5468, SC_Han, GC_Lo, NULL}, //  呞..周
    {0x546A, 0x5489, SC_Han, GC_Lo, NULL}, //  呪..咉
    {0x548B, 0x54B4, SC_Han, GC_Lo, NULL}, //  咋..咴
    {0x54B6, 0x54F5, SC_Han, GC_Lo, NULL}, //  咶..哵
    {0x54F7, 0x5514, SC_Han, GC_Lo, NULL}, //  哷..唔
    {0x5516, 0x5517, SC_Han, GC_Lo, NULL}, //  唖..唗
    {0x551A, 0x5546, SC_Han, GC_Lo, NULL}, //  唚..商
    {0x5548, 0x555F, SC_Han, GC_Lo, NULL}, //  啈..啟
    {0x5561, 0x5579, SC_Han, GC_Lo, NULL}, //  啡..啹
    {0x557B, 0x55DF, SC_Han, GC_Lo, NULL}, //  啻..嗟
    {0x55E1, 0x55F7, SC_Han, GC_Lo, NULL}, //  嗡..嗷
    {0x55F9, 0x5609, SC_Han, GC_Lo, NULL}, //  嗹..嘉
    {0x560C, 0x561F, SC_Han, GC_Lo, NULL}, //  嘌..嘟
    {0x5621, 0x562A, SC_Han, GC_Lo, NULL}, //  嘡..嘪
    {0x562C, 0x5636, SC_Han, GC_Lo, NULL}, //  嘬..嘶
    {0x5638, 0x563B, SC_Han, GC_Lo, NULL}, //  嘸..嘻
    {0x563D, 0x5643, SC_Han, GC_Lo, NULL}, //  嘽..噃
    {0x5645, 0x564A, SC_Han, GC_Lo, NULL}, //  噅..噊
    {0x564C, 0x5650, SC_Han, GC_Lo, NULL}, //  噌..噐
    {0x5652, 0x5655, SC_Han, GC_Lo, NULL}, //  噒..噕
    {0x5657, 0x565E, SC_Han, GC_Lo, NULL}, //  噗..噞
    {0x5660, 0x5660, SC_Han, GC_Lo, NULL}, //  噠
    {0x5662, 0x5674, SC_Han, GC_Lo, NULL}, //  噢..噴
    {0x5676, 0x567C, SC_Han, GC_Lo, NULL}, //  噶..噼
    {0x567E, 0x5687, SC_Han, GC_Lo, NULL}, //  噾..嚇
    {0x5689, 0x568A, SC_Han, GC_Lo, NULL}, //  嚉..嚊
    {0x568C, 0x5695, SC_Han, GC_Lo, NULL}, //  嚌..嚕
    {0x5697, 0x569D, SC_Han, GC_Lo, NULL}, //  嚗..嚝
    {0x569F, 0x56B9, SC_Han, GC_Lo, NULL}, //  嚟..嚹
    {0x56BB, 0x56CE, SC_Han, GC_Lo, NULL}, //  嚻..囎
    {0x56D0, 0x56D8, SC_Han, GC_Lo, NULL}, //  囐..囘
    {0x56DA, 0x56E5, SC_Han, GC_Lo, NULL}, //  囚..囥
    {0x56E7, 0x56F5, SC_Han, GC_Lo, NULL}, //  囧..囵
    {0x56F7, 0x56F7, SC_Han, GC_Lo, NULL}, //  囷
    {0x56F9, 0x56FA, SC_Han, GC_Lo, NULL}, //  囹..固
    {0x56FD, 0x5704, SC_Han, GC_Lo, NULL}, //  国..圄
    {0x5706, 0x5710, SC_Han, GC_Lo, NULL}, //  圆..圐
    {0x5712, 0x5716, SC_Han, GC_Lo, NULL}, //  園..圖
    {0x5718, 0x5720, SC_Han, GC_Lo, NULL}, //  團..圠
    {0x5722, 0x5723, SC_Han, GC_Lo, NULL}, //  圢..圣
    {0x5725, 0x573C, SC_Han, GC_Lo, NULL}, //  圥..圼
    {0x573E, 0x5742, SC_Han, GC_Lo, NULL}, //  圾..坂
    {0x5744, 0x5747, SC_Han, GC_Lo, NULL}, //  坄..均
    {0x5749, 0x5754, SC_Han, GC_Lo, NULL}, //  坉..坔
    {0x5757, 0x5757, SC_Han, GC_Lo, NULL}, //  块
    {0x5759, 0x5762, SC_Han, GC_Lo, NULL}, //  坙..坢
    {0x5764, 0x5777, SC_Han, GC_Lo, NULL}, //  坤..坷
    {0x5779, 0x5780, SC_Han, GC_Lo, NULL}, //  坹..垀
    {0x5782, 0x5786, SC_Han, GC_Lo, NULL}, //  垂..垆
    {0x5788, 0x5795, SC_Han, GC_Lo, NULL}, //  垈..垕
    {0x5797, 0x57A7, SC_Han, GC_Lo, NULL}, //  垗..垧
    {0x57A9, 0x57C9, SC_Han, GC_Lo, NULL}, //  垩..埉
    {0x57CB, 0x57D0, SC_Han, GC_Lo, NULL}, //  埋..埐
    {0x57D2, 0x57DA, SC_Han, GC_Lo, NULL}, //  埒..埚
    {0x57DC, 0x5816, SC_Han, GC_Lo, NULL}, //  埜..堖
    {0x5819, 0x584F, SC_Han, GC_Lo, NULL}, //  堙..塏
    {0x5851, 0x5855, SC_Han, GC_Lo, NULL}, //  塑..塕
    {0x5857, 0x585F, SC_Han, GC_Lo, NULL}, //  塗..塟
    {0x5861, 0x5865, SC_Han, GC_Lo, NULL}, //  塡..塥
    {0x5868, 0x5876, SC_Han, GC_Lo, NULL}, //  塨..塶
    {0x5878, 0x5894, SC_Han, GC_Lo, NULL}, //  塸..墔
    {0x5896, 0x58A9, SC_Han, GC_Lo, NULL}, //  墖..墩
    {0x58AB, 0x58B5, SC_Han, GC_Lo, NULL}, //  墫..墵
    {0x58B7, 0x58BF, SC_Han, GC_Lo, NULL}, //  墷..墿
    {0x58C1, 0x58C2, SC_Han, GC_Lo, NULL}, //  壁..壂
    {0x58C5, 0x58CC, SC_Han, GC_Lo, NULL}, //  壅..壌
    {0x58CE, 0x58CF, SC_Han, GC_Lo, NULL}, //  壎..壏
    {0x58D1, 0x58E0, SC_Han, GC_Lo, NULL}, //  壑..壠
    {0x58E2, 0x58E5, SC_Han, GC_Lo, NULL}, //  壢..壥
    {0x58E7, 0x58F4, SC_Han, GC_Lo, NULL}, //  壧..壴
    {0x58F6, 0x5900, SC_Han, GC_Lo, NULL}, //  壶..夀
    {0x5902, 0x5904, SC_Han, GC_Lo, NULL}, //  夂..处
    {0x5906, 0x5907, SC_Han, GC_Lo, NULL}, //  夆..备
    {0x5909, 0x5910, SC_Han, GC_Lo, NULL}, //  変..夐
    {0x5912, 0x5912, SC_Han, GC_Lo, NULL}, //  夒
    {0x5914, 0x5922, SC_Han, GC_Lo, NULL}, //  夔..夢
    {0x5924, 0x5932, SC_Han, GC_Lo, NULL}, //  夤..夲
    {0x5934, 0x5935, SC_Han, GC_Lo, NULL}, //  头..夵
    {0x5937, 0x5958, SC_Han, GC_Lo, NULL}, //  夷..奘
    {0x595A, 0x595A, SC_Han, GC_Lo, NULL}, //  奚
    {0x595C, 0x59B6, SC_Han, GC_Lo, NULL}, //  奜..妶
    {0x59B8, 0x59E6, SC_Han, GC_Lo, NULL}, //  妸..姦
    {0x59E8, 0x5A23, SC_Han, GC_Lo, NULL}, //  姨..娣
    {0x5A25, 0x5A25, SC_Han, GC_Lo, NULL}, //  娥
    {0x5A27, 0x5A2B, SC_Han, GC_Lo, NULL}, //  娧..娫
    {0x5A2D, 0x5A2F, SC_Han, GC_Lo, NULL}, //  娭..娯
    {0x5A31, 0x5A53, SC_Han, GC_Lo, NULL}, //  娱..婓
    {0x5A55, 0x5A58, SC_Han, GC_Lo, NULL}, //  婕..婘
    {0x5A5A, 0x5A6E, SC_Han, GC_Lo, NULL}, //  婚..婮
    {0x5A70, 0x5A70, SC_Han, GC_Lo, NULL}, //  婰
    {0x5A72, 0x5A86, SC_Han, GC_Lo, NULL}, //  婲..媆
    {0x5A88, 0x5A8C, SC_Han, GC_Lo, NULL}, //  媈..媌
    {0x5A8E, 0x5AAA, SC_Han, GC_Lo, NULL}, //  媎..媪
    {0x5AAC, 0x5AD2, SC_Han, GC_Lo, NULL}, //  媬..嫒
    {0x5AD4, 0x5AEE, SC_Han, GC_Lo, NULL}, //  嫔..嫮
    {0x5AF1, 0x5B09, SC_Han, GC_Lo, NULL}, //  嫱..嬉
    {0x5B0B, 0x5B0C, SC_Han, GC_Lo, NULL}, //  嬋..嬌
    {0x5B0E, 0x5B38, SC_Han, GC_Lo, NULL}, //  嬎..嬸
    {0x5B3A, 0x5B45, SC_Han, GC_Lo, NULL}, //  嬺..孅
    {0x5B47, 0x5B4E, SC_Han, GC_Lo, NULL}, //  孇..孎
    {0x5B50, 0x5B51, SC_Han, GC_Lo, NULL}, //  子..孑
    {0x5B53, 0x5B5F, SC_Han, GC_Lo, NULL}, //  孓..孟
    {0x5B62, 0x5B6E, SC_Han, GC_Lo, NULL}, //  孢..孮
    {0x5B70, 0x5B78, SC_Han, GC_Lo, NULL}, //  孰..學
    {0x5B7A, 0x5B7D, SC_Han, GC_Lo, NULL}, //  孺..孽
    {0x5B7F, 0x5B85, SC_Han, GC_Lo, NULL}, //  孿..宅
    {0x5B87, 0x5B8F, SC_Han, GC_Lo, NULL}, //  宇..宏
    {0x5B91, 0x5BA8, SC_Han, GC_Lo, NULL}, //  宑..宨
    {0x5BAA, 0x5BB1, SC_Han, GC_Lo, NULL}, //  宪..宱
    {0x5BB3, 0x5BB6, SC_Han, GC_Lo, NULL}, //  害..家
    {0x5BB8, 0x5BBB, SC_Han, GC_Lo, NULL}, //  宸..宻
    {0x5BBD, 0x5BC7, SC_Han, GC_Lo, NULL}, //  宽..寇
    {0x5BC9, 0x5BD9, SC_Han, GC_Lo, NULL}, //  寉..寙
    {0x5BDB, 0x5BFF, SC_Han, GC_Lo, NULL}, //  寛..寿
    {0x5C01, 0x5C1A, SC_Han, GC_Lo, NULL}, //  封..尚
    {0x5C1C, 0x5C22, SC_Han, GC_Lo, NULL}, //  尜..尢
    {0x5C24, 0x5C25, SC_Han, GC_Lo, NULL}, //  尤..尥
    {0x5C27, 0x5C28, SC_Han, GC_Lo, NULL}, //  尧..尨
    {0x5C2A, 0x5C35, SC_Han, GC_Lo, NULL}, //  尪..尵
    {0x5C37, 0x5C59, SC_Han, GC_Lo, NULL}, //  尷..屙
    {0x5C5B, 0x5C84, SC_Han, GC_Lo, NULL}, //  屛..岄
    {0x5C86, 0x5CB3, SC_Han, GC_Lo, NULL}, //  岆..岳
    {0x5CB5, 0x5CB8, SC_Han, GC_Lo, NULL}, //  岵..岸
    {0x5CBA, 0x5CD4, SC_Han, GC_Lo, NULL}, //  岺..峔
    {0x5CD6, 0x5CDC, SC_Han, GC_Lo, NULL}, //  峖..峜
    {0x5CDE, 0x5CF4, SC_Han, GC_Lo, NULL}, //  峞..峴
    {0x5CF6, 0x5D2A, SC_Han, GC_Lo, NULL}, //  島..崪
    {0x5D2C, 0x5D2E, SC_Han, GC_Lo, NULL}, //  崬..崮
    {0x5D30, 0x5D3A, SC_Han, GC_Lo, NULL}, //  崰..崺
    {0x5D3C, 0x5D52, SC_Han, GC_Lo, NULL}, //  崼..嵒
    {0x5D54, 0x5D56, SC_Han, GC_Lo, NULL}, //  嵔..嵖
    {0x5D58, 0x5D5F, SC_Han, GC_Lo, NULL}, //  嵘..嵟
    {0x5D61, 0x5D82, SC_Han, GC_Lo, NULL}, //  嵡..嶂
    {0x5D84, 0x5D95, SC_Han, GC_Lo, NULL}, //  嶄..嶕
    {0x5D97, 0x5DA2, SC_Han, GC_Lo, NULL}, //  嶗..嶢
    {0x5DA5, 0x5DAA, SC_Han, GC_Lo, NULL}, //  嶥..嶪
    {0x5DAC, 0x5DB2, SC_Han, GC_Lo, NULL}, //  嶬..嶲
    {0x5DB4, 0x5DB8, SC_Han, GC_Lo, NULL}, //  嶴..嶸
    {0x5DBA, 0x5DC3, SC_Han, GC_Lo, NULL}, //  嶺..巃
    {0x5DC5, 0x5DD6, SC_Han, GC_Lo, NULL}, //  巅..巖
    {0x5DD8, 0x5DD9, SC_Han, GC_Lo, NULL}, //  巘..巙
    {0x5DDB, 0x5DDB, SC_Han, GC_Lo, NULL}, //  巛
    {0x5DDD, 0x5DF5, SC_Han, GC_Lo, NULL}, //  川..巵
    {0x5DF7, 0x5E11, SC_Han, GC_Lo, NULL}, //  巷..帑
    {0x5E13, 0x5E47, SC_Han, GC_Lo, NULL}, //  帓..幇
    {0x5E49, 0x5E50, SC_Han, GC_Lo, NULL}, //  幉..幐
    {0x5E52, 0x5E91, SC_Han, GC_Lo, NULL}, //  幒..庑
    {0x5E93, 0x5EB9, SC_Han, GC_Lo, NULL}, //  库..庹
    {0x5EBB, 0x5EBF, SC_Han, GC_Lo, NULL}, //  庻..庿
    {0x5EC1, 0x5EEA, SC_Han, GC_Lo, NULL}, //  廁..廪
    {0x5EEC, 0x5EF8, SC_Han, GC_Lo, NULL}, //  廬..廸
    {0x5EFA, 0x5F0D, SC_Han, GC_Lo, NULL}, //  建..弍
    {0x5F0F, 0x5F3A, SC_Han, GC_Lo, NULL}, //  式..强
    {0x5F3C, 0x5F3C, SC_Han, GC_Lo, NULL}, //  弼
    {0x5F3E, 0x5F8E, SC_Han, GC_Lo, NULL}, //  弾..徎
    {0x5F90, 0x5F99, SC_Han, GC_Lo, NULL}, //  徐..徙
    {0x5F9B, 0x5FA2, SC_Han, GC_Lo, NULL}, //  徛..徢
    {0x5FA5, 0x5FAF, SC_Han, GC_Lo, NULL}, //  徥..徯
    {0x5FB1, 0x5FC1, SC_Han, GC_Lo, NULL}, //  徱..忁
    {0x5FC3, 0x5FCD, SC_Han, GC_Lo, NULL}, //  心..忍
    {0x5FCF, 0x5FDA, SC_Han, GC_Lo, NULL}, //  忏..忚
    {0x5FDC, 0x5FE1, SC_Han, GC_Lo, NULL}, //  応..忡
    {0x5FE3, 0x5FEB, SC_Han, GC_Lo, NULL}, //  忣..快
    {0x5FED, 0x5FFB, SC_Han, GC_Lo, NULL}, //  忭..忻
    {0x5FFD, 0x6022, SC_Han, GC_Lo, NULL}, //  忽..怢
    {0x6024, 0x6055, SC_Han, GC_Lo, NULL}, //  怤..恕
    {0x6057, 0x6060, SC_Han, GC_Lo, NULL}, //  恗..恠
    {0x6062, 0x6070, SC_Han, GC_Lo, NULL}, //  恢..恰
    {0x6072, 0x6073, SC_Han, GC_Lo, NULL}, //  恲..恳
    {0x6075, 0x6090, SC_Han, GC_Lo, NULL}, //  恵..悐
    {0x6092, 0x6092, SC_Han, GC_Lo, NULL}, //  悒
    {0x6094, 0x60A4, SC_Han, GC_Lo, NULL}, //  悔..悤
    {0x60A6, 0x60D1, SC_Han, GC_Lo, NULL}, //  悦..惑
    {0x60D3, 0x60D5, SC_Han, GC_Lo, NULL}, //  惓..惕
    {0x60D7, 0x60DD, SC_Han, GC_Lo, NULL}, //  惗..惝
    {0x60DF, 0x60E4, SC_Han, GC_Lo, NULL}, //  惟..惤
    {0x60E6, 0x60FC, SC_Han, GC_Lo, NULL}, //  惦..惼
    {0x60FE, 0x6101, SC_Han, GC_Lo, NULL}, //  惾..愁
    {0x6103, 0x6106, SC_Han, GC_Lo, NULL}, //  愃..愆
    {0x6108, 0x6110, SC_Han, GC_Lo, NULL}, //  愈..愐
    {0x6112, 0x611D, SC_Han, GC_Lo, NULL}, //  愒..愝
    {0x611F, 0x6130, SC_Han, GC_Lo, NULL}, //  感..愰
    {0x6132, 0x6132, SC_Han, GC_Lo, NULL}, //  愲
    {0x6134, 0x6134, SC_Han, GC_Lo, NULL}, //  愴
    {0x6136, 0x6137, SC_Han, GC_Lo, NULL}, //  愶..愷
    {0x613A, 0x615F, SC_Han, GC_Lo, NULL}, //  愺..慟
    {0x6161, 0x617A, SC_Han, GC_Lo, NULL}, //  慡..慺
    {0x617C, 0x617E, SC_Han, GC_Lo, NULL}, //  慼..慾
    {0x6180, 0x6185, SC_Han, GC_Lo, NULL}, //  憀..憅
    {0x6187, 0x6196, SC_Han, GC_Lo, NULL}, //  憇..憖
    {0x6198, 0x619B, SC_Han, GC_Lo, NULL}, //  憘..憛
    {0x619D, 0x61B8, SC_Han, GC_Lo, NULL}, //  憝..憸
    {0x61BA, 0x61BA, SC_Han, GC_Lo, NULL}, //  憺
    {0x61BC, 0x61D2, SC_Han, GC_Lo, NULL}, //  憼..懒
    {0x61D4, 0x61D4, SC_Han, GC_Lo, NULL}, //  懔
    {0x61D6, 0x61EB, SC_Han, GC_Lo, NULL}, //  懖..懫
    {0x61ED, 0x61EE, SC_Han, GC_Lo, NULL}, //  懭..懮
    {0x61F0, 0x6204, SC_Han, GC_Lo, NULL}, //  懰..戄
    {0x6206, 0x6234, SC_Han, GC_Lo, NULL}, //  戆..戴
    {0x6236, 0x6238, SC_Han, GC_Lo, NULL}, //  戶..戸
    {0x623A, 0x6256, SC_Han, GC_Lo, NULL}, //  戺..扖
    {0x6258, 0x628C, SC_Han, GC_Lo, NULL}, //  托..抌
    {0x628E, 0x629C, SC_Han, GC_Lo, NULL}, //  抎..抜
    {0x629E, 0x62DD, SC_Han, GC_Lo, NULL}, //  択..拝
    {0x62DF, 0x62E9, SC_Han, GC_Lo, NULL}, //  拟..择
    {0x62EB, 0x6309, SC_Han, GC_Lo, NULL}, //  拫..按
    {0x630B, 0x6316, SC_Han, GC_Lo, NULL}, //  挋..挖
    {0x6318, 0x6330, SC_Han, GC_Lo, NULL}, //  挘..挰
    {0x6332, 0x6336, SC_Han, GC_Lo, NULL}, //  挲..挶
    {0x6338, 0x635A, SC_Han, GC_Lo, NULL}, //  挸..捚
    {0x635C, 0x638A, SC_Han, GC_Lo, NULL}, //  捜..掊
    {0x638C, 0x6392, SC_Han, GC_Lo, NULL}, //  掌..排
    {0x6394, 0x63D0, SC_Han, GC_Lo, NULL}, //  掔..提
    {0x63D2, 0x643A, SC_Han, GC_Lo, NULL}, //  插..携
    {0x643D, 0x6448, SC_Han, GC_Lo, NULL}, //  搽..摈
    {0x644A, 0x6459, SC_Han, GC_Lo, NULL}, //  摊..摙
    {0x645B, 0x647D, SC_Han, GC_Lo, NULL}, //  摛..摽
    {0x647F, 0x6485, SC_Han, GC_Lo, NULL}, //  摿..撅
    {0x6487, 0x64A0, SC_Han, GC_Lo, NULL}, //  撇..撠
    {0x64A2, 0x64AE, SC_Han, GC_Lo, NULL}, //  撢..撮
    {0x64B0, 0x64B5, SC_Han, GC_Lo, NULL}, //  撰..撵
    {0x64B7, 0x64C7, SC_Han, GC_Lo, NULL}, //  撷..擇
    {0x64C9, 0x64D4, SC_Han, GC_Lo, NULL}, //  擉..擔
    {0x64D6, 0x64ED, SC_Han, GC_Lo, NULL}, //  擖..擭
    {0x64EF, 0x64F4, SC_Han, GC_Lo, NULL}, //  擯..擴
    {0x64F6, 0x64F8, SC_Han, GC_Lo, NULL}, //  擶..擸
    {0x64FA, 0x6501, SC_Han, GC_Lo, NULL}, //  擺..攁
    {0x6503, 0x6509, SC_Han, GC_Lo, NULL}, //  攃..攉
    {0x650B, 0x651E, SC_Han, GC_Lo, NULL}, //  攋..攞
    {0x6520, 0x6527, SC_Han, GC_Lo, NULL}, //  攠..攧
    {0x6529, 0x653F, SC_Han, GC_Lo, NULL}, //  攩..政
    {0x6541, 0x6541, SC_Han, GC_Lo, NULL}, //  敁
    {0x6543, 0x6559, SC_Han, GC_Lo, NULL}, //  敃..教
    {0x655B, 0x655E, SC_Han, GC_Lo, NULL}, //  敛..敞
    {0x6560, 0x657C, SC_Han, GC_Lo, NULL}, //  敠..敼
    {0x657E, 0x6589, SC_Han, GC_Lo, NULL}, //  敾..斉
    {0x658B, 0x6599, SC_Han, GC_Lo, NULL}, //  斋..料
    {0x659B, 0x65B4, SC_Han, GC_Lo, NULL}, //  斛..斴
    {0x65B6, 0x65BD, SC_Han, GC_Lo, NULL}, //  斶..施
    {0x65BF, 0x65C7, SC_Han, GC_Lo, NULL}, //  斿..旇
    {0x65CA, 0x65D0, SC_Han, GC_Lo, NULL}, //  旊..旐
    {0x65D2, 0x65D7, SC_Han, GC_Lo, NULL}, //  旒..旗
    {0x65D9, 0x65DB, SC_Han, GC_Lo, NULL}, //  旙..旛
    {0x65DD, 0x65E3, SC_Han, GC_Lo, NULL}, //  旝..旣
    {0x65E5, 0x65E9, SC_Han, GC_Lo, NULL}, //  日..早
    {0x65EB, 0x65F8, SC_Han, GC_Lo, NULL}, //  旫..旸
    {0x65FA, 0x65FD, SC_Han, GC_Lo, NULL}, //  旺..旽
    {0x65FF, 0x6616, SC_Han, GC_Lo, NULL}, //  旿..昖
    {0x6618, 0x662B, SC_Han, GC_Lo, NULL}, //  昘..昫
    {0x662D, 0x6636, SC_Han, GC_Lo, NULL}, //  昭..昶
    {0x6639, 0x6647, SC_Han, GC_Lo, NULL}, //  昹..晇
    {0x6649, 0x664C, SC_Han, GC_Lo, NULL}, //  晉..晌
    {0x664E, 0x665F, SC_Han, GC_Lo, NULL}, //  晎..晟
    {0x6661, 0x6662, SC_Han, GC_Lo, NULL}, //  晡..晢
    {0x6664, 0x6691, SC_Han, GC_Lo, NULL}, //  晤..暑
    {0x6693, 0x669B, SC_Han, GC_Lo, NULL}, //  暓..暛
    {0x669D, 0x669D, SC_Han, GC_Lo, NULL}, //  暝
    {0x669F, 0x66AB, SC_Han, GC_Lo, NULL}, //  暟..暫
    {0x66AE, 0x66CF, SC_Han, GC_Lo, NULL}, //  暮..曏
    {0x66D1, 0x66D2, SC_Han, GC_Lo, NULL}, //  曑..曒
    {0x66D4, 0x66D6, SC_Han, GC_Lo, NULL}, //  曔..曖
    {0x66D8, 0x66DE, SC_Han, GC_Lo, NULL}, //  曘..曞
    {0x66E0, 0x66EE, SC_Han, GC_Lo, NULL}, //  曠..曮
    {0x66F0, 0x6701, SC_Han, GC_Lo, NULL}, //  曰..朁
    {0x6703, 0x6706, SC_Han, GC_Lo, NULL}, //  會..朆
    {0x6708, 0x6718, SC_Han, GC_Lo, NULL}, //  月..朘
    {0x671A, 0x6723, SC_Han, GC_Lo, NULL}, //  朚..朣
    {0x6725, 0x6728, SC_Han, GC_Lo, NULL}, //  朥..木
    {0x672A, 0x6766, SC_Han, GC_Lo, NULL}, //  未..杦
    {0x6768, 0x6787, SC_Han, GC_Lo, NULL}, //  杨..枇
    {0x6789, 0x6795, SC_Han, GC_Lo, NULL}, //  枉..枕
    {0x6797, 0x67BC, SC_Han, GC_Lo, NULL}, //  林..枼
    {0x67BE, 0x67BE, SC_Han, GC_Lo, NULL}, //  枾
    {0x67C0, 0x67D4, SC_Han, GC_Lo, NULL}, //  柀..柔
    {0x67D6, 0x67D6, SC_Han, GC_Lo, NULL}, //  柖
    {0x67D8, 0x67F8, SC_Han, GC_Lo, NULL}, //  柘..柸
    {0x67FA, 0x6800, SC_Han, GC_Lo, NULL}, //  柺..栀
    {0x6802, 0x6814, SC_Han, GC_Lo, NULL}, //  栂..栔
    {0x6816, 0x6826, SC_Han, GC_Lo, NULL}, //  栖..栦
    {0x6828, 0x682F, SC_Han, GC_Lo, NULL}, //  栨..栯
    {0x6831, 0x6857, SC_Han, GC_Lo, NULL}, //  栱..桗
    {0x6859, 0x6859, SC_Han, GC_Lo, NULL}, //  桙
    {0x685B, 0x685D, SC_Han, GC_Lo, NULL}, //  桛..桝
    {0x685F, 0x6879, SC_Han, GC_Lo, NULL}, //  桟..桹
    {0x687B, 0x6894, SC_Han, GC_Lo, NULL}, //  桻..梔
    {0x6896, 0x6898, SC_Han, GC_Lo, NULL}, //  梖..梘
    {0x689A, 0x68A4, SC_Han, GC_Lo, NULL}, //  梚..梤
    {0x68A6, 0x68B7, SC_Han, GC_Lo, NULL}, //  梦..梷
    {0x68B9, 0x68C2, SC_Han, GC_Lo, NULL}, //  梹..棂
    {0x68C4, 0x68D8, SC_Han, GC_Lo, NULL}, //  棄..棘
    {0x68DA, 0x68E1, SC_Han, GC_Lo, NULL}, //  棚..棡
    {0x68E3, 0x68E4, SC_Han, GC_Lo, NULL}, //  棣..棤
    {0x68E6, 0x6908, SC_Han, GC_Lo, NULL}, //  棦..椈
    {0x690A, 0x693D, SC_Han, GC_Lo, NULL}, //  椊..椽
    {0x693F, 0x694C, SC_Han, GC_Lo, NULL}, //  椿..楌
    {0x694E, 0x699E, SC_Han, GC_Lo, NULL}, //  楎..榞
    {0x69A0, 0x69A1, SC_Han, GC_Lo, NULL}, //  榠..榡
    {0x69A3, 0x69BF, SC_Han, GC_Lo, NULL}, //  榣..榿
    {0x69C1, 0x69D0, SC_Han, GC_Lo, NULL}, //  槁..槐
    {0x69D3, 0x69D4, SC_Han, GC_Lo, NULL}, //  槓..槔
    {0x69D8, 0x6A02, SC_Han, GC_Lo, NULL}, //  様..樂
    {0x6A04, 0x6A1B, SC_Han, GC_Lo, NULL}, //  樄..樛
    {0x6A1D, 0x6A23, SC_Han, GC_Lo, NULL}, //  樝..樣
    {0x6A25, 0x6A36, SC_Han, GC_Lo, NULL}, //  樥..樶
    {0x6A38, 0x6A49, SC_Han, GC_Lo, NULL}, //  樸..橉
    {0x6A4B, 0x6A5B, SC_Han, GC_Lo, NULL}, //  橋..橛
    {0x6A5D, 0x6A6D, SC_Han, GC_Lo, NULL}, //  橝..橭
    {0x6A6F, 0x6A6F, SC_Han, GC_Lo, NULL}, //  橯
    {0x6A71, 0x6A85, SC_Han, GC_Lo, NULL}, //  橱..檅
    {0x6A87, 0x6A89, SC_Han, GC_Lo, NULL}, //  檇..檉
    {0x6A8B, 0x6A8E, SC_Han, GC_Lo, NULL}, //  檋..檎
    {0x6A90, 0x6A98, SC_Han, GC_Lo, NULL}, //  檐..檘
    {0x6A9A, 0x6A9C, SC_Han, GC_Lo, NULL}, //  檚..檜
    {0x6A9E, 0x6AB0, SC_Han, GC_Lo, NULL}, //  檞..檰
    {0x6AB2, 0x6ABD, SC_Han, GC_Lo, NULL}, //  檲..檽
    {0x6ABF, 0x6ABF, SC_Han, GC_Lo, NULL}, //  檿
    {0x6AC1, 0x6AC3, SC_Han, GC_Lo, NULL}, //  櫁..櫃
    {0x6AC5, 0x6AC8, SC_Han, GC_Lo, NULL}, //  櫅..櫈
    {0x6ACA, 0x6AD7, SC_Han, GC_Lo, NULL}, //  櫊..櫗
    {0x6AD9, 0x6AE8, SC_Han, GC_Lo, NULL}, //  櫙..櫨
    {0x6AEA, 0x6B0D, SC_Han, GC_Lo, NULL}, //  櫪..欍
    {0x6B0F, 0x6B1A, SC_Han, GC_Lo, NULL}, //  欏..欚
    {0x6B1C, 0x6B2D, SC_Han, GC_Lo, NULL}, //  欜..欭
    {0x6B2F, 0x6B34, SC_Han, GC_Lo, NULL}, //  欯..欴
    {0x6B36, 0x6B3F, SC_Han, GC_Lo, NULL}, //  欶..欿
    {0x6B41, 0x6B56, SC_Han, GC_Lo, NULL}, //  歁..歖
    {0x6B59, 0x6B5C, SC_Han, GC_Lo, NULL}, //  歙..歜
    {0x6B5E, 0x6B67, SC_Han, GC_Lo, NULL}, //  歞..歧
    {0x6B69, 0x6B6B, SC_Han, GC_Lo, NULL}, //  歩..歫
    {0x6B6D, 0x6B6D, SC_Han, GC_Lo, NULL}, //  歭
    {0x6B6F, 0x6B70, SC_Han, GC_Lo, NULL}, //  歯..歰
    {0x6B72, 0x6B74, SC_Han, GC_Lo, NULL}, //  歲..歴
    {0x6B76, 0x6B7C, SC_Han, GC_Lo, NULL}, //  歶..歼
    {0x6B7E, 0x6BB7, SC_Han, GC_Lo, NULL}, //  歾..殷
    {0x6BB9, 0x6BE8, SC_Han, GC_Lo, NULL}, //  殹..毨
    {0x6BEA, 0x6BF0, SC_Han, GC_Lo, NULL}, //  毪..毰
    {0x6BF2, 0x6BF3, SC_Han, GC_Lo, NULL}, //  毲..毳
    {0x6BF5, 0x6BF9, SC_Han, GC_Lo, NULL}, //  毵..毹
    {0x6BFB, 0x6C09, SC_Han, GC_Lo, NULL}, //  毻..氉
    {0x6C0B, 0x6C1B, SC_Han, GC_Lo, NULL}, //  氋..氛
    {0x6C1D, 0x6C2C, SC_Han, GC_Lo, NULL}, //  氝..氬
    {0x6C2E, 0x6C3B, SC_Han, GC_Lo, NULL}, //  氮..氻
    {0x6C3D, 0x6C44, SC_Han, GC_Lo, NULL}, //  氽..汄
    {0x6C46, 0x6C6B, SC_Han, GC_Lo, NULL}, //  汆..汫
    {0x6C6D, 0x6C6D, SC_Han, GC_Lo, NULL}, //  汭
    {0x6C6F, 0x6C9F, SC_Han, GC_Lo, NULL}, //  汯..沟
    {0x6CA1, 0x6CD7, SC_Han, GC_Lo, NULL}, //  没..泗
    {0x6CD9, 0x6CF3, SC_Han, GC_Lo, NULL}, //  泙..泳
    {0x6CF5, 0x6D01, SC_Han, GC_Lo, NULL}, //  泵..洁
    {0x6D03, 0x6D1B, SC_Han, GC_Lo, NULL}, //  洃..洛
    {0x6D1D, 0x6D23, SC_Han, GC_Lo, NULL}, //  洝..洣
    {0x6D25, 0x6D70, SC_Han, GC_Lo, NULL}, //  津..浰
    {0x6D72, 0x6D80, SC_Han, GC_Lo, NULL}, //  浲..涀
    {0x6D82, 0x6D95, SC_Han, GC_Lo, NULL}, //  涂..涕
    {0x6D97, 0x6DAF, SC_Han, GC_Lo, NULL}, //  涗..涯
    {0x6DB2, 0x6DB5, SC_Han, GC_Lo, NULL}, //  液..涵
    {0x6DB7, 0x6DFD, SC_Han, GC_Lo, NULL}, //  涷..淽
    {0x6E00, 0x6E00, SC_Han, GC_Lo, NULL}, //  渀
    {0x6E03, 0x6E05, SC_Han, GC_Lo, NULL}, //  渃..清
    {0x6E07, 0x6E11, SC_Han, GC_Lo, NULL}, //  渇..渑
    {0x6E13, 0x6E17, SC_Han, GC_Lo, NULL}, //  渓..渗
    {0x6E19, 0x6E29, SC_Han, GC_Lo, NULL}, //  渙..温
    {0x6E2B, 0x6E4B, SC_Han, GC_Lo, NULL}, //  渫..湋
    {0x6E4D, 0x6E6B, SC_Han, GC_Lo, NULL}, //  湍..湫
    {0x6E6D, 0x6E7A, SC_Han, GC_Lo, NULL}, //  湭..湺
    {0x6E7E, 0x6E8A, SC_Han, GC_Lo, NULL}, //  湾..溊
    {0x6E8C, 0x6E94, SC_Han, GC_Lo, NULL}, //  溌..溔
    {0x6E96, 0x6EDA, SC_Han, GC_Lo, NULL}, //  準..滚
    {0x6EDC, 0x6EE2, SC_Han, GC_Lo, NULL}, //  滜..滢
    {0x6EE4, 0x6F03, SC_Han, GC_Lo, NULL}, //  滤..漃
    {0x6F05, 0x6F0A, SC_Han, GC_Lo, NULL}, //  漅..漊
    {0x6F0C, 0x6F41, SC_Han, GC_Lo, NULL}, //  漌..潁
    {0x6F43, 0x6F47, SC_Han, GC_Lo, NULL}, //  潃..潇
    {0x6F49, 0x6F49, SC_Han, GC_Lo, NULL}, //  潉
    {0x6F4B, 0x6F78, SC_Han, GC_Lo, NULL}, //  潋..潸
    {0x6F7A, 0x6F97, SC_Han, GC_Lo, NULL}, //  潺..澗
    {0x6F99, 0x6F99, SC_Han, GC_Lo, NULL}, //  澙
    {0x6F9B, 0x6F9E, SC_Han, GC_Lo, NULL}, //  澛..澞
    {0x6FA0, 0x6FB6, SC_Han, GC_Lo, NULL}, //  澠..澶
    {0x6FB8, 0x6FC4, SC_Han, GC_Lo, NULL}, //  澸..濄
    {0x6FC6, 0x6FCF, SC_Han, GC_Lo, NULL}, //  濆..濏
    {0x6FD1, 0x6FD2, SC_Han, GC_Lo, NULL}, //  濑..濒
    {0x6FD4, 0x6FF4, SC_Han, GC_Lo, NULL}, //  濔..濴
    {0x6FF6, 0x6FFC, SC_Han, GC_Lo, NULL}, //  濶..濼
    {0x6FFE, 0x700F, SC_Han, GC_Lo, NULL}, //  濾..瀏
    {0x7011, 0x7012, SC_Han, GC_Lo, NULL}, //  瀑..瀒
    {0x7014, 0x7046, SC_Han, GC_Lo, NULL}, //  瀔..灆
    {0x7048, 0x704A, SC_Han, GC_Lo, NULL}, //  灈..灊
    {0x704C, 0x704D, SC_Han, GC_Lo, NULL}, //  灌..灍
    {0x704F, 0x7071, SC_Han, GC_Lo, NULL}, //  灏..灱
    {0x7074, 0x707A, SC_Han, GC_Lo, NULL}, //  灴..灺
    {0x707C, 0x7080, SC_Han, GC_Lo, NULL}, //  灼..炀
    {0x7082, 0x708C, SC_Han, GC_Lo, NULL}, //  炂..炌
    {0x708E, 0x7096, SC_Han, GC_Lo, NULL}, //  炎..炖
    {0x7098, 0x709A, SC_Han, GC_Lo, NULL}, //  炘..炚
    {0x709C, 0x70A9, SC_Han, GC_Lo, NULL}, //  炜..炩
    {0x70AB, 0x70B1, SC_Han, GC_Lo, NULL}, //  炫..炱
    {0x70B3, 0x70B5, SC_Han, GC_Lo, NULL}, //  炳..炵
    {0x70B7, 0x70D4, SC_Han, GC_Lo, NULL}, //  炷..烔
    {0x70D6, 0x70FD, SC_Han, GC_Lo, NULL}, //  烖..烽
    {0x70FF, 0x7107, SC_Han, GC_Lo, NULL}, //  烿..焇
    {0x7109, 0x7123, SC_Han, GC_Lo, NULL}, //  焉..焣
    {0x7125, 0x7132, SC_Han, GC_Lo, NULL}, //  焥..焲
    {0x7135, 0x7156, SC_Han, GC_Lo, NULL}, //  焵..煖
    {0x7158, 0x716A, SC_Han, GC_Lo, NULL}, //  煘..煪
    {0x716C, 0x716C, SC_Han, GC_Lo, NULL}, //  煬
    {0x716E, 0x718C, SC_Han, GC_Lo, NULL}, //  煮..熌
    {0x718E, 0x7195, SC_Han, GC_Lo, NULL}, //  熎..熕
    {0x7197, 0x71A5, SC_Han, GC_Lo, NULL}, //  熗..熥
    {0x71A7, 0x71AA, SC_Han, GC_Lo, NULL}, //  熧..熪
    {0x71AC, 0x71B5, SC_Han, GC_Lo, NULL}, //  熬..熵
    {0x71B7, 0x71CB, SC_Han, GC_Lo, NULL}, //  熷..燋
    {0x71CD, 0x71D2, SC_Han, GC_Lo, NULL}, //  燍..燒
    {0x71D4, 0x71F2, SC_Han, GC_Lo, NULL}, //  燔..燲
    {0x71F4, 0x71F9, SC_Han, GC_Lo, NULL}, //  燴..燹
    {0x71FB, 0x720A, SC_Han, GC_Lo, NULL}, //  燻..爊
    {0x720C, 0x7210, SC_Han, GC_Lo, NULL}, //  爌..爐
    {0x7212, 0x7214, SC_Han, GC_Lo, NULL}, //  爒..爔
    {0x7216, 0x7216, SC_Han, GC_Lo, NULL}, //  爖
    {0x7218, 0x721F, SC_Han, GC_Lo, NULL}, //  爘..爟
    {0x7221, 0x7223, SC_Han, GC_Lo, NULL}, //  爡..爣
    {0x7226, 0x722E, SC_Han, GC_Lo, NULL}, //  爦..爮
    {0x7230, 0x7233, SC_Han, GC_Lo, NULL}, //  爰..爳
    {0x7235, 0x7244, SC_Han, GC_Lo, NULL}, //  爵..牄
    {0x7246, 0x724D, SC_Han, GC_Lo, NULL}, //  牆..牍
    {0x724F, 0x724F, SC_Han, GC_Lo, NULL}, //  牏
    {0x7251, 0x7254, SC_Han, GC_Lo, NULL}, //  牑..牔
    {0x7256, 0x72AA, SC_Han, GC_Lo, NULL}, //  牖..犪
    {0x72AC, 0x72BD, SC_Han, GC_Lo, NULL}, //  犬..犽
    {0x72BF, 0x7301, SC_Han, GC_Lo, NULL}, //  犿..猁
    {0x7303, 0x730F, SC_Han, GC_Lo, NULL}, //  猃..猏
    {0x7311, 0x7327, SC_Han, GC_Lo, NULL}, //  猑..猧
    {0x7329, 0x7352, SC_Han, GC_Lo, NULL}, //  猩..獒
    {0x7354, 0x739B, SC_Han, GC_Lo, NULL}, //  獔..玛
    {0x739D, 0x73C0, SC_Han, GC_Lo, NULL}, //  玝..珀
    {0x73C2, 0x73F2, SC_Han, GC_Lo, NULL}, //  珂..珲
    {0x73F4, 0x73FA, SC_Han, GC_Lo, NULL}, //  珴..珺
    {0x73FC, 0x7417, SC_Han, GC_Lo, NULL}, //  珼..琗
    {0x7419, 0x7438, SC_Han, GC_Lo, NULL}, //  琙..琸
    {0x743A, 0x743D, SC_Han, GC_Lo, NULL}, //  琺..琽
    {0x743F, 0x7446, SC_Han, GC_Lo, NULL}, //  琿..瑆
    {0x7448, 0x7448, SC_Han, GC_Lo, NULL}, //  瑈
    {0x744A, 0x7457, SC_Han, GC_Lo, NULL}, //  瑊..瑗
    {0x7459, 0x747A, SC_Han, GC_Lo, NULL}, //  瑙..瑺
    {0x747C, 0x7483, SC_Han, GC_Lo, NULL}, //  瑼..璃
    {0x7485, 0x7495, SC_Han, GC_Lo, NULL}, //  璅..璕
    {0x7497, 0x749C, SC_Han, GC_Lo, NULL}, //  璗..璜
    {0x749E, 0x74C6, SC_Han, GC_Lo, NULL}, //  璞..瓆
    {0x74C8, 0x74C8, SC_Han, GC_Lo, NULL}, //  瓈
    {0x74CA, 0x74CB, SC_Han, GC_Lo, NULL}, //  瓊..瓋
    {0x74CD, 0x74EA, SC_Han, GC_Lo, NULL}, //  瓍..瓪
    {0x74EC, 0x751F, SC_Han, GC_Lo, NULL}, //  瓬..生
    {0x7521, 0x7540, SC_Han, GC_Lo, NULL}, //  甡..畀
    {0x7542, 0x7551, SC_Han, GC_Lo, NULL}, //  畂..畑
    {0x7553, 0x7554, SC_Han, GC_Lo, NULL}, //  畓..畔
    {0x7556, 0x755D, SC_Han, GC_Lo, NULL}, //  畖..畝
    {0x755F, 0x7560, SC_Han, GC_Lo, NULL}, //  畟..畠
    {0x7562, 0x7570, SC_Han, GC_Lo, NULL}, //  畢..異
    {0x7572, 0x757A, SC_Han, GC_Lo, NULL}, //  畲..畺
    {0x757C, 0x7584, SC_Han, GC_Lo, NULL}, //  畼..疄
    {0x7586, 0x75A8, SC_Han, GC_Lo, NULL}, //  疆..疨
    {0x75AA, 0x75B6, SC_Han, GC_Lo, NULL}, //  疪..疶
    {0x75B8, 0x75DB, SC_Han, GC_Lo, NULL}, //  疸..痛
    {0x75DD, 0x75ED, SC_Han, GC_Lo, NULL}, //  痝..痭
    {0x75EF, 0x762B, SC_Han, GC_Lo, NULL}, //  痯..瘫
    {0x762D, 0x7643, SC_Han, GC_Lo, NULL}, //  瘭..癃
    {0x7646, 0x7650, SC_Han, GC_Lo, NULL}, //  癆..癐
    {0x7652, 0x7654, SC_Han, GC_Lo, NULL}, //  癒..癔
    {0x7656, 0x7672, SC_Han, GC_Lo, NULL}, //  癖..癲
    {0x7674, 0x768C, SC_Han, GC_Lo, NULL}, //  癴..皌
    {0x768E, 0x76A0, SC_Han, GC_Lo, NULL}, //  皎..皠
    {0x76A3, 0x76A4, SC_Han, GC_Lo, NULL}, //  皣..皤
    {0x76A6, 0x76A7, SC_Han, GC_Lo, NULL}, //  皦..皧
    {0x76A9, 0x76B2, SC_Han, GC_Lo, NULL}, //  皩..皲
    {0x76B4, 0x76B5, SC_Han, GC_Lo, NULL}, //  皴..皵
    {0x76B7, 0x76C0, SC_Han, GC_Lo, NULL}, //  皷..盀
    {0x76C2, 0x76CA, SC_Han, GC_Lo, NULL}, //  盂..益
    {0x76CC, 0x76D8, SC_Han, GC_Lo, NULL}, //  盌..盘
    {0x76DA, 0x76EA, SC_Han, GC_Lo, NULL}, //  盚..盪
    {0x76EC, 0x76FF, SC_Han, GC_Lo, NULL}, //  盬..盿
    {0x7701, 0x7701, SC_Han, GC_Lo, NULL}, //  省
    {0x7703, 0x770D, SC_Han, GC_Lo, NULL}, //  眃..眍
    {0x770F, 0x7720, SC_Han, GC_Lo, NULL}, //  眏..眠
    {0x7722, 0x772A, SC_Han, GC_Lo, NULL}, //  眢..眪
    {0x772C, 0x773E, SC_Han, GC_Lo, NULL}, //  眬..眾
    {0x7740, 0x7741, SC_Han, GC_Lo, NULL}, //  着..睁
    {0x7743, 0x7763, SC_Han, GC_Lo, NULL}, //  睃..督
    {0x7765, 0x7795, SC_Han, GC_Lo, NULL}, //  睥..瞕
    {0x7797, 0x77A3, SC_Han, GC_Lo, NULL}, //  瞗..瞣
    {0x77A5, 0x77BD, SC_Han, GC_Lo, NULL}, //  瞥..瞽
    {0x77BF, 0x77C0, SC_Han, GC_Lo, NULL}, //  瞿..矀
    {0x77C2, 0x77D1, SC_Han, GC_Lo, NULL}, //  矂..矑
    {0x77D3, 0x77DC, SC_Han, GC_Lo, NULL}, //  矓..矜
    {0x77DE, 0x77E3, SC_Han, GC_Lo, NULL}, //  矞..矣
    {0x77E5, 0x77E5, SC_Han, GC_Lo, NULL}, //  知
    {0x77E7, 0x77F3, SC_Han, GC_Lo, NULL}, //  矧..石
    {0x77F6, 0x7823, SC_Han, GC_Lo, NULL}, //  矶..砣
    {0x7825, 0x7835, SC_Han, GC_Lo, NULL}, //  砥..砵
    {0x7837, 0x7841, SC_Han, GC_Lo, NULL}, //  砷..硁
    {0x7843, 0x7845, SC_Han, GC_Lo, NULL}, //  硃..硅
    {0x7847, 0x784A, SC_Han, GC_Lo, NULL}, //  硇..硊
    {0x784C, 0x7875, SC_Han, GC_Lo, NULL}, //  硌..硵
    {0x7877, 0x7887, SC_Han, GC_Lo, NULL}, //  硷..碇
    {0x7889, 0x78C1, SC_Han, GC_Lo, NULL}, //  碉..磁
    {0x78C3, 0x78C6, SC_Han, GC_Lo, NULL}, //  磃..磆
    {0x78C8, 0x78D1, SC_Han, GC_Lo, NULL}, //  磈..磑
    {0x78D3, 0x78EF, SC_Han, GC_Lo, NULL}, //  磓..磯
    {0x78F1, 0x78F7, SC_Han, GC_Lo, NULL}, //  磱..磷
    {0x78F9, 0x78FF, SC_Han, GC_Lo, NULL}, //  磹..磿
    {0x7901, 0x7907, SC_Han, GC_Lo, NULL}, //  礁..礇
    {0x7909, 0x790C, SC_Han, GC_Lo, NULL}, //  礉..礌
    {0x790E, 0x7914, SC_Han, GC_Lo, NULL}, //  礎..礔
    {0x7916, 0x791E, SC_Han, GC_Lo, NULL}, //  礖..礞
    {0x7921, 0x7931, SC_Han, GC_Lo, NULL}, //  礡..礱
    {0x7933, 0x7935, SC_Han, GC_Lo, NULL}, //  礳..礵
    {0x7937, 0x7958, SC_Han, GC_Lo, NULL}, //  礷..祘
    {0x795A, 0x796B, SC_Han, GC_Lo, NULL}, //  祚..祫
    {0x796D, 0x796D, SC_Han, GC_Lo, NULL}, //  祭
    {0x796F, 0x7974, SC_Han, GC_Lo, NULL}, //  祯..祴
    {0x7977, 0x7985, SC_Han, GC_Lo, NULL}, //  祷..禅
    {0x7988, 0x799D, SC_Han, GC_Lo, NULL}, //  禈..禝
    {0x799F, 0x79A8, SC_Han, GC_Lo, NULL}, //  禟..禨
    {0x79AA, 0x79BB, SC_Han, GC_Lo, NULL}, //  禪..离
    {0x79BD, 0x79C3, SC_Han, GC_Lo, NULL}, //  禽..秃
    {0x79C5, 0x79C6, SC_Han, GC_Lo, NULL}, //  秅..秆
    {0x79C8, 0x79CB, SC_Han, GC_Lo, NULL}, //  秈..秋
    {0x79CD, 0x79D3, SC_Han, GC_Lo, NULL}, //  种..秓
    {0x79D5, 0x79D6, SC_Han, GC_Lo, NULL}, //  秕..秖
    {0x79D8, 0x7A00, SC_Han, GC_Lo, NULL}, //  秘..稀
    {0x7A02, 0x7A06, SC_Han, GC_Lo, NULL}, //  稂..稆
    {0x7A08, 0x7A08, SC_Han, GC_Lo, NULL}, //  稈
    {0x7A0A, 0x7A2B, SC_Han, GC_Lo, NULL}, //  稊..稫
    {0x7A2D, 0x7A37, SC_Han, GC_Lo, NULL}, //  稭..稷
    {0x7A39, 0x7A39, SC_Han, GC_Lo, NULL}, //  稹
    {0x7A3B, 0x7A63, SC_Han, GC_Lo, NULL}, //  稻..穣
    {0x7A65, 0x7A69, SC_Han, GC_Lo, NULL}, //  穥..穩
    {0x7A6B, 0x7A6E, SC_Han, GC_Lo, NULL}, //  穫..穮
    {0x7A70, 0x7A81, SC_Han, GC_Lo, NULL}, //  穰..突
    {0x7A83, 0x7A99, SC_Han, GC_Lo, NULL}, //  窃..窙
    {0x7A9C, 0x7AB8, SC_Han, GC_Lo, NULL}, //  窜..窸
    {0x7ABA, 0x7ABA, SC_Han, GC_Lo, NULL}, //  窺
    {0x7ABE, 0x7AC1, SC_Han, GC_Lo, NULL}, //  窾..竁
    {0x7AC3, 0x7AC5, SC_Han, GC_Lo, NULL}, //  竃..竅
    {0x7AC7, 0x7AE8, SC_Han, GC_Lo, NULL}, //  竇..竨
    {0x7AEA, 0x7AF4, SC_Han, GC_Lo, NULL}, //  竪..竴
    {0x7AF6, 0x7AFB, SC_Han, GC_Lo, NULL}, //  競..竻
    {0x7AFD, 0x7B06, SC_Han, GC_Lo, NULL}, //  竽..笆
    {0x7B08, 0x7B1E, SC_Han, GC_Lo, NULL}, //  笈..笞
    {0x7B20, 0x7B26, SC_Han, GC_Lo, NULL}, //  笠..符
    {0x7B28, 0x7B28, SC_Han, GC_Lo, NULL}, //  笨
    {0x7B2A, 0x7B41, SC_Han, GC_Lo, NULL}, //  笪..筁
    {0x7B43, 0x7B52, SC_Han, GC_Lo, NULL}, //  筃..筒
    {0x7B54, 0x7BA2, SC_Han, GC_Lo, NULL}, //  答..箢
    {0x7BA4, 0x7BA4, SC_Han, GC_Lo, NULL}, //  箤
    {0x7BA6, 0x7BAF, SC_Han, GC_Lo, NULL}, //  箦..箯
    {0x7BB1, 0x7BB1, SC_Han, GC_Lo, NULL}, //  箱
    {0x7BB3, 0x7BF9, SC_Han, GC_Lo, NULL}, //  箳..篹
    {0x7BFB, 0x7C1A, SC_Han, GC_Lo, NULL}, //  篻..簚
    {0x7C1C, 0x7C2D, SC_Han, GC_Lo, NULL}, //  簜..簭
    {0x7C30, 0x7C51, SC_Han, GC_Lo, NULL}, //  簰..籑
    {0x7C53, 0x7C54, SC_Han, GC_Lo, NULL}, //  籓..籔
    {0x7C56, 0x7C5C, SC_Han, GC_Lo, NULL}, //  籖..籜
    {0x7C5E, 0x7C75, SC_Han, GC_Lo, NULL}, //  籞..籵
    {0x7C77, 0x7C86, SC_Han, GC_Lo, NULL}, //  籷..粆
    {0x7C88, 0x7C92, SC_Han, GC_Lo, NULL}, //  粈..粒
    {0x7C94, 0x7C99, SC_Han, GC_Lo, NULL}, //  粔..粙
    {0x7C9B, 0x7CAB, SC_Han, GC_Lo, NULL}, //  粛..粫
    {0x7CAD, 0x7CD2, SC_Han, GC_Lo, NULL}, //  粭..糒
    {0x7CD4, 0x7CD9, SC_Han, GC_Lo, NULL}, //  糔..糙
    {0x7CDC, 0x7CE0, SC_Han, GC_Lo, NULL}, //  糜..糠
    {0x7CE2, 0x7CE2, SC_Han, GC_Lo, NULL}, //  糢
    {0x7CE4, 0x7CE4, SC_Han, GC_Lo, NULL}, //  糤
    {0x7CE7, 0x7CFB, SC_Han, GC_Lo, NULL}, //  糧..系
    {0x7CFD, 0x7CFE, SC_Han, GC_Lo, NULL}, //  糽..糾
    {0x7D00, 0x7D22, SC_Han, GC_Lo, NULL}, //  紀..索
    {0x7D24, 0x7D29, SC_Han, GC_Lo, NULL}, //  紤..紩
    {0x7D2B, 0x7D2C, SC_Han, GC_Lo, NULL}, //  紫..紬
    {0x7D2E, 0x7D47, SC_Han, GC_Lo, NULL}, //  紮..絇
    {0x7D49, 0x7D4C, SC_Han, GC_Lo, NULL}, //  絉..経
    {0x7D4E, 0x7D59, SC_Han, GC_Lo, NULL}, //  絎..絙
    {0x7D5B, 0x7D63, SC_Han, GC_Lo, NULL}, //  絛..絣
    {0x7D65, 0x7D77, SC_Han, GC_Lo, NULL}, //  絥..絷
    {0x7D79, 0x7D81, SC_Han, GC_Lo, NULL}, //  絹..綁
    {0x7D83, 0x7D94, SC_Han, GC_Lo, NULL}, //  綃..綔
    {0x7D96, 0x7D97, SC_Han, GC_Lo, NULL}, //  綖..綗
    {0x7D99, 0x7DA3, SC_Han, GC_Lo, NULL}, //  継..綣
    {0x7DA5, 0x7DA7, SC_Han, GC_Lo, NULL}, //  綥..綧
    {0x7DA9, 0x7DCC, SC_Han, GC_Lo, NULL}, //  綩..緌
    {0x7DCE, 0x7DD2, SC_Han, GC_Lo, NULL}, //  緎..緒
    {0x7DD4, 0x7DE4, SC_Han, GC_Lo, NULL}, //  緔..緤
    {0x7DE6, 0x7DEA, SC_Han, GC_Lo, NULL}, //  緦..緪
    {0x7DEC, 0x7DFC, SC_Han, GC_Lo, NULL}, //  緬..緼
    {0x7E00, 0x7E17, SC_Han, GC_Lo, NULL}, //  縀..縗
    {0x7E19, 0x7E5A, SC_Han, GC_Lo, NULL}, //  縙..繚
    {0x7E5C, 0x7E63, SC_Han, GC_Lo, NULL}, //  繜..繣
    {0x7E65, 0x7E9C, SC_Han, GC_Lo, NULL}, //  繥..纜
    {0x7E9E, 0x7F3A, SC_Han, GC_Lo, NULL}, //  纞..缺
    {0x7F3D, 0x7F40, SC_Han, GC_Lo, NULL}, //  缽..罀
    {0x7F42, 0x7F45, SC_Han, GC_Lo, NULL}, //  罂..罅
    {0x7F47, 0x7F58, SC_Han, GC_Lo, NULL}, //  罇..罘
    {0x7F5A, 0x7F83, SC_Han, GC_Lo, NULL}, //  罚..羃
    {0x7F85, 0x7F8F, SC_Han, GC_Lo, NULL}, //  羅..羏
    {0x7F91, 0x7F96, SC_Han, GC_Lo, NULL}, //  羑..羖
    {0x7F98, 0x7F98, SC_Han, GC_Lo, NULL}, //  羘
    {0x7F9A, 0x7FB3, SC_Han, GC_Lo, NULL}, //  羚..羳
    {0x7FB5, 0x7FD5, SC_Han, GC_Lo, NULL}, //  羵..翕
    {0x7FD7, 0x7FDC, SC_Han, GC_Lo, NULL}, //  翗..翜
    {0x7FDE, 0x7FE3, SC_Han, GC_Lo, NULL}, //  翞..翣
    {0x7FE5, 0x8009, SC_Han, GC_Lo, NULL}, //  翥..耉
    {0x800B, 0x802E, SC_Han, GC_Lo, NULL}, //  耋..耮
    {0x8030, 0x803B, SC_Han, GC_Lo, NULL}, //  耰..耻
    {0x803D, 0x803F, SC_Han, GC_Lo, NULL}, //  耽..耿
    {0x8041, 0x8065, SC_Han, GC_Lo, NULL}, //  聁..聥
    {0x8067, 0x8087, SC_Han, GC_Lo, NULL}, //  聧..肇
    {0x8089, 0x808D, SC_Han, GC_Lo, NULL}, //  肉..肍
    {0x808F, 0x8093, SC_Han, GC_Lo, NULL}, //  肏..肓
    {0x8095, 0x80A5, SC_Han, GC_Lo, NULL}, //  肕..肥
    {0x80A9, 0x80B2, SC_Han, GC_Lo, NULL}, //  肩..育
    {0x80B4, 0x80B8, SC_Han, GC_Lo, NULL}, //  肴..肸
    {0x80BA, 0x80DE, SC_Han, GC_Lo, NULL}, //  肺..胞
    {0x80E0, 0x8102, SC_Han, GC_Lo, NULL}, //  胠..脂
    {0x8105, 0x8133, SC_Han, GC_Lo, NULL}, //  脅..脳
    {0x8136, 0x8183, SC_Han, GC_Lo, NULL}, //  脶..膃
    {0x8185, 0x818F, SC_Han, GC_Lo, NULL}, //  膅..膏
    {0x8191, 0x8195, SC_Han, GC_Lo, NULL}, //  膑..膕
    {0x8197, 0x81CA, SC_Han, GC_Lo, NULL}, //  膗..臊
    {0x81CC, 0x81E3, SC_Han, GC_Lo, NULL}, //  臌..臣
    {0x81E5, 0x81EE, SC_Han, GC_Lo, NULL}, //  臥..臮
    {0x81F1, 0x8212, SC_Han, GC_Lo, NULL}, //  臱..舒
    {0x8214, 0x8223, SC_Han, GC_Lo, NULL}, //  舔..舣
    {0x8225, 0x8240, SC_Han, GC_Lo, NULL}, //  舥..艀
    {0x8242, 0x8264, SC_Han, GC_Lo, NULL}, //  艂..艤
    {0x8266, 0x828B, SC_Han, GC_Lo, NULL}, //  艦..芋
    {0x828D, 0x82B1, SC_Han, GC_Lo, NULL}, //  芍..花
    {0x82B3, 0x82E1, SC_Han, GC_Lo, NULL}, //  芳..苡
    {0x82E3, 0x82FB, SC_Han, GC_Lo, NULL}, //  苣..苻
    {0x82FD, 0x8309, SC_Han, GC_Lo, NULL}, //  苽..茉
    {0x830B, 0x830F, SC_Han, GC_Lo, NULL}, //  茋..茏
    {0x8311, 0x832F, SC_Han, GC_Lo, NULL}, //  茑..茯
    {0x8331, 0x8354, SC_Han, GC_Lo, NULL}, //  茱..荔
    {0x8356, 0x83BD, SC_Han, GC_Lo, NULL}, //  荖..莽
    {0x83BF, 0x83E5, SC_Han, GC_Lo, NULL}, //  莿..菥
    {0x83E7, 0x83EC, SC_Han, GC_Lo, NULL}, //  菧..菬
    {0x83EE, 0x8413, SC_Han, GC_Lo, NULL}, //  菮..萓
    {0x8415, 0x8415, SC_Han, GC_Lo, NULL}, //  萕
    {0x8418, 0x841E, SC_Han, GC_Lo, NULL}, //  萘..萞
    {0x8420, 0x8457, SC_Han, GC_Lo, NULL}, //  萠..著
    {0x8459, 0x8482, SC_Han, GC_Lo, NULL}, //  葙..蒂
    {0x8484, 0x8494, SC_Han, GC_Lo, NULL}, //  蒄..蒔
    {0x8496, 0x84B6, SC_Han, GC_Lo, NULL}, //  蒖..蒶
    {0x84B8, 0x84C2, SC_Han, GC_Lo, NULL}, //  蒸..蓂
    {0x84C4, 0x84EC, SC_Han, GC_Lo, NULL}, //  蓄..蓬
    {0x84EE, 0x8504, SC_Han, GC_Lo, NULL}, //  蓮..蔄
    {0x8506, 0x850F, SC_Han, GC_Lo, NULL}, //  蔆..蔏
    {0x8511, 0x8531, SC_Han, GC_Lo, NULL}, //  蔑..蔱
    {0x8534, 0x854B, SC_Han, GC_Lo, NULL}, //  蔴..蕋
    {0x854D, 0x854F, SC_Han, GC_Lo, NULL}, //  蕍..蕏
    {0x8551, 0x857E, SC_Han, GC_Lo, NULL}, //  蕑..蕾
    {0x8580, 0x8592, SC_Han, GC_Lo, NULL}, //  薀..薒
    {0x8594, 0x85B1, SC_Han, GC_Lo, NULL}, //  薔..薱
    {0x85B3, 0x85BA, SC_Han, GC_Lo, NULL}, //  薳..薺
    {0x85BC, 0x85CB, SC_Han, GC_Lo, NULL}, //  薼..藋
    {0x85CD, 0x85ED, SC_Han, GC_Lo, NULL}, //  藍..藭
    {0x85EF, 0x85F2, SC_Han, GC_Lo, NULL}, //  藯..藲
    {0x85F4, 0x85FB, SC_Han, GC_Lo, NULL}, //  藴..藻
    {0x85FD, 0x8602, SC_Han, GC_Lo, NULL}, //  藽..蘂
    {0x8604, 0x860C, SC_Han, GC_Lo, NULL}, //  蘄..蘌
    {0x860F, 0x860F, SC_Han, GC_Lo, NULL}, //  蘏
    {0x8611, 0x8614, SC_Han, GC_Lo, NULL}, //  蘑..蘔
    {0x8616, 0x861C, SC_Han, GC_Lo, NULL}, //  蘖..蘜
    {0x861E, 0x8636, SC_Han, GC_Lo, NULL}, //  蘞..蘶
    {0x8638, 0x8656, SC_Han, GC_Lo, NULL}, //  蘸..虖
    {0x8658, 0x8674, SC_Han, GC_Lo, NULL}, //  虘..虴
    {0x8676, 0x8688, SC_Han, GC_Lo, NULL}, //  虶..蚈
    {0x868A, 0x8691, SC_Han, GC_Lo, NULL}, //  蚊..蚑
    {0x8693, 0x869F, SC_Han, GC_Lo, NULL}, //  蚓..蚟
    {0x86A1, 0x86A5, SC_Han, GC_Lo, NULL}, //  蚡..蚥
    {0x86A7, 0x86D4, SC_Han, GC_Lo, NULL}, //  蚧..蛔
    {0x86D6, 0x86DF, SC_Han, GC_Lo, NULL}, //  蛖..蛟
    {0x86E1, 0x86E6, SC_Han, GC_Lo, NULL}, //  蛡..蛦
    {0x86E8, 0x86FC, SC_Han, GC_Lo, NULL}, //  蛨..蛼
    {0x86FE, 0x871C, SC_Han, GC_Lo, NULL}, //  蛾..蜜
    {0x871E, 0x872E, SC_Han, GC_Lo, NULL}, //  蜞..蜮
    {0x8730, 0x873C, SC_Han, GC_Lo, NULL}, //  蜰..蜼
    {0x873E, 0x8744, SC_Han, GC_Lo, NULL}, //  蜾..蝄
    {0x8746, 0x8770, SC_Han, GC_Lo, NULL}, //  蝆..蝰
    {0x8772, 0x878D, SC_Han, GC_Lo, NULL}, //  蝲..融
    {0x878F, 0x8798, SC_Han, GC_Lo, NULL}, //  螏..螘
    {0x879A, 0x87D9, SC_Han, GC_Lo, NULL}, //  螚..蟙
    {0x87DB, 0x87EF, SC_Han, GC_Lo, NULL}, //  蟛..蟯
    {0x87F1, 0x8806, SC_Han, GC_Lo, NULL}, //  蟱..蠆
    {0x8808, 0x8811, SC_Han, GC_Lo, NULL}, //  蠈..蠑
    {0x8813, 0x882C, SC_Han, GC_Lo, NULL}, //  蠓..蠬
    {0x882E, 0x8839, SC_Han, GC_Lo, NULL}, //  蠮..蠹
    {0x883B, 0x8846, SC_Han, GC_Lo, NULL}, //  蠻..衆
    {0x8848, 0x8857, SC_Han, GC_Lo, NULL}, //  衈..街
    {0x8859, 0x885B, SC_Han, GC_Lo, NULL}, //  衙..衛
    {0x885D, 0x885E, SC_Han, GC_Lo, NULL}, //  衝..衞
    {0x8860, 0x8879, SC_Han, GC_Lo, NULL}, //  衠..衹
    {0x887B, 0x88E5, SC_Han, GC_Lo, NULL}, //  衻..裥
    {0x88E7, 0x88E8, SC_Han, GC_Lo, NULL}, //  裧..裨
    {0x88EA, 0x88EC, SC_Han, GC_Lo, NULL}, //  裪..裬
    {0x88EE, 0x8902, SC_Han, GC_Lo, NULL}, //  裮..褂
    {0x8904, 0x890E, SC_Han, GC_Lo, NULL}, //  褄..褎
    {0x8910, 0x8923, SC_Han, GC_Lo, NULL}, //  褐..褣
    {0x8925, 0x8964, SC_Han, GC_Lo, NULL}, //  褥..襤
    {0x8966, 0x8974, SC_Han, GC_Lo, NULL}, //  襦..襴
    {0x8976, 0x897C, SC_Han, GC_Lo, NULL}, //  襶..襼
    {0x897E, 0x898C, SC_Han, GC_Lo, NULL}, //  襾..覌
    {0x898E, 0x898F, SC_Han, GC_Lo, NULL}, //  覎..規
    {0x8991, 0x8993, SC_Han, GC_Lo, NULL}, //  覑..覓
    {0x8995, 0x8998, SC_Han, GC_Lo, NULL}, //  覕..覘
    {0x899A, 0x89AF, SC_Han, GC_Lo, NULL}, //  覚..覯
    {0x89B1, 0x89B3, SC_Han, GC_Lo, NULL}, //  覱..観
    {0x89B5, 0x89BA, SC_Han, GC_Lo, NULL}, //  覵..覺
    {0x89BD, 0x89ED, SC_Han, GC_Lo, NULL}, //  覽..觭
    {0x89EF, 0x89F4, SC_Han, GC_Lo, NULL}, //  觯..觴
    {0x89F6, 0x89F8, SC_Han, GC_Lo, NULL}, //  觶..觸
    {0x89FA, 0x89FC, SC_Han, GC_Lo, NULL}, //  觺..觼
    {0x89FE, 0x8A04, SC_Han, GC_Lo, NULL}, //  觾..訄
    {0x8A07, 0x8A13, SC_Han, GC_Lo, NULL}, //  訇..訓
    {0x8A15, 0x8A18, SC_Han, GC_Lo, NULL}, //  訕..記
    {0x8A1A, 0x8A1F, SC_Han, GC_Lo, NULL}, //  訚..訟
    {0x8A22, 0x8A2A, SC_Han, GC_Lo, NULL}, //  訢..訪
    {0x8A2C, 0x8A3C, SC_Han, GC_Lo, NULL}, //  訬..証
    {0x8A3E, 0x8A4A, SC_Han, GC_Lo, NULL}, //  訾..詊
    {0x8A4C, 0x8A63, SC_Han, GC_Lo, NULL}, //  詌..詣
    {0x8A65, 0x8A77, SC_Han, GC_Lo, NULL}, //  詥..詷
    {0x8A79, 0x8A7C, SC_Han, GC_Lo, NULL}, //  詹..詼
    {0x8A7E, 0x8A87, SC_Han, GC_Lo, NULL}, //  詾..誇
    {0x8A89, 0x8A9E, SC_Han, GC_Lo, NULL}, //  誉..語
    {0x8AA0, 0x8AAE, SC_Han, GC_Lo, NULL}, //  誠..誮
    {0x8AB0, 0x8AB6, SC_Han, GC_Lo, NULL}, //  誰..誶
    {0x8AB8, 0x8ACF, SC_Han, GC_Lo, NULL}, //  誸..諏
    {0x8AD1, 0x8AEB, SC_Han, GC_Lo, NULL}, //  諑..諫
    {0x8AED, 0x8B28, SC_Han, GC_Lo, NULL}, //  諭..謨
    {0x8B2A, 0x8B31, SC_Han, GC_Lo, NULL}, //  謪..謱
    {0x8B33, 0x8B37, SC_Han, GC_Lo, NULL}, //  謳..謷
    {0x8B39, 0x8B3E, SC_Han, GC_Lo, NULL}, //  謹..謾
    {0x8B40, 0x8B60, SC_Han, GC_Lo, NULL}, //  譀..譠
    {0x8B63, 0x8B68, SC_Han, GC_Lo, NULL}, //  譣..譨
    {0x8B6A, 0x8B74, SC_Han, GC_Lo, NULL}, //  譪..譴
    {0x8B76, 0x8B7B, SC_Han, GC_Lo, NULL}, //  譶..譻
    {0x8B7D, 0x8B80, SC_Han, GC_Lo, NULL}, //  譽..讀
    {0x8B82, 0x8B86, SC_Han, GC_Lo, NULL}, //  讂..讆
    {0x8B88, 0x8B8C, SC_Han, GC_Lo, NULL}, //  讈..讌
    {0x8B8E, 0x8B8E, SC_Han, GC_Lo, NULL}, //  讎
    {0x8B90, 0x8B9A, SC_Han, GC_Lo, NULL}, //  讐..讚
    {0x8B9C, 0x8C37, SC_Han, GC_Lo, NULL}, //  讜..谷
    {0x8C39, 0x8C3F, SC_Han, GC_Lo, NULL}, //  谹..谿
    {0x8C41, 0x8C43, SC_Han, GC_Lo, NULL}, //  豁..豃
    {0x8C45, 0x8C50, SC_Han, GC_Lo, NULL}, //  豅..豐
    {0x8C54, 0x8C57, SC_Han, GC_Lo, NULL}, //  豔..豗
    {0x8C59, 0x8C73, SC_Han, GC_Lo, NULL}, //  豙..豳
    {0x8C75, 0x8C7E, SC_Han, GC_Lo, NULL}, //  豵..豾
    {0x8C80, 0x8C82, SC_Han, GC_Lo, NULL}, //  貀..貂
    {0x8C84, 0x8C86, SC_Han, GC_Lo, NULL}, //  貄..貆
    {0x8C88, 0x8C8A, SC_Han, GC_Lo, NULL}, //  貈..貊
    {0x8C8C, 0x8C9A, SC_Han, GC_Lo, NULL}, //  貌..貚
    {0x8C9C, 0x8CA5, SC_Han, GC_Lo, NULL}, //  貜..貥
    {0x8CA7, 0x8CCA, SC_Han, GC_Lo, NULL}, //  貧..賊
    {0x8CCC, 0x8CD5, SC_Han, GC_Lo, NULL}, //  賌..賕
    {0x8CD7, 0x8CD7, SC_Han, GC_Lo, NULL}, //  賗
    {0x8CD9, 0x8CE8, SC_Han, GC_Lo, NULL}, //  賙..賨
    {0x8CEA, 0x8CF6, SC_Han, GC_Lo, NULL}, //  質..賶
    {0x8CF8, 0x8D00, SC_Han, GC_Lo, NULL}, //  賸..贀
    {0x8D02, 0x8D10, SC_Han, GC_Lo, NULL}, //  贂..贐
    {0x8D13, 0x8D7B, SC_Han, GC_Lo, NULL}, //  贓..赻
    {0x8D7D, 0x8DA5, SC_Han, GC_Lo, NULL}, //  赽..趥
    {0x8DA7, 0x8DBF, SC_Han, GC_Lo, NULL}, //  趧..趿
    {0x8DC1, 0x8DE4, SC_Han, GC_Lo, NULL}, //  跁..跤
    {0x8DE6, 0x8E00, SC_Han, GC_Lo, NULL}, //  跦..踀
    {0x8E02, 0x8E0A, SC_Han, GC_Lo, NULL}, //  踂..踊
    {0x8E0C, 0x8E31, SC_Han, GC_Lo, NULL}, //  踌..踱
    {0x8E33, 0x8E45, SC_Han, GC_Lo, NULL}, //  踳..蹅
    {0x8E47, 0x8E4E, SC_Han, GC_Lo, NULL}, //  蹇..蹎
    {0x8E50, 0x8E6D, SC_Han, GC_Lo, NULL}, //  蹐..蹭
    {0x8E6F, 0x8E74, SC_Han, GC_Lo, NULL}, //  蹯..蹴
    {0x8E76, 0x8E76, SC_Han, GC_Lo, NULL}, //  蹶
    {0x8E78, 0x8E78, SC_Han, GC_Lo, NULL}, //  蹸
    {0x8E7A, 0x8E9A, SC_Han, GC_Lo, NULL}, //  蹺..躚
    {0x8E9C, 0x8EA1, SC_Han, GC_Lo, NULL}, //  躜..躡
    {0x8EA3, 0x8EB2, SC_Han, GC_Lo, NULL}, //  躣..躲
    {0x8EB4, 0x8EB5, SC_Han, GC_Lo, NULL}, //  躴..躵
    {0x8EB8, 0x8EC0, SC_Han, GC_Lo, NULL}, //  躸..軀
    {0x8EC2, 0x8EC3, SC_Han, GC_Lo, NULL}, //  軂..軃
    {0x8EC5, 0x8ED8, SC_Han, GC_Lo, NULL}, //  軅..軘
    {0x8EDA, 0x8EEF, SC_Han, GC_Lo, NULL}, //  軚..軯
    {0x8EF1, 0x8F0E, SC_Han, GC_Lo, NULL}, //  軱..輎
    {0x8F10, 0x8F2C, SC_Han, GC_Lo, NULL}, //  輐..輬
    {0x8F2E, 0x8F39, SC_Han, GC_Lo, NULL}, //  輮..輹
    {0x8F3B, 0x8F40, SC_Han, GC_Lo, NULL}, //  輻..轀
    {0x8F42, 0x8F9C, SC_Han, GC_Lo, NULL}, //  轂..辜
    {0x8F9E, 0x8FA3, SC_Han, GC_Lo, NULL}, //  辞..辣
    {0x8FA5, 0x8FB2, SC_Han, GC_Lo, NULL}, //  辥..農
    {0x8FB4, 0x8FC2, SC_Han, GC_Lo, NULL}, //  辴..迂
    {0x8FC4, 0x8FC9, SC_Han, GC_Lo, NULL}, //  迄..迉
    {0x8FCB, 0x8FE6, SC_Han, GC_Lo, NULL}, //  迋..迦
    {0x8FE8, 0x9029, SC_Han, GC_Lo, NULL}, //  迨..逩
    {0x902B, 0x902B, SC_Han, GC_Lo, NULL}, //  逫
    {0x902D, 0x9036, SC_Han, GC_Lo, NULL}, //  逭..逶
    {0x9038, 0x903F, SC_Han, GC_Lo, NULL}, //  逸..逿
    {0x9041, 0x9045, SC_Han, GC_Lo, NULL}, //  遁..遅
    {0x9047, 0x90AA, SC_Han, GC_Lo, NULL}, //  遇..邪
    {0x90AC, 0x90CB, SC_Han, GC_Lo, NULL}, //  邬..郋
    {0x90CE, 0x90D1, SC_Han, GC_Lo, NULL}, //  郎..郑
    {0x90D3, 0x90F5, SC_Han, GC_Lo, NULL}, //  郓..郵
    {0x90F7, 0x9109, SC_Han, GC_Lo, NULL}, //  郷..鄉
    {0x910B, 0x913B, SC_Han, GC_Lo, NULL}, //  鄋..鄻
    {0x913E, 0x9158, SC_Han, GC_Lo, NULL}, //  鄾..酘
    {0x915A, 0x917A, SC_Han, GC_Lo, NULL}, //  酚..酺
    {0x917C, 0x9194, SC_Han, GC_Lo, NULL}, //  酼..醔
    {0x9196, 0x9197, SC_Han, GC_Lo, NULL}, //  醖..醗
    {0x9199, 0x91A8, SC_Han, GC_Lo, NULL}, //  醙..醨
    {0x91AA, 0x91BE, SC_Han, GC_Lo, NULL}, //  醪..醾
    {0x91C0, 0x91C3, SC_Han, GC_Lo, NULL}, //  釀..釃
    {0x91C5, 0x91DF, SC_Han, GC_Lo, NULL}, //  釅..釟
    {0x91E1, 0x91EE, SC_Han, GC_Lo, NULL}, //  釡..釮
    {0x91F0, 0x9212, SC_Han, GC_Lo, NULL}, //  釰..鈒
    {0x9214, 0x921E, SC_Han, GC_Lo, NULL}, //  鈔..鈞
    {0x9220, 0x9221, SC_Han, GC_Lo, NULL}, //  鈠..鈡
    {0x9223, 0x9242, SC_Han, GC_Lo, NULL}, //  鈣..鉂
    {0x9244, 0x9268, SC_Han, GC_Lo, NULL}, //  鉄..鉨
    {0x926B, 0x9280, SC_Han, GC_Lo, NULL}, //  鉫..銀
    {0x9282, 0x9283, SC_Han, GC_Lo, NULL}, //  銂..銃
    {0x9285, 0x929D, SC_Han, GC_Lo, NULL}, //  銅..銝
    {0x929F, 0x92BC, SC_Han, GC_Lo, NULL}, //  銟..銼
    {0x92BE, 0x92D3, SC_Han, GC_Lo, NULL}, //  銾..鋓
    {0x92D5, 0x92DA, SC_Han, GC_Lo, NULL}, //  鋕..鋚
    {0x92DC, 0x92E1, SC_Han, GC_Lo, NULL}, //  鋜..鋡
    {0x92E3, 0x931B, SC_Han, GC_Lo, NULL}, //  鋣..錛
    {0x931D, 0x932F, SC_Han, GC_Lo, NULL}, //  錝..錯
    {0x9332, 0x9361, SC_Han, GC_Lo, NULL}, //  録..鍡
    {0x9363, 0x9367, SC_Han, GC_Lo, NULL}, //  鍣..鍧
    {0x9369, 0x936A, SC_Han, GC_Lo, NULL}, //  鍩..鍪
    {0x936C, 0x936E, SC_Han, GC_Lo, NULL}, //  鍬..鍮
    {0x9370, 0x9372, SC_Han, GC_Lo, NULL}, //  鍰..鍲
    {0x9374, 0x9377, SC_Han, GC_Lo, NULL}, //  鍴..鍷
    {0x9379, 0x937E, SC_Han, GC_Lo, NULL}, //  鍹..鍾
    {0x9380, 0x9380, SC_Han, GC_Lo, NULL}, //  鎀
    {0x9382, 0x938A, SC_Han, GC_Lo, NULL}, //  鎂..鎊
    {0x938C, 0x939B, SC_Han, GC_Lo, NULL}, //  鎌..鎛
    {0x939D, 0x939F, SC_Han, GC_Lo, NULL}, //  鎝..鎟
    {0x93A1, 0x93AA, SC_Han, GC_Lo, NULL}, //  鎡..鎪
    {0x93AC, 0x93BA, SC_Han, GC_Lo, NULL}, //  鎬..鎺
    {0x93BC, 0x93DF, SC_Han, GC_Lo, NULL}, //  鎼..鏟
    {0x93E1, 0x93F2, SC_Han, GC_Lo, NULL}, //  鏡..鏲
    {0x93F4, 0x9401, SC_Han, GC_Lo, NULL}, //  鏴..鐁
    {0x9403, 0x9416, SC_Han, GC_Lo, NULL}, //  鐃..鐖
    {0x9418, 0x941B, SC_Han, GC_Lo, NULL}, //  鐘..鐛
    {0x941D, 0x941D, SC_Han, GC_Lo, NULL}, //  鐝
    {0x9420, 0x9423, SC_Han, GC_Lo, NULL}, //  鐠..鐣
    {0x9425, 0x9442, SC_Han, GC_Lo, NULL}, //  鐥..鑂
    {0x9444, 0x944D, SC_Han, GC_Lo, NULL}, //  鑄..鑍
    {0x944F, 0x946B, SC_Han, GC_Lo, NULL}, //  鑏..鑫
    {0x946D, 0x947A, SC_Han, GC_Lo, NULL}, //  鑭..鑺
    {0x947C, 0x9577, SC_Han, GC_Lo, NULL}, //  鑼..長
    {0x957A, 0x957D, SC_Han, GC_Lo, NULL}, //  镺..镽
    {0x957F, 0x9584, SC_Han, GC_Lo, NULL}, //  长..閄
    {0x9586, 0x9596, SC_Han, GC_Lo, NULL}, //  閆..閖
    {0x9598, 0x95B2, SC_Han, GC_Lo, NULL}, //  閘..閲
    {0x95B5, 0x95B7, SC_Han, GC_Lo, NULL}, //  閵..閷
    {0x95B9, 0x95C0, SC_Han, GC_Lo, NULL}, //  閹..闀
    {0x95C2, 0x95D8, SC_Han, GC_Lo, NULL}, //  闂..闘
    {0x95DA, 0x95DC, SC_Han, GC_Lo, NULL}, //  闚..關
    {0x95DE, 0x9624, SC_Han, GC_Lo, NULL}, //  闞..阤
    {0x9627, 0x9628, SC_Han, GC_Lo, NULL}, //  阧..阨
    {0x962A, 0x963D, SC_Han, GC_Lo, NULL}, //  阪..阽
    {0x963F, 0x9655, SC_Han, GC_Lo, NULL}, //  阿..陕
    {0x9658, 0x9678, SC_Han, GC_Lo, NULL}, //  陘..陸
    {0x967A, 0x967A, SC_Han, GC_Lo, NULL}, //  険
    {0x967C, 0x967E, SC_Han, GC_Lo, NULL}, //  陼..陾
    {0x9680, 0x9680, SC_Han, GC_Lo, NULL}, //  隀
    {0x9683, 0x968B, SC_Han, GC_Lo, NULL}, //  隃..隋
    {0x968D, 0x9695, SC_Han, GC_Lo, NULL}, //  隍..隕
    {0x9697, 0x9699, SC_Han, GC_Lo, NULL}, //  隗..隙
    {0x969B, 0x969C, SC_Han, GC_Lo, NULL}, //  際..障
    {0x969E, 0x969E, SC_Han, GC_Lo, NULL}, //  隞
    {0x96A0, 0x96AA, SC_Han, GC_Lo, NULL}, //  隠..險
    {0x96AC, 0x96AE, SC_Han, GC_Lo, NULL}, //  隬..隮
    {0x96B0, 0x96B4, SC_Han, GC_Lo, NULL}, //  隰..隴
    {0x96B6, 0x96E3, SC_Han, GC_Lo, NULL}, //  隶..難
    {0x96E5, 0x96E5, SC_Han, GC_Lo, NULL}, //  雥
    {0x96E8, 0x96FB, SC_Han, GC_Lo, NULL}, //  雨..電
    {0x96FD, 0x9713, SC_Han, GC_Lo, NULL}, //  雽..霓
    {0x9715, 0x9716, SC_Han, GC_Lo, NULL}, //  霕..霖
    {0x9718, 0x9719, SC_Han, GC_Lo, NULL}, //  霘..霙
    {0x971C, 0x9732, SC_Han, GC_Lo, NULL}, //  霜..露
    {0x9735, 0x9736, SC_Han, GC_Lo, NULL}, //  霵..霶
    {0x9738, 0x973F, SC_Han, GC_Lo, NULL}, //  霸..霿
    {0x9742, 0x974C, SC_Han, GC_Lo, NULL}, //  靂..靌
    {0x974E, 0x9756, SC_Han, GC_Lo, NULL}, //  靎..靖
    {0x9758, 0x9762, SC_Han, GC_Lo, NULL}, //  靘..面
    {0x9764, 0x9774, SC_Han, GC_Lo, NULL}, //  靤..靴
    {0x9776, 0x9786, SC_Han, GC_Lo, NULL}, //  靶..鞆
    {0x9788, 0x9788, SC_Han, GC_Lo, NULL}, //  鞈
    {0x978A, 0x979A, SC_Han, GC_Lo, NULL}, //  鞊..鞚
    {0x979C, 0x97A8, SC_Han, GC_Lo, NULL}, //  鞜..鞨
    {0x97AA, 0x97AF, SC_Han, GC_Lo, NULL}, //  鞪..鞯
    {0x97B2, 0x97B4, SC_Han, GC_Lo, NULL}, //  鞲..鞴
    {0x97B6, 0x97BD, SC_Han, GC_Lo, NULL}, //  鞶..鞽
    {0x97BF, 0x97BF, SC_Han, GC_Lo, NULL}, //  鞿
    {0x97C1, 0x97D1, SC_Han, GC_Lo, NULL}, //  韁..韑
    {0x97D3, 0x97FB, SC_Han, GC_Lo, NULL}, //  韓..韻
    {0x97FD, 0x981E, SC_Han, GC_Lo, NULL}, //  韽..頞
    {0x9820, 0x9824, SC_Han, GC_Lo, NULL}, //  頠..頤
    {0x9826, 0x9829, SC_Han, GC_Lo, NULL}, //  頦..頩
    {0x982B, 0x9832, SC_Han, GC_Lo, NULL}, //  頫..頲
    {0x9834, 0x9839, SC_Han, GC_Lo, NULL}, //  頴..頹
    {0x983B, 0x983D, SC_Han, GC_Lo, NULL}, //  頻..頽
    {0x983F, 0x9841, SC_Han, GC_Lo, NULL}, //  頿..顁
    {0x9843, 0x9846, SC_Han, GC_Lo, NULL}, //  顃..顆
    {0x9848, 0x9855, SC_Han, GC_Lo, NULL}, //  顈..顕
    {0x9857, 0x9865, SC_Han, GC_Lo, NULL}, //  顗..顥
    {0x9867, 0x9867, SC_Han, GC_Lo, NULL}, //  顧
    {0x9869, 0x98B6, SC_Han, GC_Lo, NULL}, //  顩..颶
    {0x98B8, 0x98C9, SC_Han, GC_Lo, NULL}, //  颸..飉
    {0x98CB, 0x98E3, SC_Han, GC_Lo, NULL}, //  飋..飣
    {0x98E5, 0x98EB, SC_Han, GC_Lo, NULL}, //  飥..飫
    {0x98ED, 0x98F0, SC_Han, GC_Lo, NULL}, //  飭..飰
    {0x98F2, 0x98F7, SC_Han, GC_Lo, NULL}, //  飲..飷
    {0x98F9, 0x98FA, SC_Han, GC_Lo, NULL}, //  飹..飺
    {0x98FC, 0x9918, SC_Han, GC_Lo, NULL}, //  飼..餘
    {0x991A, 0x993A, SC_Han, GC_Lo, NULL}, //  餚..餺
    {0x993C, 0x9943, SC_Han, GC_Lo, NULL}, //  餼..饃
    {0x9945, 0x9959, SC_Han, GC_Lo, NULL}, //  饅..饙
    {0x995B, 0x995C, SC_Han, GC_Lo, NULL}, //  饛..饜
    {0x995E, 0x99BE, SC_Han, GC_Lo, NULL}, //  饞..馾
    {0x99C0, 0x99DF, SC_Han, GC_Lo, NULL}, //  駀..駟
    {0x99E1, 0x99E5, SC_Han, GC_Lo, NULL}, //  駡..駥
    {0x99E7, 0x99EA, SC_Han, GC_Lo, NULL}, //  駧..駪
    {0x99EC, 0x99F4, SC_Han, GC_Lo, NULL}, //  駬..駴
    {0x99F6, 0x9A0F, SC_Han, GC_Lo, NULL}, //  駶..騏
    {0x9A11, 0x9A16, SC_Han, GC_Lo, NULL}, //  騑..騖
    {0x9A19, 0x9A3A, SC_Han, GC_Lo, NULL}, //  騙..騺
    {0x9A3C, 0x9A50, SC_Han, GC_Lo, NULL}, //  騼..驐
    {0x9A52, 0x9A57, SC_Han, GC_Lo, NULL}, //  驒..驗
    {0x9A59, 0x9A5C, SC_Han, GC_Lo, NULL}, //  驙..驜
    {0x9A5E, 0x9A62, SC_Han, GC_Lo, NULL}, //  驞..驢
    {0x9A64, 0x9AA8, SC_Han, GC_Lo, NULL}, //  驤..骨
    {0x9AAA, 0x9ABC, SC_Han, GC_Lo, NULL}, //  骪..骼
    {0x9ABE, 0x9AC7, SC_Han, GC_Lo, NULL}, //  骾..髇
    {0x9AC9, 0x9AD6, SC_Han, GC_Lo, NULL}, //  髉..髖
    {0x9AD8, 0x9ADF, SC_Han, GC_Lo, NULL}, //  高..髟
    {0x9AE1, 0x9AE3, SC_Han, GC_Lo, NULL}, //  髡..髣
    {0x9AE5, 0x9AE7, SC_Han, GC_Lo, NULL}, //  髥..髧
    {0x9AEA, 0x9AEF, SC_Han, GC_Lo, NULL}, //  髪..髯
    {0x9AF1, 0x9AFF, SC_Han, GC_Lo, NULL}, //  髱..髿
    {0x9B01, 0x9B01, SC_Han, GC_Lo, NULL}, //  鬁
    {0x9B03, 0x9B08, SC_Han, GC_Lo, NULL}, //  鬃..鬈
    {0x9B0A, 0x9B13, SC_Han, GC_Lo, NULL}, //  鬊..鬓
    {0x9B15, 0x9B1A, SC_Han, GC_Lo, NULL}, //  鬕..鬚
    {0x9B1C, 0x9B33, SC_Han, GC_Lo, NULL}, //  鬜..鬳
    {0x9B35, 0x9B3C, SC_Han, GC_Lo, NULL}, //  鬵..鬼
    {0x9B3E, 0x9B3F, SC_Han, GC_Lo, NULL}, //  鬾..鬿
    {0x9B41, 0x9B4F, SC_Han, GC_Lo, NULL}, //  魁..魏
    {0x9B51, 0x9B56, SC_Han, GC_Lo, NULL}, //  魑..魖
    {0x9B58, 0x9B61, SC_Han, GC_Lo, NULL}, //  魘..魡
    {0x9B63, 0x9B71, SC_Han, GC_Lo, NULL}, //  魣..魱
    {0x9B73, 0x9B88, SC_Han, GC_Lo, NULL}, //  魳..鮈
    {0x9B8A, 0x9B8B, SC_Han, GC_Lo, NULL}, //  鮊..鮋
    {0x9B8D, 0x9B98, SC_Han, GC_Lo, NULL}, //  鮍..鮘
    {0x9B9A, 0x9BC1, SC_Han, GC_Lo, NULL}, //  鮚..鯁
    {0x9BC3, 0x9BF5, SC_Han, GC_Lo, NULL}, //  鯃..鯵
    {0x9BF7, 0x9BFF, SC_Han, GC_Lo, NULL}, //  鯷..鯿
    {0x9C02, 0x9C02, SC_Han, GC_Lo, NULL}, //  鰂
    {0x9C04, 0x9C41, SC_Han, GC_Lo, NULL}, //  鰄..鱁
    {0x9C43, 0x9C4E, SC_Han, GC_Lo, NULL}, //  鱃..鱎
    {0x9C50, 0x9C50, SC_Han, GC_Lo, NULL}, //  鱐
    {0x9C52, 0x9C60, SC_Han, GC_Lo, NULL}, //  鱒..鱠
    {0x9C62, 0x9C63, SC_Han, GC_Lo, NULL}, //  鱢..鱣
    {0x9C65, 0x9C7A, SC_Han, GC_Lo, NULL}, //  鱥..鱺
    {0x9C7C, 0x9D0B, SC_Han, GC_Lo, NULL}, //  鱼..鴋
    {0x9D0E, 0x9D10, SC_Han, GC_Lo, NULL}, //  鴎..鴐
    {0x9D12, 0x9D26, SC_Han, GC_Lo, NULL}, //  鴒..鴦
    {0x9D28, 0x9D34, SC_Han, GC_Lo, NULL}, //  鴨..鴴
    {0x9D36, 0x9D3B, SC_Han, GC_Lo, NULL}, //  鴶..鴻
    {0x9D3D, 0x9D6C, SC_Han, GC_Lo, NULL}, //  鴽..鵬
    {0x9D6E, 0x9D94, SC_Han, GC_Lo, NULL}, //  鵮..鶔
    {0x9D96, 0x9DAD, SC_Han, GC_Lo, NULL}, //  鶖..鶭
    {0x9DAF, 0x9DBC, SC_Han, GC_Lo, NULL}, //  鶯..鶼
    {0x9DBE, 0x9DBF, SC_Han, GC_Lo, NULL}, //  鶾..鶿
    {0x9DC1, 0x9DE9, SC_Han, GC_Lo, NULL}, //  鷁..鷩
    {0x9DEB, 0x9DFB, SC_Han, GC_Lo, NULL}, //  鷫..鷻
    {0x9DFD, 0x9E0D, SC_Han, GC_Lo, NULL}, //  鷽..鸍
    {0x9E0F, 0x9E15, SC_Han, GC_Lo, NULL}, //  鸏..鸕
    {0x9E17, 0x9E1B, SC_Han, GC_Lo, NULL}, //  鸗..鸛
    {0x9E1D, 0x9E7A, SC_Han, GC_Lo, NULL}, //  鸝..鹺
    {0x9E7C, 0x9E8E, SC_Han, GC_Lo, NULL}, //  鹼..麎
    {0x9E91, 0x9E97, SC_Han, GC_Lo, NULL}, //  麑..麗
    {0x9E99, 0x9E9D, SC_Han, GC_Lo, NULL}, //  麙..麝
    {0x9E9F, 0x9EA1, SC_Han, GC_Lo, NULL}, //  麟..麡
    {0x9EA3, 0x9EAA, SC_Han, GC_Lo, NULL}, //  麣..麪
    {0x9EAD, 0x9EB0, SC_Han, GC_Lo, NULL}, //  麭..麰
    {0x9EB2, 0x9EEB, SC_Han, GC_Lo, NULL}, //  麲..黫
    {0x9EED, 0x9EF0, SC_Han, GC_Lo, NULL}, //  黭..黰
    {0x9EF2, 0x9F02, SC_Han, GC_Lo, NULL}, //  黲..鼂
    {0x9F04, 0x9F10, SC_Han, GC_Lo, NULL}, //  鼄..鼐
    {0x9F12, 0x9F13, SC_Han, GC_Lo, NULL}, //  鼒..鼓
    {0x9F15, 0x9F25, SC_Han, GC_Lo, NULL}, //  鼕..鼥
    {0x9F27, 0x9F44, SC_Han, GC_Lo, NULL}, //  鼧..齄
    {0x9F46, 0x9F52, SC_Han, GC_Lo, NULL}, //  齆..齒
    {0x9F54, 0x9F6C, SC_Han, GC_Lo, NULL}, //  齔..齬
    {0x9F6E, 0x9FA0, SC_Han, GC_Lo, NULL}, //  齮..龠
    {0x9FA2, 0x9FA2, SC_Han, GC_Lo, NULL}, //  龢
    {0x9FA4, 0x9FA5, SC_Han, GC_Lo, NULL}, //  龤..龥
    {0xA717, 0xA71F, SC_Common, GC_Lm, NULL}, //  ꜗ..ꜟ
    {0xA788, 0xA788, SC_Common, GC_Lm, NULL}, //  ꞈ
    {0xA78D, 0xA78E, SC_Latin, GC_L, NULL}, //  Ɥ..ꞎ
    {0xA7AA, 0xA7AA, SC_Latin, GC_Lu, NULL}, //  Ɦ
    {0xA7AE, 0xA7AF, SC_Latin, GC_L, NULL}, //  Ɪ..ꞯ
    {0xA7BA, 0xA7BF, SC_Latin, GC_L, NULL}, //  Ꞻ..ꞿ
    {0xA7C5, 0xA7C6, SC_Latin, GC_Lu, NULL}, //  Ʂ..Ᶎ
    {0xA7FA, 0xA7FA, SC_Latin, GC_Ll, NULL}, //  ꟺ
    {0xAB68, 0xAB68, SC_Latin, GC_Ll, NULL}, //  ꭨ
    {0x10EC5, 0x10EC6, SC_Arabic, GC_L, NULL}, //  𐻅..𐻆
    {0x16FF2, 0x16FF6, SC_Han, GC_V, NULL}, //  𖿲..𖿶
    {0x1DF00, 0x1DF1E, SC_Latin, GC_L, NULL}, //  𝼀..𝼞
    {0x1DF25, 0x1DF2A, SC_Latin, GC_Ll, NULL}, //  𝼥..𝼪
    {0x1E7E0, 0x1E7E6, SC_Ethiopic, GC_Lo, NULL}, //  𞟠..𞟦
    {0x1E7E8, 0x1E7EB, SC_Ethiopic, GC_Lo, NULL}, //  𞟨..𞟫
    {0x1E7ED, 0x1E7EE, SC_Ethiopic, GC_Lo, NULL}, //  𞟭..𞟮
    {0x1E7F0, 0x1E7FE, SC_Ethiopic, GC_Lo, NULL}, //  𞟰..𞟾
    {0x2070E, 0x2070E, SC_Han, GC_Lo, NULL}, //  𠜎
    {0x20731, 0x20731, SC_Han, GC_Lo, NULL}, //  𠜱
    {0x20779, 0x20779, SC_Han, GC_Lo, NULL}, //  𠝹
    {0x20C53, 0x20C53, SC_Han, GC_Lo, NULL}, //  𠱓
    {0x20C78, 0x20C78, SC_Han, GC_Lo, NULL}, //  𠱸
    {0x20C96, 0x20C96, SC_Han, GC_Lo, NULL}, //  𠲖
    {0x20CCF, 0x20CCF, SC_Han, GC_Lo, NULL}, //  𠳏
    {0x20CD5, 0x20CD5, SC_Han, GC_Lo, NULL}, //  𠳕
    {0x20D15, 0x20D15, SC_Han, GC_Lo, NULL}, //  𠴕
    {0x20D7C, 0x20D7C, SC_Han, GC_Lo, NULL}, //  𠵼
    {0x20D7F, 0x20D7F, SC_Han, GC_Lo, NULL}, //  𠵿
    {0x20E0E, 0x20E0F, SC_Han, GC_Lo, NULL}, //  𠸎..𠸏
    {0x20E77, 0x20E77, SC_Han, GC_Lo, NULL}, //  𠹷
    {0x20E9D, 0x20E9D, SC_Han, GC_Lo, NULL}, //  𠺝
    {0x20EA2, 0x20EA2, SC_Han, GC_Lo, NULL}, //  𠺢
    {0x20ED7, 0x20ED7, SC_Han, GC_Lo, NULL}, //  𠻗
    {0x20EF9, 0x20EFA, SC_Han, GC_Lo, NULL}, //  𠻹..𠻺
    {0x20F2D, 0x20F2E, SC_Han, GC_Lo, NULL}, //  𠼭..𠼮
    {0x20F4C, 0x20F4C, SC_Han, GC_Lo, NULL}, //  𠽌
    {0x20FB4, 0x20FB4, SC_Han, GC_Lo, NULL}, //  𠾴
    {0x20FBC, 0x20FBC, SC_Han, GC_Lo, NULL}, //  𠾼
    {0x20FEA, 0x20FEA, SC_Han, GC_Lo, NULL}, //  𠿪
    {0x2105C, 0x2105C, SC_Han, GC_Lo, NULL}, //  𡁜
    {0x2106F, 0x2106F, SC_Han, GC_Lo, NULL}, //  𡁯
    {0x21075, 0x21076, SC_Han, GC_Lo, NULL}, //  𡁵..𡁶
    {0x2107B, 0x2107B, SC_Han, GC_Lo, NULL}, //  𡁻
    {0x210C1, 0x210C1, SC_Han, GC_Lo, NULL}, //  𡃁
    {0x210C9, 0x210C9, SC_Han, GC_Lo, NULL}, //  𡃉
    {0x211D9, 0x211D9, SC_Han, GC_Lo, NULL}, //  𡇙
    {0x220C7, 0x220C7, SC_Han, GC_Lo, NULL}, //  𢃇
    {0x227B5, 0x227B5, SC_Han, GC_Lo, NULL}, //  𢞵
    {0x22AD5, 0x22AD5, SC_Han, GC_Lo, NULL}, //  𢫕
    {0x22B43, 0x22B43, SC_Han, GC_Lo, NULL}, //  𢭃
    {0x22BCA, 0x22BCA, SC_Han, GC_Lo, NULL}, //  𢯊
    {0x22C51, 0x22C51, SC_Han, GC_Lo, NULL}, //  𢱑
    {0x22C55, 0x22C55, SC_Han, GC_Lo, NULL}, //  𢱕
    {0x22CC2, 0x22CC2, SC_Han, GC_Lo, NULL}, //  𢳂
    {0x22D08, 0x22D08, SC_Han, GC_Lo, NULL}, //  𢴈
    {0x22D4C, 0x22D4C, SC_Han, GC_Lo, NULL}, //  𢵌
    {0x22D67, 0x22D67, SC_Han, GC_Lo, NULL}, //  𢵧
    {0x22EB3, 0x22EB3, SC_Han, GC_Lo, NULL}, //  𢺳
    {0x23CB7, 0x23CB7, SC_Han, GC_Lo, NULL}, //  𣲷
    {0x244D3, 0x244D3, SC_Han, GC_Lo, NULL}, //  𤓓
    {0x24DB8, 0x24DB8, SC_Han, GC_Lo, NULL}, //  𤶸
    {0x24DEA, 0x24DEA, SC_Han, GC_Lo, NULL}, //  𤷪
    {0x2512B, 0x2512B, SC_Han, GC_Lo, NULL}, //  𥄫
    {0x26258, 0x26258, SC_Han, GC_Lo, NULL}, //  𦉘
    {0x267CC, 0x267CC, SC_Han, GC_Lo, NULL}, //  𦟌
    {0x269F2, 0x269F2, SC_Han, GC_Lo, NULL}, //  𦧲
    {0x269FA, 0x269FA, SC_Han, GC_Lo, NULL}, //  𦧺
    {0x27A3E, 0x27A3E, SC_Han, GC_Lo, NULL}, //  𧨾
    {0x2815D, 0x2815D, SC_Han, GC_Lo, NULL}, //  𨅝
    {0x28207, 0x28207, SC_Han, GC_Lo, NULL}, //  𨈇
    {0x282E2, 0x282E2, SC_Han, GC_Lo, NULL}, //  𨋢
    {0x28CCA, 0x28CCA, SC_Han, GC_Lo, NULL}, //  𨳊
    {0x28CCD, 0x28CCD, SC_Han, GC_Lo, NULL}, //  𨳍
    {0x28CD2, 0x28CD2, SC_Han, GC_Lo, NULL}, //  𨳒
    {0x29D98, 0x29D98, SC_Han, GC_Lo, NULL}, //  𩶘
}; // 1279 ranges, 282 singles, 20907 codepoints

// Filtering allowed scripts, XID_Continue,!XID_Start, safe IDTypes, NFC
// MEDIAL from XID_Start and !MARK. Split on GC and SCX
static const struct sc_tr39 tr39_cont_list[] = {
    {0x30, 0x39, SC_Common, GC_Nd, NULL}, //  0..9
    {0x5F, 0x5F, SC_Common, GC_Pc, NULL}, //  _
    {0x660, 0x669, SC_Arabic, GC_Nd, "\x03\x1c\x8a"}, //Arabic,Thaana,Yezidi //  ٠..٩
    {0x6F0, 0x6F9, SC_Arabic, GC_Nd, NULL}, //  ۰..۹
    {0x966, 0x96F, SC_Devanagari, GC_Nd, "\x08\x32\x42\x4d"}, //Devanagari,Dogra,Kaithi,Mahajani //  ०..९
    {0x9E6, 0x9EF, SC_Bengali, GC_Nd, "\x05\x91\xa5"}, //Bengali,Chakma,Syloti_Nagri //  ০..৯
    {0xAE6, 0xAEF, SC_Gujarati, GC_Nd, "\x0c\x46"}, //Gujarati,Khojki //  ૦..૯
    {0xCE6, 0xCEF, SC_Kannada, GC_Nd, "\x13\x5c\x86"}, //Kannada,Nandinagari,Tulu_Tigalari //  ೦..೯
    {0xE50, 0xE59, SC_Thai, GC_Nd, NULL}, //  ๐..๙
    {0xED0, 0xED9, SC_Lao, GC_Nd, NULL}, //  ໐..໙
    {0xF20, 0xF29, SC_Tibetan, GC_Nd, NULL}, //  ༠..༩
    {0x1040, 0x1049, SC_Myanmar, GC_Nd, "\x91\x17\xa7"}, //Chakma,Myanmar,Tai_Le //  ၀..၉
    {0x17E0, 0x17E9, SC_Khmer, GC_Nd, NULL}, //  ០..៩
    {0x203F, 0x2040, SC_Common, GC_Pc, NULL}, //  ‿..⁀
    {0x30FB, 0x30FB, SC_Common, GC_Po, "\x06\x0e\x0f\x11\x12\xad"}, //Bopomofo,Hangul,Han,Hiragana,Katakana,Yi //  ・
}; // 13 ranges, 2 singles, 109 codepoints


//---------------------------------------------------

// Only excluded scripts, XID_Start, more IDTypes, NFC, !MEDIAL and !MARK
// codeql[cpp/unused-static-variable]: generated TR39 data table, not
// wired into the current script-check profile (excluded-scripts variant).
static const struct sc_tr39 tr39_excl_start_list[] = {
    {0x3E2, 0x3EF, SC_Coptic, GC_L, NULL}, //  (Excluded) Ϣ..ϯ
    {0x800, 0x815, SC_Samaritan, GC_Lo, NULL}, //  (Excluded) ࠀ..ࠕ
    {0x81A, 0x81A, SC_Samaritan, GC_Lm, NULL}, //  (Excluded) ࠚ
    {0x824, 0x824, SC_Samaritan, GC_Lm, NULL}, //  (Excluded) ࠤ
    {0x828, 0x828, SC_Samaritan, GC_Lm, NULL}, //  (Excluded) ࠨ
    {0x1681, 0x169A, SC_Ogham, GC_Lo, NULL}, //  (Excluded) ᚁ..ᚚ
    {0x16A0, 0x16EA, SC_Runic, GC_Lo, NULL}, //  (Excluded) ᚠ..ᛪ
    {0x16EE, 0x16F8, SC_Runic, GC_V, NULL}, //  (Excluded) ᛮ..ᛸ
    {0x1700, 0x1711, SC_Tagalog, GC_Lo, NULL}, //  (Excluded) ᜀ..ᜑ
    {0x171F, 0x171F, SC_Tagalog, GC_Lo, NULL}, //  (Excluded) ᜟ
    {0x1721, 0x1731, SC_Hanunoo, GC_Lo, NULL}, //  (Excluded) ᜡ..ᜱ
    {0x1740, 0x1751, SC_Buhid, GC_Lo, NULL}, //  (Excluded) ᝀ..ᝑ
    {0x1760, 0x176C, SC_Tagbanwa, GC_Lo, NULL}, //  (Excluded) ᝠ..ᝬ
    {0x176E, 0x1770, SC_Tagbanwa, GC_Lo, NULL}, //  (Excluded) ᝮ..ᝰ
    {0x1820, 0x1878, SC_Mongolian, GC_L, NULL}, //  (Excluded) ᠠ..ᡸ
    {0x1880, 0x1884, SC_Mongolian, GC_Lo, NULL}, //  (Excluded) ᢀ..ᢄ
    {0x1887, 0x18A8, SC_Mongolian, GC_Lo, NULL}, //  (Excluded) ᢇ..ᢨ
    {0x18AA, 0x18AA, SC_Mongolian, GC_Lo, NULL}, //  (Excluded) ᢪ
    {0x1A00, 0x1A16, SC_Buginese, GC_Lo, NULL}, //  (Excluded) ᨀ..ᨖ
    {0x2C00, 0x2C5F, SC_Glagolitic, GC_L, NULL}, //  (Excluded) Ⰰ..ⱟ
    {0x2C80, 0x2CE4, SC_Coptic, GC_L, NULL}, //  (Excluded) Ⲁ..ⳤ
    {0x2CEB, 0x2CEE, SC_Coptic, GC_L, NULL}, //  (Excluded) Ⳬ..ⳮ
    {0x2CF2, 0x2CF3, SC_Coptic, GC_L, NULL}, //  (Excluded) Ⳳ..ⳳ
    {0xA840, 0xA873, SC_Phags_Pa, GC_Lo, NULL}, //  (Excluded) ꡀ..ꡳ
    {0xA930, 0xA946, SC_Rejang, GC_Lo, NULL}, //  (Excluded) ꤰ..ꥆ
    {0x10000, 0x1000B, SC_Linear_B, GC_Lo, NULL}, //  (Excluded) 𐀀..𐀋
    {0x1000D, 0x10026, SC_Linear_B, GC_Lo, NULL}, //  (Excluded) 𐀍..𐀦
    {0x10028, 0x1003A, SC_Linear_B, GC_Lo, NULL}, //  (Excluded) 𐀨..𐀺
    {0x1003C, 0x1003D, SC_Linear_B, GC_Lo, NULL}, //  (Excluded) 𐀼..𐀽
    {0x1003F, 0x1004D, SC_Linear_B, GC_Lo, NULL}, //  (Excluded) 𐀿..𐁍
    {0x10050, 0x1005D, SC_Linear_B, GC_Lo, NULL}, //  (Excluded) 𐁐..𐁝
    {0x10080, 0x100FA, SC_Linear_B, GC_Lo, NULL}, //  (Excluded) 𐂀..𐃺
    {0x10280, 0x1029C, SC_Lycian, GC_Lo, NULL}, //  (Excluded) 𐊀..𐊜
    {0x102A0, 0x102D0, SC_Carian, GC_Lo, NULL}, //  (Excluded) 𐊠..𐋐
    {0x10300, 0x1031F, SC_Old_Italic, GC_Lo, NULL}, //  (Excluded) 𐌀..𐌟
    {0x1032D, 0x1032F, SC_Old_Italic, GC_Lo, NULL}, //  (Excluded) 𐌭..𐌯
    {0x10331, 0x1034A, SC_Gothic, GC_V, NULL}, //  (Excluded) 𐌱..𐍊
    {0x10350, 0x10375, SC_Old_Permic, GC_Lo, NULL}, //  (Excluded) 𐍐..𐍵
    {0x10380, 0x1039D, SC_Ugaritic, GC_Lo, NULL}, //  (Excluded) 𐎀..𐎝
    {0x103A0, 0x103C3, SC_Old_Persian, GC_Lo, NULL}, //  (Excluded) 𐎠..𐏃
    {0x103C8, 0x103CF, SC_Old_Persian, GC_Lo, NULL}, //  (Excluded) 𐏈..𐏏
    {0x103D1, 0x103D5, SC_Old_Persian, GC_Nl, NULL}, //  (Excluded) 𐏑..𐏕
    {0x10400, 0x1044F, SC_Deseret, GC_L, NULL}, //  (Excluded) 𐐀..𐑏
    {0x10451, 0x1047F, SC_Shavian, GC_Lo, NULL}, //  (Excluded) 𐑑..𐑿
    {0x10481, 0x1049D, SC_Osmanya, GC_Lo, NULL}, //  (Excluded) 𐒁..𐒝
    {0x10500, 0x10527, SC_Elbasan, GC_Lo, NULL}, //  (Excluded) 𐔀..𐔧
    {0x10530, 0x10563, SC_Caucasian_Albanian, GC_Lo, NULL}, //  (Excluded) 𐔰..𐕣
    {0x10570, 0x1057A, SC_Vithkuqi, GC_Lu, NULL}, //  (Excluded) 𐕰..𐕺
    {0x1057C, 0x1058A, SC_Vithkuqi, GC_Lu, NULL}, //  (Excluded) 𐕼..𐖊
    {0x1058C, 0x10592, SC_Vithkuqi, GC_Lu, NULL}, //  (Excluded) 𐖌..𐖒
    {0x10594, 0x10595, SC_Vithkuqi, GC_Lu, NULL}, //  (Excluded) 𐖔..𐖕
    {0x10597, 0x105A1, SC_Vithkuqi, GC_Ll, NULL}, //  (Excluded) 𐖗..𐖡
    {0x105A3, 0x105B1, SC_Vithkuqi, GC_Ll, NULL}, //  (Excluded) 𐖣..𐖱
    {0x105B3, 0x105B9, SC_Vithkuqi, GC_Ll, NULL}, //  (Excluded) 𐖳..𐖹
    {0x105BB, 0x105BC, SC_Vithkuqi, GC_Ll, NULL}, //  (Excluded) 𐖻..𐖼
    {0x105C0, 0x105F3, SC_Todhri, GC_Lo, NULL}, //  (Excluded) 𐗀..𐗳
    {0x10600, 0x10736, SC_Linear_A, GC_Lo, NULL}, //  (Excluded) 𐘀..𐜶
    {0x10740, 0x10755, SC_Linear_A, GC_Lo, NULL}, //  (Excluded) 𐝀..𐝕
    {0x10760, 0x10767, SC_Linear_A, GC_Lo, NULL}, //  (Excluded) 𐝠..𐝧
    {0x10800, 0x10805, SC_Cypriot, GC_Lo, NULL}, //  (Excluded) 𐠀..𐠅
    {0x10808, 0x10808, SC_Cypriot, GC_Lo, NULL}, //  (Excluded) 𐠈
    {0x1080A, 0x10835, SC_Cypriot, GC_Lo, NULL}, //  (Excluded) 𐠊..𐠵
    {0x10837, 0x10838, SC_Cypriot, GC_Lo, NULL}, //  (Excluded) 𐠷..𐠸
    {0x1083C, 0x1083C, SC_Cypriot, GC_Lo, NULL}, //  (Excluded) 𐠼
    {0x1083F, 0x1083F, SC_Cypriot, GC_Lo, NULL}, //  (Excluded) 𐠿
    {0x10841, 0x10855, SC_Imperial_Aramaic, GC_Lo, NULL}, //  (Excluded) 𐡁..𐡕
    {0x10860, 0x10876, SC_Palmyrene, GC_Lo, NULL}, //  (Excluded) 𐡠..𐡶
    {0x10880, 0x1089E, SC_Nabataean, GC_Lo, NULL}, //  (Excluded) 𐢀..𐢞
    {0x108E0, 0x108F2, SC_Hatran, GC_Lo, NULL}, //  (Excluded) 𐣠..𐣲
    {0x108F4, 0x108F5, SC_Hatran, GC_Lo, NULL}, //  (Excluded) 𐣴..𐣵
    {0x10900, 0x10915, SC_Phoenician, GC_Lo, NULL}, //  (Excluded) 𐤀..𐤕
    {0x10920, 0x10939, SC_Lydian, GC_Lo, NULL}, //  (Excluded) 𐤠..𐤹
    {0x10940, 0x10959, SC_Sidetic, GC_Lo, NULL}, //  (Excluded) 𐥀..𐥙
    {0x10980, 0x1099F, SC_Meroitic_Hieroglyphs, GC_Lo, NULL}, //  (Excluded) 𐦀..𐦟
    {0x109A1, 0x109B7, SC_Meroitic_Cursive, GC_Lo, NULL}, //  (Excluded) 𐦡..𐦷
    {0x109BE, 0x109BF, SC_Meroitic_Cursive, GC_Lo, NULL}, //  (Excluded) 𐦾..𐦿
    {0x10A00, 0x10A00, SC_Kharoshthi, GC_Lo, NULL}, //  (Excluded) 𐨀
    {0x10A10, 0x10A13, SC_Kharoshthi, GC_Lo, NULL}, //  (Excluded) 𐨐..𐨓
    {0x10A15, 0x10A17, SC_Kharoshthi, GC_Lo, NULL}, //  (Excluded) 𐨕..𐨗
    {0x10A19, 0x10A35, SC_Kharoshthi, GC_Lo, NULL}, //  (Excluded) 𐨙..𐨵
    {0x10A60, 0x10A7C, SC_Old_South_Arabian, GC_Lo, NULL}, //  (Excluded) 𐩠..𐩼
    {0x10A80, 0x10A9C, SC_Old_North_Arabian, GC_Lo, NULL}, //  (Excluded) 𐪀..𐪜
    {0x10AC0, 0x10AC7, SC_Manichaean, GC_Lo, NULL}, //  (Excluded) 𐫀..𐫇
    {0x10AC9, 0x10AE4, SC_Manichaean, GC_Lo, NULL}, //  (Excluded) 𐫉..𐫤
    {0x10B00, 0x10B35, SC_Avestan, GC_Lo, NULL}, //  (Excluded) 𐬀..𐬵
    {0x10B40, 0x10B55, SC_Inscriptional_Parthian, GC_Lo, NULL}, //  (Excluded) 𐭀..𐭕
    {0x10B60, 0x10B72, SC_Inscriptional_Pahlavi, GC_Lo, NULL}, //  (Excluded) 𐭠..𐭲
    {0x10B80, 0x10B91, SC_Psalter_Pahlavi, GC_Lo, NULL}, //  (Excluded) 𐮀..𐮑
    {0x10C00, 0x10C48, SC_Old_Turkic, GC_Lo, NULL}, //  (Excluded) 𐰀..𐱈
    {0x10C80, 0x10CB2, SC_Old_Hungarian, GC_Lu, NULL}, //  (Excluded) 𐲀..𐲲
    {0x10CC0, 0x10CF2, SC_Old_Hungarian, GC_Ll, NULL}, //  (Excluded) 𐳀..𐳲
    {0x10D4A, 0x10D65, SC_Garay, GC_L, NULL}, //  (Excluded) 𐵊..𐵥
    {0x10D6F, 0x10D85, SC_Garay, GC_L, NULL}, //  (Excluded) 𐵯..𐶅
    {0x10E80, 0x10EA9, SC_Yezidi, GC_Lo, NULL}, //  (Excluded) 𐺀..𐺩
    {0x10EB0, 0x10EB1, SC_Yezidi, GC_Lo, NULL}, //  (Excluded) 𐺰..𐺱
    {0x10F00, 0x10F1C, SC_Old_Sogdian, GC_Lo, NULL}, //  (Excluded) 𐼀..𐼜
    {0x10F27, 0x10F27, SC_Old_Sogdian, GC_Lo, NULL}, //  (Excluded) 𐼧
    {0x10F30, 0x10F45, SC_Sogdian, GC_Lo, NULL}, //  (Excluded) 𐼰..𐽅
    {0x10F70, 0x10F81, SC_Old_Uyghur, GC_Lo, NULL}, //  (Excluded) 𐽰..𐾁
    {0x10FB0, 0x10FC4, SC_Chorasmian, GC_Lo, NULL}, //  (Excluded) 𐾰..𐿄
    {0x10FE0, 0x10FF6, SC_Elymaic, GC_Lo, NULL}, //  (Excluded) 𐿠..𐿶
    {0x11003, 0x11037, SC_Brahmi, GC_Lo, NULL}, //  (Excluded) 𑀃..𑀷
    {0x11071, 0x11072, SC_Brahmi, GC_Lo, NULL}, //  (Excluded) 𑁱..𑁲
    {0x11075, 0x11075, SC_Brahmi, GC_Lo, NULL}, //  (Excluded) 𑁵
    {0x11083, 0x110AF, SC_Kaithi, GC_Lo, NULL}, //  (Excluded) 𑂃..𑂯
    {0x110D0, 0x110E8, SC_Sora_Sompeng, GC_Lo, NULL}, //  (Excluded) 𑃐..𑃨
    {0x11150, 0x11172, SC_Mahajani, GC_Lo, NULL}, //  (Excluded) 𑅐..𑅲
    {0x11176, 0x11176, SC_Mahajani, GC_Lo, NULL}, //  (Excluded) 𑅶
    {0x11183, 0x111B2, SC_Sharada, GC_Lo, NULL}, //  (Excluded) 𑆃..𑆲
    {0x111C1, 0x111C4, SC_Sharada, GC_Lo, NULL}, //  (Excluded) 𑇁..𑇄
    {0x111DA, 0x111DA, SC_Sharada, GC_Lo, NULL}, //  (Excluded) 𑇚
    {0x111DC, 0x111DC, SC_Sharada, GC_Lo, NULL}, //  (Excluded) 𑇜
    {0x11200, 0x11211, SC_Khojki, GC_Lo, NULL}, //  (Excluded) 𑈀..𑈑
    {0x11213, 0x1122B, SC_Khojki, GC_Lo, NULL}, //  (Excluded) 𑈓..𑈫
    {0x1123F, 0x11240, SC_Khojki, GC_Lo, NULL}, //  (Excluded) 𑈿..𑉀
    {0x11280, 0x11286, SC_Multani, GC_Lo, NULL}, //  (Excluded) 𑊀..𑊆
    {0x11288, 0x11288, SC_Multani, GC_Lo, NULL}, //  (Excluded) 𑊈
    {0x1128A, 0x1128D, SC_Multani, GC_Lo, NULL}, //  (Excluded) 𑊊..𑊍
    {0x1128F, 0x1129D, SC_Multani, GC_Lo, NULL}, //  (Excluded) 𑊏..𑊝
    {0x1129F, 0x112A8, SC_Multani, GC_Lo, NULL}, //  (Excluded) 𑊟..𑊨
    {0x112B0, 0x112DE, SC_Khudawadi, GC_Lo, NULL}, //  (Excluded) 𑊰..𑋞
    {0x11305, 0x1130C, SC_Grantha, GC_Lo, NULL}, //  (Excluded) 𑌅..𑌌
    {0x1130F, 0x11310, SC_Grantha, GC_Lo, NULL}, //  (Excluded) 𑌏..𑌐
    {0x11313, 0x11328, SC_Grantha, GC_Lo, NULL}, //  (Excluded) 𑌓..𑌨
    {0x1132A, 0x11330, SC_Grantha, GC_Lo, NULL}, //  (Excluded) 𑌪..𑌰
    {0x11332, 0x11333, SC_Grantha, GC_Lo, NULL}, //  (Excluded) 𑌲..𑌳
    {0x11335, 0x11339, SC_Grantha, GC_Lo, NULL}, //  (Excluded) 𑌵..𑌹
    {0x1133D, 0x1133D, SC_Grantha, GC_Lo, NULL}, //  (Excluded) 𑌽
    {0x11350, 0x11350, SC_Grantha, GC_Lo, NULL}, //  (Excluded) 𑍐
    {0x1135D, 0x11361, SC_Grantha, GC_Lo, NULL}, //  (Excluded) 𑍝..𑍡
    {0x11380, 0x11389, SC_Tulu_Tigalari, GC_Lo, NULL}, //  (Excluded) 𑎀..𑎉
    {0x1138B, 0x1138B, SC_Tulu_Tigalari, GC_Lo, NULL}, //  (Excluded) 𑎋
    {0x1138E, 0x1138E, SC_Tulu_Tigalari, GC_Lo, NULL}, //  (Excluded) 𑎎
    {0x11390, 0x113B5, SC_Tulu_Tigalari, GC_Lo, NULL}, //  (Excluded) 𑎐..𑎵
    {0x113B7, 0x113B7, SC_Tulu_Tigalari, GC_Lo, NULL}, //  (Excluded) 𑎷
    {0x113D1, 0x113D1, SC_Tulu_Tigalari, GC_Lo, NULL}, //  (Excluded) 𑏑
    {0x113D3, 0x113D3, SC_Tulu_Tigalari, GC_Lo, NULL}, //  (Excluded) 𑏓
    {0x11480, 0x114AF, SC_Tirhuta, GC_Lo, NULL}, //  (Excluded) 𑒀..𑒯
    {0x114C4, 0x114C5, SC_Tirhuta, GC_Lo, NULL}, //  (Excluded) 𑓄..𑓅
    {0x114C7, 0x114C7, SC_Tirhuta, GC_Lo, NULL}, //  (Excluded) 𑓇
    {0x11580, 0x115AE, SC_Siddham, GC_Lo, NULL}, //  (Excluded) 𑖀..𑖮
    {0x115D8, 0x115DB, SC_Siddham, GC_Lo, NULL}, //  (Excluded) 𑗘..𑗛
    {0x11600, 0x1162F, SC_Modi, GC_Lo, NULL}, //  (Excluded) 𑘀..𑘯
    {0x11644, 0x11644, SC_Modi, GC_Lo, NULL}, //  (Excluded) 𑙄
    {0x11680, 0x116AA, SC_Takri, GC_Lo, NULL}, //  (Excluded) 𑚀..𑚪
    {0x116B8, 0x116B8, SC_Takri, GC_Lo, NULL}, //  (Excluded) 𑚸
    {0x11700, 0x1171A, SC_Ahom, GC_Lo, NULL}, //  (Excluded) 𑜀..𑜚
    {0x11740, 0x11746, SC_Ahom, GC_Lo, NULL}, //  (Excluded) 𑝀..𑝆
    {0x11800, 0x1182B, SC_Dogra, GC_Lo, NULL}, //  (Excluded) 𑠀..𑠫
    {0x118A0, 0x118DF, SC_Warang_Citi, GC_L, NULL}, //  (Excluded) 𑢠..𑣟
    {0x118FF, 0x118FF, SC_Warang_Citi, GC_Lo, NULL}, //  (Excluded) 𑣿
    {0x11901, 0x11906, SC_Dives_Akuru, GC_Lo, NULL}, //  (Excluded) 𑤁..𑤆
    {0x11909, 0x11909, SC_Dives_Akuru, GC_Lo, NULL}, //  (Excluded) 𑤉
    {0x1190C, 0x11913, SC_Dives_Akuru, GC_Lo, NULL}, //  (Excluded) 𑤌..𑤓
    {0x11915, 0x11916, SC_Dives_Akuru, GC_Lo, NULL}, //  (Excluded) 𑤕..𑤖
    {0x11918, 0x1192F, SC_Dives_Akuru, GC_Lo, NULL}, //  (Excluded) 𑤘..𑤯
    {0x1193F, 0x1193F, SC_Dives_Akuru, GC_Lo, NULL}, //  (Excluded) 𑤿
    {0x11941, 0x11941, SC_Dives_Akuru, GC_Lo, NULL}, //  (Excluded) 𑥁
    {0x119A0, 0x119A7, SC_Nandinagari, GC_Lo, NULL}, //  (Excluded) 𑦠..𑦧
    {0x119AA, 0x119D0, SC_Nandinagari, GC_Lo, NULL}, //  (Excluded) 𑦪..𑧐
    {0x119E1, 0x119E1, SC_Nandinagari, GC_Lo, NULL}, //  (Excluded) 𑧡
    {0x119E3, 0x119E3, SC_Nandinagari, GC_Lo, NULL}, //  (Excluded) 𑧣
    {0x11A00, 0x11A00, SC_Zanabazar_Square, GC_Lo, NULL}, //  (Excluded) 𑨀
    {0x11A0B, 0x11A32, SC_Zanabazar_Square, GC_Lo, NULL}, //  (Excluded) 𑨋..𑨲
    {0x11A3A, 0x11A3A, SC_Zanabazar_Square, GC_Lo, NULL}, //  (Excluded) 𑨺
    {0x11A50, 0x11A50, SC_Soyombo, GC_Lo, NULL}, //  (Excluded) 𑩐
    {0x11A5C, 0x11A89, SC_Soyombo, GC_Lo, NULL}, //  (Excluded) 𑩜..𑪉
    {0x11A9D, 0x11A9D, SC_Soyombo, GC_Lo, NULL}, //  (Excluded) 𑪝
    {0x11AC0, 0x11AF8, SC_Pau_Cin_Hau, GC_Lo, NULL}, //  (Excluded) 𑫀..𑫸
    {0x11BC0, 0x11BE0, SC_Sunuwar, GC_Lo, NULL}, //  (Excluded) 𑯀..𑯠
    {0x11C00, 0x11C08, SC_Bhaiksuki, GC_Lo, NULL}, //  (Excluded) 𑰀..𑰈
    {0x11C0A, 0x11C2E, SC_Bhaiksuki, GC_Lo, NULL}, //  (Excluded) 𑰊..𑰮
    {0x11C40, 0x11C40, SC_Bhaiksuki, GC_Lo, NULL}, //  (Excluded) 𑱀
    {0x11C72, 0x11C8F, SC_Marchen, GC_Lo, NULL}, //  (Excluded) 𑱲..𑲏
    {0x11D00, 0x11D06, SC_Masaram_Gondi, GC_Lo, NULL}, //  (Excluded) 𑴀..𑴆
    {0x11D08, 0x11D09, SC_Masaram_Gondi, GC_Lo, NULL}, //  (Excluded) 𑴈..𑴉
    {0x11D0B, 0x11D30, SC_Masaram_Gondi, GC_Lo, NULL}, //  (Excluded) 𑴋..𑴰
    {0x11D46, 0x11D46, SC_Masaram_Gondi, GC_Lo, NULL}, //  (Excluded) 𑵆
    {0x11D60, 0x11D65, SC_Gunjala_Gondi, GC_Lo, NULL}, //  (Excluded) 𑵠..𑵥
    {0x11D67, 0x11D68, SC_Gunjala_Gondi, GC_Lo, NULL}, //  (Excluded) 𑵧..𑵨
    {0x11D6A, 0x11D89, SC_Gunjala_Gondi, GC_Lo, NULL}, //  (Excluded) 𑵪..𑶉
    {0x11D98, 0x11D98, SC_Gunjala_Gondi, GC_Lo, NULL}, //  (Excluded) 𑶘
    {0x11DB0, 0x11DDB, SC_Tolong_Siki, GC_L, NULL}, //  (Excluded) 𑶰..𑷛
    {0x11EE0, 0x11EF2, SC_Makasar, GC_Lo, NULL}, //  (Excluded) 𑻠..𑻲
    {0x11F02, 0x11F02, SC_Kawi, GC_Lo, NULL}, //  (Excluded) 𑼂
    {0x11F04, 0x11F10, SC_Kawi, GC_Lo, NULL}, //  (Excluded) 𑼄..𑼐
    {0x11F12, 0x11F33, SC_Kawi, GC_Lo, NULL}, //  (Excluded) 𑼒..𑼳
    {0x12000, 0x12399, SC_Cuneiform, GC_Lo, NULL}, //  (Excluded) 𒀀..𒎙
    {0x12400, 0x1246E, SC_Cuneiform, GC_Nl, NULL}, //  (Excluded) 𒐀..𒑮
    {0x12480, 0x12543, SC_Cuneiform, GC_Lo, NULL}, //  (Excluded) 𒒀..𒕃
    {0x12F90, 0x12FF0, SC_Cypro_Minoan, GC_Lo, NULL}, //  (Excluded) 𒾐..𒿰
    {0x13000, 0x1342F, SC_Egyptian_Hieroglyphs, GC_Lo, NULL}, //  (Excluded) 𓀀..𓐯
    {0x13441, 0x13446, SC_Egyptian_Hieroglyphs, GC_Lo, NULL}, //  (Excluded) 𓑁..𓑆
    {0x13460, 0x143FA, SC_Egyptian_Hieroglyphs, GC_Lo, NULL}, //  (Excluded) 𓑠..𔏺
    {0x14400, 0x14646, SC_Anatolian_Hieroglyphs, GC_Lo, NULL}, //  (Excluded) 𔐀..𔙆
    {0x16100, 0x1611D, SC_Gurung_Khema, GC_Lo, NULL}, //  (Excluded) 𖄀..𖄝
    {0x16A70, 0x16ABE, SC_Tangsa, GC_Lo, NULL}, //  (Excluded) 𖩰..𖪾
    {0x16AD0, 0x16AED, SC_Bassa_Vah, GC_Lo, NULL}, //  (Excluded) 𖫐..𖫭
    {0x16B00, 0x16B2F, SC_Pahawh_Hmong, GC_Lo, NULL}, //  (Excluded) 𖬀..𖬯
    {0x16B40, 0x16B43, SC_Pahawh_Hmong, GC_Lm, NULL}, //  (Excluded) 𖭀..𖭃
    {0x16B63, 0x16B77, SC_Pahawh_Hmong, GC_Lo, NULL}, //  (Excluded) 𖭣..𖭷
    {0x16B7D, 0x16B8F, SC_Pahawh_Hmong, GC_Lo, NULL}, //  (Excluded) 𖭽..𖮏
    {0x16D40, 0x16D6C, SC_Kirat_Rai, GC_L, NULL}, //  (Excluded) 𖵀..𖵬
    {0x16E40, 0x16E7F, SC_Medefaidrin, GC_L, NULL}, //  (Excluded) 𖹀..𖹿
    {0x16EA0, 0x16EB8, SC_Beria_Erfe, GC_Lu, NULL}, //  (Excluded) 𖺠..𖺸
    {0x16EBB, 0x16ED3, SC_Beria_Erfe, GC_Ll, NULL}, //  (Excluded) 𖺻..𖻓
    {0x16FE0, 0x16FE0, SC_Tangut, GC_Lm, NULL}, //  (Excluded) 𖿠
    {0x17000, 0x18AFF, SC_Tangut, GC_Lo, NULL}, //  (Excluded) 𗀀..𘫿
    {0x18B01, 0x18CD5, SC_Khitan_Small_Script, GC_Lo, NULL}, //  (Excluded) 𘬁..𘳕
    {0x18CFF, 0x18CFF, SC_Khitan_Small_Script, GC_Lo, NULL}, //  (Excluded) 𘳿
    {0x18D01, 0x18D1E, SC_Tangut, GC_Lo, NULL}, //  (Excluded) 𘴁..𘴞
    {0x18D80, 0x18DF2, SC_Tangut, GC_Lo, NULL}, //  (Excluded) 𘶀..𘷲
    {0x1B170, 0x1B2FB, SC_Nushu, GC_Lo, NULL}, //  (Excluded) 𛅰..𛋻
    {0x1BC00, 0x1BC6A, SC_Duployan, GC_Lo, NULL}, //  (Excluded) 𛰀..𛱪
    {0x1BC70, 0x1BC7C, SC_Duployan, GC_Lo, NULL}, //  (Excluded) 𛱰..𛱼
    {0x1BC80, 0x1BC88, SC_Duployan, GC_Lo, NULL}, //  (Excluded) 𛲀..𛲈
    {0x1BC90, 0x1BC99, SC_Duployan, GC_Lo, NULL}, //  (Excluded) 𛲐..𛲙
    {0x1E290, 0x1E2AD, SC_Toto, GC_Lo, NULL}, //  (Excluded) 𞊐..𞊭
    {0x1E4D0, 0x1E4EB, SC_Nag_Mundari, GC_L, NULL}, //  (Excluded) 𞓐..𞓫
    {0x1E5D0, 0x1E5ED, SC_Ol_Onal, GC_Lo, NULL}, //  (Excluded) 𞗐..𞗭
    {0x1E5F0, 0x1E5F0, SC_Ol_Onal, GC_Lo, NULL}, //  (Excluded) 𞗰
    {0x1E6C0, 0x1E6DE, SC_Tai_Yo, GC_Lo, NULL}, //  (Excluded) 𞛀..𞛞
    {0x1E6E0, 0x1E6E2, SC_Tai_Yo, GC_Lo, NULL}, //  (Excluded) 𞛠..𞛢
    {0x1E6E4, 0x1E6E5, SC_Tai_Yo, GC_Lo, NULL}, //  (Excluded) 𞛤..𞛥
    {0x1E6E7, 0x1E6ED, SC_Tai_Yo, GC_Lo, NULL}, //  (Excluded) 𞛧..𞛭
    {0x1E6F0, 0x1E6F4, SC_Tai_Yo, GC_Lo, NULL}, //  (Excluded) 𞛰..𞛴
    {0x1E6FE, 0x1E6FF, SC_Tai_Yo, GC_L, NULL}, //  (Excluded) 𞛾..𞛿
    {0x1E800, 0x1E8C4, SC_Mende_Kikakui, GC_Lo, NULL}, //  (Excluded) 𞠀..𞣄
}; // 186 ranges, 42 singles, 19620 codepoints

// Only excluded scripts, XID_Continue,!XID_Start, more IDTypes, NFC and !MARK
// codeql[cpp/unused-static-variable]: generated TR39 data table, not
// wired into the current script-check profile (excluded-scripts variant).
static const struct sc_tr39 tr39_excl_cont_list[] = {
    {0x1810, 0x1819, SC_Mongolian, GC_Nd, NULL}, //  (Excluded) ᠐..᠙
    {0x104A0, 0x104A9, SC_Osmanya, GC_Nd, NULL}, //  (Excluded) 𐒠..𐒩
    {0x10D40, 0x10D49, SC_Garay, GC_Nd, NULL}, //  (Excluded) 𐵀..𐵉
    {0x11066, 0x1106F, SC_Brahmi, GC_Nd, NULL}, //  (Excluded) 𑁦..𑁯
    {0x110F0, 0x110F9, SC_Sora_Sompeng, GC_Nd, NULL}, //  (Excluded) 𑃰..𑃹
    {0x111D0, 0x111D9, SC_Sharada, GC_Nd, NULL}, //  (Excluded) 𑇐..𑇙
    {0x112F0, 0x112F9, SC_Khudawadi, GC_Nd, NULL}, //  (Excluded) 𑋰..𑋹
    {0x114D0, 0x114D9, SC_Tirhuta, GC_Nd, NULL}, //  (Excluded) 𑓐..𑓙
    {0x11650, 0x11659, SC_Modi, GC_Nd, NULL}, //  (Excluded) 𑙐..𑙙
    {0x116C0, 0x116C9, SC_Takri, GC_Nd, NULL}, //  (Excluded) 𑛀..𑛉
    {0x11730, 0x11739, SC_Ahom, GC_Nd, NULL}, //  (Excluded) 𑜰..𑜹
    {0x118E0, 0x118E9, SC_Warang_Citi, GC_Nd, NULL}, //  (Excluded) 𑣠..𑣩
    {0x11950, 0x11959, SC_Dives_Akuru, GC_Nd, NULL}, //  (Excluded) 𑥐..𑥙
    {0x11BF0, 0x11BF9, SC_Sunuwar, GC_Nd, NULL}, //  (Excluded) 𑯰..𑯹
    {0x11C50, 0x11C59, SC_Bhaiksuki, GC_Nd, NULL}, //  (Excluded) 𑱐..𑱙
    {0x11D50, 0x11D59, SC_Masaram_Gondi, GC_Nd, NULL}, //  (Excluded) 𑵐..𑵙
    {0x11DA0, 0x11DA9, SC_Gunjala_Gondi, GC_Nd, NULL}, //  (Excluded) 𑶠..𑶩
    {0x11DE0, 0x11DE9, SC_Tolong_Siki, GC_Nd, NULL}, //  (Excluded) 𑷠..𑷩
    {0x11F50, 0x11F59, SC_Kawi, GC_Nd, NULL}, //  (Excluded) 𑽐..𑽙
    {0x16130, 0x16139, SC_Gurung_Khema, GC_Nd, NULL}, //  (Excluded) 𖄰..𖄹
    {0x16AC0, 0x16AC9, SC_Tangsa, GC_Nd, NULL}, //  (Excluded) 𖫀..𖫉
    {0x16B50, 0x16B59, SC_Pahawh_Hmong, GC_Nd, NULL}, //  (Excluded) 𖭐..𖭙
    {0x16D70, 0x16D79, SC_Kirat_Rai, GC_Nd, NULL}, //  (Excluded) 𖵰..𖵹
    {0x1E4F0, 0x1E4F9, SC_Nag_Mundari, GC_Nd, NULL}, //  (Excluded) 𞓰..𞓹
    {0x1E5F1, 0x1E5FA, SC_Ol_Onal, GC_Nd, NULL}, //  (Excluded) 𞗱..𞗺
}; // 25 ranges, 0 singles, 225 codepoints

// tr39_start/cont + MEDIAL
static const struct range_bool tr39_medial_list[] = {
    {0x30FB, 0x30FB}, // Common ・
}; // 0 ranges, 1 singles, 0 codepoints


typedef bool func_tr31(const uint32_t cp);
struct func_tr31_s {
    func_tr31 *start;
    func_tr31 *cont;
};
static const struct sc_tr39 *isTR39_start(const uint32_t cp);
static const struct sc_tr39 *isTR39_cont(const uint32_t cp);

/* Provide a mapping of the 174 Script properties to an index byte.
   Sorted into usages.
 */
static const char *const all_scripts[] = {
    // clang-format off
    // Recommended Scripts (not need to add them)
    // https://www.unicode.org/reports/tr31/#Table_Recommended_Scripts
    "Common",	// 0
    "Inherited",	// 1
    "Latin",	// 2
    "Arabic",	// 3
    "Armenian",	// 4
    "Bengali",	// 5
    "Bopomofo",	// 6
    "Cyrillic",	// 7
    "Devanagari",	// 8
    "Ethiopic",	// 9
    "Georgian",	// 10
    "Greek",	// 11
    "Gujarati",	// 12
    "Gurmukhi",	// 13
    "Hangul",	// 14
    "Han",	// 15
    "Hebrew",	// 16
    "Hiragana",	// 17
    "Katakana",	// 18
    "Kannada",	// 19
    "Khmer",	// 20
    "Lao",	// 21
    "Malayalam",	// 22
    "Myanmar",	// 23
    "Oriya",	// 24
    "Sinhala",	// 25
    "Tamil",	// 26
    "Telugu",	// 27
    "Thaana",	// 28
    "Thai",	// 29
    "Tibetan",	// 30
    // Excluded Scripts (but can be added expliclitly)
    // https://www.unicode.org/reports/tr31/#Table_Candidate_Characters_for_Exclusion_from_Identifiers
    "Ahom",	// 31
    "Anatolian_Hieroglyphs",	// 32
    "Avestan",	// 33
    "Bassa_Vah",	// 34
    "Beria_Erfe",	// 35
    "Bhaiksuki",	// 36
    "Brahmi",	// 37
    "Braille",	// 38
    "Buginese",	// 39
    "Buhid",	// 40
    "Carian",	// 41
    "Caucasian_Albanian",	// 42
    "Chorasmian",	// 43
    "Coptic",	// 44
    "Cuneiform",	// 45
    "Cypriot",	// 46
    "Cypro_Minoan",	// 47
    "Deseret",	// 48
    "Dives_Akuru",	// 49
    "Dogra",	// 50
    "Duployan",	// 51
    "Egyptian_Hieroglyphs",	// 52
    "Elbasan",	// 53
    "Elymaic",	// 54
    "Garay",	// 55
    "Glagolitic",	// 56
    "Gothic",	// 57
    "Grantha",	// 58
    "Gunjala_Gondi",	// 59
    "Gurung_Khema",	// 60
    "Hanunoo",	// 61
    "Hatran",	// 62
    "Imperial_Aramaic",	// 63
    "Inscriptional_Pahlavi",	// 64
    "Inscriptional_Parthian",	// 65
    "Kaithi",	// 66
    "Kawi",	// 67
    "Kharoshthi",	// 68
    "Khitan_Small_Script",	// 69
    "Khojki",	// 70
    "Khudawadi",	// 71
    "Kirat_Rai",	// 72
    "Linear_A",	// 73
    "Linear_B",	// 74
    "Lycian",	// 75
    "Lydian",	// 76
    "Mahajani",	// 77
    "Makasar",	// 78
    "Manichaean",	// 79
    "Marchen",	// 80
    "Masaram_Gondi",	// 81
    "Medefaidrin",	// 82
    "Mende_Kikakui",	// 83
    "Meroitic_Cursive",	// 84
    "Meroitic_Hieroglyphs",	// 85
    "Modi",	// 86
    "Mongolian",	// 87
    "Mro",	// 88
    "Multani",	// 89
    "Nabataean",	// 90
    "Nag_Mundari",	// 91
    "Nandinagari",	// 92
    "Nushu",	// 93
    "Ogham",	// 94
    "Ol_Onal",	// 95
    "Old_Hungarian",	// 96
    "Old_Italic",	// 97
    "Old_North_Arabian",	// 98
    "Old_Permic",	// 99
    "Old_Persian",	// 100
    "Old_Sogdian",	// 101
    "Old_South_Arabian",	// 102
    "Old_Turkic",	// 103
    "Old_Uyghur",	// 104
    "Osmanya",	// 105
    "Pahawh_Hmong",	// 106
    "Palmyrene",	// 107
    "Pau_Cin_Hau",	// 108
    "Phags_Pa",	// 109
    "Phoenician",	// 110
    "Psalter_Pahlavi",	// 111
    "Rejang",	// 112
    "Runic",	// 113
    "Samaritan",	// 114
    "Sharada",	// 115
    "Shavian",	// 116
    "Siddham",	// 117
    "Sidetic",	// 118
    "SignWriting",	// 119
    "Sogdian",	// 120
    "Sora_Sompeng",	// 121
    "Soyombo",	// 122
    "Sunuwar",	// 123
    "Tagalog",	// 124
    "Tagbanwa",	// 125
    "Tai_Yo",	// 126
    "Takri",	// 127
    "Tangsa",	// 128
    "Tangut",	// 129
    "Tirhuta",	// 130
    "Todhri",	// 131
    "Tolong_Siki",	// 132
    "Toto",	// 133
    "Tulu_Tigalari",	// 134
    "Ugaritic",	// 135
    "Vithkuqi",	// 136
    "Warang_Citi",	// 137
    "Yezidi",	// 138
    "Zanabazar_Square",	// 139
    // Limited Use Scripts
    // https://www.unicode.org/reports/tr31/#Table_Limited_Use_Scripts
    "Adlam",	// 140
    "Balinese",	// 141
    "Bamum",	// 142
    "Batak",	// 143
    "Canadian_Aboriginal",	// 144
    "Chakma",	// 145
    "Cham",	// 146
    "Cherokee",	// 147
    "Hanifi_Rohingya",	// 148
    "Javanese",	// 149
    "Kayah_Li",	// 150
    "Lepcha",	// 151
    "Limbu",	// 152
    "Lisu",	// 153
    "Mandaic",	// 154
    "Meetei_Mayek",	// 155
    "Miao",	// 156
    "New_Tai_Lue",	// 157
    "Newa",	// 158
    "Nko",	// 159
    "Nyiakeng_Puachue_Hmong",	// 160
    "Ol_Chiki",	// 161
    "Osage",	// 162
    "Saurashtra",	// 163
    "Sundanese",	// 164
    "Syloti_Nagri",	// 165
    "Syriac",	// 166
    "Tai_Le",	// 167
    "Tai_Tham",	// 168
    "Tai_Viet",	// 169
    "Tifinagh",	// 170
    "Vai",	// 171
    "Wancho",	// 172
    "Yi",	// 173
    "Unknown",	// 174
    // clang-format on
};

struct scx {
    uint32_t from;
    uint32_t to;
    uint8_t gc; // enum u8id_gc is too large
    const char *scx; // indices into sc
};

// Maybe remove all Limited Use SC's from the list on hardcoded profiles 3-5.
// But 120 entries is small enough.
// codeql[cpp/unused-static-variable]: generated data table; its only
// consumer (u8ident_get_scx above) was removed.
static const struct scx scx_list[] = {
    // clang-format off
    {0x02C7, 0x02C7, GC_Lm, "\x06\x02"},	// Bopo Latn
    {0x02C9, 0x02CB, GC_Lm, "\x06\x02"},	// Bopo Latn
    {0x02CD, 0x02CD, GC_Lm, "\x02\x99"},	// Latn Lisu
    {0x02D7, 0x02D7, GC_Sk, "\x02\x1d"},	// Latn Thai                      (Not_XID)
    {0x02D9, 0x02D9, GC_Sk, "\x06\x02"},	// Bopo Latn                      (Not_NFKC)
    {0x0302, 0x0302, GC_Mn, "\x93\x07\x02\xaa"},	// Cher Cyrl Latn Tfng
    {0x0303, 0x0303, GC_Mn, "\x38\x02\x7b\xa6\x1d"},	// Glag Latn Sunu Syrc Thai
    {0x0305, 0x0305, GC_Mn, "\x2c\x35\x38\x39\x12\x02"},	// Copt Elba Glag Goth Kana Latn  (Uncommon_Use)
    {0x0306, 0x0306, GC_Mn, "\x07\x0b\x02\x63\xaa"},	// Cyrl Grek Latn Perm Tfng
    {0x0309, 0x0309, GC_Mn, "\x02\xaa"},	// Latn Tfng
    {0x030A, 0x030A, GC_Mn, "\x33\x02\xa6"},	// Dupl Latn Syrc
    {0x030B, 0x030B, GC_Mn, "\x93\x07\x02\xa2"},	// Cher Cyrl Latn Osge
    {0x030C, 0x030C, GC_Mn, "\x93\x02\xa7"},	// Cher Latn Tale
    {0x030D, 0x030D, GC_Mn, "\x02\x7b"},	// Latn Sunu                      (Uncommon_Use)
    {0x030E, 0x030E, GC_Mn, "\x09\x02"},	// Ethi Latn
    {0x0310, 0x0310, GC_Mn, "\x02\x7b"},	// Latn Sunu
    {0x0311, 0x0311, GC_Mn, "\x07\x02\x83"},	// Cyrl Latn Todr
    {0x0313, 0x0313, GC_Mn, "\x0b\x02\x63\x83"},	// Grek Latn Perm Todr
    {0x0323, 0x0323, GC_Mn, "\x93\x33\x12\x02\xa6\xaa"},	// Cher Dupl Kana Latn Syrc Tfng
    {0x0324, 0x0324, GC_Mn, "\x93\x33\x02\xa6"},	// Cher Dupl Latn Syrc
    {0x0325, 0x0325, GC_Mn, "\x02\xa6"},	// Latn Syrc
    {0x032D, 0x032D, GC_Mn, "\x02\x7b\xa6"},	// Latn Sunu Syrc
    {0x032E, 0x032E, GC_Mn, "\x02\xa6"},	// Latn Syrc
    {0x0330, 0x0330, GC_Mn, "\x93\x02\xa6"},	// Cher Latn Syrc
    {0x0342, 0x0345, GC_Mn, "\x0b"},	// Grek
    {0x0358, 0x0358, GC_Mn, "\x02\xa2"},	// Latn Osge                      (Uncommon_Use)
    {0x035E, 0x035E, GC_Mn, "\x2a\x02\x83"},	// Aghb Latn Todr
    {0x0363, 0x036F, GC_Mn, "\x02"},	// Latn                           (Obsolete)
    {0x0374, 0x0375, GC_Lm, "\x2c\x0b"},	// Copt Grek                      (Not_NFKC)
    {0x0483, 0x0483, GC_Mn, "\x07\x63"},	// Cyrl Perm                      (Obsolete)
    {0x0484, 0x0484, GC_Mn, "\x07\x38"},	// Cyrl Glag                      (Technical Obsolete)
    {0x0485, 0x0486, GC_Mn, "\x07\x02"},	// Cyrl Latn                      (Technical Obsolete)
    {0x0487, 0x0487, GC_Mn, "\x07\x38"},	// Cyrl Glag                      (Technical Obsolete)
    {0x0589, 0x0589, GC_Po, "\x04\x0a\x38"},	// Armn Geor Glag                 (Not_XID)
    {0x061C, 0x061C, GC_Cf, "\x03\xa6\x1c"},	// Arab Syrc Thaa                 (Default_Ignorable)
    {0x064B, 0x0655, GC_Mn, "\x03\xa6"},	// Arab Syrc
    {0x0660, 0x0669, GC_Nd, "\x03\x1c\x8a"},	// Arab Thaa Yezi
    {0x0670, 0x0670, GC_Mn, "\x03\xa6"},	// Arab Syrc
    {0x06D4, 0x06D4, GC_Po, "\x03\x94"},	// Arab Rohg                      (Not_XID)
    {0x0966, 0x096F, GC_Nd, "\x08\x32\x42\x4d"},	// Deva Dogr Kthi Mahj
    {0x09E6, 0x09EF, GC_Nd, "\x05\x91\xa5"},	// Beng Cakm Sylo
    {0x0A66, 0x0A6F, GC_Nd, "\x0d\x59"},	// Guru Mult                      (Uncommon_Use)
    {0x0AE6, 0x0AEF, GC_Nd, "\x0c\x46"},	// Gujr Khoj
    {0x0BE6, 0x0BF3, GC_Nd, "\x3a\x1a"},	// Gran Taml                      (Uncommon_Use)
    {0x0CE6, 0x0CEF, GC_Nd, "\x13\x5c\x86"},	// Knda Nand Tutg
    {0x1040, 0x1049, GC_Nd, "\x91\x17\xa7"},	// Cakm Mymr Tale
    {0x10FB, 0x10FB, GC_Po, "\x0a\x38\x02"},	// Geor Glag Latn                 (Not_XID)
    {0x16EB, 0x16ED, GC_Po, "\x71"},	// Runr                           (Exclusion Not_XID)
    {0x1735, 0x1736, GC_Po, "\x28\x3d\x7d\x7c"},	// Buhd Hano Tagb Tglg            (Exclusion Not_XID)
    {0x1802, 0x1803, GC_Po, "\x57\x6d"},	// Mong Phag                      (Exclusion Not_XID)
    {0x1805, 0x1805, GC_Po, "\x57\x6d"},	// Mong Phag                      (Exclusion Not_XID)
    {0x1CD0, 0x1CD0, GC_Mn, "\x05\x08\x3a\x13"},	// Beng Deva Gran Knda            (Obsolete)
    {0x1CD1, 0x1CD1, GC_Mn, "\x08"},	// Deva                           (Obsolete)
    {0x1CD2, 0x1CD2, GC_Mn, "\x05\x08\x3a\x13"},	// Beng Deva Gran Knda            (Obsolete)
    {0x1CD3, 0x1CD3, GC_Po, "\x08\x3a\x13"},	// Deva Gran Knda                 (Obsolete Not_XID)
    {0x1CD4, 0x1CD4, GC_Mn, "\x08"},	// Deva                           (Obsolete)
    {0x1CD5, 0x1CD5, GC_Mn, "\x05\x08\x9e\x1b\x82"},	// Beng Deva Newa Telu Tirh       (Obsolete)
    {0x1CD6, 0x1CD6, GC_Mn, "\x05\x08\x1b"},	// Beng Deva Telu                 (Obsolete)
    {0x1CD7, 0x1CD7, GC_Mn, "\x08\x9e\x73"},	// Deva Newa Shrd                 (Obsolete)
    {0x1CD8, 0x1CD8, GC_Mn, "\x05\x08\x9e\x1b"},	// Beng Deva Newa Telu            (Obsolete)
    {0x1CD9, 0x1CD9, GC_Mn, "\x08\x73"},	// Deva Shrd                      (Obsolete)
    {0x1CDA, 0x1CDA, GC_Mn, "\x08\x13\x16\x18\x1a\x1b"},	// Deva Knda Mlym Orya Taml Telu  (Obsolete)
    {0x1CDB, 0x1CDB, GC_Mn, "\x08"},	// Deva                           (Obsolete)
    {0x1CDC, 0x1CDD, GC_Mn, "\x08\x73"},	// Deva Shrd                      (Obsolete)
    {0x1CDE, 0x1CDF, GC_Mn, "\x08"},	// Deva                           (Obsolete)
    {0x1CE0, 0x1CE0, GC_Mn, "\x08\x73"},	// Deva Shrd                      (Obsolete)
    {0x1CE1, 0x1CE1, GC_Mc, "\x05\x08"},	// Beng Deva                      (Obsolete)
    {0x1CE2, 0x1CE2, GC_Mn, "\x08\x9e\x82"},	// Deva Newa Tirh                 (Obsolete)
    {0x1CE3, 0x1CE8, GC_Mn, "\x08"},	// Deva                           (Obsolete)
    {0x1CE9, 0x1CE9, GC_Lo, "\x08\x5c\x9e"},	// Deva Nand Newa                 (Obsolete)
    {0x1CEA, 0x1CEA, GC_Lo, "\x05\x08\x73"},	// Beng Deva Shrd                 (Obsolete)
    {0x1CEB, 0x1CEB, GC_Lo, "\x08\x9e"},	// Deva Newa                      (Obsolete)
    {0x1CEC, 0x1CEC, GC_Lo, "\x08"},	// Deva                           (Obsolete)
    {0x1CED, 0x1CED, GC_Mn, "\x05\x08\x9e\x73"},	// Beng Deva Newa Shrd            (Obsolete)
    {0x1CEE, 0x1CF1, GC_Lo, "\x08"},	// Deva                           (Obsolete)
    {0x1CF3, 0x1CF3, GC_Lo, "\x08\x3a"},	// Deva Gran                      (Obsolete)
    {0x1CF4, 0x1CF4, GC_Mn, "\x08\x3a\x13\x86"},	// Deva Gran Knda Tutg            (Obsolete)
    {0x1CF5, 0x1CF6, GC_Lo, "\x05\x08"},	// Beng Deva                      (Obsolete)
    {0x1CF7, 0x1CF7, GC_Mc, "\x05"},	// Beng                           (Obsolete)
    {0x1CF8, 0x1CF9, GC_Mn, "\x08\x3a"},	// Deva Gran                      (Obsolete)
    {0x1CFA, 0x1CFA, GC_Lo, "\x5c"},	// Nand                           (Exclusion)
    {0x1DC0, 0x1DC1, GC_Mn, "\x0b"},	// Grek                           (Technical Obsolete)
    {0x1DF8, 0x1DF8, GC_Mn, "\x07\x02\xa6"},	// Cyrl Latn Syrc
    {0x1DFA, 0x1DFA, GC_Mn, "\xa6"},	// Syrc                           (Limited_Use Technical)
    {0x202F, 0x202F, GC_Zs, "\x02\x57\x6d"},	// Latn Mong Phag                 (Not_NFKC)
    {0x204F, 0x204F, GC_Po, "\x8c\x03"},	// Adlm Arab                      (Not_XID)
    {0x205A, 0x205A, GC_Po, "\x29\x0a\x38\x60\x4b\x67"},	// Cari Geor Glag Hung Lyci Orkh  (Obsolete Not_XID)
    {0x205D, 0x205D, GC_Po, "\x29\x0b\x60\x55"},	// Cari Grek Hung Mero            (Obsolete Not_XID)
    {0x20F0, 0x20F0, GC_Mn, "\x08\x3a\x02"},	// Deva Gran Latn
    {0x2E17, 0x2E17, GC_Pd, "\x2c\x02"},	// Copt Latn                      (Not_XID)
    {0x2E30, 0x2E30, GC_Po, "\x21\x67"},	// Avst Orkh                      (Exclusion Not_XID)
    {0x2E3C, 0x2E3C, GC_Po, "\x33"},	// Dupl                           (Exclusion Not_XID)
    {0x2E41, 0x2E41, GC_Po, "\x8c\x03\x60"},	// Adlm Arab Hung                 (Not_XID)
    {0x2E43, 0x2E43, GC_Po, "\x07\x38"},	// Cyrl Glag                      (Not_XID)
    {0x2FF0, 0x2FFF, GC_So, "\x0f\x81"},	// Hani Tang                      (Not_XID)
    {0x3003, 0x3003, GC_Po, "\x06\x0e\x0f\x11\x12"},	// Bopo Hang Hani Hira Kana       (Not_XID)
    {0x3006, 0x3006, GC_Lo, "\x0f"},	// Hani
    {0x300C, 0x3011, GC_Ps, "\x06\x0e\x0f\x11\x12\xad"},	// Bopo Hang Hani Hira Kana Yiii  (Not_XID)
    {0x3013, 0x3013, GC_So, "\x06\x0e\x0f\x11\x12"},	// Bopo Hang Hani Hira Kana       (Not_XID)
    {0x3014, 0x301B, GC_Ps, "\x06\x0e\x0f\x11\x12\xad"},	// Bopo Hang Hani Hira Kana Yiii  (Not_XID)
    {0x301C, 0x301F, GC_Pd, "\x06\x0e\x0f\x11\x12"},	// Bopo Hang Hani Hira Kana       (Not_XID)
    {0x302A, 0x302D, GC_Mn, "\x06\x0f"},	// Bopo Hani
    {0x3030, 0x3030, GC_Pd, "\x06\x0e\x0f\x11\x12"},	// Bopo Hang Hani Hira Kana       (Not_XID)
    {0x3031, 0x3035, GC_Lm, "\x11\x12"},	// Hira Kana
    {0x3037, 0x3037, GC_So, "\x06\x0e\x0f\x11\x12"},	// Bopo Hang Hani Hira Kana       (Not_XID)
    {0x303C, 0x303D, GC_Lo, "\x0f\x11\x12"},	// Hani Hira Kana
    {0x303E, 0x303F, GC_So, "\x0f"},	// Hani                           (Not_XID)
    {0x3099, 0x309C, GC_Mn, "\x11\x12"},	// Hira Kana                      (Uncommon_Use)
    {0x30A0, 0x30A0, GC_Pd, "\x11\x12"},	// Hira Kana
    {0x30FB, 0x30FB, GC_Po, "\x06\x0e\x0f\x11\x12\xad"},	// Bopo Hang Hani Hira Kana Yiii
    {0x30FC, 0x30FC, GC_Lm, "\x11\x12"},	// Hira Kana
    {0x3190, 0x319F, GC_So, "\x0f"},	// Hani                           (Not_XID)
    {0x31C0, 0x31E5, GC_So, "\x0f"},	// Hani                           (Not_XID)
    {0x31EF, 0x31EF, GC_So, "\x0f\x81"},	// Hani Tang                      (Not_XID)
    {0x3220, 0x3247, GC_No, "\x0f"},	// Hani                           (Not_NFKC)
    {0x3280, 0x32B0, GC_No, "\x0f"},	// Hani                           (Not_NFKC)
    {0x32C0, 0x32CB, GC_So, "\x0f"},	// Hani                           (Not_NFKC)
    {0x32FF, 0x32FF, GC_So, "\x0f"},	// Hani                           (Not_NFKC)
    {0x3358, 0x3370, GC_So, "\x0f"},	// Hani                           (Not_NFKC)
    {0x337B, 0x337F, GC_So, "\x0f"},	// Hani                           (Not_NFKC)
    {0x33E0, 0x33FE, GC_So, "\x0f"},	// Hani                           (Not_NFKC)
    {0xA66F, 0xA66F, GC_Mn, "\x07\x38"},	// Cyrl Glag                      (Uncommon_Use)
    {0xA700, 0xA707, GC_Sk, "\x0f\x02"},	// Hani Latn                      (Obsolete Not_XID)
    {0xA8F1, 0xA8F1, GC_Mn, "\x05\x08\x86"},	// Beng Deva Tutg                 (Obsolete)
    {0xA8F3, 0xA8F3, GC_Lo, "\x08\x1a"},	// Deva Taml                      (Obsolete)
    {0xA92E, 0xA92E, GC_Po, "\x96\x02\x17"},	// Kali Latn Mymr                 (Not_XID)
    {0xA9CF, 0xA9CF, GC_Lm, "\x27\x95"},	// Bugi Java                      (Limited_Use Uncommon_Use)
    {0xFD3E, 0xFD3F, GC_Pe, "\x03\x9f"},	// Arab Nkoo                      (Technical Not_XID)
    {0xFDF2, 0xFDF2, GC_Lo, "\x03\x1c"},	// Arab Thaa                      (Not_NFKC)
    {0xFDFD, 0xFDFD, GC_So, "\x03\x1c"},	// Arab Thaa                      (Technical Not_XID)
    {0xFE45, 0xFE46, GC_Po, "\x06\x0e\x0f\x11\x12"},	// Bopo Hang Hani Hira Kana       (Technical Not_XID)
    {0xFF61, 0xFF65, GC_Po, "\x06\x0e\x0f\x11\x12\xad"},	// Bopo Hang Hani Hira Kana Yiii  (Not_NFKC)
    {0xFF70, 0xFF70, GC_Lm, "\x11\x12"},	// Hira Kana                      (Not_NFKC)
    {0xFF9E, 0xFF9F, GC_Lm, "\x11\x12"},	// Hira Kana                      (Not_NFKC)
    {0x10100, 0x10101, GC_Po, "\x2f\x2e\x4a"},	// Cpmn Cprt Linb                 (Exclusion Not_XID)
    {0x10102, 0x10102, GC_Po, "\x2e\x4a"},	// Cprt Linb                      (Exclusion Not_XID)
    {0x10107, 0x10133, GC_No, "\x2e\x49\x4a"},	// Cprt Lina Linb                 (Exclusion Not_XID)
    {0x10137, 0x1013F, GC_So, "\x2e\x4a"},	// Cprt Linb                      (Exclusion Not_XID)
    {0x102E0, 0x102FB, GC_Mn, "\x03\x2c"},	// Arab Copt                      (Obsolete)
    {0x10AF2, 0x10AF2, GC_Po, "\x4f\x68"},	// Mani Ougr                      (Exclusion Not_XID)
    {0x11301, 0x11301, GC_Mn, "\x3a\x1a"},	// Gran Taml
    {0x11303, 0x11303, GC_Mc, "\x3a\x1a"},	// Gran Taml
    {0x1133B, 0x1133C, GC_Mn, "\x3a\x1a"},	// Gran Taml                      (Uncommon_Use)
    {0x11FD0, 0x11FD1, GC_No, "\x3a\x1a"},	// Gran Taml                      (Not_XID)
    {0x11FD3, 0x11FD3, GC_No, "\x3a\x1a"},	// Gran Taml                      (Not_XID)
    {0x1BCA0, 0x1BCA3, GC_Cf, "\x33"},	// Dupl                           (Default_Ignorable)
    {0x1D360, 0x1D371, GC_No, "\x0f"},	// Hani                           (Not_XID)
    {0x1F250, 0x1F251, GC_So, "\x0f"},	// Hani                           (Not_NFKC)
    // clang-format on
}; // 55 ranges, 93 single codepoints


// Bidi formatting characters for reordering attacks.
// Only valid with RTL scripts, such as Hebrew and Arabic.
static const struct range_bool bidi_list[] = {
    // clang-format off
    { 0x202A, 0x202E }, // LRE, RLE, PDF, LRO, RLO
    { 0x2066, 0x2069 }, // LRI, RLI, FSI, PDI
    // clang-format on
};

// Greek-Latin confusables. See doc/P2538R0.md (Appendix F - Greek Confusables)
// for TR39
static const uint32_t greek_confus_list[] = {
    0x0370, // ( Ͱ → Ⱶ ) GREEK CAPITAL LETTER HETA → LATIN CAPITAL LETTER HALF H
    0x0377, // ( ͷ → ᴎ ) GREEK SMALL LETTER PAMPHYLIAN DIGAMMA → LATIN LETTER
    // SMALL CAPITAL REVERSED N
    0x037B, // ( ͻ → ɔ ) GREEK SMALL REVERSED LUNATE SIGMA SYMBOL → LATIN SMALL
    // LETTER OPEN O
    0x037D, // ( ͽ → ꜿ ) GREEK SMALL REVERSED DOTTED LUNATE SIGMA SYMBOL → LATIN
    // SMALL LETTER REVERSED C WITH DOT
    0x037F, // ( Ϳ → J ) GREEK CAPITAL LETTER YOT → LATIN CAPITAL LETTER J
    0x0391, // ( Α → A ) GREEK CAPITAL LETTER ALPHA → LATIN CAPITAL LETTER A
    0x0392, // ( Β → B ) GREEK CAPITAL LETTER BETA → LATIN CAPITAL LETTER B
    0x0395, // ( Ε → E ) GREEK CAPITAL LETTER EPSILON → LATIN CAPITAL LETTER E
    0x0396, // ( Ζ → Z ) GREEK CAPITAL LETTER ZETA → LATIN CAPITAL LETTER Z
    0x0397, // ( Η → H ) GREEK CAPITAL LETTER ETA → LATIN CAPITAL LETTER H
    0x0399, // ( Ι → l ) GREEK CAPITAL LETTER IOTA → LATIN SMALL LETTER L
    0x039A, // ( Κ → K ) GREEK CAPITAL LETTER KAPPA → LATIN CAPITAL LETTER K
    0x039B, // ( Λ → Ʌ ) GREEK CAPITAL LETTER LAMDA → LATIN CAPITAL LETTER
    // TURNED V
    0x039C, // ( Μ → M ) GREEK CAPITAL LETTER MU → LATIN CAPITAL LETTER M
    0x039D, // ( Ν → N ) GREEK CAPITAL LETTER NU → LATIN CAPITAL LETTER N
    0x039F, // ( Ο → O ) GREEK CAPITAL LETTER OMICRON → LATIN CAPITAL LETTER O
    0x03A1, // ( Ρ → P ) GREEK CAPITAL LETTER RHO → LATIN CAPITAL LETTER P
    0x03A3, // ( Σ → Ʃ ) GREEK CAPITAL LETTER SIGMA → LATIN CAPITAL LETTER ESH
    0x03A4, // ( Τ → T ) GREEK CAPITAL LETTER TAU → LATIN CAPITAL LETTER T
    0x03A5, // ( Υ → Y ) GREEK CAPITAL LETTER UPSILON → LATIN CAPITAL LETTER Y
    0x03A7, // ( Χ → X ) GREEK CAPITAL LETTER CHI → LATIN CAPITAL LETTER X
    0x03B1, // ( α → a ) GREEK SMALL LETTER ALPHA → LATIN SMALL LETTER A
    0x03B2, // ( β → ß ) GREEK SMALL LETTER BETA → LATIN SMALL LETTER SHARP S
    0x03B3, // ( γ → y ) GREEK SMALL LETTER GAMMA → LATIN SMALL LETTER Y
    0x03B4, // ( δ → ẟ ) GREEK SMALL LETTER DELTA → LATIN SMALL LETTER DELTA
    0x03BA, // ( κ → ĸ ) GREEK SMALL LETTER KAPPA → LATIN SMALL LETTER KRA
    0x03BB, // ( ꟛ → λ ) LATIN SMALL LETTER LAMBDA → GREEK SMALL LETTER
    // LAMDA
    0x03BF, // ( ο → o ) GREEK SMALL LETTER OMICRON → LATIN SMALL LETTER O
    0x03C1, // ( ρ → p ) GREEK SMALL LETTER RHO → LATIN SMALL LETTER P
    0x03C4, // ( τ → ᴛ ) GREEK SMALL LETTER TAU → LATIN LETTER SMALL CAPITAL T
    0x03C5, // ( υ → u ) GREEK SMALL LETTER UPSILON → LATIN SMALL LETTER U
    0x03C6, // ( φ → ɸ ) GREEK SMALL LETTER PHI → LATIN SMALL LETTER PHI
    0x03C7, // ( ꭕ → χ ) LATIN SMALL LETTER CHI WITH LOW LEFT SERIF → GREEK
    // SMALL LETTER CHI
    0x03C9, // ( ꞷ → ω ) LATIN SMALL LETTER OMEGA → GREEK SMALL LETTER OMEGA
    0x03D0, // ( ϐ → ß ) GREEK BETA SYMBOL → LATIN SMALL LETTER SHARP S
    0x03D2, // ( ϒ → Y ) GREEK UPSILON WITH HOOK SYMBOL → LATIN CAPITAL LETTER Y
    0x03D5, // ( ϕ → ɸ ) GREEK PHI SYMBOL → LATIN SMALL LETTER PHI
    0x03DC, // ( Ϝ → F ) GREEK LETTER DIGAMMA → LATIN CAPITAL LETTER F
    0x03F0, // ( ϰ → ĸ ) GREEK KAPPA SYMBOL → LATIN SMALL LETTER KRA
    0x03F2, // ( ϲ → c ) GREEK LUNATE SIGMA SYMBOL → LATIN SMALL LETTER C
    0x03F3, // ( ϳ → j ) GREEK LETTER YOT → LATIN SMALL LETTER J
    0x03F5, // ( ϵ → ꞓ ) GREEK LUNATE EPSILON SYMBOL → LATIN SMALL LETTER C WITH
    // BAR
    0x03F7, // ( Ϸ → Þ ) GREEK CAPITAL LETTER SHO → LATIN CAPITAL LETTER THORN
    0x03F8, // ( ϸ → p ) GREEK SMALL LETTER SHO → LATIN SMALL LETTER P
    0x03F9, // ( Ϲ → C ) GREEK CAPITAL LUNATE SIGMA SYMBOL → LATIN CAPITAL
    // LETTER C
    0x03FA, // ( Ϻ → M ) GREEK CAPITAL LETTER SAN → LATIN CAPITAL LETTER M
    0x03FD, // ( Ͻ → Ɔ ) GREEK CAPITAL REVERSED LUNATE SIGMA SYMBOL → LATIN
    // CAPITAL LETTER OPEN O
    0x03FF, // ( Ͽ → Ꜿ ) GREEK CAPITAL REVERSED DOTTED LUNATE SIGMA SYMBOL →
    // LATIN CAPITAL LETTER REVERSED C WITH DOT
    0x1D26, // ( ᴦ → r ) GREEK LETTER SMALL CAPITAL GAMMA → LATIN SMALL LETTER R
    0x1D27, // ( ᴧ → ʌ ) GREEK LETTER SMALL CAPITAL LAMDA → LATIN SMALL LETTER
    // TURNED V
    0x1D29, // ( ᴩ → ᴘ ) GREEK LETTER SMALL CAPITAL RHO → LATIN LETTER SMALL
    // CAPITAL P
    0x1FBE, // ( ι → i ) GREEK PROSGEGRAMMENI → LATIN SMALL LETTER I
    0x2129, // ( ℩ → ɿ ) TURNED GREEK SMALL LETTER IOTA → LATIN SMALL LETTER
    // REVERSED R WITH FISHHOOK
    0x1D20D, // ( 𝈍 → V ) GREEK VOCAL NOTATION SYMBOL-14 → LATIN CAPITAL LETTER
    // V
    0x1D213, // ( 𝈓 → F ) GREEK VOCAL NOTATION SYMBOL-20 → LATIN CAPITAL LETTER
    // F
    0x1D216, // ( 𝈖 → R ) GREEK VOCAL NOTATION SYMBOL-23 → LATIN CAPITAL LETTER
    // R
    0x1D217, // ( 𝈗 → Ɐ ) GREEK VOCAL NOTATION SYMBOL-24 → LATIN CAPITAL LETTER
    // TURNED A
    0x1D21A, // ( 𝈚 → O̵ ) GREEK VOCAL NOTATION SYMBOL-52 → LATIN CAPITAL LETTER
    // O, COMBINING SHORT STROKE OVERLAY
    0x1D221, // ( 𝈡 → Ɛ ) GREEK INSTRUMENTAL NOTATION SYMBOL-7 → LATIN CAPITAL
    // LETTER OPEN E
    0x1D22A, // ( 𝈪 → L ) GREEK INSTRUMENTAL NOTATION SYMBOL-23 → LATIN CAPITAL
    // LETTER L
    0x1D230, // ( 𝈰 → ꟻ ) GREEK INSTRUMENTAL NOTATION SYMBOL-30 → LATIN
    // EPIGRAPHIC LETTER REVERSED F
};
