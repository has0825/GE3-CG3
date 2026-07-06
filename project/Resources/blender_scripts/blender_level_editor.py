"""
blender_level_editor.py
=======================
Blender レベルエディタ (双方向)

【機能】
- scene_layout.txt を読み込んでBlenderに建物・床・初期敵配置を3D表示
- Blender上でオブジェクトを自由に移動・追加・削除してレベルをデザイン
- 「ゲームへ送信」ボタンで変更内容をscene_layout.txtに書き出し
  → ゲームが次の起動で自動的に新しい配置を読み込む

【使い方】
1. このスクリプトを実行
2. Nパネル(Nキー)の「GE3 Level Editor」タブを開く
3. 「Import from Game」でゲームの配置を読み込む
4. Blenderで自由に建物を移動
5. 「Export to Game」でscene_layout.txtに書き出し
6. ゲームを再起動すると反映される！
"""

import bpy
import os
from bpy.types import Panel, Operator

# ──────────────────────────────────────────
# 設定
# ──────────────────────────────────────────
LAYOUT_PATH = r"C:\Users\k024g\Desktop\GE3&CG3\project\Resources\scene_layout.txt"
SCALE = 0.1  # ゲーム座標 → Blender座標のスケール
# ──────────────────────────────────────────

BUILDING_COLOR = (0.4, 0.5, 0.6, 1.0)
FLOOR_COLOR    = (0.3, 0.8, 0.4, 1.0)
PLAYER_COLOR   = (0.2, 0.5, 1.0, 1.0)
ENEMY_COLOR    = (1.0, 0.4, 0.2, 1.0)

COLLECTION_NAME = "GE3_LevelEditor"


def get_or_create_collection():
    """専用コレクションを取得・作成"""
    if COLLECTION_NAME not in bpy.data.collections:
        col = bpy.data.collections.new(COLLECTION_NAME)
        bpy.context.scene.collection.children.link(col)
    return bpy.data.collections[COLLECTION_NAME]


def add_to_collection(obj):
    col = get_or_create_collection()
    if obj.name not in col.objects:
        col.objects.link(obj)
    # メインシーンから除外（重複防止）
    if obj.name in bpy.context.scene.collection.objects:
        bpy.context.scene.collection.objects.unlink(obj)


def make_material(name, color):
    mat = bpy.data.materials.get(name)
    if mat is None:
        mat = bpy.data.materials.new(name)
        mat.use_nodes = True
        bsdf = mat.node_tree.nodes.get("Principled BSDF")
        if bsdf:
            bsdf.inputs["Base Color"].default_value = color
    return mat


def update_building_floors(self, context):
    """建物の階数が変更されたときにスケールと位置を自動更新するコールバック"""
    if self.get("ge3_type") != "BUILDING":
        return
    
    floors = self.ge3_floors
    if floors < 1:
        self.ge3_floors = 1
        return
        
    # ゲーム座標におけるYスケール（高さ）
    sy = 10.0 * floors
    # ゲーム座標における中心Y座標
    gy = -20.0 + sy * 0.5
    
    # Blender座標に反映
    self.scale.z = sy * SCALE
    self.location.z = gy * SCALE
    
    # データ整合性のためにカスタムプロパティも更新
    self["ge3_sy"] = sy


def create_building_obj(name, gx, gz, floors, sx=10.0, sz=10.0):
    """ビルオブジェクトをBlenderに作成"""
    # 初期位置は Z=0 付近にしておき、ge3_floors を設定することで自動的にスケールと位置が適用される
    bpy.ops.mesh.primitive_cube_add(size=1, location=(gx*SCALE, gz*SCALE, 0.0))
    obj = bpy.context.active_object
    obj.name = name
    
    # X, Y スケール（ゲーム座標の X, Z）を設定
    obj.scale.x = sx * SCALE
    obj.scale.y = sz * SCALE
    
    mat = make_material("Mat_Building", BUILDING_COLOR)
    if obj.data.materials:
        obj.data.materials[0] = mat
    else:
        obj.data.materials.append(mat)
        
    obj["ge3_type"] = "BUILDING"
    # ge3_sx, ge3_sz も設定しておく
    obj["ge3_sx"]   = sx
    obj["ge3_sz"]   = sz
    
    # 登録プロパティを代入してコールバックをトリガーする
    obj.ge3_floors = floors
    
    add_to_collection(obj)
    return obj


