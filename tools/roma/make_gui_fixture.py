#!/usr/bin/env python3
"""Cut a few images out of the staircase `spirula densify --check --check-dir` writes.

The GUI gate (tools/roma/densify_gui_gate.py) wants a dataset small enough to
densify in a minute: the first N pinhole images and a sparse
model reduced to them. No masks: the staircase's pinhole masks keep every pixel, which
densify's polarity guard refuses. Points left with fewer than two observations are
dropped from the tracks; every 2-D row keeps its index, so a track still
points at the right keypoint.

    spirula densify --check --check-dir SRC
    python3 tools/roma/make_gui_fixture.py SRC DST [--images 6]
"""
import argparse
import pathlib
import shutil
import struct


def read_images(path):
    b = path.read_bytes()
    (n,) = struct.unpack_from("<Q", b, 0)
    o, out = 8, []
    for _ in range(n):
        iid, = struct.unpack_from("<i", b, o)
        pose = struct.unpack_from("<7d", b, o + 4)
        cam, = struct.unpack_from("<i", b, o + 60)
        o += 64
        end = b.index(b"\0", o)
        name = b[o:end].decode()
        o = end + 1
        (k,) = struct.unpack_from("<Q", b, o)
        o += 8
        pts = [struct.unpack_from("<ddq", b, o + 24 * i) for i in range(k)]
        o += 24 * k
        out.append((iid, pose, cam, name, pts))
    return out


def read_points(path):
    b = path.read_bytes()
    (n,) = struct.unpack_from("<Q", b, 0)
    o, out = 8, []
    for _ in range(n):
        pid, x, y, z, r, g, bl, err, tl = struct.unpack_from("<Q3d3BdQ", b, o)
        o += 8 + 24 + 3 + 8 + 8
        track = [struct.unpack_from("<ii", b, o + 8 * i) for i in range(tl)]
        o += 8 * tl
        out.append((pid, (x, y, z), (r, g, bl), err, track))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("src", type=pathlib.Path)
    ap.add_argument("dst", type=pathlib.Path)
    ap.add_argument("--images", type=int, default=6)
    ap.add_argument("--depths", action="store_true",
                    help="also copy SRC/depths for the kept images (SRC from --check-source moge)")
    a = ap.parse_args()

    images = [i for i in read_images(a.src / "sparse/0/images.bin") if i[3].startswith("pin_")]
    images = sorted(images, key=lambda i: i[3])[: a.images]
    assert len(images) == a.images, "the source has fewer pinhole images than asked for"
    keep_ids = {i[0] for i in images}

    points = []
    for pid, xyz, rgb, err, track in read_points(a.src / "sparse/0/points3D.bin"):
        track = [t for t in track if t[0] in keep_ids]
        if len(track) >= 2:
            points.append((pid, xyz, rgb, err, track))
    alive = {p[0] for p in points}

    out = a.dst / "sparse/0"
    out.mkdir(parents=True, exist_ok=True)
    shutil.copy(a.src / "sparse/0/cameras.bin", out / "cameras.bin")
    with open(out / "images.bin", "wb") as f:
        f.write(struct.pack("<Q", len(images)))
        for iid, pose, cam, name, pts in images:
            f.write(struct.pack("<i7di", iid, *pose, cam) + name.encode() + b"\0")
            f.write(struct.pack("<Q", len(pts)))
            for x, y, pid in pts:
                f.write(struct.pack("<ddq", x, y, pid if pid in alive else -1))
    with open(out / "points3D.bin", "wb") as f:
        f.write(struct.pack("<Q", len(points)))
        for pid, xyz, rgb, err, track in points:
            f.write(struct.pack("<Q3d3BdQ", pid, *xyz, *rgb, err, len(track)))
            for t in track:
                f.write(struct.pack("<ii", *t))
    (a.dst / "images").mkdir(exist_ok=True)
    for _, _, _, name, _ in images:
        shutil.copy(a.src / "images" / name, a.dst / "images" / name)
    if a.depths:
        (a.dst / "depths").mkdir(exist_ok=True)
        for _, _, _, name, _ in images:
            found = list((a.src / "depths").glob(pathlib.Path(name).stem + ".*"))
            assert found, "no depth map for " + name
            shutil.copy(found[0], a.dst / "depths" / found[0].name)
    print(f"{len(images)} images, {len(points)} points -> {a.dst}")


if __name__ == "__main__":
    main()
