"""Changing native hooks must not silently relink old generated guest objects."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('native_builder',ROOT/'builder/build_native.py')
builder=importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)

class Upgrade(unittest.TestCase):
    def test_old_relink_runs_codegen_path(self):
        class StopAtCopyCheck(Exception): pass
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp)
            here=root/'builder'; out=root/'native'; here.mkdir(); out.mkdir()
            (here/'build.version').write_text('4')
            (here/'compile.json').write_text('{}')
            (out/'build.version').write_text('3')
            def check(_): raise StopAtCopyCheck()
            with patch.multiple(builder,HERE=str(here),OUT=str(out),GAME=str(root/'game')):
                with patch.object(builder.sys,'argv',['build_native.py','--relink']), \
                     patch.object(builder,'check',side_effect=check) as checker, \
                     patch.object(builder,'link') as linker:
                    try:
                        with self.assertRaises(StopAtCopyCheck): builder.main()
                        checker.assert_called_once()
                        linker.assert_not_called()
                    finally:
                        if builder.log_file: builder.log_file.close()

if __name__=='__main__': unittest.main()
