import os, glob

chunks = [
"0JvYmn1EZxOU-A8", "rotNyBWDdAa1BgG", "rmBrYht91TqHOwZ", "ezfMy4Zlw$4mPwf",
"wd1RUBVIEOIqyLH", "vKKqYgV0M1hqFXw", "kas3cTiJGaeZb$-", "re76eB2CJnPE7YK",
"xkYq0Xf$vk$GnHi", "cT3WnyWqGkvC4fB", "oNmL8up$kJ0lCUr", "kYkhBCpy8tZ8V-V",
"PgwVIOeNJmCQ9VB", "uojPwGHK71DWHdY", "14UbWCEtbWa8WzJ", "g6qoZ9XQRjjGkj5",
"kRZqhUvEiVGU5Es", "JZIAiZ6CuCN-FSC", "fArfgRgUMwsTMe4", "ZrIryasfNnmQWt2",
"Q6lPDFhNJVU97sH", "nMMFINv16k$YFFZ", "afhYUyKfsw7J-lf", "KqZKGwqLXHLkgoT",
"tyoSIt-vl2HIjYl", "lulPkcRMxxzUgmF", "m2-JLFIv8HRljGC", "NHnqA85uo6j9fRd"
]

present = []
missing = []
for c in chunks:
    matches = glob.glob(f"tools/luna_probe/bundles/*{c}*")
    if matches:
        present.append((c, matches[0]))
    else:
        missing.append(c)

print("Present:", len(present))
print("Missing:", missing)
