[![DebugBuild](https://github.com/has0825/GE3-CG3/actions/workflows/DebugBuild.yml/badge.svg)](https://github.com/has0825/GE3-CG3/actions/workflows/DebugBuild.yml)
[![ReleaseBuild](https://github.com/has0825/GE3-CG3/actions/workflows/ReleaseBuild.yml/badge.svg)](https://github.com/has0825/GE3-CG3/actions/workflows/ReleaseBuild.yml)
[![DevelopmentBuild](https://github.com/has0825/GE3-CG3/actions/workflows/DevelopmentBuild.yml/badge.svg)](https://github.com/has0825/GE3-CG3/actions/workflows/DevelopmentBuild.yml)

# CG5 評価課題 - ポストエフェクト (PostEffect) 実装概要

本プロジェクト（`CG5評価課題` ブランチ）では、以下のポストエフェクト（PostEffect）をゲームに組み込み、リアルタイムで切替・調整できるシステムを実装しています。

## 🎮 ポストエフェクトの切替操作方法
* **キーボード十字キー「←」「→」（`DIK_LEFT` / `DIK_RIGHT`）**:
  ゲームプレイ中にキーボードの左右矢印キー（十字キー横移動）を押すことで、全13種類のポストエフェクトをリアルタイムで順次切り替えることができます。
* **ImGui パネル (`PostProcess`)**:
  画面上の ImGui UI からもエフェクトの種類選択および各エフェクトの各種パラメータ（モザイク解像度、色収差強度、カーネルサイズ、ブラー幅、しきい値、ノイズ強度等）のリアルタイム調整が可能です。

---

## 🎨 実装・組み込み済みポストエフェクト一覧

| 項目名 (加点要素) | 最高点 | シェーダーファイル | ゲーム内での利用・適用方法 |
|---|---|---|---|
| **Vignetting** | 3 | `Vignette.PS.hlsl` | ダメージ・暗転演出、およびImGuiでの画面周辺減光効果として利用 |
| **BoxFilter** | 3 | `BoxFilter.PS.hlsl` | 画面全体の平滑化・簡易ぼかし処理として利用 |
| **GaussianFilter** | 5 | `GaussianFilter.PS.hlsl` | 標準偏差 $\sigma$ とカーネルサイズによる高品質ガウシアンブラーとして利用 |
| **LuminanceBasedOutline** | 5 | `LuminanceBasedOutline.PS.hlsl` | 輝度勾配（Prewittフィルタ）によるエッジ検出輪郭描画として利用 |
| **DepthBasedOutline** | 8 | `DepthBasedOutline.PS.hlsl` | 深度バッファ（Zバッファ）から算出した線形深度勾配による輪郭抽出として利用 |
| **Radial Blur** | 5 | `RadialBlur.PS.hlsl` | 自機のブースト加速・スピード感演出および衝撃エフェクトとして利用 |
| **Dissolve** | 4 | `Dissolve.PS.hlsl` | ノイズテクスチャを用いたシーン遷移および消滅演出として利用 |
| **Random** | 4 | `Random.PS.hlsl` | 画面ノイズ・テレビノイズ（砂嵐）・走査線演出として利用 |
| **その他 (追加枠)** | 20 | `Pixelate.PS.hlsl`<br>`ChromaticAberration.PS.hlsl`<br>`Invert.PS.hlsl`<br>`Grayscale.PS.hlsl`<br>`Sepia.PS.hlsl` | **【その他・大幅拡充】**<br>・**Pixelate**: レトロドット絵風モザイクエフェクト<br>・**ChromaticAberration**: RGB色チャンネルの光学歪み（色収差・色ズレ）<br>・**Invert**: ネガポジ色反転<br>・**Grayscale**: モノクロ化<br>・**Sepia**: セピア調化 |

---

## 🛠️ 技術仕様
- **DirectX 12 / HLSL (Shader Model 6.0)**
- バックバッファへのレンダーターゲット書き戻し時に各エフェクトの PSO (Pipeline State Object) を切り替えてフルスクリーン描画を実行しています。
