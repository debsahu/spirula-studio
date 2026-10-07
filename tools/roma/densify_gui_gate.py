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

spec = importlib.util.spec_from_file_location("fixture", HERE / "make_gui_fixture.py")
fixture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fixture)

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


def pick(combo_id, text):
    """Open a combo by id and click the entry whose label contains `text`."""
    ctl("click", combo_id)
    hits = [i for i in guictl.call("/ui/tree", {"named": "0", "q": text})["items"]
            if i["id"] != combo_id and text in i["label"]]
    if not hits:
        check(False, "no entry %r in %s" % (text, combo_id))
        return False
    x0, y0, x1, y1 = hits[0]["rect"]
    ctl("click", "--at", "%d,%d" % ((x0 + x1) / 2, (y0 + y1) / 2))
    return True


def entries(combo_id, text):
    ctl("click", combo_id)
    n = [i for i in guictl.call("/ui/tree", {"named": "0", "q": text})["items"]
         if i["id"] != combo_id and text in i["label"]]
    ctl("key", "Escape")
    return n


def sha(p):
    return hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()


def count(p):
    return struct.unpack("<Q", pathlib.Path(p).read_bytes()[:8])[0]


def quit_gui(pid, exe):
    """Quit through the menu; signal only a process that is verifiably ours."""
    with contextlib.suppress(Exception, SystemExit):   # the app exits mid-request
        ctl("click", "menu_file")
        ctl("click", "menu_quit")
    end = time.time() + 15
    while time.time() < end and alive(pid):
        time.sleep(0.5)
    if alive(pid):
        args = subprocess.run(["ps", "-p", str(pid), "-o", "args="], capture_output=True,
                              text=True).stdout.strip()
        if args.split(" ")[0] == exe:
            os.kill(pid, 15)


def alive(pid):
    return subprocess.run(["ps", "-p", str(pid), "-o", "comm="],
                          capture_output=True, text=True).stdout.strip() != ""


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
    # Only the scene it writes matters here, not whether S-1's gates pass.
    subprocess.run([exe, "densify", "--check", "--check-dir", str(src)],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
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
        check(state()["densify_enable"] is False, "dense points start switched off")
        ctl("click", "enable")
        until(lambda: items("ckpt_get"), "the checkpoint row")
        check(state()["densify_enable"] is True, "the box switches them on")
        check(state()["densify_missing"], "the run is held back for the checkpoint")

        # ---- refusal -------------------------------------------------------
        check(not state()["densify_ready"], "not ready before any licence or checkpoint")
        ctl("click", "ckpt_get")
        check(state()["license_prompt"] == "romav2", "the RoMa v2 terms come first")
        # Every family wants the tick, the MIT one included: Accept does nothing without it.
        ctl("click", "license_accept")
        check(state()["license_prompt"] == "romav2" and not state()["license_tick"],
              "Accept without the tick does nothing (RoMa v2, MIT)")
        ctl("click", "license_accept_tick")
        check(state()["license_tick"], "the tick takes")
        ctl("click", "license_accept")
        check(state()["license_prompt"] == "dinov3", "then the DINOv3 agreement")
        ctl("click", "license_accept")
        check(state()["license_prompt"] == "dinov3" and "accepted_license=dinov3" not in
              (gui_conf.read_text() if gui_conf.exists() else ""),
              "Accept without the tick does nothing (DINOv3 either)")
        ctl("click", "cancel")
        time.sleep(4)   # a download started by mistake needs a moment to show
        s = state()
        conf = gui_conf.read_text() if gui_conf.exists() else ""
        check(s["license_prompt"] == "" and not s["densify_ready"],
              "cancelling the second one leaves the step not ready")
        check("accepted_license=dinov3" not in conf, "... and nothing recorded for DINOv3")
        check(s["license_notice"] != "", "... and the user is told nothing was downloaded")
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
        check(not state()["densify_missing"], "... and released once it is ready")
        check(state()["densify_ready"] and "accepted_license=dinov3" in gui_conf.read_text(),
              "ready, and the acceptance is recorded where the CLI reads it")

        # ---- the run -------------------------------------------------------
        check(pick("source_model", "sparse/0 ") and until(lambda: state()["densify_model"] == "sparse/0",
                                                   "the source model to be picked", 10),
              "a source model can be chosen on this screen")
        ctl("click", "update_dataset")
        until(lambda: state()["busy"], "the run to start", 30)
        until(lambda: not state()["busy"], "the run to finish", 900)
        dense = ds / "sparse/0-roma"
        n_dense = count(dense / "points3D.bin") if (dense / "points3D.bin").exists() else -1
        check((dense / "points3D.bin").exists(), "the dense model was written")
        if (dense / "points3D.bin").exists():
            n = count(dense / "points3D.bin")
            meta = json.loads((dense / "densify.json").read_text())
            check(n > 10 * n_sparse and n == meta["points"],
                  "far more points than the sparse model, and as many as densify.json says (%d)" % n)
            keep = lambda p: [(i, pose, cam, name) for i, pose, cam, name, _ in
                              fixture.read_images(p)]
            check(len(keep(dense / "images.bin")) == n_images and
                  keep(dense / "images.bin") == keep(ds / "sparse/0/images.bin") and
                  sha(dense / "cameras.bin") == before["cameras.bin"],
                  "cameras and every pose are the source's, exactly")
        check(all(sha(ds / "sparse/0" / f) == h for f, h in before.items()),
              "the source model is untouched")
        rec = json.loads((ds / ".spirula-dataset.json").read_text())
        check(rec["steps"].get("densify", {}).get("complete") is True,
              "the record names a finished dense points step")

        check(not entries("source_model", "-roma"),
              "the source list does not offer the dense model it just made")
        ctl("click", "back_home")
        ctl("click", "home_new_dataset")
        ctl("drop", str(ds))
        until(lambda: state()["screen"] == "new_dataset", "the dataset to be dropped again")
        until(lambda: state()["densify_model"] == "sparse/0", "the record to be read back", 10)
        check(state()["densify_model"] == "sparse/0" and state()["densify_enable"],
              "dropped again, the dataset's own source model and switch come back")

        # ---- the training screen ------------------------------------------
        until(lambda: items("open_in_trainer"), "the Open in Trainer button", 30)
        ctl("click", "open_in_trainer")
        until(lambda: state()["screen"] == "train" and state()["train_phase"] == "ready",
              "the trainer to load", 120)
        check(state()["recon_dir"] == "", "the trainer starts on its own pick")
        check(state()["preview_points"] == n_sparse,
              "the dataset preview holds the sparse model's points (%d)" % state()["preview_points"])
        pick("model_combo", "sparse/0-roma")
        until(lambda: state()["recon_dir"] == "sparse/0-roma", "the dense model to be picked", 10)
        until(lambda: state()["train_phase"] == "ready", "the dataset to reload", 120)
        until(lambda: state()["preview_points"] == n_dense, "the preview to load the dense points", 30)
        check(state()["preview_points"] == n_dense > n_sparse,
              "... and then the dense model's points (%d), the seed the trainer will use"
              % state()["preview_points"])
        check(state()["recon_dir"] == "sparse/0-roma" and state()["train_phase"] == "ready",
              "the Model combo feeds the trainer's recon dir and the dataset reloads on it")
    finally:
        quit_gui(pid, exe)
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
    print("\nFAILED: %d" % failures if failures else "\nall passed")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
