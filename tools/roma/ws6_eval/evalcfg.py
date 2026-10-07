"""evalcfg.py: shared path resolution for the WS-6 evaluation harness.

Nothing in this directory opens a path relative to the current working directory. Every input is either
  * an explicit flag, or
  * a default hung off the DATA ROOT:  --root, else env SS_EVAL_ROOT, else the nearest ancestor of this
    file's directory that contains work/ws6_eval  (work/ws6_eval of the enclosing checkout).
A path that does not exist is refused up front, with a message naming the path and the flag that sets it.
There is deliberately no fallback to a second location: a wrong default must stop the run, not quietly
score a different cloud.
"""
import argparse, importlib.util, json, os, sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def die(msg):
    print('error: ' + msg, file=sys.stderr)
    raise SystemExit(2)


def find_root(cli=None):
    if cli:
        src, p = '--root', Path(cli)
    elif os.environ.get('SS_EVAL_ROOT'):
        src, p = 'env SS_EVAL_ROOT', Path(os.environ['SS_EVAL_ROOT'])
    else:
        for anc in [HERE, *HERE.parents]:
            if (anc / 'work' / 'ws6_eval').is_dir():
                return anc / 'work' / 'ws6_eval'
        die(f'cannot locate the data root: no ancestor of {HERE} contains work/ws6_eval. Pass --root <dir> or set SS_EVAL_ROOT.')
    if not p.is_dir():
        die(f'data root from {src} is not a directory: {p}. Fix {src}.')
    return p.absolute()


def parser(doc, **kw):
    ap = argparse.ArgumentParser(description=doc, formatter_class=argparse.RawDescriptionHelpFormatter, **kw)
    ap.add_argument('--root', default=None, help='data root (default: $SS_EVAL_ROOT, else nearest ancestor work/ws6_eval); other defaults are <root>/...')
    return ap


def parse(ap, argv=None):
    a = ap.parse_args(argv)
    a.root = find_root(a.root)
    return a


def resolve(a, attr, flag, rel, kind, what):
    """Value of --flag if given, else <root>/<rel>. Must exist (kind 'file' or 'dir'), else exit naming path + flag."""
    v = getattr(a, attr, None)
    p = Path(v) if v else a.root / rel
    ok = p.is_file() if kind == 'file' else p.is_dir()
    if not ok:
        die(f'{what} not found: {p}\n       expected a {kind}; set it with {flag} (default <root>/{rel}, root = {a.root})')
    setattr(a, attr, p)
    return p


def check_path(p, flag, kind, what):
    """Existence check for a path that has no root default (positional or required flag)."""
    p = Path(p)
    ok = p.is_file() if kind == 'file' else p.is_dir()
    if not ok:
        die(f'{what} not found: {p}\n       expected a {kind}; it is given by {flag}')
    return p


def add_spike(ap):
    ap.add_argument('--spike-dir', default=None, help='dir with roi.py, steps_roi.json, model.pkl, da360_seed_only.npy (default <root>/spike)')


def add_m3(ap):
    ap.add_argument('--m3', default=None, help='M3 cloud, COLMAP points3D.txt (default <root>/spike/m3_points3D.txt)')


def load_spike(a):
    """Resolve --spike-dir, import roi.py from it under the name 'roi' (by path, not sys.path), apply steps_roi.json.
    Returns (roi_module, spike_dir)."""
    sd = resolve(a, 'spike_dir', '--spike-dir', 'spike', 'dir', 'spike directory')
    for f in ('roi.py', 'steps_roi.json', 'model.pkl', 'da360_seed_only.npy'):
        if not (sd / f).is_file():
            die(f'spike file not found: {sd / f}\n       set the directory with --spike-dir (default <root>/spike)')
    spec = importlib.util.spec_from_file_location('roi', sd / 'roi.py')
    R = importlib.util.module_from_spec(spec); spec.loader.exec_module(R)
    J = json.load(open(sd / 'steps_roi.json'))
    R.ROI.update(dict(u=J['u'], w=J['w'], h=J['h']))
    return R, sd, J


def import_sibling(name):
    """Import a module that lives beside this file, regardless of the current working directory."""
    spec = importlib.util.spec_from_file_location(name, HERE / f'{name}.py')
    m = importlib.util.module_from_spec(spec); sys.modules.setdefault(name, m); spec.loader.exec_module(m)
    return m


def check_outdir(out, flag):
    """The parent of an output file must exist; fail before any work rather than after."""
    parent = Path(out).resolve().parent
    if not parent.is_dir():
        die(f'output directory not found: {parent}\n       the output file is given by {flag}')


def check_any(p, flag, what):
    """Existence check for a path that may be a file or a directory."""
    if not Path(p).exists():
        die(f'{what} not found: {p}\n       it is given by {flag}')
    return Path(p)
