"""Check decoded-column cache behavior against the previous LRU policy."""

import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class ColumnCacheTests(unittest.TestCase):
    def test_lru_collisions_and_independent_owners(self):
        root = Path(__file__).resolve().parents[1]
        compiler = shutil.which(os.environ.get("CC", "gcc"))
        self.assertIsNotNone(compiler, "a C compiler is required")
        with tempfile.TemporaryDirectory(prefix="doom-column-cache-") as tmp:
            executable = Path(tmp) / "column_cache.exe"
            for capacity, buckets in ((255, 1), (255, 128), (255, 512), (300, 512)):
                with self.subTest(capacity=capacity, buckets=buckets):
                    subprocess.run(
                        [compiler, "-std=c17", "-O2", "-Wall", "-Wextra", "-Werror",
                         f"-DHAL_PATCH_COLUMN_CACHE_HASH_SIZE={buckets}",
                         f"-DDOOM_COLUMN_CACHE_CAPACITY={capacity}",
                         "-I", str(root / "src"),
                         str(root / "tests" / "doom_column_cache_test.c"),
                         "-o", str(executable)],
                        check=True, capture_output=True, text=True,
                    )
                    subprocess.run([str(executable)], check=True,
                                   capture_output=True, text=True, timeout=30)


if __name__ == "__main__":
    unittest.main()
