"""Exercise the actual four-pass pipeline and reject partially tracked outputs."""
import importlib.util
import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'tools/prepare_snapshot_codegen.py'
spec = importlib.util.spec_from_file_location('snapshot_codegen', SCRIPT)
pipeline = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pipeline)


class SnapshotCodegenTest(unittest.TestCase):
    def test_pipeline_is_complete_and_idempotent(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = pathlib.Path(temporary)
            (directory / 'tekken6_pch.h').write_text('#pragma once\n')
            unit = directory / 'tekken6_recomp.0.cpp'
            unit.write_text('''DEFINE_REX_FUNC(sub_822188C8) {
\tREX_FUNC_PROLOGUE();
\tREX_STORE_U32(ctx.r1.u32 + 4, 1);
\tREX_CALL_INDIRECT_FUNC(ctx.ctr.u32);
}
DEFINE_REX_FUNC(sub_82570010) {
\tREX_FUNC_PROLOGUE();
\tsub_824C64B8(ctx, base);
}
''')
            subprocess.run([sys.executable, str(SCRIPT), str(directory)], check=True)
            text = unit.read_text()
            for token in ['T6_KIND(1)', 'T6_KIND(4)', 'T6_SND_INDIRECT',
                          'T6_STORE_SP_U32', 't6_lib_alloc(ctx, base)']:
                self.assertIn(token, text)
            before = {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in directory.iterdir()}
            subprocess.run([sys.executable, str(SCRIPT), str(directory)], check=True)
            self.assertEqual(before, {p.name: (p.read_bytes(), p.stat().st_mtime_ns) for p in directory.iterdir()})
            # The old patchers skip a whole unit if they see any marker. Audit
            # each prologue so a partially patched unit cannot silently compile.
            unit.write_text(text.replace('T6_KIND(4);', '', 1))
            with self.assertRaisesRegex(ValueError, 'Incomplete snapshot tracking'):
                pipeline.validate(directory)


if __name__ == '__main__':
    unittest.main()
