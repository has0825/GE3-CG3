bl_info = {
    "name": "GE3 Level Editor",
    "description": "GE3 Level Editor with Colliders and Disabled settings (Modular)",
    "author": "Antigravity",
    "version": (1, 1),
    "blender": (2, 80, 0),
    "location": "Properties > Object & 3D Viewport",
    "warning": "",
    "category": "Development",
}

import bpy
from . import collider
from . import disabled
from . import level_editor

classes = [
    level_editor.GE3_OT_ImportLayout, 
    level_editor.GE3_OT_ExportLayout, 
    level_editor.GE3_OT_GenerateCity, 
    level_editor.GE3_OT_TurnRouteFromSelected,
    level_editor.GE3_OT_AlignLanesToCenter,
    level_editor.GE3_OT_RemoveOverlappingBuildings,
    level_editor.GE3_OT_RemoveOverlappingFloors,
    level_editor.GE3_PT_LevelEditorPanel,
    collider.GE3_OT_AddCollider,
    collider.GE3_PT_ColliderPanel,
    disabled.GE3_OT_AddDisabled,
    disabled.GE3_PT_DisabledPanel
]

def register():
    for cls in classes:
        bpy.utils.register_class(cls)
        
    # コライダー描画ハンドラの登録
    collider.draw_handle = bpy.types.SpaceView3D.draw_handler_add(
        collider.draw_collider_callback, (), 'WINDOW', 'POST_VIEW'
    )
    
    # カスタムプロパティ ge3_floors を登録
    bpy.types.Object.ge3_floors = bpy.props.IntProperty(
        name="Floors",
        description="Number of floors for the building",
        default=1,
        min=1,
        max=100,
        update=level_editor.update_building_floors
    )
    
    # 自動生成パラメータの登録
    bpy.types.Scene.ge3_city_length = bpy.props.FloatProperty(
        name="City Length",
        description="Total length of the generated city in Z direction (game units)",
        default=3200.0,
        min=500.0,
        max=10000.0
    )
    bpy.types.Scene.ge3_building_density = bpy.props.FloatProperty(
        name="Building Density",
        description="Probability of spawning a building at each slot",
        default=0.7,
        min=0.0,
        max=1.0
    )
    bpy.types.Scene.ge3_max_floors = bpy.props.IntProperty(
        name="Max Floors",
        description="Maximum height of buildings in floors",
        default=12,
        min=1,
        max=50
    )
    bpy.types.Scene.ge3_height_noise_scale = bpy.props.FloatProperty(
        name="Terrain Noise Scale",
        description="Scale of the terrain height variations (linked to size)",
        default=400.0,
        min=50.0,
        max=2000.0
    )
    bpy.types.Scene.ge3_height_noise_strength = bpy.props.FloatProperty(
        name="Terrain Noise Strength",
        description="Multiplier for the terrain height",
        default=1.2,
        min=0.0,
        max=5.0
    )
    bpy.types.Scene.ge3_curve_strength = bpy.props.FloatProperty(
        name="Curve Strength",
        description="Horizontal road curving strength",
        default=30.0,
        min=0.0,
        max=100.0
    )
    bpy.types.Scene.ge3_slope_strength = bpy.props.FloatProperty(
        name="Slope Strength",
        description="Vertical road slope strength",
        default=8.0,
        min=0.0,
        max=30.0
    )
    bpy.types.Scene.ge3_enemy_spawn_rate = bpy.props.FloatProperty(
        name="Enemy Spawn Rate",
        description="Probability of spawning an enemy near buildings",
        default=0.05,
        min=0.0,
        max=0.5
    )
    
    # 区画ベース自動生成用のパラメータ
    bpy.types.Scene.ge3_num_sections = bpy.props.IntProperty(
        name="Num Sections",
        description="Number of sections in the course",
        default=40,
        min=5,
        max=100
    )
    bpy.types.Scene.ge3_section_length = bpy.props.FloatProperty(
        name="Section Length",
        description="Length of each section (game units)",
        default=160.0,
        min=50.0,
        max=500.0
    )
    bpy.types.Scene.ge3_section_behaviors = bpy.props.StringProperty(
        name="Section Behaviors",
        description="Behavior for each section (S: Straight, R: Right, L: Left, U: Up, D: Down). E.g. S,S,S,R,S,S,L,S,U,S",
        default="S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S,S"
    )
    bpy.types.Scene.ge3_curve_angle = bpy.props.FloatProperty(
        name="Curve Angle",
        description="Turn angle in degrees for R/L sections",
        default=22.5,
        min=0.0,
        max=90.0
    )
    bpy.types.Scene.ge3_slope_height = bpy.props.FloatProperty(
        name="Slope Height",
        description="Height offset in game units for U/D sections",
        default=15.0,
        min=0.0,
        max=50.0
    )
    bpy.types.Scene.ge3_manual_turn_angle = bpy.props.FloatProperty(
        name="Manual Turn Angle",
        description="Angle in degrees to turn the route from selected object",
        default=15.0,
        min=-90.0,
        max=90.0
    )
    
    print("[Level Editor] Registered package!")


def unregister():
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)
        
    if collider.draw_handle is not None:
        bpy.types.SpaceView3D.draw_handler_remove(collider.draw_handle, 'WINDOW')
        collider.draw_handle = None
        
    # プロパティの削除
    if hasattr(bpy.types.Object, "ge3_floors"):
        del bpy.types.Object.ge3_floors
        
    for prop in ["ge3_city_length", "ge3_building_density", "ge3_max_floors", 
                 "ge3_height_noise_scale", "ge3_height_noise_strength", 
                 "ge3_curve_strength", "ge3_slope_strength", "ge3_enemy_spawn_rate",
                 "ge3_num_sections", "ge3_section_length", "ge3_section_behaviors",
                 "ge3_curve_angle", "ge3_slope_height", "ge3_manual_turn_angle"]:
        if hasattr(bpy.types.Scene, prop):
            delattr(bpy.types.Scene, prop)
            
    print("[Level Editor] Unregistered package!")


# スクリプト直接実行時
if __name__ == "__main__":
    register()
