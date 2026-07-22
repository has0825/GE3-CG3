import bpy
import mathutils
import gpu
from gpu_extras.batch import batch_for_shader

# コライダー描画ハンドラ追跡用
draw_handle = None

def draw_collider_callback():
    try:
        shader = gpu.shader.from_builtin('3D_UNIFORM_COLOR')
    except Exception:
        return
        
    shader.bind()
    shader.uniform_float("color", (0.0, 1.0, 1.0, 1.0)) # シアン色
    
    offsets = [
        mathutils.Vector((-1, -1, -1)),
        mathutils.Vector(( 1, -1, -1)),
        mathutils.Vector(( 1,  1, -1)),
        mathutils.Vector((-1,  1, -1)),
        mathutils.Vector((-1, -1,  1)),
        mathutils.Vector(( 1, -1,  1)),
        mathutils.Vector(( 1,  1,  1)),
        mathutils.Vector((-1,  1,  1))
    ]
    edges = [
        (0, 1), (1, 2), (2, 3), (3, 0),
        (4, 5), (5, 6), (6, 7), (7, 4),
        (0, 4), (1, 5), (2, 6), (3, 7)
    ]
    
    coords = []
    for obj in bpy.context.scene.objects:
        if "collider" not in obj:
            continue
            
        center = mathutils.Vector(obj.get("collider_center", (0.0, 0.0, 0.0)))
        size = mathutils.Vector(obj.get("collider_size", (2.0, 2.0, 2.0)))
        
        obj_coords = []
        for offset in offsets:
            pos = mathutils.Vector(center)
            pos[0] += offset[0] * size[0]
            pos[1] += offset[1] * size[1]
            pos[2] += offset[2] * size[2]
            
            pos_world = obj.matrix_world @ pos
            obj_coords.append(pos_world)
            
        for edge in edges:
            coords.append(obj_coords[edge[0]])
            coords.append(obj_coords[edge[1]])
            
    if coords:
        gpu.state.line_width_set(2.0)
        batch = batch_for_shader(shader, 'LINES', {"pos": coords})
        batch.draw(shader)


class GE3_OT_AddCollider(bpy.types.Operator):
    """オブジェクトにBoxコライダーを追加する"""
    bl_idname = "ge3.add_collider"
    bl_label = "Add Collider"
    bl_description = "Add Box collider custom properties to the active object"
    bl_options = {"REGISTER", "UNDO"}
    
    def execute(self, context):
        obj = context.active_object
        if obj:
            obj["collider"] = "BOX"
            obj["collider_center"] = mathutils.Vector((0.0, 0.0, 0.0))
            obj["collider_size"] = mathutils.Vector((2.0, 2.0, 2.0))
            # ビューポートを再描画
            for area in context.screen.areas:
                if area.type == 'VIEW_3D':
                    area.tag_redraw()
        return {"FINISHED"}


class GE3_PT_ColliderPanel(bpy.types.Panel):
    """コライダー設定パネル"""
    bl_label = "GE3 Collider"
    bl_idname = "GE3_PT_ColliderPanel"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "object"
    
    def draw(self, context):
        layout = self.layout
        obj = context.object
        if not obj:
            return
            
        if "collider" in obj:
            layout.prop(obj, '["collider"]', text="Type")
            layout.prop(obj, '["collider_center"]', text="Center")
            layout.prop(obj, '["collider_size"]', text="Size")
        else:
            layout.operator("ge3.add_collider", text="Add Collider", icon="ADD")
