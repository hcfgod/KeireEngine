"""Portable authoring checks: python Scripts/Examples/Outpost/test-outpost.py."""
import contextlib
import io
from pathlib import Path
import runpy
import tempfile
import unittest

api = runpy.run_path(str(Path(__file__).with_name('configure-vanguard.py')))


class VanguardAuthoringTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.project = self.root / 'project'
        self.source = self.root / 'downloads'
        self.source.mkdir()
        self.target = self.project / 'Assets/Owned/Vanguard'
        self.target.mkdir(parents=True)
        self.scene = self.project / 'Assets/Scenes/KeireOutpost.keirescene'
        self.scene.parent.mkdir()
        api['write'](self.scene, {'entities': [{'name': 'Explorer model', 'components': [
            {'data': {}}, {'version': 3, 'data': {'material': 'old-monster'}}, {'data': {}}]}]})
        catalog = []
        for name in [api['MODEL'], *api['CLIPS']]:
            for folder in [self.source, self.target]:
                (folder / (name + '.fbx')).write_bytes(name.encode())
            clip = api['identity']('clip/' + name)
            # Textures may accompany an animation source, so ordering is not a type contract.
            api['write'](self.target / (name + '.fbx.keiremeta'), dict(id=api['identity'](name),
                subAssets=[clip, api['identity']('texture/' + name)]))
            catalog.append(dict(id=clip, type='4b454952-4541-4e49-4d43-4c4950000001'))
        path = self.project / 'Library/AssetCache/Runtime/catalog.json'
        path.parent.mkdir(parents=True)
        api['write'](path, {'assets': catalog})

    def configure(self):
        with contextlib.redirect_stdout(io.StringIO()):
            api['configure'](self.project, self.source, Path('unused'), skip_import=True)

    def test_all_motion_clips_bound_by_catalog_type_and_rerun_is_stable(self):
        self.configure()
        graph_path = self.target / 'VanguardLocomotion.keireanimgraph'
        graph = api['read'](graph_path)
        states = graph['layers'][0]['states']
        self.assertEqual([s['name'] for s in states], api['CLIPS'])
        for state in states:
            self.assertEqual(state['motion']['clip'], api['identity']('clip/' + state['name']))
        actor = api['read'](self.scene)['entities'][0]
        self.assertEqual(actor['name'], 'Vanguard model')
        self.assertEqual(actor['components'][0]['data']['rotation'], [0, 1, 0, 0])
        self.assertIsNone(actor['components'][1]['data']['material'])
        self.assertFalse(actor['components'][2]['data']['applyRootMotion'])
        first = (self.scene.read_bytes(), graph_path.read_bytes())
        self.configure()
        self.assertEqual(first, (self.scene.read_bytes(), graph_path.read_bytes()))

    def test_missing_or_changed_source_does_not_change_scene(self):
        before = self.scene.read_bytes()
        (self.source / 'Walking.fbx').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError, 'Source changed'):
            self.configure()
        self.assertEqual(before, self.scene.read_bytes())
        (self.source / 'Walking.fbx').unlink()
        with self.assertRaisesRegex(ValueError, 'Missing source'):
            self.configure()
        self.assertEqual(before, self.scene.read_bytes())


if __name__ == '__main__':
    unittest.main()
