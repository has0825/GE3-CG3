"""
blender_level_editor.py
=======================
Blender レベルエディタ (双方向 & 地形・街並み自動生成)

【機能】
- scene_layout.txt を読み込んでBlenderに建物・床・初期敵配置を3D表示
- Blender上でオブジェクトを自由に移動・追加・削除してレベルをデザイン
- 「Export to Game」ボタンで変更内容をscene_layout.txtに書き出し
- 新機能：「Generate Terrain & City」で起伏のある地形、道路、ビル、敵をプロシージャルに自動生成！
  - 地形に連動するノイズスケールとサンプリング解像度（サイズ変更時の尖り防止）
  - 窓が発光する夜景ビルマテリアルのプロシージャル自動適用
  - 道路中心部の平坦化（ゲームプレイエリアの路面確保）
  - 地形メッシュ（terrain.obj）の自動エクスポート
  - ビルモデル（building.obj）のインポート＆コピー配置による超リアルな街並み再現！

【使い方】
1. このスクリプトを実行
2. Nパネル(Nキー)の「GE3 Level Editor」タブを開く
3. 必要に応じて City & Terrain Generator パラメータを調整
4. 「Generate Terrain & City」を押して街並みを自動生成
5. 「Export to Game」で scene_layout.txt および terrain.obj に書き出し
6. ゲームを再起動すると反映される！
"""

import bpy
import os
import math
import random
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


def make_window_material():
    """夜のビル窓を表現するプロシージャルマテリアルを作成・取得"""
    mat = bpy.data.materials.get("Mat_Building_Window")
    if mat is None:
        mat = bpy.data.materials.new("Mat_Building_Window")
        mat.use_nodes = True
        nodes = mat.node_tree.nodes
        links = mat.node_tree.links
        
        # 初期状態のノードをクリア
        nodes.clear()
        
        # 必要なノードを作成
        node_output = nodes.new("ShaderNodeOutputMaterial")
        node_output.location = (400, 0)
        
        node_bsdf = nodes.new("ShaderNodeBsdfPrincipled")
        node_bsdf.location = (100, 0)
        
        # Brick Texture を使用して窓のグリッドを表現
        node_brick = nodes.new("ShaderNodeTexBrick")
        node_brick.location = (-200, 0)
        node_brick.inputs["Offset"].default_value = 0.0
        node_brick.inputs["Frequency"].default_value = 1
        node_brick.inputs["Squash"].default_value = 1.0
        # 窓の色と壁の色を設定 (窓は黄色/オレンジ系、壁はダークグレー系)
        node_brick.inputs["Color1"].default_value = (0.05, 0.05, 0.06, 1.0) # 窓消灯
        node_brick.inputs["Color2"].default_value = (1.0, 0.75, 0.25, 1.0) # 窓点灯 (温かみのある黄色)
        node_brick.inputs["Mortar"].default_value = (0.1, 0.12, 0.15, 1.0) # 外壁のコンクリート
        
        node_brick.inputs["Scale"].default_value = 15.0
        node_brick.inputs["Mortar Size"].default_value = 0.06
        node_brick.inputs["Row Height"].default_value = 0.5
        node_brick.inputs["Brick Width"].default_value = 0.5
        
        # Mapping でビルの大きさに合わせてテクスチャ座標を調整
        node_mapping = nodes.new("ShaderNodeMapping")
        node_mapping.location = (-400, 0)
        
        # Texture Coordinate (Object 座標を用いることでビルの変形に窓グリッドが追従)
        node_coord = nodes.new("ShaderNodeTexCoord")
        node_coord.location = (-600, 0)
        
        # ノード接続
        links.new(node_coord.outputs["Object"], node_mapping.inputs["Vector"])
        links.new(node_mapping.outputs["Vector"], node_brick.inputs["Vector"])
        links.new(node_brick.outputs["Color"], node_bsdf.inputs["Base Color"])
        
        # 窓を発光（Emission）させて夜景を表現
        if "Emission" in node_bsdf.inputs: # Blender 4.0+
            links.new(node_brick.outputs["Color"], node_bsdf.inputs["Emission"])
        elif "Emission Color" in node_bsdf.inputs: # Blender 3.x
            links.new(node_brick.outputs["Color"], node_bsdf.inputs["Emission Color"])
            
        if "Emission Strength" in node_bsdf.inputs:
            node_bsdf.inputs["Emission Strength"].default_value = 2.0
            
        links.new(node_bsdf.outputs["Shader"], node_output.inputs["Surface"])
        
    return mat


