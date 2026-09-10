"""Import the user's local Vanguard and motion clips into an existing private Outpost."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
import uuid

NS = uuid.UUID('1ec83b90-285c-4a49-94e6-5cc361050ab7')
CLIPS = ['Breathing Idle', 'Walking', 'Walking Backward', 'Standard Run',
         'Left Strafe Walking', 'Right Strafe Walking', 'Left Strafe Run', 'Right Strafe Run',
         'Crouch Idle', 'Crouch Walk Forward', 'Crouch Walk Back', 'Crouch Walk Left',
         'Crouch Walk Right', 'Crouch To Standing Idle', 'Jump', 'Falling Idle']
MODEL = 'Vanguard By T. Choonyung'


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def write(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def identity(name):
    return str(uuid.uuid5(NS, 'vanguard/' + name))


def subasset(owner, key):
    digest = hashlib.sha256((owner + '\n' + key).encode()).digest()[:16]
    return str(uuid.UUID(bytes=digest, version=5))


def configure(project, source, tool, skip_import=False):
    scene_path = project / 'Assets/Scenes/KeireOutpost.keirescene'
    scene = read(scene_path)
    actor = next(e for e in scene['entities'] if e['name'] in ['Explorer model', 'Vanguard model'])
    for name in [MODEL, *CLIPS]:
        if not (source / (name + '.fbx')).is_file():
            raise ValueError(f'Missing source: {name}.fbx')
    target = project / 'Assets/Owned/Vanguard'
    if skip_import:
        for name in [MODEL, *CLIPS]:
            path = target / (name + '.fbx')
            if not path.is_file() or path.read_bytes() != (source / path.name).read_bytes():
                raise ValueError(f'Source changed for {name}; run without --skip-import.')
    target.mkdir(parents=True, exist_ok=True)
    for name in [MODEL, *CLIPS]:
        path = target / (name + '.fbx')
        shutil.copy2(source / path.name, path)
        meta_path = Path(str(path) + '.keiremeta')
        meta = read(meta_path) if meta_path.exists() else dict(schemaVersion=1,
            id=identity(name), importer='Keire.Mesh', importerVersion=23,
            type='4b454952-454d-4553-4841-535345540001', dependencies=[], subAssets=[])
        meta['importSettings'] = dict(contentType='model' if name == MODEL else 'animation',
            maximumInfluences='8', rigProfile='humanoid', rigSource='embedded',
            skinningMethod='linearBlend', animationMotion='inPlaceHorizontal')
        write(meta_path, meta)
    if not skip_import:
        subprocess.run([str(tool), 'import', '--project', str(project)], check=True)
    catalog = read(project / 'Library/AssetCache/Runtime/catalog.json')
    clip_ids = {a['id'] for a in catalog['assets'] if a['type'] == '4b454952-4541-4e49-4d43-4c4950000001'}
    states = []
    for index, name in enumerate(CLIPS):
        meta = read(target / (name + '.fbx.keiremeta'))
        clips = [asset for asset in meta['subAssets'] if asset in clip_ids]
        if len(clips) != 1:
            raise ValueError(f'Expected one imported clip for {name}; inspect import before binding.')
        states.append(dict(id=identity('state/' + name), name=name, loop=name not in
            ['Jump', 'Crouch To Standing Idle'], speed=1, editorPosition=[(index % 4)*260, (index // 4)*170],
            motion=dict(type=0, clip=clips[0]), transitions=[]))
    graph_path = target / 'VanguardLocomotion.keireanimgraph'
    write(graph_path, dict(schemaVersion=2, parameters=[], layers=[dict(id=identity('base-layer'),
        name='Base Layer', avatarMask='', defaultWeight=1, mode=0,
        entryStateId=states[0]['id'], states=states)]))
    graph_id = identity('graph')
    write(Path(str(graph_path) + '.keiremeta'), dict(schemaVersion=1, id=graph_id,
        importer='Keire.AnimationGraph', importerVersion=2,
        type='4b454952-4541-4e49-4d47-524150480001', dependencies=[], subAssets=[]))
    model_id = read(target / (MODEL + '.fbx.keiremeta'))['id']
    actor['name'] = 'Vanguard model'
    actor['components'][0]['data'].update(scale=[.01]*3, rotation=[0, 1, 0, 0])
    actor['components'][1]['version'] = 4
    actor['components'][1]['data'].update(mesh=model_id, material=None)
    actor['components'][2]['data'].update(graph=graph_id,
        skeleton=subasset(model_id, 'skeleton/default'), skinnedMesh=subasset(model_id, 'skinned-mesh/default'),
        rigDefinition=subasset(model_id, 'rig/default'), applyRootMotion=False, footGrounding=False)
    write(scene_path, scene)
    print(f'Bound Vanguard and {len(states)} motion states in {scene_path}')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--asset-tool', type=Path, required=True)
    parser.add_argument('--skip-import', action='store_true', help='Bind an already imported, unchanged source set.')
    args = parser.parse_args()
    configure(args.project.resolve(), args.source.resolve(), args.asset_tool.resolve(), args.skip_import)
