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

SCALE       = 1.0   # ゲーム座標 → Blender座標 (レベルエディタと統一)
FPS         = 60     # ゲームの固定 60fps
FRAME_SKIP  = 2      # 1=全フレーム, 2=2フレームに1回 (ファイルサイズ・速度バランス)
MAX_ENEMIES = 10
MAX_BULLETS = 20
# ──────────────────────────────────────────

OBJ_PLAYER = "RP_Player"
OBJ_ENEMY  = "RP_Enemy_{:02d}"
OBJ_BOSS   = "RP_Boss"
OBJ_BULLET = "RP_Bullet_{:02d}"


def gs2bl(gx, gy, gz):
    """ゲーム座標 → Blender座標 (X右,Y上,Z奥) → (X右,Y奥,Z上)"""
    return (gx * SCALE, gz * SCALE, gy * SCALE)


def force_object_mode():
    """実行モードをオブジェクトモードに変更（エディットモード等の干渉を防ぐ）"""
    if bpy.context.active_object and bpy.context.active_object.mode != 'OBJECT':
        try:
            bpy.ops.object.mode_set(mode='OBJECT')
        except Exception as e:
            print(f"[Replay] Failed to set object mode: {e}")


def cleanup_object_transforms(obj, scale_val=None):
    """オブジェクトの親子関係を解除し、制約を完全削除、スケールを適用し、アクションデータも解除する"""
    if not obj:
        return
    # 親子関係の解除
    try:
        obj.parent = None
    except Exception:
        pass
    # 制約の完全クリア
    if hasattr(obj, "constraints") and obj.constraints:
        for const in list(obj.constraints):
            try:
                obj.constraints.remove(const)
            except Exception:
                pass
    # スケールの設定
    if scale_val is not None:
        try:
            obj.scale = (scale_val, scale_val, scale_val)
        except Exception:
            pass
    # スムーズシェーディングの適用（ガビガビ感の解消）
    try:
        if obj.type == 'MESH' and obj.data:
            for poly in obj.data.polygons:
                poly.use_smooth = True
            obj.data.update()
    except Exception:
        pass
    # アニメーションデータの解除（古いキーフレームの干渉防止）
    if obj.animation_data:
        try:
            obj.animation_data.action = None
        except Exception:
            pass


def delete_object_with_data(name):
    """オブジェクトと、それが使用しているメッシュデータなどをBlenderから完全に削除する"""
    # 削除対象のオブジェクト名をリストアップ（イテレート中のReferenceErrorを防ぐため名前のリストにする）
    obj_names = [obj.name for obj in bpy.data.objects if obj.name == name or obj.name.startswith(name + ".")]
            
    for n in obj_names:
        if n not in bpy.data.objects:
            continue
        obj = bpy.data.objects[n]
        
        # メッシュの参照を保持
        mesh = None
        try:
            if obj.type == 'MESH':
                mesh = obj.data
        except Exception:
            pass
            
        # アクションの参照を保持
        action = None
        try:
            if obj.animation_data:
                action = obj.animation_data.action
        except Exception:
            pass
            
        # 親子関係の解除
        try:
            obj.parent = None
        except Exception:
            pass
            
        # オブジェクトの削除
        try:
            bpy.data.objects.remove(obj, do_unlink=True)
        except Exception:
            pass
            
        # メッシュの削除
        if mesh:
            try:
                if mesh.name in bpy.data.meshes:
                    bpy.data.meshes.remove(mesh, do_unlink=True)
            except Exception:
                pass
                
        # アクションの削除
        if action:
            try:
                if action.name in bpy.data.actions:
                    bpy.data.actions.remove(action, do_unlink=True)
            except Exception:
                pass


