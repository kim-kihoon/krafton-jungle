#pragma once

namespace GameSettings
{
    // 게임 시작 시 로드할 씬 파일 이름 (Asset/Scene/ 기준 상대 경로)
    constexpr const wchar_t* StartupSceneName = L"Title.Scene";

    // START 버튼을 누른 뒤 로드할 실제 게임플레이 씬
    constexpr const wchar_t* MainSceneName = L"Scene_00_GameScene.Scene";
}
