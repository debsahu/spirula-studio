"""Run: python3 -m unittest test_integrity (from this directory). Each case names the mutation it catches."""
import json, os, struct, tempfile, unittest
import integrity

def images_bin(n_obs, ids=None):
    b = struct.pack('<Q', 1) + struct.pack('<i', 1) + b'\0' * 56 + struct.pack('<i', 1) + b'a.jpg\0'
    b += struct.pack('<Q', n_obs)
    for k in range(n_obs):
        i = (2**64 - 1) if ids is None else ids[k]
        b += struct.pack('<ddQ', 0.0, 0.0, i)
    return b

def model(root, name, imgs, cams=b'cams'):
    d = os.path.join(root, name); os.makedirs(d)
    open(d + '/images.bin', 'wb').write(imgs); open(d + '/cameras.bin', 'wb').write(cams)
    json.dump(dict(mask_keep=0.8), open(d + '/densify.json', 'w'))
    return d

class Integrity(unittest.TestCase):
    def verdict(self, sib_imgs, src_imgs, sib_cams=b'cams'):
        with tempfile.TemporaryDirectory() as t:
            return integrity.check(model(t, 's', sib_imgs, sib_cams), model(t, 'r', src_imgs))

    def test_a_clean_sibling_passes(self):
        r = self.verdict(images_bin(3), images_bin(3, [1, 2, 3]))
        self.assertFalse(r['void'])

    def test_an_empty_sibling_is_void(self):
        # Mutant: all() over an empty list is True, so a sibling with no observations passed.
        r = self.verdict(images_bin(0), images_bin(0))
        self.assertTrue(r['void'])

    def test_a_linked_observation_is_void(self):
        r = self.verdict(images_bin(3, [1, 2, 3]), images_bin(3, [1, 2, 3]))
        self.assertTrue(r['void'])

    def test_different_cameras_are_void(self):
        r = self.verdict(images_bin(3), images_bin(3, [1, 2, 3]), sib_cams=b'other')
        self.assertTrue(r['void'])

    def test_main_exits_nonzero_when_void(self):
        # Mutant: main() printing the verdict but returning 0.
        import contextlib, io, sys
        with tempfile.TemporaryDirectory() as t:
            sib, src = model(t, 's', images_bin(0)), model(t, 'r', images_bin(0))
            for sibling, want in ((sib, 1), (model(t, 'ok', images_bin(3)), 0)):
                if want == 0: src = model(t, 'r2', images_bin(3, [1, 2, 3]))
                sys.argv = ['integrity.py', '--root', t, sibling, src]
                with contextlib.redirect_stdout(io.StringIO()):
                    self.assertEqual(integrity.main(), want)

class Anchors(unittest.TestCase):
    def setUp(self):
        import numpy as np
        self.np = np
        self.P = np.random.default_rng(0).uniform(0, 10, (9000, 3))
        self.M = np.random.default_rng(1).uniform(20, 30, (500, 3))

    def test_a_clean_cloud_has_none(self):
        self.assertFalse(integrity.contains_anchors(self.M, self.P))

    def test_a_float32_copy_is_found(self):
        # Mutant: `== 0.0` misses a copy that went through float32.
        M = self.np.vstack([self.M, self.P[10].astype(self.np.float32).astype(self.np.float64) + 1e-7])
        self.assertTrue(integrity.contains_anchors(M, self.P))

    def test_an_anchor_past_the_first_5000_is_found(self):
        # Mutant: querying P[:5000] only.
        M = self.np.vstack([self.M, self.P[8000]])
        self.assertTrue(integrity.contains_anchors(M, self.P))

    def test_an_empty_cloud_is_not_clean(self):
        self.assertTrue(integrity.contains_anchors(self.M[:0], self.P))

if __name__ == '__main__':
    unittest.main()
