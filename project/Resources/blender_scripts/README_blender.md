# Blender 連携システム 使い方ガイド

## 📁 スクリプト一覧

`Resources/blender_scripts/` フォルダに以下の4本があります：

| スクリプト | 機能 |
|-----------|------|
| `blender_realtime_viewer.py` | **リアルタイム同期** - 自機・敵・ボス・弾をBlenderで3D表示 |
| `blender_replay_viewer.py` | **リプレイビューア** - プレイをBlenderアニメで再生 |
| `blender_level_editor.py` | **レベルエディタ** - Blenderで建物・敵配置を編集してゲームに反映 |
| `blender_param_editor.py` | **パラメータエディタ** - Blenderスライダーでゲームパラメータを調整 |

---

## 🎮 1. リアルタイム同期ビューア

**「ゲームが動いている様子をBlenderで3D表示する」**

### 使い方
1. ゲームを起動してプレイ開始
2. Blenderを開く → **Scripting** ワークスペース
3. `blender_realtime_viewer.py` を開いて **[Run Script]**
4. Blenderの3Dビューで自機・敵・ボス・弾が動く！

### 表示オブジェクト
- 🔵 **青いコーン** = プレイヤー (ブースト中は軌道が変わる)
- 🟠 **オレンジ球** = 敵 (Dive中は赤、Wander中は明るいオレンジ)
- 🔴 **赤い大球** = ボス (HPが減ると色が変化)
- 🟡 **小さい黄球** = 弾

### 停止方法
Blender の Python コンソールで：
```python
bpy.app.timers.unregister(update_from_game_state)
```

---

## 🎬 2. リプレイビューア

**「プレイを録画してBlenderのタイムラインで再生する」**

### 使い方
1. ゲームを起動
2. **R キー** を押して録画開始（ImGuiの[REC] Start Replayボタンでも可）
3. プレイ！（敵との戦闘・ボス戦など）
4. **R キー** で録画停止
5. Blenderのスクリプトエディタで `blender_replay_viewer.py` を実行
6. **Space キー** でリプレイ再生！

### 活用例
- 自分のプレイを3Dカメラでいろんな角度から振り返る
- 被弾時の当たり判定の様子を確認
- プレゼン・デモ動画の素材として使う

---

## 🏗️ 3. レベルエディタ（双方向）

**「Blender上でビルや敵の配置を自由に変えてゲームに反映する」**

### 使い方
1. Blender で `blender_level_editor.py` を実行
2. **N キー** → サイドパネル「**GE3 Sync**」タブが出る
3. **[Import from Game]** ボタン → 現在のゲームの配置がBlenderに読み込まれる
4. Blenderで建物オブジェクトを自由に移動！
5. **[Export to Game]** ボタン → `scene_layout.txt` に書き出し
6. **ゲームを再起動** すると新しい配置が反映される！

---

## ⚙️ 4. パラメータエディタ

**「Blenderのスライダーでゲームのパラメータをリアルタイム調整する」**

### 使い方
1. ゲームを起動
2. Blender で `blender_param_editor.py` を実行
3. **N キー** → 「**GE3 Sync**」タブ → **GE3 Param Editor** パネル
4. スライダーを動かす
5. **[Apply to Game]** ボタン → ゲームに60フレーム以内に反映！

### 調整できるパラメータ
| パラメータ | 説明 |
|-----------|------|
| Boss Z Offset | ボスとの前後距離 |
| Boss Y Height | ボスの高さ |
| Boss Scale | ボスの大きさ |
| Player Speed X/Y | 自機の移動速度 |
| Player Limit X/Y | 自機の移動範囲 |
| Boss HP / Player HP | HP（デバッグ用） |

---

## 📂 データファイル

| ファイル | 更新頻度 | 説明 |
|---------|---------|------|
| `Resources/game_state.json` | 3フレームごと | ゲーム全状態（C++→Blender） |
| `Resources/replay_frames.csv` | 録画中毎フレーム | リプレイデータ（C++→Blender） |
| `Resources/scene_layout.txt` | 起動時 / Export時 | シーン配置（双方向） |
| `Resources/blender_params.txt` | [Apply]ボタン時 | パラメータ（Blender→C++） |

---

## ⌨️ ゲーム内キー操作（追加分）

| キー | 機能 |
|-----|------|
| **R** | リプレイ録画の開始/停止 |
| (既存) WASD | 自機移動 |
| (既存) LShift | バレルロール/ブースト |
| (既存) Space | 弾の発射 |
