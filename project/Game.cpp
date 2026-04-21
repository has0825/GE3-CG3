#include "Game.h"
#include "SceneManager.h"
#include "SceneFactory.h"
#include "Input.h" // 忘れずに
 
#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#endif


void Game::Initialize() {
    Framework::Initialize();
    Input::GetInstance()->Initialize(winApp_);

    sceneFactory_ = std::make_unique<SceneFactory>();

    // get() で生ポインタを渡す（所有権は移さない）
    SceneManager::GetInstance()->SetFactory(sceneFactory_.get());
    SceneManager::GetInstance()->ChangeScene("TITLE");
}

void Game::Finalize() {

    Framework::Finalize();
}

void Game::Update() {
    Framework::Update();

    // 入力情報の更新 (これで全シーンで入力が効くようになります)
    Input::GetInstance()->Update();

    SceneManager::GetInstance()->Update();
}

void Game::Draw() {
    dxCommon_->PreDraw();
    SceneManager::GetInstance()->Draw();

#ifdef USE_IMGUI
    // ImGui描画
    ImDrawData* drawData = ImGui::GetDrawData();
    if (drawData) {
        ID3D12DescriptorHeap* descriptorHeaps[] = { imguiDescriptorHeap_.Get() };
        dxCommon_->GetCommandList()->SetDescriptorHeaps(1, descriptorHeaps);
        ImGui_ImplDX12_RenderDrawData(drawData, dxCommon_->GetCommandList());
    }
#endif

    dxCommon_->PostDraw();
}