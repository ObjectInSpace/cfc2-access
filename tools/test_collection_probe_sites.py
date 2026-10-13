"""The optional probe's generated guards must match reviewed complete bodies."""
import unittest
from pathlib import Path
import collection_probe_sites as sites
from collection_lifecycle_audit import SHARED_SPANS
from collection_snapshot_audit import SPANS
from collection_quicksave_audit import SPANS as QUICK_SPANS
from collection_native_boundary_audit import SPANS as NATIVE_SPANS


class ProbeSitesTests(unittest.TestCase):
    def test_generated_header_reproduces(self):
        path = Path(__file__).resolve().parents[1] / 'src/collection_probe_sites.inc'
        self.assertEqual(path.read_text(encoding='utf-8'), sites.render())

    def test_every_site_is_a_reviewed_whole_body(self):
        reviewed = {(a,b) for _,a,b in (*SHARED_SPANS, *SPANS, *QUICK_SPANS, *NATIVE_SPANS)}
        for _,start,end in sites.SITES + sites.WAIT_GUARDS:
            self.assertIn((start,end), reviewed)
            self.assertLessEqual(end-start, 4096)
        self.assertEqual(len(sites.SITES), 10)
        self.assertEqual(len(sites.WAIT_GUARDS), 4)


if __name__ == '__main__':
    unittest.main()
