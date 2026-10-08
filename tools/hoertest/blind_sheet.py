#!/usr/bin/env python3
"""Blind comparison helper for the listening test (docs/HOERTEST.md, part C).

Standard library only. Steps:

  make   pick trials from the mmgen manifest, copy our pattern as ours/NN.mid and write trials.csv with key, scale,
         bars and tempo, so the Live 12 pattern can be generated in the same key
  pack   after the Live patterns are exported as live/NN.mid: shuffle both into pairs/NN_A.mid and NN_B.mid
         (random order per trial), write sheet.csv (to fill in) and key.csv (do not open before rating)
  score  read the filled sheet.csv and key.csv and print how often ours was preferred
"""
import argparse
import csv
import json
import random
import shutil
import sys
from pathlib import Path


def read_manifest(path):
    with open(path, newline="", encoding="utf-8") as handle:
        sample = handle.read(2048)
        handle.seek(0)
        delimiter = ";" if sample.count(";") > sample.count(",") else ","
        return list(csv.DictReader(handle, delimiter=delimiter))


def style_tempo(styles_dir, style):
    with open(Path(styles_dir) / f"{style}.json", encoding="utf-8") as handle:
        low, high = json.load(handle)["tempo"]
    return round((low + high) / 2)


def make(args):
    rows = [r for r in read_manifest(args.manifest) if r.get("status", "ok") == "ok"]
    if not rows:
        sys.exit("no usable rows in the manifest")
    rng = random.Random(args.seed)
    by_style = {}
    for row in rows:
        by_style.setdefault(row["style"], []).append(row)
    styles = sorted(by_style)
    for style in styles:
        rng.shuffle(by_style[style])
    trials = []
    index = 0
    while len(trials) < args.trials:
        style = styles[index % len(styles)]
        pool = by_style[style]
        if not pool:
            break
        trials.append(pool.pop())
        index += 1

    out = Path(args.out)
    (out / "ours").mkdir(parents=True, exist_ok=True)
    (out / "live").mkdir(parents=True, exist_ok=True)
    base = Path(args.manifest).parent
    suffix = "_bass.mid" if args.voice == "bass" else "_melody.mid"
    with open(out / "trials.csv", "w", newline="", encoding="utf-8") as handle:
        writer = csv.writer(handle)
        writer.writerow(["trial", "voice", "style", "key", "scale", "bars", "bpm", "ours_from"])
        for number, row in enumerate(trials, 1):
            source = base / row["file"].replace("_all.mid", suffix)
            shutil.copyfile(source, out / "ours" / f"{number:02d}.mid")
            writer.writerow([f"{number:02d}", args.voice, row["style"], row["key"], row["scale"], row["bars"],
                             style_tempo(args.styles, row["style"]), row["file"]])
    print(f"{len(trials)} trials in {out}/trials.csv")
    print(f"Generate one {args.voice} pattern per trial in Live 12 (same key, scale, bars), export each as "
          f"{out}/live/NN.mid, then run: blind_sheet.py pack --out {out}")


def pack(args):
    out = Path(args.out)
    trials = list(csv.DictReader(open(out / "trials.csv", newline="", encoding="utf-8")))
    rng = random.Random(args.seed)
    (out / "pairs").mkdir(exist_ok=True)
    with open(out / "sheet.csv", "w", newline="", encoding="utf-8") as sheet, \
            open(out / "key.csv", "w", newline="", encoding="utf-8") as key:
        sheet_writer = csv.writer(sheet)
        key_writer = csv.writer(key)
        sheet_writer.writerow(["trial", "better", "note"])
        key_writer.writerow(["trial", "ours_is"])
        for trial in trials:
            number = trial["trial"]
            ours, live = out / "ours" / f"{number}.mid", out / "live" / f"{number}.mid"
            if not live.exists():
                sys.exit(f"missing {live}")
            ours_is = rng.choice("AB")
            shutil.copyfile(ours, out / "pairs" / f"{number}_{ours_is}.mid")
            shutil.copyfile(live, out / "pairs" / f"{number}_{'B' if ours_is == 'A' else 'A'}.mid")
            sheet_writer.writerow([number, "", ""])
            key_writer.writerow([number, ours_is])
    print(f"pairs in {out}/pairs. Rate in {out}/sheet.csv (column better: A, B or =). Do not open key.csv.")


def score(args):
    out = Path(args.out)
    key = {r["trial"]: r["ours_is"] for r in csv.DictReader(open(out / "key.csv", newline="", encoding="utf-8"))}
    trials = {r["trial"]: r for r in csv.DictReader(open(out / "trials.csv", newline="", encoding="utf-8"))}
    ours = live = tie = open_ = 0
    per_style = {}
    for row in csv.DictReader(open(out / "sheet.csv", newline="", encoding="utf-8")):
        verdict = row["better"].strip().upper()
        style = trials[row["trial"]]["style"]
        counts = per_style.setdefault(style, [0, 0, 0])
        if verdict in ("A", "B"):
            if verdict == key[row["trial"]]:
                ours += 1
                counts[0] += 1
            else:
                live += 1
                counts[1] += 1
        elif verdict in ("=", "TIE"):
            tie += 1
            counts[2] += 1
        else:
            open_ += 1
    rated = ours + live + tie
    print(f"rated {rated}, open {open_}: ours {ours}, Live 12 {live}, tie {tie}")
    if rated:
        print(f"ours preferred or tied: {100 * (ours + tie) / rated:.0f} %")
    for style, (o, l, t) in sorted(per_style.items()):
        print(f"  {style}: ours {o}, Live 12 {l}, tie {t}")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    m = sub.add_parser("make")
    m.add_argument("--manifest", required=True)
    m.add_argument("--out", default="hoertest/blind")
    m.add_argument("--styles", default="resources/styles")
    m.add_argument("--trials", type=int, default=12)
    m.add_argument("--voice", choices=["bass", "melody"], default="bass")
    m.add_argument("--seed", type=int, default=1)
    m.set_defaults(func=make)
    p = sub.add_parser("pack")
    p.add_argument("--out", default="hoertest/blind")
    p.add_argument("--seed", type=int, default=1)
    p.set_defaults(func=pack)
    s = sub.add_parser("score")
    s.add_argument("--out", default="hoertest/blind")
    s.set_defaults(func=score)
    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