def create_floor_obj(name, gx, gy, gz):
    """床オブジェクトをBlenderに作成"""
    bpy.ops.mesh.primitive_plane_add(size=20*SCALE, location=(gx*SCALE, gz*SCALE, gy*SCALE))
    obj = bpy.context.active_object
    obj.name = name
    mat = make_material("Mat_Floor", FLOOR_COLOR)
    if obj.data.materials:
        obj.data.materials[0] = mat
    else:
        obj.data.materials.append(mat)
    obj["ge3_type"] = "FLOOR"
    add_to_collection(obj)
    return obj


def create_player_obj(name, gx, gy, gz):
    """プレイヤー初期位置マーカー"""
    bpy.ops.mesh.primitive_cone_add(radius1=1.5*SCALE, depth=3*SCALE,
                                    location=(gx*SCALE, gz*SCALE, gy*SCALE))
    obj = bpy.context.active_object
    obj.name = name
    mat = make_material("Mat_Player", PLAYER_COLOR)
    if obj.data.materials:
        obj.data.materials[0] = mat
    else:
        obj.data.materials.append(mat)
    obj["ge3_type"] = "PLAYER"
    add_to_collection(obj)
    return obj


def create_enemy_obj(name, gx, gy, gz):
    """敵初期位置マーカー"""
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.8*SCALE,
                                         location=(gx*SCALE, gz*SCALE, gy*SCALE))
    obj = bpy.context.active_object
    obj.name = name
    mat = make_material("Mat_Enemy", ENEMY_COLOR)
    if obj.data.materials:
        obj.data.materials[0] = mat
    else:
        obj.data.materials.append(mat)
    obj["ge3_type"] = "ENEMY"
    add_to_collection(obj)
    return obj


class GE3_OT_ImportLayout(Operator):
    """scene_layout.txt を読んでBlenderにオブジェクトを配置する"""
    bl_idname  = "ge3.import_layout"
    bl_label   = "Import from Game"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        if not os.path.exists(LAYOUT_PATH):
            self.report({"ERROR"}, f"File not found: {LAYOUT_PATH}")
            return {"CANCELLED"}

        # 既存のレベルエディタオブジェクトを削除
        col = get_or_create_collection()
        for obj in list(col.objects):
            bpy.data.objects.remove(obj, do_unlink=True)

        buildings_raw = []
        floor_count    = 0
        enemy_count    = 0

        with open(LAYOUT_PATH, "r") as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                parts = line.split(",")
                obj_type = parts[0]

                if obj_type == "BUILDING" and len(parts) >= 10:
                    gx, gy, gz = float(parts[1]), float(parts[2]), float(parts[3])
                    sx, sy, sz = float(parts[4]), float(parts[5]), float(parts[6])
                    buildings_raw.append({
                        "gx": gx, "gy": gy, "gz": gz,
                        "sx": sx, "sy": sy, "sz": sz
                    })

                elif obj_type == "FLOOR" and len(parts) >= 7:
                    gx, gy, gz = float(parts[1]), float(parts[2]), float(parts[3])
                    name = f"GE3_Floor_{floor_count:03d}"
                    create_floor_obj(name, gx, gy, gz)
                    floor_count += 1

                elif obj_type == "PLAYER" and len(parts) >= 4:
                    gx, gy, gz = float(parts[1]), float(parts[2]), float(parts[3])
                    create_player_obj("GE3_Player_Start", gx, gy, gz)

                elif obj_type == "ENEMY" and len(parts) >= 4:
                    gx, gy, gz = float(parts[1]), float(parts[2]), float(parts[3])
                    name = f"GE3_Enemy_{enemy_count:02d}"
                    create_enemy_obj(name, gx, gy, gz)
                    enemy_count += 1

        # ビルのグループ化処理 (X, Z 座標が近いものを同一ビルとして扱う)
        building_groups = {}
        for b in buildings_raw:
            # 浮動小数点の誤差を考慮して丸める
            key = (round(b["gx"], 1), round(b["gz"], 1))
            if key not in building_groups:
                building_groups[key] = {
                    "gx": b["gx"],
                    "gz": b["gz"],
                    "sx": b["sx"],
                    "sz": b["sz"],
                    "gys": []
                }
            building_groups[key]["gys"].append(b["gy"])

        building_count = 0
        for key, group in building_groups.items():
            floors = len(group["gys"])
            name = f"GE3_Building_{building_count:03d}"
            create_building_obj(name, group["gx"], group["gz"], floors, group["sx"], group["sz"])
            building_count += 1

        msg = (f"Imported: {building_count} buildings (grouped), "
               f"{floor_count} floors, {enemy_count} enemies")
        self.report({"INFO"}, msg)
        print(f"[Level Editor] {msg}")
        return {"FINISHED"}


