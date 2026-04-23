import json
import os

scene_path = 'KraftonEngine/Asset/Content/Scene/SolarSystem.Scene'
with open(scene_path, 'r', encoding='utf-8') as f:
    scene_data = json.load(f)

def remove_spotlights(node):
    if isinstance(node, dict):
        if 'Children' in node and isinstance(node['Children'], list):
            node['Children'] = [c for c in node['Children'] if c.get('ClassName') != 'USpotLightComponent']
            for c in node['Children']:
                remove_spotlights(c)

for actor in scene_data.get('Actors', []):
    if 'RootComponent' in actor:
        remove_spotlights(actor['RootComponent'])
        
with open(scene_path, 'w', encoding='utf-8') as f:
    json.dump(scene_data, f, indent=2)

print('Cleaned up nested spotlights.')
