import json
import os

# Fix sun.json
sun_path = 'KraftonEngine/Asset/Content/Materials/sun.json'
with open(sun_path, 'r', encoding='utf-8') as f:
    sun_data = json.load(f)

sun_data['Parameters']['HasBaseTexture'] = 1.0
sun_data['Parameters']['MaterialParam'] = [0.0, 0.0, 5.0, 0.0]

# Opaque shaders expect ShaderPath to point to something valid, or it doesn't matter for Opaque pass, 
# but let's make sure SectionColor is white
sun_data['Parameters']['SectionColor'] = [1.0, 1.0, 1.0, 1.0]

with open(sun_path, 'w', encoding='utf-8') as f:
    json.dump(sun_data, f, indent=2)

# Fix SolarSystem.Scene
scene_path = 'KraftonEngine/Asset/Content/Scene/SolarSystem.Scene'
with open(scene_path, 'r', encoding='utf-8') as f:
    scene_data = json.load(f)

# Find the sun and attach a PointLightComponent
for actor in scene_data.get('Actors', []):
    if actor.get('PrimitiveKey') == '12': # Sun
        root = actor.get('RootComponent', {})
        children = root.get('Children', [])
        
        # Check if PointLight already exists
        has_pl = any(c.get('ClassName') == 'UPointLightComponent' for c in children)
        if not has_pl:
            point_light = {
                "Children" : [],
                "ClassName" : "UPointLightComponent",
                "Properties" : {
                    "AttenuationRadius" : 20000.0,
                    "Intensity" : 500.0,
                    "LightColor" : [1.0, 1.0, 1.0, 1.0],
                    "LightFalloffExponent" : 1.0,
                    "Location" : [0.0, 0.0, 0.0],
                    "Rotation" : [0.0, 0.0, 0.0],
                    "Scale" : [1.0, 1.0, 1.0],
                    "UseInverseSquaredFalloff" : True,
                    "bAffectsWorld" : True,
                    "bCastShadows" : True,
                    "bTickEnable" : True
                }
            }
            children.append(point_light)
            root['Children'] = children
            actor['RootComponent'] = root

# Ensure no APointLightActor is duplicated
scene_data['Actors'] = [a for a in scene_data.get('Actors', []) if a.get('ClassName') != 'APointLightActor']

with open(scene_path, 'w', encoding='utf-8') as f:
    json.dump(scene_data, f, indent=2)

print('PointLight attached and sun.json updated.')
