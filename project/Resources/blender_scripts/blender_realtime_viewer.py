"""
blender_realtime_viewer.py
==========================
Blender リアルタイム同期ビューア (修正版)

【修正内容 v2】
- プレイヤーモデルを実際の Player.obj / player.obj に変更
- ビューポート再描画 (tag_redraw) を追加 → 敵が動くようになった
- 弾を固定インデックスで追跡 → まっすぐ飛ぶようになった
- 敵のスケールを修正（より見やすいサイズに）

【使い方】
1. ゲームを起動・プレイ開始（Fighter モード）
2. Blender → Scripting ワークスペース
3. このスクリプトを開いて [Run Script]
4. 3Dビューで自機・敵・ボスが動く！

【パス設定】
スクリプト上部の各パス変数を自分の環境に合わせて変更してください。
"""

import bpy
import json
import math
import os

# ──────────────────────────────────────────
# 設定 (必要ならここのパスを変更)
# ──────────────────────────────────────────
GAME_STATE_PATH  = r"C:\Users\k024g\Desktop\GE3&CG3\project\Resources\game_state.json"
PLAYER_MODEL_OBJ = r"C:\Users\k024g\Desktop\GE3&CG3\project\Resources\Player2\Player.obj"
ENEMY_MODEL_OBJ  = r"C:\Users\k024g\Desktop\GE3&CG3\project\Resources\Player\player.obj"

UPDATE_INTERVAL = 0.05   # 更新間隔(秒) ≈ 20fps
SCALE           = 0.05   # ゲーム座標 → Blender座標のスケール (小さめに)
MAX_BULLETS     = 20     # 追跡する弾の最大数 (C++側と合わせる)
MAX_ENEMIES     = 10
# ──────────────────────────────────────────

# コレクション名
COLL_NAME = "GE3_Realtime"

# オブジェクト名
OBJ_PLAYER        = "RT_Player"
OBJ_BOSS          = "RT_Boss"
OBJ_ENEMY_PREFIX  = "RT_Enemy_"
OBJ_BULLET_PREFIX = "RT_Bullet_"


# ─── ヘルパー ────────────────────────────────────────────

def get_or_create_coll():
    if COLL_NAME not in bpy.data.collections:
        col = bpy.data.collections.new(COLL_NAME)
        bpy.context.scene.collection.children.link(col)
    return bpy.data.collections[COLL_NAME]


def link_to_coll(obj):
    col = get_or_create_coll()
    if obj.name not in col.objects:
        col.objects.link(obj)
    if obj.name in bpy.context.scene.collection.objects:
        bpy.context.scene.collection.objects.unlink(obj)


def make_material(name, color=(0.5, 0.5, 0.5, 1.0), emission_strength=0.3):
    mat = bpy.data.materials.get(f"RTMat_{name}")
    if mat is None:
        mat = bpy.data.materials.new(f"RTMat_{name}")
        mat.use_nodes = True
        bsdf = mat.node_tree.nodes.get("Principled BSDF")
        if bsdf:
            bsdf.inputs["Base Color"].default_value = color
            bsdf.inputs["Emission"].default_value   = (*color[:3], 1.0)
            bsdf.inputs["Emission Strength"].default_value = emission_strength
    return mat


def delete_object_with_data(name):
    """オブジェクトと、それが使用しているメッシュデータなどをBlenderから完全に削除する"""
    objs_to_remove = []
    for obj in bpy.data.objects:
        if obj.name == name or obj.name.startswith(name + "."):
            objs_to_remove.append(obj)
            
    for obj in objs_to_remove:
        obj.parent = None
        if obj.type == 'MESH':
            mesh = obj.data
            if mesh:
                try:
                    bpy.data.meshes.remove(mesh, do_unlink=True)
                except Exception:
                    pass
        if obj.animation_data:
            action = obj.animation_data.action
            if action:
                try:
                    bpy.data.actions.remove(action, do_unlink=True)
                except Exception:
                    pass
        try:
            bpy.data.objects.remove(obj, do_unlink=True)
        except Exception:
            pass