class GE3_OT_ExportLayout(Operator):
    """Blenderのオブジェクト配置をscene_layout.txtに書き出す"""
    bl_idname  = "ge3.export_layout"
    bl_label   = "Export to Game"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        col = get_or_create_collection()
        lines = []

        for obj in col.objects:
            ge3_type = obj.get("ge3_type", None)
            if ge3_type is None:
                continue

            # Blender座標 → ゲーム座標逆変換
            # Blender: (X, Y, Z) = (ゲームX, ゲームZ, ゲームY)
            gx = obj.location.x / SCALE
            gz = obj.location.y / SCALE
            gy = obj.location.z / SCALE

            if ge3_type == "BUILDING":
                floors = obj.ge3_floors if hasattr(obj, "ge3_floors") else obj.get("ge3_floors", 1)
                # ユーザーがBlender上で直接スケール変更した場合も考慮して逆算
                sx = obj.scale.x / SCALE
                sz = obj.scale.y / SCALE
                
                # floors階分出力する。Y座標は基準面 -20.0f から 10.0刻み
                for f in range(floors):
                    layer_gy = -20.0 + f * 10.0 + 5.0
                    lines.append(f"BUILDING,{gx:.2f},{layer_gy:.2f},{gz:.2f},"
                                 f"{sx:.2f},10.00,{sz:.2f},0.0,0.0,0.0")

            elif ge3_type == "FLOOR":
                lines.append(f"FLOOR,{gx:.2f},{gy:.2f},{gz:.2f},"
                             f"300.0,1.0,200.0")

            elif ge3_type == "PLAYER":
                lines.append(f"PLAYER,{gx:.2f},{gy:.2f},{gz:.2f},"
                             f"10.0,10.0,10.0,0.0,0.0,0.0")

            elif ge3_type == "ENEMY":
                lines.append(f"ENEMY,{gx:.2f},{gy:.2f},{gz:.2f},"
                             f"3.8,3.8,3.8,0.0,0.0,0.0")

        with open(LAYOUT_PATH, "w") as f:
            f.write("\n".join(lines) + "\n")

        msg = f"Exported {len(lines)} objects to {LAYOUT_PATH}"
        self.report({"INFO"}, msg)
        print(f"[Level Editor] {msg}")
        print("[Level Editor] Restart the game to apply changes!")
        return {"FINISHED"}


class GE3_PT_LevelEditorPanel(Panel):
    """サイドパネル (N-Panel) の「GE3 Level Editor」タブ"""
    bl_label      = "GE3 Level Editor"
    bl_idname     = "GE3_PT_LevelEditorPanel"
    bl_space_type = "VIEW_3D"
    bl_region_type = "UI"
    bl_category   = "GE3 Sync"

    def draw(self, context):
        layout = self.layout

        layout.label(text="Scene Layout Sync", icon="SCENE_DATA")
        layout.separator()

        box = layout.box()
        box.label(text="Import from game:", icon="IMPORT")
        box.operator("ge3.import_layout", icon="PACKAGE")

        box2 = layout.box()
        box2.label(text="Export to game:", icon="EXPORT")
        box2.operator("ge3.export_layout", icon="EXPORT")

        active_obj = context.active_object
        if active_obj and active_obj.get("ge3_type") == "BUILDING":
            box3 = layout.box()
            box3.label(text="Building Settings", icon="OBJECT_DATA")
            box3.prop(active_obj, "ge3_floors", text="Floors")
            
            row = box3.row()
            row.label(text=f"Width (X): {active_obj.scale.x / SCALE:.1f}")
            row.label(text=f"Depth (Y): {active_obj.scale.y / SCALE:.1f}")

        layout.separator()
        layout.label(text="File: scene_layout.txt", icon="FILE_TEXT")
        layout.label(text="Edit objects, then Export.")
        layout.label(text="Restart game to see changes.")


classes = [GE3_OT_ImportLayout, GE3_OT_ExportLayout, GE3_PT_LevelEditorPanel]

# ─── 登録 ────────────────────────────────────

def register():
    for cls in classes:
        bpy.utils.register_class(cls)
    
    # カスタムプロパティ ge3_floors を登録
    bpy.types.Object.ge3_floors = bpy.props.IntProperty(
        name="Floors",
        description="Number of floors for the building",
        default=1,
        min=1,
        max=100,
        update=update_building_floors
    )
    
    print("[Level Editor] Registered! Check N-Panel > 'GE3 Sync' tab.")


def unregister():
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)
        
    # プロパティの削除
    if hasattr(bpy.types.Object, "ge3_floors"):
        del bpy.types.Object.ge3_floors


# スクリプト直接実行時
if __name__ == "__main__":
    register()
    print("[Level Editor] Ready! Open N-Panel and use 'GE3 Sync' tab.")
