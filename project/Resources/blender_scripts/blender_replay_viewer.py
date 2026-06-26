"""
blender_replay_viewer.py
========================
Blender リプレイビューア (修正版 v2)

【修正内容】
- プレイヤー・敵のモデルを実際の OBJ ファイルに変更
- 敵の alive フラグをキーフレームで正しく制御（非表示になる）
- 弾の表示を改善

【使い方】
1. ゲームで R キーを押して録画 → 再度 R キーで停止
2. このスクリプトを Blender Scripting ワークスペースで実行
3. Space キーでリプレイ再生！
"""

import bpy
import csv
import os
import math

# ──────────────────────────────────────────
# 設定
# ──────────────────────────────────────────
REPLAY_CSV_PATH  = r"C:\Users\k024g\Desktop\GE3&CG3\project\Resources\replay_frames.csv"
PLAYER_MODEL_OBJ = r"C:\Users\k024g\Desktop\GE3&CG3\project\Resources\Player2\Player.obj"
ENEMY_MODEL_OBJ  = r"C:\Users\k024g\Desktop\GE3&CG3\project\Resources\Player\player.obj"

SCALE       = 0.05   # ゲーム座標 → Blender座標
FPS         = 60     # ゲームの固定 60fps
FRAME_SKIP  = 2      # 1=全フレーム, 2=2フレームに1回 (ファイルサイズ・速度バランス)
MAX_ENEMIES = 10
# ──────────────────────────────────────────

OBJ_PLAYER = "RP_Player"
OBJ_ENEMY  = "RP_Enemy_{:02d}"
OBJ_BOSS   = "RP_Boss"


def gs2bl(gx, gy, gz):
    """ゲーム座標 → Blender座標 (X右,Y上,Z奥) → (X右,Y奥,Z上)"""
    return (gx * SCALE, gz * SCALE, gy * SCALE)


def delete_object_with_data(name):
    """オブジェクトと、それが使用しているメッシュデータなどをBlenderから完全に削除する"""
    objs_to_remove = []
    for obj in bpy.data.objects:
        if obj.name == name or obj.name.startswith(name + "."):
            objs_to_remove.append(obj)
            
    for obj in objs_to_remove:
        # 親子関係の解除
        obj.parent = None
        # メッシュデータの削除
        if obj.type == 'MESH':
            mesh = obj.data
            if mesh:
                try:
                    bpy.data.meshes.remove(mesh, do_unlink=True)
                except Exception:
                    pass
        # アニメーションデータのクリア
        if obj.animation_data:
            action = obj.animation_data.action
            if action:
                try:
                    bpy.data.actions.remove(action, do_unlink=True)
                except Exception:
                    pass
        # オブジェクトの削除
        try:
            bpy.data.objects.remove(obj, do_unlink=True)
        except Exception:
            pass


def import_or_fallback(filepath, name, fallback_shape="cone", fallback_size=1.0, color=(0.5,0.5,0.5,1.0)):
    """OBJをインポートして名前を付けて返す。失敗時はプリミティブ"""
    # 重複名も含めて既存オブジェクトを完全に削除
    delete_object_with_data(name)

    if os.path.exists(filepath):
        try:
            before = set(bpy.data.objects.keys())
            try:
                bpy.ops.wm.obj_import(filepath=filepath)
            except AttributeError:
                bpy.ops.import_scene.obj(filepath=filepath)

            after    = set(bpy.data.objects.keys())
            new_objs = [bpy.data.objects[n] for n in (after - before)]

            if new_objs:
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
                print(f"[Replay] Imported: {os.path.basename(filepath)} → {name}")
                return obj
        except Exception as e:
            print(f"[Replay] Import failed for {filepath}: {e}")

    # フォールバック
    bpy.ops.object.select_all(action="DESELECT")
    if fallback_shape == "cone":
        bpy.ops.mesh.primitive_cone_add(radius1=fallback_size, depth=fallback_size*2, location=(0,0,0))
    elif fallback_shape == "uvsphere":
        bpy.ops.mesh.primitive_uv_sphere_add(radius=fallback_size, location=(0,0,0))

    obj = bpy.context.active_object
    obj.name = name
    try:
        bpy.ops.object.origin_set(type='ORIGIN_GEOMETRY', center='MEDIAN')
    except Exception:
        pass

    obj = bpy.context.active_object
    obj.name = name
    # マテリアル
    mat = bpy.data.materials.new(f"Mat_{name}")
    mat.use_nodes = True
    bsdf = mat.node_tree.nodes.get("Principled BSDF")
    if bsdf:
        bsdf.inputs["Base Color"].default_value = color
        bsdf.inputs["Emission"].default_value = (*color[:3], 1.0)
        bsdf.inputs["Emission Strength"].default_value = 0.8
    if obj.data.materials:
        obj.data.materials[0] = mat
    else:
        obj.data.materials.append(mat)
    obj.select_set(False)
    print(f"[Replay] Created fallback primitive: {name}")
    return obj


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
    
    # プレイヤーの少し後方上方に配置 (プレイヤーのスケールが15.0なので、それに合わせた十分な距離)
    camera_obj.location = (0.0, -100.0, 45.0)  # X=0, Y=後方に-100m, Z=上方に45m
    camera_obj.rotation_euler = (math.radians(72), 0.0, 0.0)  # やや見下ろす角度 (72度)
    
    # プレイヤーに親子付け
    camera_obj.parent = player_obj
    print("[Replay] Camera set to follow RP_Player")


