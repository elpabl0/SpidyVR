#!/usr/bin/env python3
"""Fetch the city's photo materials, its sky and the image decoder.

Everything comes from public CC0 / public-domain sources, pinned by SHA-256
in quest/assets.lock.json:

  * ambientCG (https://ambientcg.com, CC0): facades, bricks, asphalt, paving,
    gravel, metal, wood -- the 1K JPG set of each, of which the colour,
    normal (OpenGL), roughness and metalness maps are kept;
  * Poly Haven (https://polyhaven.com, CC0): the sky, a 2K HDRI;
  * stb_image.h (https://github.com/nothings/stb, public domain / MIT).

Run it once before building the Quest app (or the Linux preview):

    python3 quest/tools/fetch_assets.py

Materials land in quest/app/src/main/assets/city/<Material>/ and the sky in
quest/app/src/main/assets/city/sky.hdr, stb in third_party/stb/. Files
already there with the pinned hash are kept. Standard library only.
"""
import argparse
import hashlib
import io
import json
import os
import sys
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
LOCK = os.path.join(ROOT, "quest", "assets.lock.json")
ASSETS = os.path.join(ROOT, "quest", "app", "src", "main", "assets", "city")
STB = os.path.join(ROOT, "third_party", "stb")
MAPS = {"Color": "color.jpg", "NormalGL": "normal.jpg", "Roughness": "roughness.jpg", "Metalness": "metalness.jpg"}


def download(url):
    request = urllib.request.Request(url, headers={"User-Agent": "spidy-fetch-assets/1.0"})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(request, timeout=120) as response:
                return response.read()
        except Exception as error:  # network hiccups: try again, then give up
            if attempt == 3:
                raise SystemExit(f"Could not download {url}: {error}")
            print(f"  retrying {url} ({error})")


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def have(path, digest):
    if not os.path.exists(path):
        return False
    with open(path, "rb") as f:
        return sha256(f.read()) == digest


def write(path, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def check(name, data, digest, update, lock_entry, key):
    actual = sha256(data)
    if update:
        lock_entry[key] = actual
        return
    if actual != digest:
        raise SystemExit(
            f"{name} differs from the pinned file (sha256 {actual}, expected {digest}).\n"
            "The source may have republished it. Check it, then run with --update-lock to pin the new one.")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--update-lock", action="store_true",
                        help="download everything again and pin what arrives (maintainers)")
    args = parser.parse_args()
    with open(LOCK) as f:
        lock = json.load(f)

    for material in lock["materials"]:
        name = material["id"]
        folder = os.path.join(ASSETS, name)
        wanted = {MAPS[k]: v for k, v in material["files"].items()}
        if not args.update_lock and all(have(os.path.join(folder, f), d) for f, d in wanted.items()):
            print(f"{name}: up to date")
            continue
        print(f"{name}: downloading from ambientCG")
        archive = zipfile.ZipFile(io.BytesIO(download(f"https://ambientcg.com/get?file={name}_1K-JPG.zip")))
        files = {}
        for map_name, file_name in MAPS.items():
            member = f"{name}_1K-JPG_{map_name}.jpg"
            if member in archive.namelist():
                files[map_name] = archive.read(member)
        if "Color" not in files:
            raise SystemExit(f"{name}: the archive has no colour map")
        if args.update_lock:
            material["files"] = {}
        for map_name, data in files.items():
            if not args.update_lock and map_name not in material["files"]:
                continue
            check(f"{name} {map_name}", data, material["files"].get(map_name), args.update_lock,
                  material["files"], map_name)
            write(os.path.join(folder, MAPS[map_name]), data)

    sky = lock["sky"]
    path = os.path.join(ASSETS, "sky.hdr")
    if args.update_lock or not have(path, sky["sha256"]):
        print(f"sky: downloading {sky['id']} from Poly Haven")
        data = download(sky["url"])
        check("sky", data, sky["sha256"], args.update_lock, sky, "sha256")
        write(path, data)
    else:
        print("sky: up to date")

    stb = lock["stb_image"]
    path = os.path.join(STB, "stb_image.h")
    if args.update_lock or not have(path, stb["sha256"]):
        print(f"stb_image.h: downloading revision {stb['revision']}")
        data = download(f"https://raw.githubusercontent.com/nothings/stb/{stb['revision']}/stb_image.h")
        check("stb_image.h", data, stb["sha256"], args.update_lock, stb, "sha256")
        write(path, data)
    else:
        print("stb_image.h: up to date")

    if args.update_lock:
        with open(LOCK, "w") as f:
            json.dump(lock, f, indent=2)
            f.write("\n")
        print(f"Pinned in {os.path.relpath(LOCK, ROOT)}")
    print("Assets ready.")


if __name__ == "__main__":
    sys.exit(main())
