import bpy
import os

def generate_text():
    # 既存のオブジェクトをすべて削除
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)

    # テキストオブジェクトを追加
    bpy.ops.object.text_add(location=(0, 0, 0))
    text_obj = bpy.context.active_object
    text_obj.data.body = "巣喰う街"

    # テキストの位置揃えを中央にする
    text_obj.data.align_x = 'CENTER'
    text_obj.data.align_y = 'CENTER'

    # 厚み（押し出し）を設定
    text_obj.data.extrude = 0.15

    # ベベル（面取り）を設定して立体感を出す
    text_obj.data.bevel_depth = 0.015
    text_obj.data.bevel_resolution = 4

    # X軸の周りに90度回転して起こす（直立させる）
    text_obj.rotation_euler[0] = 1.5707963  # 90度 (ラジアン)


    # 日本語フォントの設定（太めのMSゴシックを使用）
    font_path = "C:\\Windows\\Fonts\\msgothic.ttc"
    if os.path.exists(font_path):
        char_font = bpy.data.fonts.load(font_path)
        text_obj.data.font = char_font
    else:
        print("Warning: msgothic.ttc not found. Using default font.")

    # テキストをメッシュに変換
    bpy.ops.object.convert(target='MESH')

    # オブジェクト名とデータ名を「テキスト」に設定（元のモデル構造に合わせる）
    text_obj.name = "テキスト"
    if text_obj.data:
        text_obj.data.name = "テキスト"

    # UV展開（テクスチャ貼り付け用にスマートUV投影を適用）
    bpy.context.view_layer.objects.active = text_obj
    bpy.ops.object.mode_set(mode='EDIT')
    bpy.ops.mesh.select_all(action='SELECT')
    bpy.ops.uv.smart_project(angle_limit=66.0, island_margin=0.02)
    bpy.ops.object.mode_set(mode='OBJECT')

    # マテリアル設定（白色）
    mat = bpy.data.materials.new(name="Material")
    mat.use_nodes = False
    mat.diffuse_color = (1.0, 1.0, 1.0, 1.0)
    text_obj.data.materials.append(mat)

    # エクスポートパス
    output_path = r"c:\Users\k024g\Desktop\GE3&CG3\project\Resources\Title\Title.obj"
    output_dir = os.path.dirname(output_path)
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    # OBJエクスポート
    bpy.ops.wm.obj_export(
        filepath=output_path,
        export_selected_objects=True,
        export_materials=True,
        export_normals=True,
        export_uv=True,
        forward_axis='NEGATIVE_Z',
        up_axis='Y'
    )
    print("Exported Title.obj successfully to:", output_path)

if __name__ == "__main__":
    generate_text()
