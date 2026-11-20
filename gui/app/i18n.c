/*
 * Translations. The English text is the key: _("Create job").
 * Each language is a table of { english, translation } pairs in lang_xx.c;
 * a missing entry falls back to English.
 */
#include "app.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const tr_pair_t tr_de[], tr_fr[], tr_es[], tr_it[], tr_pt[], tr_nl[], tr_pl[], tr_tr[], tr_ru[], tr_uk[];

static const struct {
    const char * code;
    const char * name;      /* native name */
    const char * kbd;       /* default keyboard layout */
    const tr_pair_t * table;
} s_langs[LANG_COUNT] = {
    {"en", "English", "us", NULL},
    {"de", "Deutsch", "de", tr_de},
    {"fr", "Français", "fr", tr_fr},
    {"es", "Español", "es", tr_es},
    {"it", "Italiano", "it", tr_it},
    {"pt", "Português", "pt", tr_pt},
    {"nl", "Nederlands", "us", tr_nl},
    {"pl", "Polski", "pl", tr_pl},
    {"tr", "Türkçe", "tr", tr_tr},
    {"ru", "Русский", "ru", tr_ru},
    {"uk", "Українська", "ua", tr_uk},
};

lang_t g_lang = LANG_EN;
lang_t g_rlang = LANG_EN;

const char * lang_code(lang_t l)
{
    return s_langs[l < LANG_COUNT ? l : 0].code;
}

const char * lang_name(lang_t l)
{
    return s_langs[l < LANG_COUNT ? l : 0].name;
}

const char * lang_default_kbd(lang_t l)
{
    return s_langs[l < LANG_COUNT ? l : 0].kbd;
}

int lang_from_code(const char * code)
{
    if(!code) return -1;
    for(int i = 0; i < LANG_COUNT; i++)
        if(strncmp(code, s_langs[i].code, 2) == 0) return i;
    return -1;
}

const tr_pair_t * lang_table(lang_t l)
{
    return s_langs[l < LANG_COUNT ? l : 0].table;
}

const char * tr_in(lang_t l, const char * en)
{
    if(!en || l >= LANG_COUNT) return en;
    const tr_pair_t * t = s_langs[l].table;
    if(!t) return en;
    for(; t->en; t++)
        if(t->en == en || strcmp(t->en, en) == 0) return t->tr;
    return en;
}

/* "symbol  text" / "text  symbol" for buttons and status lines (rotating buffers) */
static char s_symbuf[4][192];
static int s_symi;

const char * symt(const char * sym, const char * text)
{
    char * b = s_symbuf[s_symi++ & 3];
    snprintf(b, sizeof(s_symbuf[0]), "%s  %s", sym, text);
    return b;
}

const char * tsym(const char * text, const char * sym)
{
    char * b = s_symbuf[s_symi++ & 3];
    snprintf(b, sizeof(s_symbuf[0]), "%s  %s", text, sym);
    return b;
}

int g_locale_chosen = 0;
int g_rlang_same = 1;

void locale_init(void)
{
    const char * e = getenv("SHREDOS_LANG");
    int forced = e && e[0];
    const char * lc = forced ? e : storage_get_setting("language");
    int l = lang_from_code(lc);
    g_locale_chosen = l >= 0;
    g_lang = l >= 0 ? (lang_t)l : LANG_EN;

    int rl = lang_from_code(storage_get_setting("report_language"));
    g_rlang_same = rl < 0;
    g_rlang = rl >= 0 ? (lang_t)rl : g_lang;

    const char * k = getenv("SHREDOS_KBD");
    int ki = input_layout_index(k && k[0] ? k : NULL);
    if(ki < 0) ki = input_layout_index(storage_get_setting("keyboard"));
    if(ki < 0) ki = input_layout_index(lang_default_kbd(g_lang));
    input_set_layout(ki < 0 ? 0 : ki);
}
