#!/usr/bin/env python3
"""Gate for the "Points from" combo: tools/guictl.py selects MoGe depth.

Launches the GUI with a private config and cache, so no licence is accepted and
no RoMa checkpoint exists, drops a dataset that has depth maps, and asserts that
Auto reads as RoMa there (hybrid only when asked), that Hybrid and RoMa hold the run back for the
checkpoint, that MoGe depth does not, and that running it opens no licence
prompt and fetches nothing. Then reads the dense model's own densify.json for
the source it ran, and drops the dataset again for the source to come back.

    python3 tools/roma/densify_source_gate.py --exe build/spirula
"""
import argparse
import json
import os
import pathlib
import shutil
import subprocess
import sys
import tempfile
import time

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import importlib.util
spec = importlib.util.spec_from_file_location("base_gate", HERE / "densify_gui_gate.py")
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
check, ctl, state, items, until, pick = (base.check, base.ctl, base.state, base.items,
                                         base.until, base.pick)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", required=True)
    ap.add_argument("--port", type=int, default=7794)
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    exe = os.path.abspath(a.exe)
    work = pathlib.Path(tempfile.mkdtemp(prefix="densify_source_gate_"))
    cfg, cache = work / "config", work / "cache"
    os.environ.update(XDG_CONFIG_HOME=str(cfg), XDG_CACHE_HOME=str(cache),
                      SS_GUI_AUTOMATION_PORT=str(a.port))
    models = cache / "spirula-studio" / "models"
    gui_conf = cfg / "spirula-studio" / "gui.conf"

    src, ds = work / "src", work / "ds6"
    subprocess.run([exe, "densify", "--check", "--check-source", "moge", "--check-dir", str(src)],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    subprocess.run([sys.executable, "-I", str(HERE / "make_gui_fixture.py"), str(src), str(ds),
                    "--depths"], check=True, stdout=subprocess.DEVNULL)
    n_depth = len(list((ds / "depths").glob("*")))
    check(n_depth == 6, "the fixture has a depth map for each of 6 images (%d)" % n_depth)

    pid = json.loads(ctl("launch", "--exe", exe, "--offscreen", "--port", str(a.port),
                         "--log", str(work / "gui.log")))["pid"]
    prompts = []
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
        until(lambda: items("source_from"), "the Points from combo")
        # The options pane is a clipped scroll region: bring the combo into it.
        for _ in range(8):
            rows = [i["rect"] for i in items("source_from") if i["id"] == "source_from"]
            if rows and 100 < rows[0][1] < 450:
                break
            ctl("scroll", "--at", "400,300", "--dy", "-12")
        s = state()
        check(s["densify_source"] == "auto", "Points from starts on Auto")
        check(s["densify_auto_picks"] == "roma",
              "Auto reads as roma on a dataset with depth maps, not hybrid (%s)" % s["densify_auto_picks"])
        check(s["densify_missing"] and not s["densify_ready"],
              "Auto needs the RoMa checkpoint, so the run is held back")

        check(pick("source_from", "RoMa matches") and
              until(lambda: state()["densify_source"] == "roma", "RoMa to be picked", 10),
              "RoMa matches can be picked")
        check(state()["densify_missing"], "RoMa matches hold the run back for the checkpoint")
        check(pick("source_from", "Hybrid") and
              until(lambda: state()["densify_source"] == "hybrid", "Hybrid to be picked", 10),
              "Hybrid can be picked")
        check(state()["densify_missing"], "Hybrid holds the run back too")

        check(pick("source_from", "MoGe depth") and
              until(lambda: state()["densify_source"] == "moge", "MoGe to be picked", 10),
              "MoGe depth can be picked")
        s = state()
        check(not s["densify_missing"] and s["densify_ready"],
              "MoGe depth is ready with no checkpoint and no licence")
        check(s["license_prompt"] == "", "... and no licence prompt is open")
        check(not models.exists() or not list(models.glob("romav2*")),
              "... and no RoMa checkpoint exists")

        ctl("click", "update_dataset")
        until(lambda: state()["busy"], "the run to start", 30)
        end = time.time() + 600
        while time.time() < end and state()["busy"]:
            if state()["license_prompt"]:
                prompts.append(state()["license_prompt"])
            time.sleep(0.3)
        check(not state()["busy"], "the run finished")
        check(not prompts, "no licence prompt appeared during the run (%s)" % prompts)
        check("accepted_license" not in (gui_conf.read_text() if gui_conf.exists() else ""),
              "no licence was accepted on the way")
        dense = ds / "sparse/0-roma"
        check((dense / "points3D.bin").exists(), "the dense model was written")
        if (dense / "densify.json").exists():
            meta = json.loads((dense / "densify.json").read_text())
            check(meta.get("source") == "moge",
                  "densify.json says it ran the moge source (%r)" % meta.get("source"))
            check(meta.get("points", 0) > 0, "it holds points (%s)" % meta.get("points"))
        else:
            check(False, "densify.json was written")
        check(not models.exists() or not list(models.glob("romav2*")),
              "still no RoMa checkpoint: nothing was fetched")

        ctl("click", "back_home")
        ctl("click", "home_new_dataset")
        ctl("drop", str(ds))
        until(lambda: state()["screen"] == "new_dataset", "the dataset to be dropped again")
        until(lambda: state()["densify_source"] == "moge", "the record to be read back", 10)
        check(state()["densify_source"] == "moge",
              "dropped again, the dataset's own source comes back")
    finally:
        base.quit_gui(pid, exe)
        if not a.keep:
            shutil.rmtree(work, ignore_errors=True)
    print("\nFAILED: %d" % base.failures if base.failures else "\nall passed")
    sys.exit(1 if base.failures else 0)


if __name__ == "__main__":
    main()