def update_building_floors(self, context):
    """建物の階数が変更されたときにモディファイアーまたはスケールと位置を更新"""
    if self.get("ge3_type") != "BUILDING":
        return
    
    floors = self.ge3_floors
    if floors < 1:
        self.ge3_floors = 1
        return
        
    array_mod = self.modifiers.get("GE3_Floors")
    if array_mod:
        # テンプレート複製モデル（配列モディファイアー適用）の場合
        array_mod.count = floors
        
        # 位置Zは常に底面で固定
        base_gy = self.get("ge3_base_gy", -20.0)
        self.location.z = base_gy * SCALE
        self["ge3_sy"] = 10.0 * floors
    else:
        # Cube（フォールバック）の場合
        sy = 10.0 * floors
        base_gy = self.get("ge3_base_gy", -20.0)
        self.scale.z = sy * SCALE
        self.location.z = (base_gy + sy * 0.5) * SCALE
        self["ge3_sy"] = sy


def import_building_template():
    """building.obj をテンプレートとしてインポートする"""
    template_name = "GE3_Building_Template"
    if template_name in bpy.data.objects:
        return bpy.data.objects[template_name]
        
    resources_dir = os.path.dirname(LAYOUT_PATH)
    obj_path = os.path.join(resources_dir, "building", "building.obj")
    
    if not os.path.exists(obj_path):
        print(f"[Level Editor] building.obj not found at {obj_path}")
        return None
        
    old_objs = set(bpy.data.objects.keys())
    
    try:
        # Blender 4.0+
        bpy.ops.wm.obj_import(filepath=obj_path)
    except AttributeError:
        # 古いBlender
        bpy.ops.import_scene.obj(filepath=obj_path)
        
    new_objs = set(bpy.data.objects.keys()) - old_objs
    
    if new_objs:
        imported_mesh = None
        for name in new_objs:
            obj = bpy.data.objects[name]
            if obj.type == 'MESH':
                imported_mesh = obj
                break
                
        if imported_mesh:
            imported_mesh.name = template_name
            
            # ビューポート非表示にしてメインからアンリンク
            imported_mesh.hide_viewport = True
            imported_mesh.hide_render = True
            
            # コレクションから除外
            for col in list(imported_mesh.users_collection):
                col.objects.unlink(imported_mesh)
                
            # 不要な親ノードなどを削除
            for name in new_objs:
                if name != template_name:
                    obj = bpy.data.objects.get(name)
                    if obj:
                        bpy.data.objects.remove(obj, do_unlink=True)
                        
            return imported_mesh
            
    return None