def import_obj_model(filepath, name, fallback_shape="cone", fallback_size=1.0, fallback_color=(0.5,0.5,0.5,1.0)):
    """OBJファイルをインポートし、全メッシュを結合して返す。失敗時はプリミティブを返す"""
    # 重複名も含めて既存オブジェクトを完全に削除
    delete_object_with_data(name)

    if os.path.exists(filepath):
        try:
            # インポート前の全オブジェクト名を記録
            before = set(bpy.data.objects.keys())

            # Blender 4.x / 3.x 両対応
            try:
                bpy.ops.wm.obj_import(filepath=filepath)
            except AttributeError:
                bpy.ops.import_scene.obj(filepath=filepath)

            # 新しく追加されたオブジェクトを取得
            after  = set(bpy.data.objects.keys())
            new_objs = [bpy.data.objects[n] for n in (after - before)]

            if not new_objs:
                raise RuntimeError("No objects imported")

            # 全インポートオブジェクトを選択して結合
            bpy.ops.object.select_all(action="DESELECT")
            for o in new_objs:
                o.select_set(True)
            bpy.context.view_layer.objects.active = new_objs[0]
            if len(new_objs) > 1:
                bpy.ops.object.join()

            obj = bpy.context.active_object
            obj.name = name
            
            # 原点をモデルの幾何学的中心に設定して位置ズレを防ぐ
            try:
                bpy.ops.object.origin_set(type='ORIGIN_GEOMETRY', center='MEDIAN')
            except Exception:
                pass
                
            obj.select_set(False)
            return obj

        except Exception as e:
            print(f"[RT Viewer] OBJ import failed ({filepath}): {e}")
            print("[RT Viewer] Falling back to primitive shape.")

    # フォールバック: プリミティブ形状
    bpy.ops.object.select_all(action="DESELECT")
    if fallback_shape == "cone":
        bpy.ops.mesh.primitive_cone_add(radius1=fallback_size, depth=fallback_size*2, location=(0,0,-9999))
    elif fallback_shape == "uvsphere":
        bpy.ops.mesh.primitive_uv_sphere_add(radius=fallback_size, location=(0,0,-9999))
    elif fallback_shape == "cube":
        bpy.ops.mesh.primitive_cube_add(size=fallback_size, location=(0,0,-9999))

    obj = bpy.context.active_object
    obj.name = name
    try:
        bpy.ops.object.origin_set(type='ORIGIN_GEOMETRY', center='MEDIAN')
    except Exception:
        pass
        
    mat = make_material(name, fallback_color)
    if obj.data.materials:
        obj.data.materials[0] = mat
    else:
        obj.data.materials.append(mat)
    return obj


def set_pos(obj, gx, gy, gz, visible=True):
    """ゲーム座標(X右,Y上,Z奥)→Blender座標(X右,Y奥,Z上)変換"""
    obj.location = (gx * SCALE, gz * SCALE, gy * SCALE)
    obj.hide_viewport = not visible
    obj.hide_render   = not visible


def set_rot(obj, rx, ry, rz):
    """ゲーム回転(ラジアン)→Blender回転変換"""
    # ゲーム: X=ピッチ, Y=ヨー, Z=ロール
    # Blender: X=ピッチ, Y=ロール, Z=ヨー (座標系の入れ替え)
    obj.rotation_euler = (rx, rz, ry)


def force_redraw():
    """3Dビューポートを強制的に再描画する（これがないと動いて見えない！）"""
    for window in bpy.context.window_manager.windows:
        for area in window.screen.areas:
            if area.type == 'VIEW_3D':
                area.tag_redraw()


def set_enemy_color(obj, state_id):
    """敵のステートに応じてマテリアル色を変更"""
    mat = obj.data.materials[0] if obj.data.materials else None
    if not mat or not mat.use_nodes:
        return
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if not bsdf:
        return
    # state: 0=SideWait(灰), 1=Appear(黄緑), 2=Wander(オレンジ), 3=Dive(赤)
    colors = {
        0: (0.5, 0.5, 0.5, 1.0),
        1: (0.6, 1.0, 0.2, 1.0),
        2: (1.0, 0.6, 0.0, 1.0),
        3: (1.0, 0.1, 0.1, 1.0),
    }
    c = colors.get(state_id, (1.0, 0.5, 0.0, 1.0))
    bsdf.inputs["Base Color"].default_value = c
    bsdf.inputs["Emission"].default_value   = (*c[:3], 1.0)


