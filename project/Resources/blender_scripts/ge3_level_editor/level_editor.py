"""
blender_level_editor.py
=======================
Blender レベルエディタ (双方向 & 地形・街並み自動生成)

【機能】
- scene_layout.txt を読み込んでBlenderに建物・床・初期敵配置を3D表示
- Blender上でオブジェクトを自由に移動・追加・削除してレベルをデザイン
- 「Export to Game」ボタンで変更内容をscene_layout.txtに書き出し
- 新機能：「Generate Terrain & City」で起伏のある地形、道路、ビル、敵をプロシージャルに自動生成！
  - 道路タイルを7列（±240m幅）に拡張して見た目のリアリティ向上
  - 曲がり道での隙間を完全に埋めるダミー道路の改善（本軌道タイルと自然につながる）
  - ビルが道路上や他のビルと重ならないよう重複チェック強化
  - 地形に連動するノイズスケールとサンプリング解像度
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
import mathutils


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

# ── 道路タイル設定（ゲームと一致させること）──
ROAD_WIDTH          = 80.0   # 1タイルの幅（ゲームユニット）
NUM_ROAD_LANES      = 21     # タイル列数（直径約20タイル = 中央1+左右各10列 = 計21列）
NUM_REAL_LANES      = 3      # リアル道路列数（中央+±1列 = lane 0,1,2）
# lane 0：中央, 1:+80, 2:-80, 3:+160, 4:-160, ..., 19:+800, 20:-800
# lane 0〜2 = リアル（WAYPOINT・ビル除外チェック対象）
# lane 3〜20 = ダミー（視覚的装飾のみ。WAYPOINTから除外）
LANE_OFFSETS = [
    0.0,
    +ROAD_WIDTH * 1.0,  -ROAD_WIDTH * 1.0,
    +ROAD_WIDTH * 2.0,  -ROAD_WIDTH * 2.0,
    +ROAD_WIDTH * 3.0,  -ROAD_WIDTH * 3.0,
    +ROAD_WIDTH * 4.0,  -ROAD_WIDTH * 4.0,
    +ROAD_WIDTH * 5.0,  -ROAD_WIDTH * 5.0,
    +ROAD_WIDTH * 6.0,  -ROAD_WIDTH * 6.0,
    +ROAD_WIDTH * 7.0,  -ROAD_WIDTH * 7.0,
    +ROAD_WIDTH * 8.0,  -ROAD_WIDTH * 8.0,
    +ROAD_WIDTH * 9.0,  -ROAD_WIDTH * 9.0,
    +ROAD_WIDTH * 10.0, -ROAD_WIDTH * 10.0,
]
# プレイヤーが走る中央3列（0, ±80m）をビル禁止にする半径
ROAD_CLEAR_HALF_WIDTH = 38.0
# ビル同士の最小間隔
BUILDING_MIN_DIST = 22.0
# タイル同士の重複判定半径
TILE_OVERLAP_RADIUS = 90.0
# すべての道路（ダミー含む）からビルを離すための最小距離（直進時のビル位置45mを考慮し25mに設定）
BUILDING_ROAD_CLEAR_DIST = 25.0



def get_or_create_collection():
    """専用コレクションを取得・作成"""
    if COLLECTION_NAME not in bpy.data.collections:
        col = bpy.data.collections.new(COLLECTION_NAME)
        bpy.context.scene.collection.children.link(col)
    return bpy.data.collections[COLLECTION_NAME]


def add_to_collection(obj):
    col = get_or_create_collection()
    
    # GE3_LevelEditor 以外の他の全コレクションから unlink する（重複登録防止）
    for other_col in list(bpy.data.collections):
        if other_col != col:
            if obj.name in other_col.objects:
                other_col.objects.unlink(obj)
                
    # 最上位シーンコレクションからも unlink する
    if obj.name in bpy.context.scene.collection.objects:
        bpy.context.scene.collection.objects.unlink(obj)
        
    # 専用コレクションにのみ link する
    if obj.name not in col.objects:
        col.objects.link(obj)


def make_material(name, color):
    mat = bpy.data.materials.get(name)
    if mat is None:
        mat = bpy.data.materials.new(name)
        mat.use_nodes = True
        bsdf = mat.node_tree.nodes.get("Principled BSDF")
        if bsdf:
            bsdf.inputs["Base Color"].default_value = color
    return mat


def make_image_material(name, relative_image_path):
    """画像テクスチャを用いたマテリアルを生成または取得"""
    mat = bpy.data.materials.get(name)
    if not mat:
        mat = bpy.data.materials.new(name=name)
        mat.use_nodes = True
        nodes = mat.node_tree.nodes
        links = mat.node_tree.links
        nodes.clear()
        
        # ビューポート用の表示色を設定 (真っ白になるのを防ぐ)
        if name == "Mat_Building":
            mat.diffuse_color = (0.05, 0.05, 0.07, 1.0) # 深い黒鉄色
        else:
            mat.diffuse_color = (0.2, 0.2, 0.2, 1.0) # アスファルトグレー
            
        # 必要なノードを作成
        node_output = nodes.new("ShaderNodeOutputMaterial")
        node_output.location = (400, 0)
        
        node_bsdf = nodes.new("ShaderNodeBsdfPrincipled")
        node_bsdf.location = (100, 0)
        
        # 反射と粗さを調整し、白飛びを防ぐ
        if "Specular" in node_bsdf.inputs:
            node_bsdf.inputs["Specular"].default_value = 0.05
        elif "Specular IOR Level" in node_bsdf.inputs: # Blender 4.0+
            node_bsdf.inputs["Specular IOR Level"].default_value = 0.1
        if "Roughness" in node_bsdf.inputs:
            node_bsdf.inputs["Roughness"].default_value = 0.8
            
        # 画像テクスチャノードを作成
        node_tex = nodes.new("ShaderNodeTexImage")
        node_tex.location = (-200, 0)
        
        # 画像をロード
        resources_dir = os.path.dirname(LAYOUT_PATH)
        abs_img_path = os.path.abspath(os.path.join(resources_dir, relative_image_path))
        
        if os.path.exists(abs_img_path):
            try:
                # すでにロードされているか確認してロード
                img = bpy.data.images.load(abs_img_path, check_existing=True)
                img.filepath = abs_img_path
                # 画像データをBlenderファイル内にパックして表示を確実にする
                if not img.packed_file:
                    img.pack()
                node_tex.image = img
            except Exception as e:
                print(f"[Level Editor] Failed to load image {abs_img_path}: {e}")
        else:
            print(f"[Level Editor] Image not found at {abs_img_path}")
            
        # ノード接続
        links.new(node_tex.outputs["Color"], node_bsdf.inputs["Base Color"])
        
        # もしビル用のマテリアルの場合は、発光(Emission)にも画像の色を繋いで発光させる
        if name == "Mat_Building":
            if "Emission" in node_bsdf.inputs: # Blender 4.0+
                links.new(node_tex.outputs["Color"], node_bsdf.inputs["Emission"])
            elif "Emission Color" in node_bsdf.inputs: # Blender 3.x
                links.new(node_tex.outputs["Color"], node_bsdf.inputs["Emission Color"])
                
            if "Emission Strength" in node_bsdf.inputs:
                node_bsdf.inputs["Emission Strength"].default_value = 1.0 # ほどよく光らせる
                
        bsdf_output = node_bsdf.outputs.get("Shader") or node_bsdf.outputs.get("BSDF")
        if bsdf_output:
            links.new(bsdf_output, node_output.inputs["Surface"])
        
    return mat


def set_material_preview_mode():
    """すべての3Dビューポートの表示モードをマテリアルプレビュー（テクスチャ表示）に変更"""
    for area in bpy.context.screen.areas:
        if area.type == 'VIEW_3D':
            for space in area.spaces:
                if space.type == 'VIEW_3D':
                    space.shading.type = 'MATERIAL'
        

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


def create_building_obj(name, gx, gz, floors, sx=10.0, sz=10.0, base_gy=-20.0, rot_y=0.0, col_info=None, disabled=False):
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
        
        # 向きを設定 (ヨー回転のみ)
        obj.rotation_euler = (0.0, 0.0, rot_y)
        
        # 位置を設定 (Zは底面接地)
        obj.location.x = gx * SCALE
        obj.location.y = gz * SCALE
        obj.location.z = base_gy * SCALE
        
        # Zアップ標準 of スケール設定 (Z=1階分の高さ)
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
        
        obj.rotation_euler = (0.0, 0.0, rot_y)
        obj.scale.x = sx * SCALE
        obj.scale.y = sz * SCALE
        obj.scale.z = sy * SCALE
    
    # メッシュデータをコピーしてシングルユーザー（個別データ）にし、マテリアル競合を防ぐ
    if obj.data:
        obj.data = obj.data.copy()
        
    mat = make_image_material("Mat_Building", "building/buillding_uv.png")
    obj.data.materials.clear()
    obj.data.materials.append(mat)
        
    obj["ge3_type"] = "BUILDING"
    obj["ge3_sx"]   = sx
    obj["ge3_sz"]   = sz
    obj["ge3_base_gy"] = base_gy
    obj["ge3_sy"]   = 10.0 * floors
    obj["ge3_ry"]   = rot_y
    
    # 登録プロパティを代入してコールバックをトリガーする
    obj.ge3_floors = floors
    
    if col_info:
        obj["collider"] = col_info[0]
        obj["collider_center"] = mathutils.Vector(col_info[1])
        obj["collider_size"] = mathutils.Vector(col_info[2])
        
    if disabled:
        obj["disabled"] = True
    
    add_to_collection(obj)
    return obj


def create_floor_obj(name, gx, gy, gz, rot_x=0.0, rot_y=0.0, rot_z=0.0):
    """床オブジェクトをBlenderに作成"""
    bpy.ops.mesh.primitive_plane_add(size=80*SCALE)
    obj = bpy.context.active_object
    obj.name = name
    obj.location.x = gx * SCALE
    obj.location.y = gz * SCALE
    obj.location.z = gy * SCALE
    obj.rotation_euler = (rot_x, rot_z, rot_y)
    mat = make_image_material("Mat_Floor", "douro.jpg")
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    obj["ge3_type"] = "FLOOR"
    obj["ge3_rx"] = rot_x
    obj["ge3_ry"] = rot_y
    obj["ge3_rz"] = rot_z
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


def create_enemy_obj(name, gx, gy, gz, col_info=None, disabled=False):
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
    if col_info:
        obj["collider"] = col_info[0]
        obj["collider_center"] = mathutils.Vector(col_info[1])
        obj["collider_size"] = mathutils.Vector(col_info[2])
    if disabled:
        obj["disabled"] = True
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
            export_materials=True,  # douro.jpg を MTL に含める
            export_colors=False
        )
    except AttributeError:
        # 古いBlenderのAPI
        bpy.ops.export_scene.obj(
            filepath=export_path,
            use_selection=True,
            use_normals=True,
            use_uvs=True,
            use_materials=True  # douro.jpg を MTL に含める
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


# ─────────────────────────────────────────────────────────────────────
# ヘルパー：パス上の任意距離での位置・方向・右方向ベクトルを計算
# ─────────────────────────────────────────────────────────────────────

def get_path_info(path_points, z_val, step_distance):
    """
    path_points リストから指定距離 z_val での位置・方向・右ベクトルを返す。
    戻り値: (pos[x,y,z], dir[x,y,z], right[x,y,z], rot_y, rot_x)
    """
    idx = int(z_val / step_distance)
    idx = max(0, min(len(path_points) - 2, idx))

    # 方向ベクトル
    next_idx = min(idx + 1, len(path_points) - 1)
    dir_vec = [path_points[next_idx][c] - path_points[idx][c] for c in range(3)]
    length = math.sqrt(sum(c**2 for c in dir_vec))
    if length > 0.001:
        dir_vec = [c / length for c in dir_vec]
    else:
        dir_vec = [0.0, 0.0, 1.0]

    # 右ベクトル（XZ平面上）
    right_vec = [-dir_vec[2], 0.0, dir_vec[0]]
    right_len = math.sqrt(right_vec[0]**2 + right_vec[2]**2)
    if right_len > 0.001:
        right_vec = [c / right_len for c in right_vec]
    else:
        right_vec = [1.0, 0.0, 0.0]

    # 回転角
    rot_y = math.atan2(dir_vec[0], dir_vec[2])
    rot_x = math.atan2(-dir_vec[1], math.sqrt(dir_vec[0]**2 + dir_vec[2]**2))

    # 位置（線形補間）
    t = (z_val - idx * step_distance) / step_distance
    t = max(0.0, min(1.0, t))
    pos = [
        path_points[idx][c] + t * (path_points[next_idx][c] - path_points[idx][c])
        for c in range(3)
    ]

    return pos, dir_vec, right_vec, rot_y, rot_x


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
                    ry = float(parts[8]) if len(parts) >= 10 else 0.0
                    
                    col_info = None
                    disabled = False
                    if len(parts) >= 17:
                        col_type = parts[10]
                        if col_type != "NONE":
                            cx, cy, cz = float(parts[11]), float(parts[12]), float(parts[13])
                            csx, csy, csz = float(parts[14]), float(parts[15]), float(parts[16])
                            col_info = (col_type, (cx, cy, cz), (csx, csy, csz))
                        
                        if len(parts) >= 18:
                            disabled = (int(parts[17]) == 1)
                        
                    buildings_raw.append({
                        "gx": gx, "gy": gy, "gz": gz,
                        "sx": sx, "sy": sy, "sz": sz,
                        "ry": ry,
                        "col_info": col_info,
                        "disabled": disabled
                    })

                elif obj_type == "FLOOR" and len(parts) >= 7:
                    gx, gy, gz = float(parts[1]), float(parts[2]), float(parts[3])
                    rx, ry, rz = 0.0, 0.0, 0.0
                    if len(parts) >= 13:
                        rx, ry, rz = float(parts[10]), float(parts[11]), float(parts[12])
                    name = f"GE3_Floor_{floor_count:03d}"
                    create_floor_obj(name, gx, gy, gz, rx, ry, rz)
                    floor_count += 1

                elif obj_type == "PLAYER" and len(parts) >= 4:
                    gx, gy, gz = float(parts[1]), float(parts[2]), float(parts[3])
                    create_player_obj("GE3_Player_Start", gx, gy, gz)

                elif obj_type == "ENEMY" and len(parts) >= 4:
                    gx, gy, gz = float(parts[1]), float(parts[2]), float(parts[3])
                    
                    col_info = None
                    disabled = False
                    if len(parts) >= 17:
                        col_type = parts[10]
                        if col_type != "NONE":
                            cx, cy, cz = float(parts[11]), float(parts[12]), float(parts[13])
                            csx, csy, csz = float(parts[14]), float(parts[15]), float(parts[16])
                            col_info = (col_type, (cx, cy, cz), (csx, csy, csz))
                        
                        if len(parts) >= 18:
                            disabled = (int(parts[17]) == 1)
                        
                    name = f"GE3_Enemy_{enemy_count:02d}"
                    create_enemy_obj(name, gx, gy, gz, col_info, disabled)
                    enemy_count += 1

        building_groups = {}
        for b in buildings_raw:
            key = (round(b["gx"], 1), round(b["gz"], 1))
            if key not in building_groups:
                building_groups[key] = {
                    "gx": b["gx"],
                    "gz": b["gz"],
                    "sx": b["sx"],
                    "sz": b["sz"],
                    "ry": b["ry"],
                    "gys": [],
                    "col_info": b.get("col_info", None),
                    "disabled": b.get("disabled", False)
                }
            building_groups[key]["gys"].append(b["gy"])
            if b.get("col_info", None):
                building_groups[key]["col_info"] = b["col_info"]
            if b.get("disabled", False):
                building_groups[key]["disabled"] = b["disabled"]

        building_count = 0
        for key, group in building_groups.items():
            floors = len(group["gys"])
            name = f"GE3_Building_{building_count:03d}"
            
            # 各レイヤーの中心Yの最小値から底面高さを逆算
            min_gy = min(group["gys"])
            base_gy = min_gy - 5.0
            
            create_building_obj(name, group["gx"], group["gz"], floors, group["sx"], group["sz"], base_gy, group["ry"], group["col_info"], group["disabled"])
            
            building_count += 1

        msg = (f"Imported: {building_count} buildings (grouped), "
               f"{floor_count} floors, {enemy_count} enemies")
        set_material_preview_mode()
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
        
        # オブジェクト分類収集
        building_objs = []
        real_floor_objs = []
        dummy_floor_objs = []
        player_objs = []
        enemy_objs = []

        for obj in col.objects:
            ge3_type = obj.get("ge3_type", None)
            if ge3_type == "BUILDING":
                building_objs.append(obj)
            elif ge3_type == "FLOOR":
                lane = obj.get("ge3_lane", -1)
                if lane != -1:
                    real_floor_objs.append(obj)
                else:
                    dummy_floor_objs.append(obj)
            elif ge3_type == "PLAYER":
                player_objs.append(obj)
            elif ge3_type == "ENEMY":
                enemy_objs.append(obj)
            elif ge3_type == "TERRAIN":
                terrain_obj = obj

        # 本物道路は名前順（GE3_Floor_000, 001...）にソートしてゲーム側の (floorCount % 21) インデックス整合性を完全保証
        real_floor_objs.sort(key=lambda x: x.name)
        sorted_floor_objs = real_floor_objs + dummy_floor_objs

        # ── BUILDING 出力 (中央タイルと重なるビルを自動除外) ──
        center_tiles_pos = []
        for fobj in real_floor_objs + dummy_floor_objs:
            fgx = fobj.location.x / SCALE
            fgz = fobj.location.y / SCALE
            lane = fobj.get("ge3_lane", -1)
            if lane == 0 or abs(fgx) <= 30.0:
                center_tiles_pos.append((fgx, fgz))

        for obj in building_objs:
            gx = obj.location.x / SCALE
            gz = obj.location.y / SCALE
            gy = obj.location.z / SCALE

            # 中央タイルと重なっているか判定 (ROAD_CLEAR_HALF_WIDTH以内なら除外)
            is_center_overlap = False
            for rx, rz in center_tiles_pos:
                if (gx - rx)**2 + (gz - rz)**2 < ROAD_CLEAR_HALF_WIDTH**2:
                    is_center_overlap = True
                    break
            if is_center_overlap:
                continue

            floors = obj.ge3_floors if hasattr(obj, "ge3_floors") else obj.get("ge3_floors", 1)
            sx = obj.get("ge3_sx", 10.0)
            sz = obj.get("ge3_sz", 10.0)
            ry = obj.get("ge3_ry", obj.rotation_euler[2])
            
            col_str = ""
            if "collider" in obj:
                col_type = obj["collider"]
                cc = obj.get("collider_center", (0.0, 0.0, 0.0))
                cs = obj.get("collider_size", (2.0, 2.0, 2.0))
                col_str = f",{col_type},{cc[0]:.2f},{cc[1]:.2f},{cc[2]:.2f},{cs[0]:.2f},{cs[1]:.2f},{cs[2]:.2f}"

            if obj.modifiers.get("GE3_Floors"):
                base_gy = gy
            else:
                base_gy = gy - 10.0 * floors * 0.5
            
            for f in range(floors):
                layer_gy = base_gy + f * 10.0 + 5.0
                lines.append(f"BUILDING,{gx:.2f},{layer_gy:.2f},{gz:.2f},"
                             f"{sx:.2f},10.00,{sz:.2f},0.0,{ry:.4f},0.0" + col_str)

        # ── FLOOR 出力（ソート済み） ──
        all_floors = []
        for obj in sorted_floor_objs:
            gx = obj.location.x / SCALE
            gz = obj.location.y / SCALE
            gy = obj.location.z / SCALE
            rx = obj.get("ge3_rx", obj.rotation_euler[0])
            ry = obj.get("ge3_ry", obj.rotation_euler[2])
            rz = obj.get("ge3_rz", obj.rotation_euler[1])
            lines.append(f"FLOOR,{gx:.2f},{gy:.2f},{gz:.2f},"
                         f"300.0,1.0,200.0,0.0,0.0,0.0,{rx:.4f},{ry:.4f},{rz:.4f}")
            
            lane = obj.get("ge3_lane", -1)
            all_floors.append((gz, gx, gy, lane))

        # ── PLAYER 出力 ──
        for obj in player_objs:
            gx = obj.location.x / SCALE
            gz = obj.location.y / SCALE
            gy = obj.location.z / SCALE
            lines.append(f"PLAYER,{gx:.2f},{gy:.2f},{gz:.2f},"
                         f"10.0,10.0,10.0,0.0,0.0,0.0")

        # ── ENEMY 出力 ──
        for obj in enemy_objs:
            gx = obj.location.x / SCALE
            gz = obj.location.y / SCALE
            gy = obj.location.z / SCALE
            
            disabled_val = 1 if obj.get("disabled", False) else 0

            col_str = ""
            if "collider" in obj:
                col_type = obj["collider"]
                cc = obj.get("collider_center", (0.0, 0.0, 0.0))
                cs = obj.get("collider_size", (2.0, 2.0, 2.0))
                col_str = f",{col_type},{cc[0]:.2f},{cc[1]:.2f},{cc[2]:.2f},{cs[0]:.2f},{cs[1]:.2f},{cs[2]:.2f}"
            else:
                col_str = f",NONE,0.00,0.00,0.00,0.00,0.00,0.00"

            col_str += f",{disabled_val}"
                
            lines.append(f"ENEMY,{gx:.2f},{gy:.2f},{gz:.2f},"
                         f"3.8,3.8,3.8,0.0,0.0,0.0" + col_str)

        # ── 軌道（WAYPOINT）の抽出アルゴリズム (Nearest Neighborによる物理的接続順ソート) ──
        center_floors = [f for f in all_floors if f[3] == 0]
        if not center_floors:
            center_floors = all_floors

        player_pos = (0.0, 0.0, 0.0)
        for obj in player_objs:
            player_pos = (obj.location.x / SCALE, obj.location.z / SCALE, obj.location.y / SCALE)
            break

        road_floors = []
        if center_floors:
            remaining = list(center_floors)
            remaining.sort(key=lambda x: (x[0] - player_pos[2])**2 + (x[1] - player_pos[0])**2)
            curr = remaining.pop(0)
            road_floors.append((curr[0], curr[1], curr[2]))
            
            while remaining:
                remaining.sort(key=lambda x: (x[0] - curr[0])**2 + (x[1] - curr[1])**2 + (x[2] - curr[2])**2)
                curr = remaining.pop(0)
                road_floors.append((curr[0], curr[1], curr[2]))

        for gz, gx, gy in road_floors:
            lines.append(f"WAYPOINT,{gx:.2f},{gy:.2f},{gz:.2f}")

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


class GE3_OT_TurnRouteFromSelected(Operator):
    """選択したオブジェクト以降の道路や建物を一括して方向転換（回転）させる"""
    bl_idname = "ge3.turn_route_from_selected"
    bl_label = "Turn Route From Selected"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        active_obj = context.active_object
        if not active_obj:
            self.report({"WARNING"}, "オブジェクトを選択してください。基準にする道路タイルなどを選択した状態で実行します。")
            return {"CANCELLED"}

        scene = context.scene
        turn_angle_deg = scene.ge3_manual_turn_angle
        rad = math.radians(turn_angle_deg)
        
        # 選択オブジェクトのBlender座標での位置をピボットとする
        pivot_x = active_obj.location.x
        pivot_y = active_obj.location.y # Blender YがゲームのZ (進行方向)

        col = get_or_create_collection()
        moved_count = 0
        
        # 進行方向 (Blender Y) が、選択オブジェクトのY以上のオブジェクトをすべて回転
        for obj in col.objects:
            ge3_type = obj.get("ge3_type", None)
            if ge3_type in ["BUILDING", "FLOOR", "ENEMY", "PLAYER"]:
                # 選択オブジェクトの手前にあるものは対象外。
                # 誤差を考慮して pivot_y - 0.1f 以上のものを対象とする
                if obj.location.y >= pivot_y - 0.1:
                    # ピボット回転
                    if obj != active_obj:
                        dx = obj.location.x - pivot_x
                        dy = obj.location.y - pivot_y
                        
                        # Z軸の回転（BlenderのXY平面上の2D回転）
                        # 右曲がり（ゲーム右➔Blender Xプラス方向）はZ軸回転の減少
                        a = -rad
                        new_x = pivot_x + (dx * math.cos(a) - dy * math.sin(a))
                        new_y = pivot_y + (dx * math.sin(a) + dy * math.cos(a))
                        
                        obj.location.x = new_x
                        obj.location.y = new_y
                        moved_count += 1
                    
                    # オブジェクト自身の向きも同期回転
                    obj.rotation_euler[2] -= rad
                    if "ge3_ry" in obj:
                        obj["ge3_ry"] = obj.rotation_euler[2]

        self.report({"INFO"}, f"{moved_count}個のオブジェクトを{turn_angle_deg}度曲げました。")
        return {"FINISHED"}


class GE3_OT_AlignLanesToCenter(Operator):
    """手動でずれた他の車線(1〜6)を、最寄りの中央車線(lane=0)に吸着・自動整列させる"""
    bl_idname = "ge3.align_lanes_to_center"
    bl_label = "Align Lanes to Center Route"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        col = get_or_create_collection()
        centers = []
        others = []
        
        for obj in col.objects:
            if obj.get("ge3_type") == "FLOOR":
                lane = obj.get("ge3_lane", -1)
                if lane == 0:
                    centers.append(obj)
                elif lane in [1, 2, 3, 4, 5, 6]:
                    others.append(obj)

        if not centers:
            self.report({"WARNING"}, "中央道路 (lane=0) が見つかりません。")
            return {"CANCELLED"}

        aligned_count = 0
        # ゲームの ROAD_WIDTH=80 → Blenderスケール×0.1 = 8.0
        road_width_b = ROAD_WIDTH * SCALE

        for other in others:
            best_center = None
            min_dist = 999999.0
            for center in centers:
                dist = abs(other.location.y - center.location.y)
                if dist < min_dist:
                    min_dist = dist
                    best_center = center

            if best_center and min_dist < 40.0:
                cx = best_center.location.x
                cy = best_center.location.y
                cz = best_center.location.z

                yaw = best_center.rotation_euler[2]
                right_vec = (math.cos(yaw), -math.sin(yaw))

                lane = other.get("ge3_lane")
                # 7列分のオフセット対応
                lane_offset_map = {
                    1: +1.0, 2: -1.0,
                    3: +2.0, 4: -2.0,
                    5: +3.0, 6: -3.0
                }
                offset_factor = lane_offset_map.get(lane, 0.0)

                # 位置を同期
                other.location.x = cx + right_vec[0] * (road_width_b * offset_factor)
                other.location.y = cy + right_vec[1] * (road_width_b * offset_factor)
                other.location.z = cz

                # 回転を同期
                other.rotation_euler[0] = best_center.rotation_euler[0]
                other.rotation_euler[1] = best_center.rotation_euler[1]
                other.rotation_euler[2] = best_center.rotation_euler[2]

                if "ge3_rx" in other: other["ge3_rx"] = other.rotation_euler[0]
                if "ge3_ry" in other: other["ge3_ry"] = other.rotation_euler[2]
                if "ge3_rz" in other: other["ge3_rz"] = other.rotation_euler[1]

                aligned_count += 1

        self.report({"INFO"}, f"{aligned_count}枚の車線道路を中央ルートに整列同期しました。")
        return {"FINISHED"}


class GE3_OT_RemoveOverlappingBuildings(Operator):
    """
    ビルが道路上やビル同士で重なっている場合に、重なっている方を削除する
    ─ 中央車線(lane=0)タイルから ROAD_CLEAR_HALF_WIDTH(=45m) 以内のビルのみ削除
      ※ 外側タイルは対象外なので、外側ビルは消えない
    ─ ビル同士の距離が BUILDING_MIN_DIST 以下なら後ろのものを削除
    """
    bl_idname = "ge3.remove_overlapping_buildings"
    bl_label  = "Remove Overlapping Buildings"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        col = get_or_create_collection()

        # ── 1. プレイヤーが通る「中央車線タイル」の正確な判定・収集 ──
        center_road_tiles = []
        for obj in col.objects:
            if obj.get("ge3_type") == "FLOOR":
                gx = obj.location.x / SCALE
                gz = obj.location.y / SCALE
                lane = obj.get("ge3_lane", -1)
                # lane=0、または直進時の中央道路タイル (|gx| <= 30.0) を中央タイルとみなす
                if lane == 0 or abs(gx) <= 30.0:
                    center_road_tiles.append((gx, gz))

        # 万が一中央タイルが特定できない場合の安全な自動抽出
        if not center_road_tiles:
            for obj in col.objects:
                if obj.get("ge3_type") == "FLOOR":
                    gx = obj.location.x / SCALE
                    gz = obj.location.y / SCALE
                    if abs(gx) <= 40.0:
                        center_road_tiles.append((gx, gz))

        # ── 2. ビルを収集 ──
        buildings = []
        for obj in col.objects:
            if obj.get("ge3_type") == "BUILDING":
                gx = obj.location.x / SCALE
                gz = obj.location.y / SCALE
                buildings.append((obj, gx, gz))

        to_delete = set()

        # ── 3. 中央車線タイルとの重なりチェック ──
        # ROAD_CLEAR_HALF_WIDTH(=45m) 以内のビルのみ削除対象
        for obj, bx, bz in buildings:
            if obj.name in to_delete:
                continue
            for rx, rz in center_road_tiles:
                dist_sq = (bx - rx)**2 + (bz - rz)**2
                if dist_sq < ROAD_CLEAR_HALF_WIDTH**2:
                    to_delete.add(obj.name)
                    break

        # ── 4. ビル同士の重なりチェック ──
        for i in range(len(buildings)):
            obj_i, bx_i, bz_i = buildings[i]
            if obj_i.name in to_delete:
                continue
            for j in range(i + 1, len(buildings)):
                obj_j, bx_j, bz_j = buildings[j]
                if obj_j.name in to_delete:
                    continue
                dist_sq = (bx_i - bx_j)**2 + (bz_i - bz_j)**2
                if dist_sq < BUILDING_MIN_DIST**2:
                    to_delete.add(obj_j.name)  # 後のものを削除

        # ── 5. 削除実行 ──
        deleted_count = 0
        for name in to_delete:
            obj = bpy.data.objects.get(name)
            if obj:
                bpy.data.objects.remove(obj, do_unlink=True)
                deleted_count += 1

        self.report({"INFO"}, f"{deleted_count} 個の重なりビルを削除しました。(中央車線±{ROAD_CLEAR_HALF_WIDTH:.0f}m保護)")
        return {"FINISHED"}


class GE3_OT_RemoveOverlappingFloors(Operator):
    """
    道路タイル（FLOOR）同士が重なっている場合に、重なっているダミー道路または重複したタイルを削除する
    - タイル同士の距離が 60m 未満のものを検出
    - 本物道路（ge3_lane != -1）とダミー道路（ge3_lane == -1）が重複している場合、ダミー側を優先的に削除
    """
    bl_idname = "ge3.remove_overlapping_floors"
    bl_label  = "Remove Overlapping Floors"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        col = get_or_create_collection()
        floors = []
        for obj in col.objects:
            if obj.get("ge3_type") == "FLOOR":
                gx = obj.location.x / SCALE
                gz = obj.location.y / SCALE
                lane = obj.get("ge3_lane", -1)
                floors.append((obj, gx, gz, lane))

        to_delete = set()
        limit_dist_sq = 60.0**2

        for i in range(len(floors)):
            obj_i, gx_i, gz_i, lane_i = floors[i]
            if obj_i.name in to_delete:
                continue
            for j in range(i + 1, len(floors)):
                obj_j, gx_j, gz_j, lane_j = floors[j]
                if obj_j.name in to_delete:
                    continue

                dist_sq = (gx_i - gx_j)**2 + (gz_i - gz_j)**2
                if dist_sq < limit_dist_sq:
                    # 本物道路同士はゲームの循環スクロールインデックスを維持するため絶対に削除しない
                    if lane_i == -1:
                        to_delete.add(obj_i.name)
                        break
                    elif lane_j == -1:
                        to_delete.add(obj_j.name)

        deleted_count = 0
        for name in to_delete:
            obj = bpy.data.objects.get(name)
            if obj:
                try:
                    for c in list(obj.users_collection):
                        c.objects.unlink(obj)
                    bpy.data.objects.remove(obj, do_unlink=True)
                    deleted_count += 1
                except Exception:
                    pass

        self.report({"INFO"}, f"{deleted_count} 枚の重複道路タイルを削除しました。")
        return {"FINISHED"}


class GE3_OT_GenerateCity(Operator):
    """起伏のある地形と街並み（ビル・敵・道路・自機）をプロシージャル自動生成する"""
    bl_idname  = "ge3.generate_city"
    bl_label   = "Generate Terrain & City"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        scene = context.scene
        building_density = scene.ge3_building_density
        max_floors = scene.ge3_max_floors
        noise_scale = scene.ge3_height_noise_scale
        noise_strength = scene.ge3_height_noise_strength
        enemy_spawn_rate = scene.ge3_enemy_spawn_rate
        
        # 新しい区画制御パラメータ
        num_sections = scene.ge3_num_sections
        section_length = scene.ge3_section_length
        city_length = num_sections * section_length
        behaviors_str = scene.ge3_section_behaviors
        curve_angle_deg = scene.ge3_curve_angle
        slope_height = scene.ge3_slope_height
        
        # 配置定数（21列幅 = 直径約20タイル）
        road_width       = ROAD_WIDTH            # 80.0
        num_lanes        = NUM_ROAD_LANES         # 21
        road_flat_width  = 880.0  # 21列道路全体 (X = -800 〜 800) + 余裕をカバーする平坦化幅
        max_height       = 25.0  # 街に合わせて谷や山を大幅に浅く調整
        
        # テンプレートビルの読み込み
        import_building_template()
        
        # シーン内の古いGE3関連オブジェクトを完全にクリーンアップ（残存による二重重なり防止）
        col = get_or_create_collection()
        to_remove = []
        for obj in list(bpy.data.objects):
            # 名前プレフィックス、コレクション所属、またはカスタムプロパティ "ge3_type" を持っているかをチェック
            is_ge3 = (
                obj.name.startswith("GE3_") or 
                obj.name in col.objects or 
                obj.get("ge3_type") is not None
            )
            if is_ge3:
                # テンプレートモデルは削除しない
                if obj.name != "GE3_Building_Template":
                    to_remove.append(obj)
        for obj in to_remove:
            try:
                for c in list(obj.users_collection):
                    c.objects.unlink(obj)
                bpy.data.objects.remove(obj, do_unlink=True)
            except Exception as e:
                print(f"Failed to remove {obj.name}: {e}")

        # ──────────────────────────────────────────
        # 0. 軌道（ウェイポイント）のプロシージャル生成 & スプライン補間
        # ──────────────────────────────────────────
        def catmull_rom_func(p0, p1, p2, p3, t):
            return 0.5 * (
                (2.0 * p1) +
                (-p0 + p2) * t +
                (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t**2 +
                (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t**3
            )

        # 挙動のパース
        behaviors = [s.strip().upper() for s in behaviors_str.split(",")]
        while len(behaviors) < num_sections:
            behaviors.append("S")

        # ウェイポイント生成のステート
        cur_pos = [0.0, -20.0, 0.0]
        cur_yaw = 0.0  # Z軸に対する角度（0はZプラス方向）
        
        wp = []
        wp.append((cur_pos[0], cur_pos[1], cur_pos[2]))
        
        for i in range(num_sections):
            behavior = behaviors[i]
            
            # Yaw (回転) 変更
            yaw_delta = 0.0
            if "R" in behavior:
                yaw_delta = math.radians(curve_angle_deg)  # 右曲がり (時計回り)
            elif "L" in behavior:
                yaw_delta = -math.radians(curve_angle_deg) # 左曲がり
                
            cur_yaw += yaw_delta
            
            # Pitch (高度) 変更
            y_delta = 0.0
            if "U" in behavior:
                y_delta = slope_height  # 上り坂
            elif "D" in behavior:
                y_delta = -slope_height # 下り坂
                
            # 次の位置を進行方向に基づいて計算
            dx = math.sin(cur_yaw) * section_length
            dz = math.cos(cur_yaw) * section_length
            
            cur_pos[0] += dx
            cur_pos[1] += y_delta
            cur_pos[2] += dz
            
            wp.append((cur_pos[0], cur_pos[1], cur_pos[2]))

        # スプライン補間用に前後を延長
        extended_wp = []
        extended_wp.append((wp[0][0] - (wp[1][0] - wp[0][0]), 
                            wp[0][1] - (wp[1][1] - wp[0][1]), 
                            wp[0][2] - (wp[1][2] - wp[0][2])))
        extended_wp.extend(wp)
        extended_wp.append((wp[-1][0] + (wp[-1][0] - wp[-2][0]), 
                            wp[-1][1] + (wp[-1][1] - wp[-2][1]), 
                            wp[-1][2] + (wp[-1][2] - wp[-2][2])))

        path_points = []
        step_distance = 10.0
        total_steps = int(city_length / step_distance)
        
        for step in range(total_steps + 1):
            progress = step * step_distance
            seg_idx = int(progress / section_length)
            seg_idx = min(seg_idx, num_sections - 1)
            
            t = (progress - seg_idx * section_length) / section_length
            
            p0 = extended_wp[seg_idx]
            p1 = extended_wp[seg_idx + 1]
            p2 = extended_wp[seg_idx + 2]
            p3 = extended_wp[seg_idx + 3]
            
            x_val = catmull_rom_func(p0[0], p1[0], p2[0], p3[0], t)
            y_val = catmull_rom_func(p0[1], p1[1], p2[1], p3[1], t)
            z_val = catmull_rom_func(p0[2], p1[2], p2[2], p3[2], t)
            
            path_points.append((x_val, y_val, z_val))
            
        # ──────────────────────────────────────────
        # 1. 地形メッシュ (GE3_Terrain) の生成
        # ──────────────────────────────────────────
        terrain_width = 600.0
        subdivisions_x = max(16, int(terrain_width / 15.0))
        subdivisions_y = max(16, int(city_length / 15.0))
        
        mesh_data = bpy.data.meshes.new("GE3_Terrain_Mesh")
        terrain_obj = bpy.data.objects.new("GE3_Terrain", mesh_data)
        col.objects.link(terrain_obj)
        
        vertices = []
        faces = []
        
        dx_t = terrain_width / (subdivisions_x - 1)
        dz_t = city_length / (subdivisions_y - 1)
        
        def get_terrain_height(gx, gz):
            val = (
                math.sin(gz / (noise_scale * 1.0)) * 0.5 +
                math.sin(gx / (noise_scale * 0.7)) * 0.3 +
                math.cos(gz / (noise_scale * 0.3) + gx / (noise_scale * 0.5)) * 0.2
            )
            base_noise_y = -20.0 + val * max_height * noise_strength
            
            # gz に基づいて path_points から補間
            idx = int(gz / step_distance)
            idx = max(0, min(len(path_points) - 1, idx))
            road_pt = path_points[idx]
            road_x = road_pt[0]
            road_y = road_pt[1]
            
            dist_from_center = abs(gx - road_x)
            if dist_from_center < road_flat_width:
                t = dist_from_center / road_flat_width
                factor = t * t * (3.0 - 2.0 * t)
                gy = road_y + (base_noise_y - road_y) * factor
            else:
                gy = base_noise_y
            return gy

        for j in range(subdivisions_y):
            gz = j * dz_t
            for i in range(subdivisions_x):
                gx = -terrain_width / 2.0 + i * dx_t
                gy = get_terrain_height(gx, gz)
                vertices.append((gx * SCALE, gz * SCALE, gy * SCALE))
                
        # 道路タイルの真下をくり抜く幅（21レーン全幅 ±410m）
        road_carve_width = 410.0

        for j in range(subdivisions_y - 1):
            gz_mid = (j + 0.5) * dz_t
            idx = int(gz_mid / step_distance)
            idx = max(0, min(len(path_points) - 1, idx))
            road_x = path_points[idx][0]

            for i in range(subdivisions_x - 1):
                gx_mid = -terrain_width / 2.0 + (i + 0.5) * dx_t
                
                # 道路タイル直下のポリゴンはスキップして完全にくり抜く（重なりゼロ化）
                if abs(gx_mid - road_x) < road_carve_width:
                    continue

                v0 = j * subdivisions_x + i
                v1 = v0 + 1
                v2 = (j + 1) * subdivisions_x + i + 1
                v3 = (j + 1) * subdivisions_x + i
                faces.append((v0, v1, v2, v3))
                
        mesh_data.from_pydata(vertices, [], faces)
        mesh_data.update()
        
        uv_layer = mesh_data.uv_layers.new(name="UVMap")
        for loop in mesh_data.loops:
            v_idx = loop.vertex_index
            j = v_idx // subdivisions_x
            i = v_idx % subdivisions_x
            u = i / (subdivisions_x - 1)
            v = j / (subdivisions_y - 1)
            uv_layer.data[loop.index].uv = (u * (terrain_width / 50.0), v * (city_length / 50.0))
            
        mat_terrain = make_image_material("Mat_Terrain", "douro.jpg")
        terrain_obj.data.materials.append(mat_terrain)
        terrain_obj["ge3_type"] = "TERRAIN"
        
        # ──────────────────────────────────────────
        # 2. 道路 (FLOOR) の生成 (軌道に沿って7列配置)
        # ──────────────────────────────────────────
        floor_size_z = 100.0  # タイル1枚のZ方向長さ（ゲームユニット）
        num_floors_per_lane = math.ceil(city_length / floor_size_z)
        
        floor_count = 0
        # タイルの (gx, gz) リスト（重複チェック用）
        all_tile_positions = []       # 全タイル位置
        center_tile_positions = []    # 中央3列のみ（ビル除外チェック用）

        for i in range(num_floors_per_lane):
            z_val = i * floor_size_z
            pos, dir_vec, right_vec, rot_y, rot_x = get_path_info(path_points, z_val, step_distance)

            for lane_idx in range(num_lanes):
                offset = LANE_OFFSETS[lane_idx]
                gx = pos[0] + offset * right_vec[0]
                gy = pos[1] + offset * right_vec[1]
                gz = pos[2] + offset * right_vec[2]

                name = f"GE3_Floor_{floor_count:03d}"
                floor_obj = create_floor_obj(name, gx, gy, gz, rot_x, rot_y, 0.0)
                # lane 0,1,2 (中央・±80m) = リアルタイル，lane 3以降 = ダミー（WAYPOINT生成から除外）
                if lane_idx < NUM_REAL_LANES:
                    floor_obj["ge3_lane"] = lane_idx  # 0,1,2 = WAYPOINTSort対象
                else:
                    floor_obj["ge3_lane"] = -1  # ダミー（視覚的のみ）
                floor_count += 1
                all_tile_positions.append((gx, gz))
                # lane 0,1,2 = 中央・±80m の3列のみをビル除外対象にする
                if lane_idx < NUM_REAL_LANES:
                    center_tile_positions.append((gx, gz))

        # ──────────────────────────────────────────
        # 2.5 曲がり角でのダミー道路の生成
        #     ★ 本軌道タイルとシームレスにつながるよう、
        #        既存タイル位置と重複するものをスキップ
        #     ★ Zファイティング防止のため、高さをわずかに下げる(-0.05m)
        # ──────────────────────────────────────────
        dummy_floor_count = 0
        dummy_floor_positions = []
        dummy_straight_segments = []  # (p_dummy, right_vec, rot_y) を記録してダミービル生成に使用
        max_dummy_floors = 12  # ダミー道路を12枚分（1200ゲームユニット）まっすぐ伸ばす

        # 曲がり角ごとに処理
        for sec_idx in range(num_sections):
            behavior = behaviors[sec_idx]
            if "R" in behavior or "L" in behavior:
                # このセクション of 開始進捗
                progress_start = sec_idx * section_length
                idx_start = int(progress_start / step_distance)
                idx_start = max(0, min(len(path_points) - 1, idx_start))
                p_start = path_points[idx_start]
                
                # 直前の直進方向を算出 (idx_start == 0 の場合はデフォルトで Z方向)
                idx_prev = max(0, idx_start - 2)
                if idx_start > 0:
                    dir_straight = [path_points[idx_start][c] - path_points[idx_prev][c] for c in range(3)]
                else:
                    dir_straight = [0.0, 0.0, 1.0]
                
                length = math.sqrt(sum(c**2 for c in dir_straight))
                if length > 0.001:
                    dir_straight = [c / length for c in dir_straight]
                else:
                    dir_straight = [0.0, 0.0, 1.0]
                
                right_vec = [-dir_straight[2], 0.0, dir_straight[0]]
                right_len = math.sqrt(right_vec[0]**2 + right_vec[2]**2)
                if right_len > 0.001:
                    right_vec = [c / right_len for c in right_vec]
                else:
                    right_vec = [1.0, 0.0, 0.0]
                
                rot_y = math.atan2(dir_straight[0], dir_straight[2])
                rot_x = math.atan2(-dir_straight[1], math.sqrt(dir_straight[0]**2 + dir_straight[2]**2))
                
                # ── ダミー道路の配置 ──
                for d in range(1, max_dummy_floors + 1):
                    d_dist = d * floor_size_z
                    p_dummy = [p_start[c] + d_dist * dir_straight[c] for c in range(3)]
                    dummy_straight_segments.append((p_dummy, right_vec, rot_y))
                    
                    for lane_idx in range(-10, 11):
                        offset = lane_idx * road_width
                        gx = p_dummy[0] + offset * right_vec[0]
                        gy = p_dummy[1] + offset * right_vec[1]
                        gz = p_dummy[2] + offset * right_vec[2]
                        
                        # ★ 本物の道路タイルとの重複チェック
                        overlap = False
                        for tx, tz in all_tile_positions:
                            if (gx - tx)**2 + (gz - tz)**2 < TILE_OVERLAP_RADIUS**2:
                                overlap = True
                                break
                        if overlap:
                            continue
                            
                        # 他のダミー道路との重複チェック
                        for dx_pos, dz_pos in dummy_floor_positions:
                            if (gx - dx_pos)**2 + (gz - dz_pos)**2 < TILE_OVERLAP_RADIUS**2:
                                overlap = True
                                break
                        if overlap:
                            continue
                        
                        name = f"GE3_Floor_Dummy_{dummy_floor_count:03d}"
                        # 中間層レイヤー (0.00m) として配置 (本物道路 +0.05m / 地形 -0.15m)
                        floor_obj = create_floor_obj(name, gx, gy, gz, rot_x, rot_y, 0.0)
                        floor_obj["ge3_lane"] = -1  # WAYPOINTから除外
                        dummy_floor_count += 1
                        dummy_floor_positions.append((gx, gz))

        # ──────────────────────────────────────────
        # 3. ビル (BUILDING) の自動生成 (軌道に沿って配置)
        #    ★ 道路タイル範囲内にはビルを置かない
        #    ★ ビル同士の重なりを排除
        # ──────────────────────────────────────────
        building_count = 0
        enemy_count = 0
        random.seed(42)
        
        # ビルの配置列（21列道路に対応した広範囲配置）
        building_cols = [
             -45.0,   45.0,
             -85.0,   85.0,
            -125.0,  125.0,
            -165.0,  165.0,
            -205.0,  205.0,
            -245.0,  245.0,
            -285.0,  285.0,
            -325.0,  325.0,
            -365.0,  365.0,
            -405.0,  405.0,
            -445.0,  445.0,
            -485.0,  485.0,
            -525.0,  525.0,
            -565.0,  565.0,
            -605.0,  605.0,
        ]
        MAX_BUILDINGS = 3000
        building_interval = 45.0
        num_buildings_z = int(city_length / building_interval)
        
        # 配置済みビル位置リスト（重複チェック用）
        placed_building_positions = []

        for i in range(num_buildings_z):
            gz_val = 50.0 + i * building_interval
            if gz_val > city_length - 100.0:
                continue
                
            pos, dir_vec, right_vec, rot_y, rot_x = get_path_info(path_points, gz_val, step_distance)
            
            for col_x in building_cols:
                if building_count >= MAX_BUILDINGS:
                    break
                    
                density_threshold = building_density if abs(col_x) > 130.0 else min(0.95, building_density + 0.3)
                if random.random() > density_threshold:
                    continue
                    
                gx = pos[0] + col_x * right_vec[0]
                gz = pos[2] + col_x * right_vec[2]

                # ── 道路タイルとの重なりチェック（本物＋ダミー道路の全タイル対象）──
                too_close_to_road = False
                # 中央3列の保護（38m）
                for tx, tz in center_tile_positions:
                    if (gx - tx)**2 + (gz - tz)**2 < ROAD_CLEAR_HALF_WIDTH**2:
                        too_close_to_road = True
                        break
                # その他のすべての車線およびダミー道路からの保護（25m）
                if not too_close_to_road:
                    for tx, tz in all_tile_positions:
                        if (gx - tx)**2 + (gz - tz)**2 < BUILDING_ROAD_CLEAR_DIST**2:
                            too_close_to_road = True
                            break
                if not too_close_to_road:
                    for tx, tz in dummy_floor_positions:
                        if (gx - tx)**2 + (gz - tz)**2 < BUILDING_ROAD_CLEAR_DIST**2:
                            too_close_to_road = True
                            break
                if too_close_to_road:
                    continue

                # ── ビル同士の重なりチェック ──
                too_close_to_building = False
                for bx, bz in placed_building_positions:
                    if (gx - bx)**2 + (gz - bz)**2 < BUILDING_MIN_DIST**2:
                        too_close_to_building = True
                        break
                if too_close_to_building:
                    continue
                    
                base_gy = get_terrain_height(gx, gz)
                
                # 地形起伏に応じた階数設定（中央脇もバランス良く中層〜高層ビル化）
                terrain_factor = (base_gy - pos[1]) / max(max_height, 1.0)
                if abs(col_x) > 200.0:
                    floors = int(6 + terrain_factor * 10 + random.randint(0, 5))
                elif abs(col_x) < 60.0:
                    floors = int(3 + terrain_factor * 4 + random.randint(0, 3))
                else:
                    floors = int(2 + terrain_factor * 5 + random.randint(0, 4))
                    
                floors = max(1, min(max_floors, floors))
                
                sx = 9.0 + random.random() * 5.0
                sz = 9.0 + random.random() * 5.0
                
                name = f"GE3_Building_{building_count:03d}"
                create_building_obj(name, gx, gz, floors, sx, sz, base_gy, rot_y)
                sy = 10.0 * floors
                
                building_count += 1
                placed_building_positions.append((gx, gz))
                
                # 敵の配置
                if random.random() < enemy_spawn_rate and enemy_count < 30:
                    enemy_gy = base_gy + sy + 15.0 + random.random() * 12.0
                    enemy_gx = gx * 0.8
                    enemy_gz = gz + (random.random() - 0.5) * 15.0
                    
                    e_name = f"GE3_Enemy_{enemy_count:02d}"
                    create_enemy_obj(e_name, enemy_gx, enemy_gy, enemy_gz)
                    enemy_count += 1

        # ──────────────────────────────────────────
        # 3.5 曲がり角でのダミービルの生成
        #     ★ 道路タイル範囲内にはビルを置かない
        #     ★ ビル同士の重なりを排除
        # ──────────────────────────────────────────
        dummy_building_positions = []

        # 曲がり角ごとに処理
        for sec_idx in range(num_sections):
            behavior = behaviors[sec_idx]
            if "R" in behavior or "L" in behavior:
                # このセクション of 開始進捗
                progress_start = sec_idx * section_length
                idx_start = int(progress_start / step_distance)
                idx_start = max(0, min(len(path_points) - 1, idx_start))
                p_start = path_points[idx_start]
                
                # 直前の直進方向を算出 (idx_start == 0 の場合はデフォルトで Z方向)
                idx_prev = max(0, idx_start - 2)
                if idx_start > 0:
                    dir_straight = [path_points[idx_start][c] - path_points[idx_prev][c] for c in range(3)]
                else:
                    dir_straight = [0.0, 0.0, 1.0]
                
                length = math.sqrt(sum(c**2 for c in dir_straight))
                if length > 0.001:
                    dir_straight = [c / length for c in dir_straight]
                else:
                    dir_straight = [0.0, 0.0, 1.0]
                
                right_vec = [-dir_straight[2], 0.0, dir_straight[0]]
                right_len = math.sqrt(right_vec[0]**2 + right_vec[2]**2)
                if right_len > 0.001:
                    right_vec = [c / right_len for c in right_vec]
                else:
                    right_vec = [1.0, 0.0, 0.0]
                
                rot_y = math.atan2(dir_straight[0], dir_straight[2])
                
                # ── ダミービルの配置 ──
                max_b_dist = max_dummy_floors * floor_size_z
                b_steps = int(max_b_dist / building_interval)
                
                # ダミービルのX列は道路より外側のみ（軌道と重ならない列）
                dummy_building_cols_signed = [
                    -125.0, 125.0,
                    -165.0, 165.0,
                    -205.0, 205.0,
                    -285.0, 285.0,
                    -365.0, 365.0,
                ]
                
                for b_step in range(b_steps):
                    d_dist = 40.0 + b_step * building_interval
                    p_b_dummy = [p_start[c] + d_dist * dir_straight[c] for c in range(3)]
                    
                    for col_x in dummy_building_cols_signed:
                        if random.random() > building_density:
                            continue
                            
                        gx = p_b_dummy[0] + col_x * right_vec[0]
                        gz = p_b_dummy[2] + col_x * right_vec[2]
                        
                        # 道路タイルとの重なりチェック（本物＋ダミー道路）
                        overlap = False
                        for tx, tz in all_tile_positions:
                            if (gx - tx)**2 + (gz - tz)**2 < ROAD_CLEAR_HALF_WIDTH**2:
                                overlap = True
                                break
                        if overlap:
                            continue
                        for dx_pos, dz_pos in dummy_floor_positions:
                            if (gx - dx_pos)**2 + (gz - dz_pos)**2 < ROAD_CLEAR_HALF_WIDTH**2:
                                overlap = True
                                break
                        if overlap:
                            continue

                        # ビル同士の重なりチェック（本物＋ダミービル）
                        for bx, bz in placed_building_positions:
                            if (gx - bx)**2 + (gz - bz)**2 < BUILDING_MIN_DIST**2:
                                overlap = True
                                break
                        if overlap:
                            continue
                        for dx_pos, dz_pos in dummy_building_positions:
                            if (gx - dx_pos)**2 + (gz - dz_pos)**2 < BUILDING_MIN_DIST**2:
                                overlap = True
                                break
                        if overlap:
                            continue
                            
                        base_gy = get_terrain_height(gx, gz)
                        
                        # 高さ (階数) の算出
                        terrain_factor = (base_gy - p_b_dummy[1]) / max(max_height, 1.0)
                        dist_factor = abs(col_x) / road_width
                        if dist_factor > 3.0:
                            floors = int(6 + terrain_factor * 10 + random.randint(0, 5))
                        else:
                            floors = int(1 + terrain_factor * 5 + random.randint(0, 3))
                        floors = max(1, min(max_floors, floors))
                        
                        sx = 9.0 + random.random() * 5.0
                        sz = 9.0 + random.random() * 5.0
                        
                        name = f"GE3_Building_{building_count:03d}"
                        create_building_obj(name, gx, gz, floors, sx, sz, base_gy, rot_y)
                        building_count += 1
                        dummy_building_positions.append((gx, gz))
                        
        # ──────────────────────────────────────────
        # 3.8 重複する道路タイルの自動クリーンアップ
        #     ★ 本物道路同士やダミー道路同士の交差による重なりを最終クリーンアップ
        # ──────────────────────────────────────────
        floors_for_clean = []
        for obj in col.objects:
            if obj.get("ge3_type") == "FLOOR":
                gx = obj.location.x / SCALE
                gz = obj.location.y / SCALE
                lane = obj.get("ge3_lane", -1)
                floors_for_clean.append((obj, gx, gz, lane))

        to_delete_floors = set()
        limit_dist_sq = 60.0**2

        for i in range(len(floors_for_clean)):
            obj_i, gx_i, gz_i, lane_i = floors_for_clean[i]
            if obj_i.name in to_delete_floors:
                continue
            for j in range(i + 1, len(floors_for_clean)):
                obj_j, gx_j, gz_j, lane_j = floors_for_clean[j]
                if obj_j.name in to_delete_floors:
                    continue

                dist_sq = (gx_i - gx_j)**2 + (gz_i - gz_j)**2
                if dist_sq < limit_dist_sq:
                    # 本物道路同士はゲームの循環スクロールインデックスを維持するため絶対に削除しない
                    if lane_i == -1:
                        to_delete_floors.add(obj_i.name)
                        break
                    elif lane_j == -1:
                        to_delete_floors.add(obj_j.name)

        for name in to_delete_floors:
            obj = bpy.data.objects.get(name)
            if obj:
                try:
                    for c in list(obj.users_collection):
                        c.objects.unlink(obj)
                    bpy.data.objects.remove(obj, do_unlink=True)
                    floor_count -= 1
                except Exception:
                    pass
                    
        # ──────────────────────────────────────────
        # 3.9 重なり合うビルの最終クリーンアップ (全道路 & ビル同士)
        #     ★ 配置チェックをすり抜けたものや、ダミービル同士の重なりをここで一掃
        # ──────────────────────────────────────────
        all_buildings = []
        for obj in col.objects:
            if obj.get("ge3_type") == "BUILDING":
                all_buildings.append(obj)
                
        to_delete_buildings = set()
        
        # 1. 道路（本物+ダミー）との重なりチェック
        for b_obj in all_buildings:
            bx = b_obj.location.x / SCALE
            bz = b_obj.location.y / SCALE
            
            # プレイヤー走行レーン（lane 0, 1, 2）との衝突判定 (55m保護)
            too_close = False
            for tx, tz in center_tile_positions:
                if (bx - tx)**2 + (bz - tz)**2 < ROAD_CLEAR_HALF_WIDTH**2:
                    too_close = True
                    break
            if too_close:
                to_delete_buildings.add(b_obj.name)
                continue
                
            # その他すべての道路およびダミー道路との衝突判定 (38m保護)
            for tx, tz in all_tile_positions:
                if (bx - tx)**2 + (bz - tz)**2 < BUILDING_ROAD_CLEAR_DIST**2:
                    too_close = True
                    break
            if too_close:
                to_delete_buildings.add(b_obj.name)
                continue
                
            for tx, tz in dummy_floor_positions:
                if (bx - tx)**2 + (bz - tz)**2 < BUILDING_ROAD_CLEAR_DIST**2:
                    too_close = True
                    break
            if too_close:
                to_delete_buildings.add(b_obj.name)
                continue

        # 2. ビル同士の重なりチェック (BUILDING_MIN_DIST = 30m)
        for i in range(len(all_buildings)):
            b1 = all_buildings[i]
            if b1.name in to_delete_buildings:
                continue
            b1_x = b1.location.x / SCALE
            b1_z = b1.location.y / SCALE
            for j in range(i + 1, len(all_buildings)):
                b2 = all_buildings[j]
                if b2.name in to_delete_buildings:
                    continue
                b2_x = b2.location.x / SCALE
                b2_z = b2.location.y / SCALE
                if (b1_x - b2_x)**2 + (b1_z - b2_z)**2 < BUILDING_MIN_DIST**2:
                    to_delete_buildings.add(b2.name)

        # 削除実行
        for b_name in to_delete_buildings:
            obj = bpy.data.objects.get(b_name)
            if obj:
                try:
                    bpy.data.objects.remove(obj, do_unlink=True)
                    building_count -= 1
                except Exception:
                    pass
                    
        # ──────────────────────────────────────────
        # 4. プレイヤー (PLAYER) の初期位置配置
        # ──────────────────────────────────────────
        create_player_obj("GE3_Player_Start", 0.0, -3.0, 0.0)
        
        # ──────────────────────────────────────────
        # 5. 地形を terrain.obj としてエクスポート
        # ──────────────────────────────────────────
        export_terrain(terrain_obj)
        
        msg = (f"Generated: {building_count} buildings (including dummy), "
               f"{floor_count} floors ({num_lanes} lanes), {dummy_floor_count} dummy floors, "
               f"{enemy_count} enemies. Terrain exported.")
        self.report({"INFO"}, msg)
        set_material_preview_mode()
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
        
        # 基本設定
        box_gen.label(text="--- Basic Settings ---")
        box_gen.prop(scene, "ge3_building_density", text="Density")
        box_gen.prop(scene, "ge3_max_floors", text="Max Floors")
        box_gen.prop(scene, "ge3_height_noise_scale", text="Noise Scale")
        box_gen.prop(scene, "ge3_height_noise_strength", text="Terrain Height")
        box_gen.prop(scene, "ge3_enemy_spawn_rate", text="Enemy Rate")
        
        # 区画制御
        box_gen.separator()
        box_gen.label(text="--- Section-based Route Settings ---")
        box_gen.prop(scene, "ge3_num_sections", text="Num Sections")
        box_gen.prop(scene, "ge3_section_length", text="Section Length")
        box_gen.prop(scene, "ge3_section_behaviors", text="Behaviors")
        box_gen.prop(scene, "ge3_curve_angle", text="Turn Angle (deg)")
        box_gen.prop(scene, "ge3_slope_height", text="Slope Height")
        
        box_gen.separator()
        box_gen.operator("ge3.generate_city", text="Generate Terrain & City", icon="MOD_OCEAN")

        # 手動編集支援ツール
        box_manual = layout.box()
        box_manual.label(text="Manual Edit Helper Tools", icon="TOOL_SETTINGS")
        
        # 車線整列
        box_manual.operator("ge3.align_lanes_to_center", text="Align Lanes to Center", icon="SNAP_GRID")

        # ★ ビル重なり除去ボタン
        box_manual.separator()
        box_manual.label(text="--- Cleanup ---")
        box_manual.operator(
            "ge3.remove_overlapping_buildings",
            text="Remove Overlapping Buildings",
            icon="X"
        )
        box_manual.operator(
            "ge3.remove_overlapping_floors",
            text="Remove Overlapping Floors",
            icon="X"
        )
        
        # 選択以降を方向転換
        box_manual.separator()
        box_manual.prop(scene, "ge3_manual_turn_angle", text="Turn Angle")
        box_manual.operator("ge3.turn_route_from_selected", text="Turn Route From Selected", icon="FORCE_CURVE")

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
        layout.label(text=f"Road lanes: {NUM_ROAD_LANES} (±{ROAD_WIDTH * (NUM_ROAD_LANES // 2):.0f}m)", icon="DRIVER_DISTANCE")



