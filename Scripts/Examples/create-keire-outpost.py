"""Build the private outpost project from user-owned POLYGON War sources.

Only project authoring is generated here. Third-party source files stay in the
explicit destination and are never added to the engine's distributable samples.
"""

import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import shutil
import uuid


ROOT = Path(__file__).resolve().parents[2]
NS = uuid.UUID("1ec83b90-285c-4a49-94e6-5cc361050ab7")
TRANSFORM = "4b454952-4554-5241-4e53-464f524d0001"
MESH = "4b454952-454d-4553-4852-454e44455201"
CUBE = "4b454952-4543-5542-454d-455348000001"
TOUR = "96312b07-df7f-46e4-9c38-a5e3b5f1e247"


def identity(name):
    return str(uuid.uuid5(NS, name))


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def metadata(path, importer, type_id, version=1):
    asset_id = identity(path.name)
    write(Path(str(path) + ".keiremeta"), dict(schemaVersion=1, id=asset_id,
          importer=importer, importerVersion=version, type=type_id, dependencies=[], subAssets=[]))
    return asset_id


def track_material(destination):
    """Give the vehicle's second slot its supplied tread texture, independently of the body atlas."""
    materials = destination / 'Assets/Owned/SyntyWar/Materials'
    graph = read(materials / 'MG_GermanArmor.keirematerialgraph')
    track = read(destination / 'Assets/Owned/Polygon/Textures/Tank_Track_Texture.png.keiremeta')['id']
    body = next(p['value'] for p in graph['properties'] if p['name'] == 'BaseTexture')
    def replace(value):
        if isinstance(value, dict):
            return {k: replace(v) for k, v in value.items()}
        if isinstance(value, list):
            return [replace(v) for v in value]
        return track if value == body else value
    graph = replace(graph)
    for node in graph['surfaceGraph']['nodes']:
        if node.get('symbol') == 'MG_Roughness':
            node['value'] = .82
    for prop in graph['properties']:
        if prop['name'] == 'Roughness':
            prop['value'] = .82
    for node in graph['nodes']:
        if node.get('name', '').endswith('Roughness'):
            node['value'] = .82
    path = materials / 'MG_OutpostTracks.keirematerialgraph'
    write(path, graph)
    asset_id = metadata(path, 'Keire.MaterialGraph', '4b454952-454d-4752-4150-480000000001', 11)
    # Match AssetDatabase's stable generated-subasset identity contract.
    digest = hashlib.sha256((asset_id + '\nmaterial/default').encode()).digest()[:16]
    return str(uuid.UUID(bytes=digest, version=5))


def quaternion(pitch=0, yaw=0):
    p, y = math.radians(pitch) / 2, math.radians(yaw) / 2
    return [math.sin(p)*math.cos(y), math.cos(p)*math.sin(y),
            -math.sin(p)*math.sin(y), math.cos(p)*math.cos(y)]


def entity(name, position, components=(), scale=(1, 1, 1), yaw=0):
    return dict(id=identity(name), name=name, active=True, parent=None, layer=0, tags=[],
                components=[dict(type=TRANSFORM, version=1, enabled=True,
                                 data=dict(position=list(position), rotation=quaternion(yaw=yaw), scale=list(scale))),
                            *copy.deepcopy(components)])


def renderer(mesh, material=None, tint=(1, 1, 1, 1)):
    return dict(type=MESH, version=4, enabled=True, data=dict(mesh=mesh, material=material,
                tint=list(tint), visible=True, alwaysVisible=False, castShadows=True,
                receiveShadows=True, staticLighting=False, giReceive=0, lightmapScale=1,
                preserveLightmapUvs=True))