def setup_camera_follow(player_obj):
    """Blenderのカメラをプレイヤーの後方に配置して親子付けし、追従させる"""
    # 既存のカメラを探す
    camera_obj = bpy.data.objects.get("Camera")
    if not camera_obj:
        # なければ作成
        bpy.ops.object.camera_add(location=(0, -30, 15))
        camera_obj = bpy.context.active_object
        camera_obj.name = "Camera"
        
    # 親子関係を一度解除
    camera_obj.parent = None
    
    # プレイヤーの少し後方上方に配置
    camera_obj.location = (0.0, -100.0, 45.0)  # X=0, Y=後方に-100m, Z=上方に45m
    camera_obj.rotation_euler = (math.radians(72), 0.0, 0.0)  # やや見下ろす角度 (72度)
    
    # プレイヤーに親子付け
    camera_obj.parent = player_obj
    print("[RT Viewer] Camera set to follow RT_Player")


# ─── シーン初期化 ────────────────────────────────────────────

_initialized = False

def setup_scene():
    """シーンに同期用オブジェクトを準備する（初回のみ）"""
    global _initialized
    if _initialized:
        return

    print("[RT Viewer] Setting up scene objects...")

    col = get_or_create_coll()

    # ── プレイヤー ──
    # Player.obj は巨大（90MB）なのでインポートに時間がかかる場合がある
    # 時間がかかりすぎる場合は player.obj (軽量版) を使用
    print("[RT Viewer] Importing player model...")
    player_obj = import_obj_model(
        PLAYER_MODEL_OBJ,
        OBJ_PLAYER,
        fallback_shape="cone",
        fallback_size=1.5,
        fallback_color=(0.2, 0.5, 1.0, 1.0)
    )
    # 大きなサイズで表示してハッキリと見えるように設定
    player_obj.scale = (15.0, 15.0, 15.0)
    link_to_coll(player_obj)

    # ── ボス ──
    boss_obj = import_obj_model(
        r"C:\Users\k024g\Desktop\GE3&CG3\project\Resources\big Spider\big+Spider_low.obj",
        OBJ_BOSS,
        fallback_shape="uvsphere",
        fallback_size=5.0,
        fallback_color=(1.0, 0.1, 0.1, 1.0)
    )
    boss_obj.scale = (35.0, 35.0, 35.0)
    link_to_coll(boss_obj)

    # ── 敵10体 (軽量な player.obj を使用) ──
    print("[RT Viewer] Creating enemy objects...")
    for i in range(MAX_ENEMIES):
        name = f"{OBJ_ENEMY_PREFIX}{i:02d}"
        if name not in bpy.data.objects:
            enemy_obj = import_obj_model(
                ENEMY_MODEL_OBJ,
                name,
                fallback_shape="uvsphere",
                fallback_size=0.8,
                fallback_color=(1.0, 0.5, 0.0, 1.0)
            )
            # 敵のサイズをプレイヤーと同等まで拡大して見やすくする
            enemy_obj.scale = (15.0, 15.0, 15.0)
            link_to_coll(enemy_obj)

    # ── 弾（小さな球、20発分）──
    print("[RT Viewer] Creating bullet objects...")
    for i in range(MAX_BULLETS):
        name = f"{OBJ_BULLET_PREFIX}{i:02d}"
        if name not in bpy.data.objects:
            bpy.ops.mesh.primitive_uv_sphere_add(radius=0.3, location=(0,0,-9999))
            blt = bpy.context.active_object
            blt.name = name
            blt.hide_viewport = True
            mat = make_material(f"Bullet_{i}", (1.0, 1.0, 0.0, 1.0), emission_strength=1.0)
            if blt.data.materials:
                blt.data.materials[0] = mat
            else:
                blt.data.materials.append(mat)
            link_to_coll(blt)

    # ── カメラ追従の設定 ──
    try:
        setup_camera_follow(player_obj)
    except Exception as e:
        print(f"[RT Viewer] Failed to setup camera follow: {e}")

    _initialized = True
    print("[RT Viewer] Scene setup complete!")
    print(f"  Collection  : {COLL_NAME}")
    print(f"  Player      : {OBJ_PLAYER}")
    print(f"  Boss        : {OBJ_BOSS}")
    print(f"  Enemies     : {MAX_ENEMIES} objects")
    print(f"  Bullets     : {MAX_BULLETS} objects")