def create_building_obj(name, gx, gz, floors, sx=10.0, sz=10.0, base_gy=-20.0):
    """ビルオブジェクトをBlenderに作成（テンプレート優先でZ軸方向に配列複製）"""
    template = bpy.data.objects.get("GE3_Building_Template")
    
    if template:
        # テンプレートをコピーしてコレクションにリンク
        obj = template.copy()
        obj.name = name
        col = get_or_create_collection()
        col.objects.link(obj)
        obj.hide_viewport = False
        obj.hide_render = False
        
        # 向きは初期状態(直立)のまま
        obj.rotation_euler = (0.0, 0.0, 0.0)
        
        # 位置を設定 (Zは底面接地)
        obj.location.x = gx * SCALE
        obj.location.y = gz * SCALE
        obj.location.z = base_gy * SCALE
        
        # Zアップ標準のスケール設定 (Z=1階分の高さ)
        obj.scale.x = sx * SCALE
        obj.scale.y = sz * SCALE
        obj.scale.z = 10.0 * SCALE  # 1階分の高さスケールに固定
        
        # 配列モディファイアーを追加してZ（上）方向に積み上げる
        obj.modifiers.clear()
        array_mod = obj.modifiers.new(name="GE3_Floors", type='ARRAY')
        array_mod.use_relative_offset = True
        array_mod.relative_offset_displace = (0.0, 0.0, 1.0) # Z（上）方向に積み上げ
        array_mod.count = floors
    else:
        # フォールバック：立方体メッシュを生成
        bpy.ops.mesh.primitive_cube_add(size=1)
        obj = bpy.context.active_object
        obj.name = name
        
        sy = 10.0 * floors
        # 位置を設定 (Cubeは中心が原点なので、高さの中心にZを配置)
        obj.location.x = gx * SCALE
        obj.location.y = gz * SCALE
        obj.location.z = (base_gy + sy * 0.5) * SCALE
        
        obj.rotation_euler = (0.0, 0.0, 0.0)
        obj.scale.x = sx * SCALE
        obj.scale.y = sz * SCALE
        obj.scale.z = sy * SCALE
    
    mat = make_window_material()
    if obj.data.materials:
        obj.data.materials[0] = mat
    else:
        obj.data.materials.append(mat)
        
    obj["ge3_type"] = "BUILDING"
    obj["ge3_sx"]   = sx
    obj["ge3_sz"]   = sz
    obj["ge3_base_gy"] = base_gy
    obj["ge3_sy"]   = 10.0 * floors
    
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


def export_terrain(terrain_obj):
    """地形メッシュを terrain.obj としてエクスポートする"""
    if terrain_obj is None:
        return
    
    # 選択状況を一時退避
    active_obj = bpy.context.active_object
    selected_objs = [obj for obj in bpy.context.selected_objects]
    
    # 地形のみを選択状態にする
    bpy.ops.object.select_all(action='DESELECT')
    terrain_obj.select_set(True)
    bpy.context.view_layer.objects.active = terrain_obj
    
    export_path = os.path.join(os.path.dirname(LAYOUT_PATH), "terrain.obj")
    
    try:
        # Blender 4.0 以降の推奨API
        bpy.ops.wm.obj_export(
            filepath=export_path,
            export_selected_objects=True,
            export_normals=True,
            export_uv=True,
            export_materials=False,
            export_colors=False
        )
    except AttributeError:
        # 古いBlenderのAPI
        bpy.ops.export_scene.obj(
            filepath=export_path,
            use_selection=True,
            use_normals=True,
            use_uvs=True,
            use_materials=False
        )
        
    # 選択状態を復元
    bpy.ops.object.select_all(action='DESELECT')
    for obj in selected_objs:
        try:
            obj.select_set(True)
        except ReferenceError:
            pass
    if active_obj:
        try:
            bpy.context.view_layer.objects.active = active_obj
        except ReferenceError:
            pass
    print(f"[City Generator] Terrain exported to {export_path}")


