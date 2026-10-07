#!/usr/bin/env python3
"""WS-5 gate: tools/guictl.py drives the dense-points step end to end.

Cuts a 6-image fixture from `spirula densify --check`, launches the GUI with a
private config and cache (so the licences and the checkpoint are this run's
own), refuses the DINOv3 licence once, accepts it, runs the step from the New
Dataset screen, then picks the dense model on the training screen. Every
assertion reads a file the run wrote or the app's own /ui/state.

    python3 tools/roma/densify_gui_gate.py --exe build/spirula \
        --checkpoint ~/.cache/spirula-studio/models/romav2.0.1.pt

--checkpoint is copied into the private cache after the refusal, so the gate
needs no network; without it the accept step downloads the real file.
"""
import argparse
import contextlib
import hashlib
import importlib.util
import io
import json
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location("guictl", HERE.parent / "guictl.py")
guictl = importlib.util.module_from_spec(spec)
spec.loader.exec_module(guictl)

failures = 0


def check(ok, what):
    global failures
    print(("ok    " if ok else "FAIL  ") + what)
    failures += 0 if ok else 1


def ctl(*argv):
    out = io.StringIO()
    with contextlib.redirect_stdout(out):
        guictl.main(list(argv))
    return out.getvalue()


def state():
    return guictl.call("/ui/state")


def items(query):
    return guictl.call("/ui/tree", {"named": "1", "q": query})["items"]


def until(pred, what, timeout=60.0):
    end = time.time() + timeout
    while time.time() < end:
        if pred():
            return True
        time.sleep(0.5)
    check(False, "timed out waiting for " + what)
    return False


def sha(p):
    return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()


