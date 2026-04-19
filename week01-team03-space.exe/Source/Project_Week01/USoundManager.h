#pragma once

#include <Windows.h>
#include <mmsystem.h>
#include <dsound.h>

#include <string>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "dsound.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "dxguid.lib")

// 배경음악 카테고리
enum class EBGM
{
    EBGM_TitleScreen,
    EBGM_Level1,
    EBGM_Level2,
    EBGM_Level3
};

// 효과음 카테고리
enum class ESFX
{
    ESFX_MouseClick,
    ESFX_Clear,
    ESFX_Fail
};

class USoundManager
{
public:
    USoundManager() = default;
    ~USoundManager() = default;

    // 1. 초기화 및 해제
    void Initialize(HWND hwnd);
    void Dispose();

    // 2. 재생 및 정지
    void PlayBGM(EBGM bgm);
    void StopBGM();

    void PlaySFX(ESFX sfx);
    void StopSFX();

    void LoadBGM(EBGM type, const std::string& path);
    void LoadSFX(ESFX type, const std::string& path, int poolSize = 5);
   
    void SetBGMVolume(float volumeScale);
    void SetSFXVolume();

    float GetBGMVolume();
    float GetSFXVolume();

private:
    // 내부 데이터 구조체
    struct SoundClip
    {
        std::vector<IDirectSoundBuffer8*> Buffers; // 멀티 채널 재생용 버퍼들
        DWORD BufferSize = 0;
        float VolumeScale = 1.0f;                  // 0.0 ~ 1.0 범위
        size_t NextIndex = 0;                      // 다음에 사용할 버퍼 인덱스
    };

    // 멤버 변수
    IDirectSound8* m_pDS = nullptr;                // DirectSound 메인 인터페이스
    std::unordered_map<EBGM, SoundClip> BgmClips;  // BGM 저장소
    std::unordered_map<ESFX, SoundClip> SfxClips;  // SFX 저장소

    // 내부 헬퍼 함수
    bool CreatePrimaryBuffer();
    void CreateSoundClip(const std::string& filePath, int poolSize, SoundClip& clip);

    EBGM m_currentBGM = (EBGM)-1;
};