# ─── メイン更新関数 ──────────────────────────────────────────

def update_from_game_state():
    """0.05秒ごとに呼ばれ、game_state.jsonを読んでBlenderを更新する"""

    if not os.path.exists(GAME_STATE_PATH):
        return UPDATE_INTERVAL

    try:
        with open(GAME_STATE_PATH, "r", encoding="utf-8") as f:
            content = f.read()
        if not content.strip():
            return UPDATE_INTERVAL
        state = json.loads(content)
    except Exception:
        return UPDATE_INTERVAL

    # ── プレイヤー更新 ──
    p = state.get("player")
    if p:
        player_obj = bpy.data.objects.get(OBJ_PLAYER)
        if player_obj:
            set_pos(player_obj, p["x"], p["y"], p["z"], visible=True)
            set_rot(player_obj, p.get("pitch", 0.0), 0.0, p.get("roll", 0.0))

    # ── 敵の更新 ──
    enemies = state.get("enemies", [])
    for i, e in enumerate(enemies[:MAX_ENEMIES]):
        name = f"{OBJ_ENEMY_PREFIX}{i:02d}"
        enemy_obj = bpy.data.objects.get(name)
        if not enemy_obj:
            continue

        alive = e.get("alive", False)
        set_pos(enemy_obj, e["x"], e["y"], e["z"], visible=alive)
        if alive:
            set_rot(enemy_obj, e.get("rx", 0.0), e.get("ry", 0.0), e.get("rz", 0.0))
            set_enemy_color(enemy_obj, e.get("state", 0))

    # ── ボス更新 ──
    b = state.get("boss")
    if b:
        boss_obj = bpy.data.objects.get(OBJ_BOSS)
        if boss_obj:
            active  = b.get("active",  False)
            visible = b.get("visible", True)
            set_pos(boss_obj, b["x"], b["y"], b["z"], visible=(active and visible))
            if active:
                ry_deg = b.get("rot_y", 0.0)
                set_rot(boss_obj, 0.0, math.radians(ry_deg), 0.0)
                # HPに応じてマテリアル色変化
                hp_ratio = b["hp"] / max(b.get("maxhp", 1), 1)
                mat = boss_obj.data.materials[0] if boss_obj.data.materials else None
                if mat and mat.use_nodes:
                    bsdf = mat.node_tree.nodes.get("Principled BSDF")
                    if bsdf:
                        bsdf.inputs["Base Color"].default_value = (1.0, hp_ratio * 0.3, hp_ratio * 0.3, 1.0)

    # ── 弾の更新（固定インデックスで追跡 → まっすぐ飛ぶ！）──
    bullets = state.get("bullets", [])
    for i in range(MAX_BULLETS):
        name = f"{OBJ_BULLET_PREFIX}{i:02d}"
        blt_obj = bpy.data.objects.get(name)
        if not blt_obj:
            continue

        if i < len(bullets):
            bt    = bullets[i]
            alive = bt.get("alive", False)
            set_pos(blt_obj, bt["x"], bt["y"], bt["z"], visible=alive)
        else:
            blt_obj.hide_viewport = True
            blt_obj.hide_render   = True

    # ── ビューポート強制再描画（これがないと敵が動いて見えない！）──
    force_redraw()

    return UPDATE_INTERVAL


# ─── タイマー登録 / 解除 ────────────────────────────────────────

def start():
    """同期を開始する"""
    setup_scene()
    if not bpy.app.timers.is_registered(update_from_game_state):
        bpy.app.timers.register(update_from_game_state, first_interval=0.1)
    print(f"[RT Viewer] Started! Watching: {GAME_STATE_PATH}")
    print("[RT Viewer] To stop, run: bpy.app.timers.unregister(update_from_game_state)")


def stop():
    """同期を停止する"""
    if bpy.app.timers.is_registered(update_from_game_state):
        bpy.app.timers.unregister(update_from_game_state)
    print("[RT Viewer] Stopped.")


# スクリプト実行時に自動で開始
start()