class GE3_OT_ImportLayout(Operator):
    """scene_layout.txt を読んでBlenderにオブジェクトを配置する"""
    bl_idname  = "ge3.import_layout"
    bl_label   = "Import from Game"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        if not os.path.exists(LAYOUT_PATH):
            self.report({"ERROR"}, f"File not found: {LAYOUT_PATH}")
            return {"CANCELLED"}

        # テンプレートの読み込みを試みる
        import_building_template()

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
            
            # 各レイヤーの中心Yの最小値から底面高さを逆算
            min_gy = min(group["gys"])
            base_gy = min_gy - 5.0
            
            create_building_obj(name, group["gx"], group["gz"], floors, group["sx"], group["sz"], base_gy)
            
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
        terrain_obj = None

        for obj in col.objects:
            ge3_type = obj.get("ge3_type", None)
            if ge3_type is None:
                continue

            # Blender座標 → ゲーム座標逆変換
            gx = obj.location.x / SCALE
            gz = obj.location.y / SCALE
            gy = obj.location.z / SCALE

            if ge3_type == "BUILDING":
                floors = obj.ge3_floors if hasattr(obj, "ge3_floors") else obj.get("ge3_floors", 1)
                sx = obj.get("ge3_sx", 10.0)
                sz = obj.get("ge3_sz", 10.0)
                
                # 起伏に沿った底面高さを逆算する
                # テンプレート配列ビルの場合はオブジェクト原点が底面にあるためgyがそのまま底面、Cubeは中心なので逆算する
                if obj.modifiers.get("GE3_Floors"):
                    base_gy = gy
                else:
                    base_gy = gy - 10.0 * floors * 0.5
                
                # floors階分出力する。各フロアの重心Yは底面から積み上げ
                for f in range(floors):
                    layer_gy = base_gy + f * 10.0 + 5.0
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
            
            elif ge3_type == "TERRAIN":
                terrain_obj = obj

        with open(LAYOUT_PATH, "w") as f:
            f.write("\n".join(lines) + "\n")

        if terrain_obj:
            export_terrain(terrain_obj)

        msg = f"Exported {len(lines)} objects to {LAYOUT_PATH}"
        if terrain_obj:
            msg += " (Terrain exported to terrain.obj)"
        self.report({"INFO"}, msg)
        print(f"[Level Editor] {msg}")
        print("[Level Editor] Restart the game to apply changes!")
        return {"FINISHED"}


