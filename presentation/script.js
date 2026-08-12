// ==========================================================================
// D3D12 Slide Show Interactive Script (Advanced Topics Version)
// ==========================================================================

document.addEventListener("DOMContentLoaded", () => {
    // --- 1. State & Data Definition ---
    let currentSlide = 1;
    const totalSlides = 8;

    // Base Script Texts
    const baseScripts = [
        "皆様お疲れ様です。{student_id}の{name}です。本日は、現在開発中の3Dルート固定シューティングゲームにおける、D3D12を用いたGPUパーティクルシステム、蜘蛛ボスにおける多脚歩行の挙動・アニメーション制御、および自機の戦闘アクション演出についての開発進捗を発表いたします。よろしくお願いいたします。",
        "まず、そもそもどのようなゲームを制作しているのか、そのコンセプトを説明いたします。本プロジェクトは、プレイヤーとカメラが完全に等速で自動前進していく『3Dルート固定レールシューティングゲーム』です。敵編隊や巨大な蜘蛛ボスとの緊迫した戦闘を特徴としています。本プロトタイプの開発目的は、大量のオブジェクト描画や高度な敵AI、ボスの多脚アニメーションを動作させるにあたり、CPUとGPUの処理を適切に分担し、描画負荷とメインスレッドの処理負荷を最小に抑えつつ、プレイヤーに『速度と破壊の強烈な手応え（爽快感）』を提供することにあります。",
        "現在の進捗状況についてご説明します。今回は第1回なので前回のおさらいはなしで、現状動作している要素を『プレイヤー関係』『敵関係』『その他』の3つに分類して整理しました。まずプレイヤー関係では、クランプ移動やエイム補正付きの通常射撃、そしてバレルロール回避やブースト時のラジアルブラーを実装しました。次に敵関係では、V字や円形などのフォーメーションを組んだ組織飛行、4状態の自律AIと特攻判定、さらにボスのフェーズ遷移や8本の足の多脚歩行位相差制御を実装しています。その他のステージやシステム面では、床のオブジェクトプールとビルの積層による軽量描画、ビルの物理崩壊とCompute ShaderによるGPUパーティクルバースト、そして天球スクロールとシーン全体の遷移ループを構築しました。ここからは、プログラマー向けに、これらの中から3つの高度な技術詳細について解説いたします。",
        "技術的なトピックの1つ目は、ボス関連である『フェーズ移行制御』と『8本足の多脚歩行位相差ロジック』です。解決すべき課題として、ボスの単調な動作を防ぐこと、そして3Dモデルの固定アニメーションアセットに依存すると、移動速度や旋回方向の変化に対して『足がすべる』という不自然な現象が起きる問題がありました。これに対し、まずHPが半分以下になると攻撃や行動が変化するフェーズ遷移を実装しました。さらに、8本の脚それぞれに対してサイン波とタイミング遅延を用いた位相テーブルを設定し、移動ベクトルに同期した関節角度をリアルタイム計算するロジックを設計しました。具体的には、時間 t と歩行周波数 f、さらにグループごとに半周期ずらした位相オフセット φi によって各脚の現在の位相角 θi(t) を決定し、それをもとに根元関節の水平スイングYaw角や中間関節の上下Pitch角を動的に変化させています。細かい工夫として、8本の足が交互に非同期に地面を踏みしめるステップ周期を数学的に構築したことで、追加の3Dモーションデータを一切読み込むことなく、ボスの速度に完璧に追従する蜘蛛らしい生々しい歩行を低コストかつ超軽量に表現しました。",
        "技術的トピックの2つ目は、パーティクル関連であるD3D12 Compute Shaderを用いたGPUパーティクルシステムです。解決したい課題は、敵やビルを破壊した際に飛び散る大量の破片や火花をCPUで計算するとメインスレッドが圧迫され、激しい戦闘中に処理落ちが発生する問題でした。これに対処するため、パーティクルの全ステート（位置・速度・生存時間）を保持する『構造化バッファ』をGPUメモリ上に一括配置し、物理計算から頂点更新までの全処理をGPUのCompute Shader上で完結させました。細かいポイントとして、16x16スレッドグループのGPU並列スレッドを用い、CPUにバッファをアンマップすることなくGPU内で直接頂点データを更新してそのままレンダリングしています。これにより、CPUに一切の物理計算負荷（0ms）をかけることなく、数千〜数万粒の破片がダイナミックに飛び散る低負荷かつ超軽量な破壊演出を実現しました。",
        "技術的トピックの3つ目は、プレイヤー関連である自機の特殊回避アクションと、ブースト時のラジアルブラー（放射状ブラー）によるポストプロセス演出です。解決すべき課題として、レールシューター特有の単調な前進感を打破し、超高速移動（最高速度480m/s）の際に、数値的な移動速度だけでなく視覚的に圧倒的なスピード感をプレイヤーにフィードバックする必要がありました。これに対し、左Shiftキーで360度ロールし無敵時間を得るバレルロールを実装し、さらにブースト時にはピクセルシェーダによるラジアルブラー（放射状ぼかし）を適用しました。実装の細かいポイントとして、ピクセルシェーダ内で画面中心から外側に向けてカラーバッファのテクスチャ座標を動的に多点サンプリングし、それらを平均化して合成しています。ブーストの進行度に応じてこのブラー幅を滑らかにフェードイン・アウトさせることで、GPU負荷を最小限に抑えつつ、戦闘機がワープ加速したかのようなダイナミックな速度フィールを実現しました。",
        "技術的トピックの4つ目は、Blender連携によるルート地形生成とゲーム内マップの双方向同期システムです。本プロジェクトの大きな技術的特徴として、Blender上でPythonスクリプト群を実行し、ステージの進行ルートや地形メッシュ、ビルの配置をすべてBlenderの3Dビューポートで視覚的に設計・調整できるシステムを構築しました。具体的には、Blender上で区画ごとの挙動パラメータ（直進、右曲がり、左曲がり、上り坂、下り坂）を設定し、ノイズベースの起伏を持つ地形メッシュとビル群をプロシージャルに一括自動生成します。この連携は4種のPythonスクリプトで実現しています。第1にリアルタイム同期ビューア。ゲームが3フレームごとに出力するJSON状態データをBlenderが定期的に読み取り、3Dビューポート上のオブジェクト位置を自動更新します。これによりゲームの『今』をBlender上から俯瞰監視できます。第2にリプレイ再生スクリプト。ゲームのCSVリプレイデータからBlenderのキーフレームを一括生成し、タイムライン上でプレイ軌跡を再現します。PV撮影やデバッグに活用可能です。第3にレベルエディタ。Blenderの3Dビュー上で直感的にビルや敵を配置し、テキストファイルに書き出し。ゲームは起動時にこれを読み込んでステージを自動構築します。第4にパラメータスライダー。Blenderのサイドパネルからプレイヤー速度やボスHPなどをスライダーで即座に変更でき、ゲーム側が60フレームごとにテキストファイルをホットリロードすることでリアルタイムに反映されます。この双方向パイプラインにより、レベルデザインのイテレーション速度を飛躍的に向上させました。",
        "最後に、今後の開発スケジュールと次回の進捗予定です。プロトタイプ完成マイルストーンである7月31日に向け、直近の2026年7月11日から14日にかけてはゲームループ全体のシーン遷移および衝突・被弾判定の堅牢性を高めるテストを行います。続いて7月15日から18日にかけては、敵の編隊飛行AI의 バリエーションや出現パターンの難易度調整を実施し、シューティングとしてのゲームプレイの魅力をブラッシュアップします。7月19日からは、プロトタイプフェーズのもう一つの大きな目玉である、蜘蛛ボスの歩行アニメーションに着手します。8本の足が交互に非同期に地面を踏みしめる、より蜘蛛らしく不気味でリアルな歩行ロジックの数学的モデルを設計し、仮実装を完了させる予定です。その後、7月26日までに自機の新規スキルのプロトタイプを作成し、最終週の7月31日にかけて全体の調整とデバッグ、そして次期Alphaフェーズの開発準備へと移行していく予定です。以上で発表を終わります。ご清聴ありがとうございました。"
    ];

    // DOM Elements
    const slides = document.querySelectorAll(".slide");
    const currentSlideNumSpan = document.getElementById("current-slide-num");
    const totalSlideNumSpan = document.getElementById("total-slide-num");
    const prevBtn = document.getElementById("prev-btn");
    const nextBtn = document.getElementById("next-btn");
    const scriptTextP = document.getElementById("script-text");
    const scriptPanel = document.getElementById("script-panel");
    const toggleScriptBtn = document.getElementById("toggle-script-btn");
    
    // Inputs
    const studentIdInput = document.getElementById("student-id-input");
    const userNameInput = document.getElementById("user-name-input");
    const replaceStudentIdSpans = document.querySelectorAll(".replace-student-id");
    const replaceNameSpans = document.querySelectorAll(".replace-name");

    // Initialize Total Slide Number
    totalSlideNumSpan.textContent = totalSlides;

    // --- 2. Core Functions ---

    // Update Student ID & Name in all slides & scripts
    function updateMetadata() {
        const studentId = studentIdInput.value.trim() || "LE3X_99";
        const name = userNameInput.value.trim() || "氏名";

        // Update spans in HTML
        replaceStudentIdSpans.forEach(span => span.textContent = studentId);
        replaceNameSpans.forEach(span => span.textContent = name);

        // Refresh script panel content
        updateScriptText(studentId, name);
    }

    // Update script text based on current slide
    function updateScriptText(studentId, name) {
        let rawText = baseScripts[currentSlide - 1];
        // Replace placeholders with real values
        let formattedText = rawText
            .replace(/{student_id}/g, studentId)
            .replace(/{name}/g, name);

        scriptTextP.innerHTML = formattedText;
    }

    // Go to specific slide
    function goToSlide(slideNum) {
        if (slideNum < 1 || slideNum > totalSlides) return;

        // Transition classes
        slides.forEach(slide => slide.classList.remove("active"));
        document.getElementById(`slide-${slideNum}`).classList.add("active");

        // Update state
        currentSlide = slideNum;
        currentSlideNumSpan.textContent = currentSlide;

        // Update script
        updateMetadata();

        // Control Buttons state
        prevBtn.disabled = currentSlide === 1;
        nextBtn.disabled = currentSlide === totalSlides;
    }

    // --- 3. Event Listeners ---

    // Nav buttons
    prevBtn.addEventListener("click", () => {
        goToSlide(currentSlide - 1);
    });

    nextBtn.addEventListener("click", () => {
        goToSlide(currentSlide + 1);
    });

    // Toggle Script panel
    toggleScriptBtn.addEventListener("click", () => {
        scriptPanel.classList.toggle("collapsed");
        toggleScriptBtn.classList.toggle("active");
        if (scriptPanel.classList.contains("collapsed")) {
            toggleScriptBtn.textContent = "台本を表示";
        } else {
            toggleScriptBtn.textContent = "台本を隠す";
        }
    });

    // User inputs change
    studentIdInput.addEventListener("input", updateMetadata);
    userNameInput.addEventListener("input", updateMetadata);

    // Keyboard navigation
    document.addEventListener("keydown", (e) => {
        // Prevent sliding while user is typing in inputs
        if (document.activeElement === studentIdInput || document.activeElement === userNameInput) {
            return;
        }

        if (e.key === "ArrowRight" || e.key === "Space" || e.key === " " || e.key === "Enter") {
            e.preventDefault();
            if (currentSlide < totalSlides) {
                goToSlide(currentSlide + 1);
            }
        } else if (e.key === "ArrowLeft" || e.key === "Backspace") {
            e.preventDefault();
            if (currentSlide > 1) {
                goToSlide(currentSlide - 1);
            }
        }
    });

    // --- 4. Initialization ---
    goToSlide(1);
});
