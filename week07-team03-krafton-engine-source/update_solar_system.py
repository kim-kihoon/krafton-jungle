import json
import os

base_dir = 'KraftonEngine/Asset/Content/Materials/SolarSystem/textures'
sun_path = 'KraftonEngine/Asset/Content/Materials/sun.json'

def update_material(path, is_sun):
    if not os.path.exists(path):
        print(f'File not found: {path}')
        return
    
    with open(path, 'r', encoding='utf-8') as f:
        data = json.load(f)
        
    if 'Parameters' not in data:
        data['Parameters'] = {}
        
    if 'MaterialParam' not in data['Parameters'] or not data['Parameters']['MaterialParam']:
        data['Parameters']['MaterialParam'] = [32.0, 0.2, 0.0, 0.0]
        
    if len(data['Parameters']['MaterialParam']) < 4:
        while len(data['Parameters']['MaterialParam']) < 4:
            data['Parameters']['MaterialParam'].append(0.0)

    if is_sun:
        data['Parameters']['MaterialParam'][0] = 0.0
        data['Parameters']['MaterialParam'][1] = 0.0
        data['Parameters']['MaterialParam'][2] = 5.0
        data['Parameters']['HasNormalTexture'] = 0.0
    else:
        data['Parameters']['MaterialParam'][0] = 32.0
        data['Parameters']['MaterialParam'][1] = 0.2
        data['Parameters']['MaterialParam'][2] = 0.0
        
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=2)

update_material(sun_path, True)

planets = [
    'mercury.json', 'venus.json', 'earth.json', 'moon.json',
    'mars.json', 'jupiter.json', 'saturn.json', 'saturn_ring.json',
    'uranus.json', 'neptune.json'
]

for p in planets:
    update_material(os.path.join(base_dir, p), False)

# Update Scene
scene_path = 'KraftonEngine/Asset/Content/Scene/SolarSystem.Scene'
with open(scene_path, 'r', encoding='utf-8') as f:
    scene_data = json.load(f)

# Remove SpotLights and update PointLight
for actor in scene_data.get('Actors', []):
    if 'RootComponent' in actor and 'Children' in actor['RootComponent']:
        children = actor['RootComponent']['Children']
        # Remove SpotLightComponent
        actor['RootComponent']['Children'] = [c for c in children if c.get('ClassName') != 'USpotLightComponent']
        
    # Update PointLight
    if actor.get('ClassName') == 'APointLightActor':
        if 'RootComponent' in actor and actor['RootComponent'].get('ClassName') == 'UPointLightComponent':
            props = actor['RootComponent'].get('Properties', {})
            props['Intensity'] = 500.0
            props['AttenuationRadius'] = 15000.0

with open(scene_path, 'w', encoding='utf-8') as f:
    json.dump(scene_data, f, indent=2)

print('Updated materials and scene.')