class GE3_OT_GenerateCity(Operator):
    """起伏のある地形と街並み（ビル・敵・道路・自機）をプロシージャル自動生成する"""
    bl_idname  = "ge3.generate_city"
    bl_label   = "Generate Terrain & City"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        scene = context.scene
        city_length = scene.ge3_city_length
        building_density = scene.ge3_building_density
        max_floors = scene.ge3_max_floors
        noise_scale = scene.ge3_height_noise_scale
        noise_strength = scene.ge3_height_noise_strength
        enemy_spawn_rate = scene.ge3_enemy_spawn_rate
        
        # 配置定数（5列幅に拡張）
        road_width = 80.0
        road_flat_width = 220.0 # 5列道路全体 (X = -160 〜 160) をカバーする平坦化幅
        max_height = 25.0       # 街に合わせて谷や山を大幅に浅く調整 (旧 80.0)
        
        # テンプレートビルの読み込み
        import_building_template()
        
        # 既存のレベルエディタコレクション内オブジェクトを削除
        col = get_or_create_collection()
        for obj in list(col.objects):
            bpy.data.objects.remove(obj, do_unlink=True)
            
        # ──────────────────────────────────────────
        # 1. 地形メッシュ (GE3_Terrain) の生成 (幅を 600m に拡張)
        # ──────────────────────────────────────────
        terrain_width = 600.0
        
        # 大きさに応じたサンプリングセグメント数（解像度の維持）
        subdivisions_x = max(16, int(terrain_width / 15.0))
        subdivisions_y = max(16, int(city_length / 15.0))
        
        mesh_data = bpy.data.meshes.new("GE3_Terrain_Mesh")
        terrain_obj = bpy.data.objects.new("GE3_Terrain", mesh_data)
        col.objects.link(terrain_obj)
        
        vertices = []
        faces = []
        
        dx = terrain_width / (subdivisions_x - 1)
        dz = city_length / (subdivisions_y - 1)
        
        # 地形高さを計算する式 (谷や山が浅くなるように制限)
        def get_terrain_height(gx, gz):
            # 複数のサイン波の重ね合わせによるコヒーレントノイズ
            val = (
                math.sin(gz / (noise_scale * 1.0)) * 0.5 +
                math.sin(gx / (noise_scale * 0.7)) * 0.3 +
                math.cos(gz / (noise_scale * 0.3) + gx / (noise_scale * 0.5)) * 0.2
            )
            gy = val * max_height * noise_strength
            
            # 中央の広い道路部分は完全に平坦（路面基準高さ -20.0）に減衰
            dist_from_center = abs(gx)
            if dist_from_center < road_flat_width:
                t = dist_from_center / road_flat_width
                factor = t * t * (3.0 - 2.0 * t)
                road_gy = -20.0
                gy = road_gy + (gy - road_gy) * factor
            return gy

        # 頂点データの計算
        for j in range(subdivisions_y):
            gz = j * dz
            for i in range(subdivisions_x):
                gx = -terrain_width / 2.0 + i * dx
                gy = get_terrain_height(gx, gz)
                # Blender座標系にスケール変換
                vertices.append((gx * SCALE, gz * SCALE, gy * SCALE))
                
        # 面接続データの計算
        for j in range(subdivisions_y - 1):
            for i in range(subdivisions_x - 1):
                v0 = j * subdivisions_x + i
                v1 = v0 + 1
                v2 = (j + 1) * subdivisions_x + i + 1
                v3 = (j + 1) * subdivisions_x + i
                faces.append((v0, v1, v2, v3))
                
        mesh_data.from_pydata(vertices, [], faces)
        mesh_data.update()
        
        # UV展開
        uv_layer = mesh_data.uv_layers.new(name="UVMap")
        for loop in mesh_data.loops:
            v_idx = loop.vertex_index
            j = v_idx // subdivisions_x
            i = v_idx % subdivisions_x
            u = i / (subdivisions_x - 1)
            v = j / (subdivisions_y - 1)
            uv_layer.data[loop.index].uv = (u * (terrain_width / 50.0), v * (city_length / 50.0))
            
        # 地形用マテリアルを適用
        mat_terrain = make_material("Mat_Terrain", (0.08, 0.09, 0.08, 1.0))
        terrain_obj.data.materials.append(mat_terrain)
        terrain_obj["ge3_type"] = "TERRAIN"
        
        # ──────────────────────────────────────────
        # 2. 道路 (FLOOR) の生成 (5列化)
        # ──────────────────────────────────────────
        floor_size_z = 200.0
        num_floor_columns = math.ceil(city_length / floor_size_z)
        
        floor_count = 0
        # lane 0:中央, 1:右1, 2:左1, 3:右2, 4:左2
        lane_offsets = [0.0, +road_width, -road_width, +road_width * 2.0, -road_width * 2.0]
        for lane in range(5):
            gx = lane_offsets[lane]
            for col_idx in range(num_floor_columns):
                gz = col_idx * floor_size_z
                name = f"GE3_Floor_{floor_count:03d}"
                create_floor_obj(name, gx, -20.0, gz)
                floor_count += 1
                
        # ──────────────────────────────────────────
        # 3. ビル (BUILDING) の自動生成 (10列に配置を拡張)
        # ──────────────────────────────────────────
        building_count = 0
        enemy_count = 0
        random.seed(42)
        
        # 道路の両側、および外側を含む広範囲な10列
        building_cols = [-45.0, 45.0, -85.0, 85.0, -125.0, 125.0, -205.0, 205.0, -285.0, 285.0]
        building_interval = 80.0
        num_buildings_y = int(city_length / building_interval)
        
        for i in range(num_buildings_y):
            gz = 50.0 + i * building_interval
            if gz > city_length - 100.0:
                continue
                
            for col_x in building_cols:
                # 限界数に達した場合はブレイク
                if building_count >= 400:
                    break
                    
                # 密度確率判定
                if random.random() > building_density:
                    continue
                    
                gx = col_x
                base_gy = get_terrain_height(gx, gz)
                
                # 地形起伏に応じた階数設定
                terrain_factor = (base_gy - (-20.0)) / max_height
                if abs(gx) > 120.0:
                    # 外側（郊外・山頂付近）はさらに高層
                    floors = int(6 + terrain_factor * 10 + random.randint(0, 5))
                else:
                    # 道路の近辺は中低層ビル
                    floors = int(1 + terrain_factor * 5 + random.randint(0, 3))
                    
                floors = max(1, min(max_floors, floors))
                
                sx = 9.0 + random.random() * 5.0
                sz = 9.0 + random.random() * 5.0
                
                name = f"GE3_Building_{building_count:03d}"
                create_building_obj(name, gx, gz, floors, sx, sz, base_gy)
                sy = 10.0 * floors
                
                building_count += 1
                
                # ──────────────────────────────────────────
                # 敵 (ENEMY) の自動生成 (最大 30体)
                # ──────────────────────────────────────────
                if random.random() < enemy_spawn_rate and enemy_count < 30:
                    enemy_gy = base_gy + sy + 15.0 + random.random() * 12.0
                    enemy_gx = gx * 0.8
                    enemy_gz = gz + (random.random() - 0.5) * 15.0
                    
                    e_name = f"GE3_Enemy_{enemy_count:02d}"
                    create_enemy_obj(e_name, enemy_gx, enemy_gy, enemy_gz)
                    enemy_count += 1
                    
        # ──────────────────────────────────────────
        # 4. プレイヤー (PLAYER) の初期位置配置
        # ──────────────────────────────────────────
        create_player_obj("GE3_Player_Start", 0.0, -3.0, 0.0)
        
        # ──────────────────────────────────────────
        # 5. 地形を terrain.obj としてエクスポート
        # ──────────────────────────────────────────
        export_terrain(terrain_obj)
        
        msg = (f"Generated: {building_count} buildings (obj model applied if found), "
               f"{floor_count} floors, {enemy_count} enemies. Terrain exported.")
        self.report({"INFO"}, msg)
        print(f"[City Generator] {msg}")
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
        scene = context.scene

        layout.label(text="Scene Layout Sync", icon="SCENE_DATA")
        layout.separator()

        # 自動生成ボックス
        box_gen = layout.box()
        box_gen.label(text="City & Terrain Generator", icon="WORLD")
        box_gen.prop(scene, "ge3_city_length", text="City Length")
        box_gen.prop(scene, "ge3_building_density", text="Density")
        box_gen.prop(scene, "ge3_max_floors", text="Max Floors")
        box_gen.prop(scene, "ge3_height_noise_scale", text="Noise Scale")
        box_gen.prop(scene, "ge3_height_noise_strength", text="Terrain Height")
        box_gen.prop(scene, "ge3_enemy_spawn_rate", text="Enemy Rate")
        box_gen.separator()
        box_gen.operator("ge3.generate_city", text="Generate Terrain & City", icon="MOD_OCEAN")

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
        layout.label(text="File: scene_layout.txt / terrain.obj", icon="FILE_TEXT")


classes = [
    GE3_OT_ImportLayout, 
    GE3_OT_ExportLayout, 
    GE3_OT_GenerateCity, 
    GE3_PT_LevelEditorPanel
]

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
    bpy.types.Scene.ge3_enemy_spawn_rate = bpy.props.FloatProperty(
        name="Enemy Spawn Rate",
        description="Probability of spawning an enemy near buildings",
        default=0.05,
        min=0.0,
        max=0.5
    )
    
    print("[Level Editor] Registered! Check N-Panel > 'GE3 Sync' tab.")


def unregister():
    for cls in reversed(classes):
        bpy.utils.unregister_class(cls)
        
    # プロパティの削除
    if hasattr(bpy.types.Object, "ge3_floors"):
        del bpy.types.Object.ge3_floors
        
    for prop in ["ge3_city_length", "ge3_building_density", "ge3_max_floors", 
                 "ge3_height_noise_scale", "ge3_height_noise_strength", "ge3_enemy_spawn_rate"]:
        if hasattr(bpy.types.Scene, prop):
            delattr(bpy.types.Scene, prop)


# スクリプト直接実行時
if __name__ == "__main__":
    register()
    print("[Level Editor] Ready! Open N-Panel and use 'GE3 Sync' tab.")
