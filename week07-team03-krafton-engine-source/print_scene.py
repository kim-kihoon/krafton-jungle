import json
scene_path = 'KraftonEngine/Asset/Content/Scene/SolarSystem.Scene'
with open(scene_path, 'r', encoding='utf-8') as f:
    data = json.load(f)

def print_tree(node, depth=0, parent_rot=[0,0,0], parent_name=""):
    cls = node.get('ClassName', 'Unknown')
    props = node.get('Properties', {})
    loc = props.get('Location', [0,0,0])
    rot = props.get('Rotation', [0,0,0])
    s = f"{'  ' * depth}- {cls} (Loc: {loc}, Rot: {rot})"
    print(s)
    children = node.get('Children', [])
    for idx, c in enumerate(children):
        print_tree(c, depth + 1, rot, f"{parent_name}/{idx}")

for i, actor in enumerate(data.get('Actors', [])):
    print(f"Actor {i}: {actor.get('ClassName')}")
    if 'NonSceneComponents' in actor:
        for nsc in actor['NonSceneComponents']:
            props = nsc.get('Properties', {})
            rot_rate = props.get('Rotation Rate', [0,0,0])
            update_comp = props.get('Updated Component', 'Unknown')
            print(f"  [Rotator] Update: {update_comp}, Rate: {rot_rate}")
    if 'RootComponent' in actor:
        print_tree(actor['RootComponent'], 1, [0,0,0], "Root")
