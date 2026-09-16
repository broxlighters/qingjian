#!/usr/bin/env python3
"""使用 fonttools 4.59.0，从指定 Noto 原字体重建测试子集；不联网、不改系统字体。"""
from pathlib import Path
import argparse
from fontTools import subset
from fontTools.ttLib import TTFont

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cjk', required=True, type=Path)
parser.add_argument('--emoji', required=True, type=Path)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
chars = (Path(__file__).parent / 'characters.txt').read_text()
for source, target, index in [(args.cjk, 'QingjianFixtureCJK.otf', 2),
                              (args.emoji, 'QingjianFixtureEmoji.ttf', -1)]:
    font = TTFont(source, fontNumber=index, recalcTimestamp=False)
    options = subset.Options()
    options.layout_features = ['*']
    options.name_IDs = ['*']
    options.name_legacy = True
    sub = subset.Subsetter(options=options)
    sub.populate(text=chars)
    sub.subset(font)
    family = target.split('.')[0]
    for record in font['name'].names:
        if record.nameID in (1, 4, 6):
            record.string = family.encode(record.getEncoding())
    if 'CFF ' in font:
        cff = font['CFF '].cff
        cff.fontNames = [family]
        top = cff.topDictIndex[0]
        top.FullName = family
        top.FamilyName = family
    font.save(Path(__file__).parent / target)
