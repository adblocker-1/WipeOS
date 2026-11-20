# Fonts for the PDF report

`WipeOS-Sans-Regular.ttf` and `WipeOS-Sans-Bold.ttf` are subsets of **IBM Plex Sans**
(SIL Open Font License 1.1, see `../app/IBM-Plex-OFL-1.1.txt`). They contain Latin, Latin-1,
Latin Extended-A, Cyrillic and common punctuation and are embedded in every erasure report.
Because they are modified (subset) versions, they carry the name "WipeOS Sans" as the
license requires.

Created with fontTools (`pyftsubset` API) from `@ibm/plex-sans` (`IBMPlexSans-Regular.woff`,
`IBMPlexSans-Bold.woff`), unicodes `U+0020-007E, U+00A0-017F, U+0400-045F, U+0490-0491`
plus `– — ‘ ’ ‚ “ ” „ • … € №`.

The bitmap fonts of the interface (`../app/f_*.c`) are generated with `lv_font_conv` from the
same family; the exact command is in the header of each file.
