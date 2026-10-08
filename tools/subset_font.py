#!/usr/bin/env python3
"""Subset a Lato TTF for portmsg and rename it.

Lato is licensed under the SIL OFL 1.1 with Reserved Font Name "Lato". A subset is a
Modified Version under the OFL, so it must not use the reserved name: the family is
renamed to "Portmsg Sans". Glyph outlines and metrics are unchanged.
usage: subset_font.py IN.ttf OUT.ttf STYLE
"""
import sys
from fontTools import subset
from fontTools.ttLib import TTFont

UNICODES = "U+0020-007E,U+00A0-017F,U+2010-2027,U+2030-203A,U+20AC,U+2122,U+2190-2193,U+2022"

src, dst, style = sys.argv[1], sys.argv[2], sys.argv[3]
opts = subset.Options()
opts.layout_features = ["kern"]
opts.hinting = False
opts.name_IDs = ["*"]
font = TTFont(src)
sub = subset.Subsetter(opts)
sub.populate(unicodes=subset.parse_unicodes(UNICODES))
sub.subset(font)

family = "Portmsg Sans"
full = f"{family} {style}" if style != "Regular" else family
names = {
    1: family,
    2: style,
    3: f"{family.replace(' ', '')}-{style};subset of Lato 2.007",
    4: full,
    6: f"{family.replace(' ', '')}-{style}",
    16: family,
    17: style,
}
table = font["name"]
for rec in list(table.names):
    if rec.nameID in (1, 2, 3, 4, 6, 16, 17, 21, 22):
        table.removeNames(nameID=rec.nameID)
for nid, val in names.items():
    table.setName(val, nid, 3, 1, 0x409)
    table.setName(val, nid, 1, 0, 0)
table.setName("Portmsg Sans is a subset of Lato by Lukasz Dziedzic (tyPoland), renamed as "
              "required by the Reserved Font Name clause of the SIL Open Font License 1.1.",
              10, 3, 1, 0x409)
font.save(dst)