def set_linear_interpolation(obj):
    """アニメーションカーブを線形補間に設定"""
    if obj.animation_data and obj.animation_data.action:
        for fc in obj.animation_data.action.fcurves:
            for kp in fc.keyframe_points:
                kp.interpolation = "LINEAR"


def load_replay():
    """録画されている CSV ファイルの中で最も行数（＝時間）が長いものを探して読み込む"""
    import glob

    dir_path = os.path.dirname(REPLAY_CSV_PATH)
    candidates = glob.glob(os.path.join(dir_path, "replay_frames_*.csv"))
    legacy_path = os.path.join(dir_path, "replay_frames.csv")
    if os.path.exists(legacy_path):
        candidates.append(legacy_path)

    if not candidates:
        print(f"[Replay] ERROR: No replay files found in {dir_path}.")
        print("[Replay] Press R key in-game to start recording first!")
        return

    best_file = None
    max_rows = -1
    best_rows_data = []

    for file_path in candidates:
        try:
            with open(file_path, "r", encoding="utf-8") as f:
                reader = csv.DictReader(f)
                rows_data = list(reader)
                if len(rows_data) > max_rows:
                    max_rows = len(rows_data)
                    best_file = file_path
                    best_rows_data = rows_data
        except Exception as e:
            print(f"[Replay] Failed to read {file_path}: {e}")

    if not best_file or max_rows <= 0:
        print("[Replay] ERROR: All replay files are empty. Record a replay first.")
        return

    print(f"[Replay] Found {len(candidates)} replay file(s).")
    print(f"[Replay] Selecting the longest session:")
    print(f"         Path: {best_file}")
    print(f"         Frames: {max_rows} ({max_rows / FPS:.1f} sec)")

    rows = best_rows_data
    total_frames = len(rows)
    duration_sec = total_frames / FPS
    print(f"[Replay] {total_frames} frames ({duration_sec:.1f} sec)")

    # ── タイムライン設定 ──
    bpy.context.scene.frame_start = 0
    bpy.context.scene.frame_end   = total_frames
    bpy.context.scene.render.fps  = FPS

    # ── オブジェクト作成 ──
    print("[Replay] Creating/importing objects...")

    player_obj = import_or_fallback(
        PLAYER_MODEL_OBJ, OBJ_PLAYER,
        fallback_shape="cone", fallback_size=1.5, color=(0.2, 0.6, 1.0, 1.0)
    )
    # 大きなサイズで表示してハッキリと見えるように設定
    player_obj.scale = (15.0, 15.0, 15.0)

    boss_obj = import_or_fallback(
        r"C:\Users\k024g\Desktop\GE3&CG3\project\Resources\big Spider\big+Spider_low.obj",
        OBJ_BOSS,
        fallback_shape="uvsphere", fallback_size=5.0, color=(1.0, 0.1, 0.1, 1.0)
    )
    boss_obj.scale = (35.0, 35.0, 35.0)

    enemy_objs = []
    for i in range(MAX_ENEMIES):
        e_obj = import_or_fallback(
            ENEMY_MODEL_OBJ, OBJ_ENEMY.format(i),
            fallback_shape="uvsphere", fallback_size=0.8, color=(1.0, 0.5, 0.0, 1.0)
        )
        # 敵のサイズをプレイヤーと同等まで拡大して見やすくする
        e_obj.scale = (15.0, 15.0, 15.0)
        enemy_objs.append(e_obj)

    # ── キーフレーム生成 ──
    print("[Replay] Generating keyframes...")
    processed = 0

    for row_idx, row in enumerate(rows):
        frame_num = int(row["frame"])

        # FRAME_SKIP: n フレームに 1 回だけキーフレームを打つ
        if frame_num % FRAME_SKIP != 0:
            continue

        bpy.context.scene.frame_set(frame_num)

        # ── プレイヤー ──
        px  = float(row["px"])
        py  = float(row["py"])
        pz  = float(row["pz"])
        roll  = float(row.get("proll",  0.0))
        pitch = float(row.get("ppitch", 0.0))

        player_obj.location        = gs2bl(px, py, pz)
        player_obj.rotation_euler  = (pitch, roll, 0.0)
        player_obj.keyframe_insert(data_path="location",       frame=frame_num)
        player_obj.keyframe_insert(data_path="rotation_euler", frame=frame_num)

        # ── 敵 ──
        for i in range(MAX_ENEMIES):
            alive_key = f"e{i}_alive"
            if alive_key not in row:
                continue

            alive = int(row[alive_key]) != 0
            ex    = float(row[f"e{i}_x"])
            ey    = float(row[f"e{i}_y"])
            ez    = float(row[f"e{i}_z"])
            erz   = float(row.get(f"e{i}_rz", 0.0))

            enemy_objs[i].location       = gs2bl(ex, ey, ez)
            enemy_objs[i].rotation_euler = (0.0, erz, 0.0)
            # 非表示制御
            enemy_objs[i].hide_viewport  = not alive
            enemy_objs[i].hide_render    = not alive

            enemy_objs[i].keyframe_insert(data_path="location",       frame=frame_num)
            enemy_objs[i].keyframe_insert(data_path="rotation_euler", frame=frame_num)
            enemy_objs[i].keyframe_insert(data_path="hide_viewport",  frame=frame_num)
            enemy_objs[i].keyframe_insert(data_path="hide_render",    frame=frame_num)

        # ── ボス ──
        if "boss_active" in row:
            boss_active = int(row["boss_active"]) != 0
            bx = float(row.get("boss_x", 0.0))
            by = float(row.get("boss_y", 0.0))
            bz = float(row.get("boss_z", 0.0))

            boss_obj.location      = gs2bl(bx, by, bz)
            boss_obj.hide_viewport = not boss_active
            boss_obj.hide_render   = not boss_active

            boss_obj.keyframe_insert(data_path="location",      frame=frame_num)
            boss_obj.keyframe_insert(data_path="hide_viewport", frame=frame_num)
            boss_obj.keyframe_insert(data_path="hide_render",   frame=frame_num)

        processed += 1
        if processed % 200 == 0:
            pct = row_idx / len(rows) * 100
            print(f"  {pct:.0f}% ({row_idx}/{len(rows)} frames)")

    # ── 補間を線形に変更（ガタガタにならないように）──
    for obj in [player_obj, boss_obj] + enemy_objs:
        set_linear_interpolation(obj)

    # ── カメラ追従の設定 ──
    try:
        setup_camera_follow(player_obj)
    except Exception as e:
        print(f"[Replay] Failed to setup camera follow: {e}")

    # ── 先頭フレームに移動して再生準備 ──
    bpy.context.scene.frame_set(0)
    bpy.ops.screen.animation_cancel()

    print(f"\n[Replay] ✅ Done! {processed} keyframes set.")
    print(f"[Replay] Duration : {total_frames/FPS:.1f} sec ({total_frames} frames)")
    print(f"[Replay] Press [Space] to play the replay!")


# 実行
load_replay()