def import_or_fallback(filepath, name, fallback_shape="cone", fallback_size=1.0, color=(0.5,0.5,0.5,1.0)):
    """OBJをインポートして名前を付けて返す。失敗時はプリミティブ"""
    # 実行モードをオブジェクトモードに変更（エディットモード等の干渉を防ぐ）
    force_object_mode()

    # 重複名も含めて既存オブジェクトを完全に削除
    delete_object_with_data(name)

    # 確実にすべてのオブジェクトの選択を解除する
    for obj in list(bpy.data.objects):
        try:
            obj.select_set(False)
        except Exception:
            pass
    if bpy.context.view_layer.objects.active:
        try:
            bpy.context.view_layer.objects.active = None
        except Exception:
            pass

    if os.path.exists(filepath):
        try:
            before = set(bpy.data.objects.keys())
            try:
                bpy.ops.wm.obj_import(filepath=filepath)
            except AttributeError:
                bpy.ops.import_scene.obj(filepath=filepath)

            after    = set(bpy.data.objects.keys())
            new_objs = [bpy.data.objects[n] for n in (after - before) if n in bpy.data.objects]

            # 新しくインポートされたオブジェクトからメッシュのみを抽出
            mesh_objs = [o for o in new_objs if o.type == 'MESH']

            if mesh_objs:
                # メッシュオブジェクトのみを選択
                for o in mesh_objs:
                    o.select_set(True)
                
                # 最初のエレメントをアクティブにする
                bpy.context.view_layer.objects.active = mesh_objs[0]
                
                # 複数あれば結合する
                if len(mesh_objs) > 1:
                    bpy.ops.object.join()
                
                # 結合後のオブジェクトを取得してリネーム
                obj = bpy.context.active_object
                obj.name = name
                
                # 原点をモデルの幾何学的中心に設定して位置ズレを防ぐ
                try:
                    bpy.ops.object.origin_set(type='ORIGIN_GEOMETRY', center='MEDIAN')
                except Exception:
                    pass
                
                # メッシュ以外の不要なオブジェクト（Emptyなど）があれば削除する
                for o in new_objs:
                    if o != obj and o.name in bpy.data.objects:
                        try:
                            bpy.data.objects.remove(o, do_unlink=True)
                        except Exception:
                            pass
                
                obj.select_set(False)
                print(f"[Replay] Imported: {os.path.basename(filepath)} → {name}")
                return obj
        except Exception as e:
            print(f"[Replay] Import failed for {filepath}: {e}")

    # フォールバック
    # 確実にすべてのオブジェクトの選択を解除する
    for obj in list(bpy.data.objects):
        try:
            obj.select_set(False)
        except Exception:
            pass
    if bpy.context.view_layer.objects.active:
        try:
            bpy.context.view_layer.objects.active = None
        except Exception:
            pass

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
    # 実行モードをオブジェクトモードに変更（エディットモード等の干渉を防ぐ）
    force_object_mode()

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

    # A. 既存の配置用オブジェクト（GE3_Player_Start, GE3_Enemy_xx）を非表示にする
    layout_player = bpy.data.objects.get("GE3_Player_Start")
    if layout_player:
        layout_player.hide_viewport = True
        layout_player.hide_render = True
        if layout_player.animation_data:
            layout_player.animation_data.action = None

    for i in range(MAX_ENEMIES):
        layout_enemy = bpy.data.objects.get(f"GE3_Enemy_{i:02d}")
        if layout_enemy:
            layout_enemy.hide_viewport = True
            layout_enemy.hide_render = True
            if layout_enemy.animation_data:
                layout_enemy.animation_data.action = None

    # B. リプレイ用オブジェクトの取得・生成
    # 1. プレイヤー
    player_obj = bpy.data.objects.get(OBJ_PLAYER)
    if not player_obj:
        player_obj = import_or_fallback(
            PLAYER_MODEL_OBJ, OBJ_PLAYER,
            fallback_shape="cone", fallback_size=1.5, color=(0.2, 0.6, 1.0, 1.0)
        )
    cleanup_object_transforms(player_obj, scale_val=15.0)

    # 2. ボス (既存の RP_Boss があれば使用、なければ胴体と足をインポートして結合)
    boss_obj = bpy.data.objects.get(OBJ_BOSS)
    if not boss_obj:
        # 胴体をインポート (低解像度版)
        boss_body = import_or_fallback(
            r"C:\Users\k024g\Desktop\GE3&CG3\project\Resources\big Spider\big+Spider_low.obj",
            "RP_Boss_Body",
            fallback_shape="uvsphere", fallback_size=5.0, color=(1.0, 0.1, 0.1, 1.0)
        )
        # 足をインポート (低解像度版でインポート確実化)
        boss_legs = import_or_fallback(
            r"C:\Users\k024g\Desktop\GE3&CG3\project\Resources\big Spider\big+spider+arm_low.obj",
            "RP_Boss_Legs",
            fallback_shape="cone", fallback_size=4.0, color=(0.8, 0.1, 0.1, 1.0)
        )

        # 胴体と足をそれぞれ原点リセット後に結合して RP_Boss にする
        for part in [boss_body, boss_legs]:
            if part:
                try:
                    for o in list(bpy.data.objects):
                        o.select_set(False)
                    bpy.context.view_layer.objects.active = part
                    part.select_set(True)
                    bpy.ops.object.origin_set(type='ORIGIN_GEOMETRY', center='MEDIAN')
                    part.select_set(False)
                except Exception as e:
                    print(f"[Replay] origin_set failed for {part.name}: {e}")

        if boss_body and boss_legs:
            for o in list(bpy.data.objects):
                try:
                    o.select_set(False)
                except Exception:
                    pass
            boss_body.select_set(True)
            boss_legs.select_set(True)
            bpy.context.view_layer.objects.active = boss_body
            bpy.ops.object.join()
            boss_obj = bpy.context.active_object
            boss_obj.name = OBJ_BOSS
        else:
            boss_obj = boss_body if boss_body else boss_legs
            if boss_obj:
                boss_obj.name = OBJ_BOSS

    cleanup_object_transforms(boss_obj, scale_val=35.0)

    # ボスの初期姿勢をメッシュ頂点に焼き込む（Transform Apply）
    # big+Spider_low.obj は OBJ インポート後にY軸方向に横倒しになるため
    # X軸+90度で直立させてから transform_apply でメッシュに適用する。
    # これにより、キーフレームループでは boss_ry だけを rotation_euler.z に設定すればよい。
    BOSS_PITCH_OFFSET = 0.0
    BOSS_YAW_OFFSET   = 0.0
    try:
        for o in list(bpy.data.objects):
            try:
                o.select_set(False)
            except Exception:
                pass
        bpy.context.view_layer.objects.active = boss_obj
        boss_obj.select_set(True)
        boss_obj.rotation_euler = (math.pi / 2, 0.0, math.pi / 2)
        bpy.ops.object.transform_apply(location=False, rotation=True, scale=False)
        boss_obj.rotation_euler = (0.0, 0.0, 0.0)
        boss_obj.select_set(False)
        print("[Replay] Boss initial rotation baked into mesh.")
    except Exception as e:
        print(f"[Replay] Boss transform_apply failed: {e}. Using offset constants.")
        BOSS_PITCH_OFFSET = math.pi / 2
        BOSS_YAW_OFFSET   = math.pi

    # 3. 敵
    enemy_objs = []
    for i in range(MAX_ENEMIES):
        e_name = OBJ_ENEMY.format(i)
        e_obj = bpy.data.objects.get(e_name)
        if not e_obj:
            e_obj = import_or_fallback(
                ENEMY_MODEL_OBJ, e_name,
                fallback_shape="uvsphere", fallback_size=0.8, color=(1.0, 0.5, 0.0, 1.0)
            )
        cleanup_object_transforms(e_obj, scale_val=15.0)
        enemy_objs.append(e_obj)

    # 4. 弾（最大20発）
    bullet_objs = []
    for i in range(MAX_BULLETS):
        b_name = OBJ_BULLET.format(i)
        b_obj = bpy.data.objects.get(b_name)
        if not b_obj:
            bpy.ops.mesh.primitive_uv_sphere_add(radius=8.0, location=(0.0, 0.0, -10000.0 * SCALE))
            b_obj = bpy.context.active_object
            b_obj.name = b_name
            # 発光する黄色のマテリアルを適用
            mat = bpy.data.materials.new(f"Mat_{b_name}")
            mat.use_nodes = True
            bsdf = mat.node_tree.nodes.get("Principled BSDF")
            if bsdf:
                bsdf.inputs["Base Color"].default_value = (1.0, 1.0, 0.0, 1.0)
                bsdf.inputs["Emission"].default_value = (1.0, 1.0, 0.0, 1.0)
                bsdf.inputs["Emission Strength"].default_value = 1.0
            if b_obj.data.materials:
                b_obj.data.materials[0] = mat
            else:
                b_obj.data.materials.append(mat)
        cleanup_object_transforms(b_obj, scale_val=1.0)
        bullet_objs.append(b_obj)

    # 初期状態でオブジェクトを非表示（奈落の底）にしておく (合体防止)
    if boss_obj:
        boss_obj.location = (0.0, 0.0, -10000.0 * SCALE)
        boss_obj.hide_viewport = True
        boss_obj.hide_render = True
    for e_obj in enemy_objs:
        if e_obj:
            e_obj.location = (0.0, 0.0, -10000.0 * SCALE)
            e_obj.hide_viewport = True
            e_obj.hide_render = True
    for b_obj in bullet_objs:
        if b_obj:
            b_obj.location = (0.0, 0.0, -10000.0 * SCALE)
            b_obj.hide_viewport = True
            b_obj.hide_render = True

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

        # プレイヤーモデルは初期姿勢が上向き（ConeやPlayer.obj）かつキャノピーが手前を向いているため、
        # 機首を前（+Y）、キャノピーを上（+Z）に向かせるためにピッチを逆方向に倒し、ロールを180度反転させる
        pitch_offset = -math.pi / 2
        roll_offset  = math.pi

        player_obj.location        = gs2bl(px, py, pz)
        player_obj.rotation_euler  = (pitch + pitch_offset, roll + roll_offset, 0.0)
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

            if alive:
                enemy_objs[i].location       = gs2bl(ex, ey, ez)
                enemy_objs[i].rotation_euler = (0.0, 0.0, erz)
                enemy_objs[i].hide_viewport  = False
                enemy_objs[i].hide_render    = False
            else:
                # 非表示のときは奈落の底に配置して絶対に見えないようにする (合体現象の防止)
                enemy_objs[i].location       = (0.0, 0.0, -10000.0 * SCALE)
                enemy_objs[i].rotation_euler = (0.0, 0.0, 0.0)
                enemy_objs[i].hide_viewport  = True
                enemy_objs[i].hide_render    = True

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
            bry = float(row.get("boss_ry", 0.0))

            if boss_active:
                boss_obj.location      = gs2bl(bx, by, bz)
                # BOSS_PITCH_OFFSET: 横倒しを直立させる 、BOSS_YAW_OFFSET: 前後反転を修正、bry: 対プレイヤー方向
                boss_obj.rotation_euler = (BOSS_PITCH_OFFSET, 0.0, bry + BOSS_YAW_OFFSET)
                boss_obj.hide_viewport = False
                boss_obj.hide_render   = False
            else:
                # 非表示のときは奈落の底に配置して絶対に見えないようにする (合体現象の防止)
                boss_obj.location      = (0.0, 0.0, -10000.0 * SCALE)
                boss_obj.rotation_euler = (BOSS_PITCH_OFFSET, 0.0, BOSS_YAW_OFFSET)
                boss_obj.hide_viewport = True
                boss_obj.hide_render   = True

            boss_obj.keyframe_insert(data_path="location",      frame=frame_num)
            boss_obj.keyframe_insert(data_path="rotation_euler", frame=frame_num)
            boss_obj.keyframe_insert(data_path="hide_viewport", frame=frame_num)
            boss_obj.keyframe_insert(data_path="hide_render",   frame=frame_num)

        # ── 弾 ──
        for i in range(MAX_BULLETS):
            alive_key = f"b{i}_alive"
            if alive_key not in row:
                continue

            balive = int(row[alive_key]) != 0
            blx   = float(row[f"b{i}_x"])
            bly   = float(row[f"b{i}_y"])
            blz   = float(row[f"b{i}_z"])

            if balive:
                bullet_objs[i].location      = gs2bl(blx, bly, blz)
                bullet_objs[i].hide_viewport = False
                bullet_objs[i].hide_render   = False
            else:
                bullet_objs[i].location      = (0.0, 0.0, -10000.0 * SCALE)
                bullet_objs[i].hide_viewport = True
                bullet_objs[i].hide_render   = True

            bullet_objs[i].keyframe_insert(data_path="location",      frame=frame_num)
            bullet_objs[i].keyframe_insert(data_path="hide_viewport", frame=frame_num)
            bullet_objs[i].keyframe_insert(data_path="hide_render",   frame=frame_num)

        processed += 1
        if processed % 200 == 0:
            pct = row_idx / len(rows) * 100
            print(f"  {pct:.0f}% ({row_idx}/{len(rows)} frames)")

    # ── 補間を線形に変更（ガタガタにならないように）──
    for obj in [player_obj, boss_obj] + enemy_objs + bullet_objs:
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