def count(p):
    return struct.unpack("<Q", pathlib.Path(p).read_bytes()[:8])[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--checkpoint")
    ap.add_argument("--port", type=int, default=7793)
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    exe = os.path.abspath(a.exe)
    work = pathlib.Path(tempfile.mkdtemp(prefix="densify_gui_gate_"))
    cfg, cache = work / "config", work / "cache"
    os.environ.update(XDG_CONFIG_HOME=str(cfg), XDG_CACHE_HOME=str(cache),
                      SS_GUI_AUTOMATION_PORT=str(a.port))
    gui_conf = cfg / "spirula-studio" / "gui.conf"
    models = cache / "spirula-studio" / "models"

    src, ds = work / "src", work / "ds6"
    subprocess.run([exe, "densify", "--check", "--check-dir", str(src)], check=True,
                   stdout=subprocess.DEVNULL)
    subprocess.run([sys.executable, "-I", str(HERE / "make_gui_fixture.py"), str(src), str(ds)],
                   check=True, stdout=subprocess.DEVNULL)
    n_images = len(list((ds / "images").glob("*.png")))
    check(n_images == 6, "the fixture has 6 images (%d)" % n_images)
    before = {f: sha(ds / "sparse/0" / f) for f in ("cameras.bin", "images.bin", "points3D.bin")}
    n_sparse = count(ds / "sparse/0/points3D.bin")
    check(n_sparse > 100 and not (ds / "sparse/0-roma").exists(),
          "a sparse model and no dense one yet (%d points)" % n_sparse)

    pid = json.loads(ctl("launch", "--exe", exe, "--offscreen", "--port", str(a.port),
                         "--log", str(work / "gui.log")))["pid"]
    try:
        ctl("click", "home_new_dataset")
        ctl("drop", str(ds))
        until(lambda: state()["screen"] == "new_dataset", "the New Dataset screen")
        for _ in range(8):
            rows = [i["rect"] for i in items("dense") if i["id"] == "enable"]
            if rows and 100 < rows[0][1] < 600:
                break
            ctl("scroll", "--at", "400,400", "--dy", "-12")
        ctl("click", "enable")
        until(lambda: items("ckpt_get"), "the checkpoint row")

        # ---- refusal -------------------------------------------------------
        check(not state()["densify_ready"], "not ready before any licence or checkpoint")
        ctl("click", "ckpt_get")
        check(state()["license_prompt"] == "romav2", "the RoMa v2 terms come first")
        ctl("click", "license_accept")
        check(state()["license_prompt"] == "dinov3", "then the DINOv3 agreement")
        ctl("click", "cancel")
        time.sleep(4)   # a download started by mistake needs a moment to show
        s = state()
        conf = gui_conf.read_text() if gui_conf.exists() else ""
        check(s["license_prompt"] == "" and not s["densify_ready"],
              "cancelling the second one leaves the step not ready")
        check("accepted_license=dinov3" not in conf, "... and nothing recorded for DINOv3")
        check(not models.exists() or not list(models.glob("romav2*")),
              "... and nothing downloaded")

        # ---- acceptance ----------------------------------------------------
        if a.checkpoint:
            models.mkdir(parents=True, exist_ok=True)
            shutil.copy(a.checkpoint, models / "romav2.0.1.pt")
        until(lambda: items("ckpt_get"), "the Get button beside a cached file", 10)
        check(not state()["densify_ready"],
              "a checkpoint on disk without the licence accepted is not ready")
        ctl("click", "ckpt_get")
        check(state()["license_prompt"] == "dinov3", "asked again, only for what is missing")
        ctl("click", "license_accept_tick")
        ctl("click", "license_accept")
        if not a.checkpoint:
            until(lambda: state()["densify_ready"], "the checkpoint download", 1800)
        until(lambda: state()["densify_ready"], "a ready checkpoint", 30)
        check(state()["densify_ready"] and "accepted_license=dinov3" in gui_conf.read_text(),
              "ready, and the acceptance is recorded where the CLI reads it")

        # ---- the run -------------------------------------------------------
        ctl("click", "update_dataset")
        until(lambda: state()["busy"], "the run to start", 30)
        until(lambda: not state()["busy"], "the run to finish", 900)
        dense = ds / "sparse/0-roma"
        check((dense / "points3D.bin").exists(), "the dense model was written")
        if (dense / "points3D.bin").exists():
            n = count(dense / "points3D.bin")
            meta = json.loads((dense / "densify.json").read_text())
            check(n > 10 * n_sparse and n == meta["points"],
                  "far more points than the sparse model, and as many as densify.json says (%d)" % n)
            check(sha(dense / "images.bin") == before["images.bin"] and
                  sha(dense / "cameras.bin") == before["cameras.bin"],
                  "cameras and poses are byte for byte the source's")
        check(all(sha(ds / "sparse/0" / f) == h for f, h in before.items()),
              "the source model is untouched")
        rec = json.loads((ds / ".spirula-dataset.json").read_text())
        check(rec["steps"].get("densify", {}).get("complete") is True,
              "the record names a finished dense points step")

        # ---- the training screen ------------------------------------------
        until(lambda: items("open_in_trainer"), "the Open in Trainer button", 30)
        ctl("click", "open_in_trainer")
        until(lambda: state()["screen"] == "train" and state()["train_phase"] == "ready",
              "the trainer to load", 120)
        check(state()["recon_dir"] == "", "the trainer starts on its own pick")
        x0, y0, x1, y1 = [i["rect"] for i in items("panel_button")][0]
        combo = (x0 + 100, (y0 + y1) / 2 + 31)
        ctl("click", "--at", "%d,%d" % combo)
        ctl("click", "--at", "%d,%d" % (combo[0], combo[1] + 72))
        until(lambda: state()["recon_dir"] == "sparse/0-roma", "the dense model to be picked", 10)
        until(lambda: state()["train_phase"] == "ready", "the dataset to reload", 120)
        check(state()["recon_dir"] == "sparse/0-roma" and state()["train_phase"] == "ready",
              "the Model combo feeds the trainer's recon dir and the dataset reloads on it")
    finally:
        with contextlib.suppress(ProcessLookupError):
            os.kill(pid, 15)
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
    print("\nFAILED: %d" % failures if failures else "\nall passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
