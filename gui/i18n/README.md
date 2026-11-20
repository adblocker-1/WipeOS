# Translations

The English text in the source code is the key: `_("Create job")` (interface),
`R_("Erasure report")` (PDF report), `N_("...")` (translated where it is used).

- `keys.txt` – all texts, numbered (`python3 extract.py` updates it; numbers stay stable)
- `<lang>.txt` – one translation per line as `number|text`
- `python3 gen.py` – checks the translations (all keys present, same `%` placeholders)
  and writes `../app/lang_<lang>.c`

**Add a language:** copy `keys.txt` to `<code>.txt` and translate it, add the code to `LANGS`
in `gen.py`, add the language to `lang_t` in `app/app.h` and to `s_langs` in `app/i18n.c`.
The interface fonts cover Latin, Latin Extended-A and Cyrillic.

**Keyboard layouts** are tables in `app/input.c` (`s_layouts`).
