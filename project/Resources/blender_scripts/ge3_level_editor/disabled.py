import bpy

class GE3_OT_AddDisabled(bpy.types.Operator):
    """オブジェクトに無効フラグを追加する"""
    bl_idname = "ge3.add_disabled"
    bl_label = "Add Disabled Option"
    bl_description = "Add disabled custom property to the active object"
    bl_options = {"REGISTER", "UNDO"}
    
    def execute(self, context):
        obj = context.active_object
        if obj:
            obj["disabled"] = False
            for area in context.screen.areas:
                if area.type == 'VIEW_3D':
                    area.tag_redraw()
        return {"FINISHED"}


class GE3_PT_DisabledPanel(bpy.types.Panel):
    """無効オプション設定パネル"""
    bl_label = "GE3 Disabled"
    bl_idname = "GE3_PT_DisabledPanel"
    bl_space_type = "PROPERTIES"
    bl_region_type = "WINDOW"
    bl_context = "object"
    
    def draw(self, context):
        layout = self.layout
        obj = context.object
        if not obj:
            return
            
        if "disabled" in obj:
            layout.prop(obj, '["disabled"]', text="Disabled")
        else:
            layout.operator("ge3.add_disabled", text="Add Disabled", icon="ADD")