def generate(destination, asset_source, gallery_source):
    destination = destination.resolve()
    if destination.exists():
        raise ValueError(f"Destination already exists; refusing to overwrite {destination}")
    sample = ROOT / "Samples/KeireSandbox"
    required = [asset_source / "FBX/SM_Bld_Guard_Tower_01.fbx", gallery_source / "Assets/Polygon"]
    for path in required:
        if not path.exists():
            raise ValueError(f"Required owned asset source is missing: {path}")
    shutil.copytree(sample / "Assets", destination / "Assets")
    shutil.copytree(sample / "ProjectSettings", destination / "ProjectSettings")
    shutil.copytree(gallery_source / "Assets/Polygon", destination / "Assets/Owned/Polygon")
    shutil.copytree(gallery_source / "Assets/Examples/FeatureGallery/SyntyWar",
                    destination / "Assets/Owned/SyntyWar", ignore=shutil.ignore_patterns('Scripts'))
    # Keep examples in the existing runtime assembly; their asset IDs are unchanged.
    scripts = destination / "Assets/Scripts/Runtime/Outpost"
    shutil.copytree(gallery_source / "Assets/Examples/FeatureGallery/SyntyWar/Scripts", scripts)
    tour_path = scripts / 'OutpostTour.cs'
    shutil.copy2(ROOT / 'Scripts/Examples/Outpost/OutpostTour.cs', tour_path)
    metadata(tour_path, 'Keire.Text', '4b454952-4554-4558-5441-535345540001')
    scene = read(sample / "Assets/Scenes/SandboxShowcase.keirescene")
    scene["name"] = "Kéire Outpost"
    scene["entities"] = []
    source_scene = read(sample / "Assets/Scenes/SampleScene.keirescene")
    key = copy.deepcopy(next(e for e in source_scene["entities"] if e["name"] == "Directional Light"))
    key["name"] = "Dusk sunlight"
    key["components"][0]["data"]["rotation"] = quaternion(32, -38)
    key["components"][1]["data"].update(color=[1, .86, .68, 1], intensity=3.8, shadowBias=.07,
                                      shadows=2, shadowResolution=3)
    scene["entities"].append(key)
    camera = copy.deepcopy(next(e for e in source_scene["entities"] if e["name"] == "Main Camera"))
    camera.update(id=identity("camera"), name="Outpost tour camera", parent=None)
    camera["components"] = camera["components"][:3]
    camera["components"][0]["data"].update(position=[25, 15, -32], rotation=quaternion(17, -34))
    # A centimetre near plane loses depth precision on the closely layered building trims.
    camera["components"][1]["data"].update(fieldOfView=52, nearPlane=.3, farPlane=180, priority=100)
    camera["components"].append(dict(type=TOUR, version=1, enabled=True, data={}))
    scene["entities"].append(camera)
    assets = destination / "Assets/Owned/Polygon"
    palette = read(assets / "Textures/PolygonWar_Texture_01_A.png.keiremeta")["id"]
    material_root = destination / "Assets/Owned/SyntyWar/Materials"
    # The graph is authoring data. Mesh renderers bind its generated runtime material.
    material = read(material_root / "MG_InfantryAtlas.keirematerialgraph.keiremeta")["subAssets"][-1]
    materials = {f.stem: read(Path(str(f)+'.keiremeta'))['subAssets'][-1]
                 for f in material_root.glob('*.keirematerialgraph')}
    models = {}
    for name in ["SM_Bld_Barracks_01", "SM_Env_Sandbag_Wall_03", "SM_Veh_German_Tank_01",
                 "SM_Veh_Plane_American_01", "SM_Bld_Guard_Tower_01", "SM_Bld_Bunker_01",
                 "SM_Env_Tree_01", "SM_Env_Tree_02", "SM_Env_Rocks_Small_01"]:
        path = assets / "FBX" / (name + ".fbx")
        if not path.exists():
            shutil.copy2(asset_source / "FBX" / path.name, path)
            metadata(path, "Keire.Mesh", "4b454952-454d-4553-4841-535345540001", 23)
        models[name] = read(Path(str(path)+'.keiremeta'))['id']
    def add_model(label, name, pos, yaw=0, scale=1, mat=material):
        scene['entities'].append(entity(label, pos, [renderer(models[name], mat)], (scale,)*3, yaw))
    ground = entity("Outpost foundation", [0, -.3, 5], [renderer(CUBE, tint=(.24,.30,.20,1))], (180,.5,180))
    collider = copy.deepcopy(next(e for e in source_scene['entities'] if e['name']=='Ground')['components'][2])
    ground['components'].append(collider)
    scene['entities'].append(ground)
    scene['entities'].append(entity('Arrival road', [0, -.02, 3], [renderer(CUBE, tint=(.32,.34,.32,1))], (8,.12,47)))
    add_model('West barracks', 'SM_Bld_Barracks_01', [-12,0,7], 90)
    add_model('East workshop', 'SM_Bld_Barracks_01', [12,0,10], -90)
    add_model('Signal tower', 'SM_Bld_Guard_Tower_01', [-10,0,23], 180)
    add_model('Operations bunker', 'SM_Bld_Bunker_01', [7,0,26], 180)
    # These two supplied vehicle exports use centimetres; the modular environment uses metres.
    add_model('Vehicle bay', 'SM_Veh_German_Tank_01', [11,0,-6], -32, .01, mat=materials['MG_GermanArmor'])
    scene['entities'][-1]['components'][1]['data']['material.1'] = track_material(destination)
    add_model('Airfield exhibit', 'SM_Veh_Plane_American_01', [-15,2.3,-8], 25, .01, mat=materials['MG_AmericanWarbird'])
    for x in (-18,-11,11,18):
        add_model(f'Entry sandbags {x}', 'SM_Env_Sandbag_Wall_03', [x,0,-17])
    for i in range(18):
        angle = i * math.tau / 18
        add_model(f'Perimeter tree {i:02}', 'SM_Env_Tree_01' if i%2 else 'SM_Env_Tree_02',
                  [math.cos(angle)*29,0,7+math.sin(angle)*32], i*47, .7+i%3*.12)
    for i in range(40):
        angle = i * math.tau / 40
        radius = 41 + i % 4 * 4
        add_model(f'Forest tree {i:02}', 'SM_Env_Tree_01' if i % 2 else 'SM_Env_Tree_02',
                  [math.cos(angle)*radius,0,7+math.sin(angle)*radius], i*67, .8+i%5*.15)
    for i in range(9):
        add_model(f'Rock formation {i:02}', 'SM_Env_Rocks_Small_01', [-24+i*6,0,33], i*31, 1.8)
    # Use the canonical paired material graphs as tangible samples beside the workshop.
    gallery = read(sample/'Assets/Scenes/SandboxShowcase.keirescene')
    sculptures = [e for e in gallery['entities'] if any(c['type']==MESH for c in e['components']) and 'Plinth' not in e['name']]
    for i, sculpture in enumerate(sculptures[:4]):
        sculpture = copy.deepcopy(sculpture)
        sculpture.update(id=identity(f'material-{i}'), parent=None)
        sculpture['components'][0]['data'].update(position=[5+i*3,1.4,4],scale=[.85,.85,.85])
        scene['entities'].append(sculpture)
        scene['entities'].append(entity(f'Material pedestal {i}',[5+i*3,.4,4],
                               [renderer(CUBE,tint=(.12,.16,.19,1))],(1.8,.8,1.8)))
    effect_positions = {'Sparks': [8,1,8], 'Mist': [-4,.2,18], 'SigilOrbit': [-10,8,23]}
    effects = [e for e in source_scene['entities'] if e['name'] in effect_positions]
    for e in effects:
        pos = effect_positions[e['name']]
        e=copy.deepcopy(e);e['parent']=None;e['components'][0]['data']['position']=pos
        scene['entities'].append(e)
    explorer = entity('Outpost explorer', [0,0,-10])
    actor = copy.deepcopy(next(e for e in source_scene['entities'] if e['name'] == 'T-Pose'))
    actor.update(id=identity('explorer-model'), name='Explorer model', parent=explorer['id'])
    actor['components'] = actor['components'][:3]
    actor['components'][0]['data'].update(position=[0,0,0], rotation=quaternion(yaw=180), scale=[.022]*3)
    actor['components'][2]['data'].update(footGrounding=False)
    scene['entities'].extend([explorer, actor])
    scene['entities'].append(entity('Signal gate', [0,1.8,19],
                            [renderer(CUBE,tint=(.8,.39,.10,1))], (6,.18,.24)))
    for x in (-3.2,3.2):
        scene['entities'].append(entity(f'Gate post {x}', [x,2.5,19],
                                [renderer(CUBE,tint=(.12,.17,.19,1))], (.25,5,.25)))
    scene_path = destination/'Assets/Scenes/KeireOutpost.keirescene'
    write(scene_path,scene)
    scene_id=metadata(scene_path,'Keire.Scene','4b454952-4553-4345-4e45-415353455401',8)
    project=read(destination/'ProjectSettings/Project.keireproject')
    project.update(id=identity('project'),name='KeireOutpost',startupScene=scene_id,lastSavedWithEngineVersion='0.4.4')
    write(destination/'ProjectSettings/Project.keireproject',project)
    player=read(destination/'ProjectSettings/Player.keiresettings')
    player.update(productName='KeireOutpost', windowTitle='Kéire Outpost', version='1.0.0',
                  applicationIdentifier='com.keire.showcase.outpost')
    write(destination/'ProjectSettings/Player.keiresettings',player)
    write(destination/'ProjectSettings/BuildScenes.keiresettings',dict(schemaVersion=1,scenes=[dict(scene=scene_id,enabled=True)]))
    rendering=read(destination/'ProjectSettings/Rendering.keiresettings')
    rendering.update(schemaVersion=5,renderPath='forwardPlus',globalIllumination='realtime',
                     irradynQuality='balanced',antiAliasing='msaa4',dynamicResolution='disabled',renderScale=1,
                     minimumDynamicResolutionScale=.67,maximumDynamicResolutionScale=1,
                     dynamicResolutionTargetMilliseconds=16.667,
                     ambientColor=[.46,.59,.78,1],ambientIntensity=.9,environmentDiffuseIntensity=.8,
                     environmentSpecularIntensity=.8,exposure=1.1,directionalShadowDistance=90)
    write(destination/'ProjectSettings/Rendering.keiresettings',rendering)
    (destination/'README.md').write_text('# Kéire Outpost\n\nPrivate interactive website showcase.\n\n'
        'Open Assets/Scenes/KeireOutpost.keirescene. Play starts the camera tour.\n'
        'Space pauses the camera; R restarts; Tab switches to third-person exploration.\n'
        'WASD moves and arrow keys turn. E opens the signal gate when nearby.\n'
        'C switches to capture flight; WASD and Q/E move, and arrow keys turn.\n'
        'F1–F6 frame individual showcase stations; F7 previews character idle animation.\n'
        'F8 compares the scene with and without shadows.\n'
        'Third-party files under Assets/Owned are not distributable engine samples.\n',encoding='utf-8')
    (destination/'ASSET_PROVENANCE.md').write_text('# Asset provenance\n\n'
        f'POLYGON War: user-owned source files at {asset_source}.\n'
        f'Previously imported source: {gallery_source}/Assets/Polygon.\n'
        'Engine sample content: Samples/KeireSandbox.\n'
        'Owned assets retain their original licence. Publish only rendered scene media, not source packs.\n'
        'Reference terms: https://unity.com/legal/as-terms\n',encoding='utf-8')
    print(f'Created {destination}: {len(scene["entities"])} scene entities; palette {palette}')


if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--destination',type=Path,required=True)
    parser.add_argument('--assets',type=Path,required=True)
    parser.add_argument('--gallery',type=Path,required=True)
    args=parser.parse_args()
    generate(args.destination,args.assets,args.gallery)
