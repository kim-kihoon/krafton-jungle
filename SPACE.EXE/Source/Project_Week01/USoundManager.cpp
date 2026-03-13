#include "USoundManager.h"
#include <mmsystem.h>

#pragma comment(lib, "winmm.lib")

struct WaveData
{
    WAVEFORMATEX wfx = {};
    BYTE* pBuffer = nullptr;
    DWORD bufferSize = 0;

    ~WaveData() { if (pBuffer) delete[] pBuffer; }
};

bool LoadWaveFile(const std::string& filePath, WaveData& outData)
{
    HMMIO hMmio = mmioOpenA((LPSTR)filePath.c_str(), NULL, MMIO_READ | MMIO_ALLOCBUF);
    if (!hMmio) return false;

    MMCKINFO parentChunk = {};
    parentChunk.fccType = mmioFOURCC('W', 'A', 'V', 'E');
    if (mmioDescend(hMmio, &parentChunk, NULL, MMIO_FINDRIFF) != MMSYSERR_NOERROR)
    {
        mmioClose(hMmio, 0); return false;
    }

    MMCKINFO childChunk = {};
    childChunk.ckid = mmioFOURCC('f', 'm', 't', ' ');
    if (mmioDescend(hMmio, &childChunk, &parentChunk, MMIO_FINDCHUNK) != MMSYSERR_NOERROR)
    {
        mmioClose(hMmio, 0); return false;
    }
    mmioRead(hMmio, (HPSTR)&outData.wfx, sizeof(WAVEFORMATEX));
    mmioAscend(hMmio, &childChunk, 0);

    childChunk.ckid = mmioFOURCC('d', 'a', 't', 'a');
    if (mmioDescend(hMmio, &childChunk, &parentChunk, MMIO_FINDCHUNK) != MMSYSERR_NOERROR)
    {
        mmioClose(hMmio, 0); return false;
    }

    outData.bufferSize = childChunk.cksize;
    outData.pBuffer = new BYTE[outData.bufferSize];
    mmioRead(hMmio, (HPSTR)outData.pBuffer, outData.bufferSize);

    mmioClose(hMmio, 0);
    return true;
}

void USoundManager::Initialize(HWND hwnd)
{
    if (FAILED(DirectSoundCreate8(NULL, &m_pDS, NULL))) return;
    m_pDS->SetCooperativeLevel(hwnd, DSSCL_PRIORITY);
    CreatePrimaryBuffer();
}

void USoundManager::CreateSoundClip(const std::string& filePath, int poolSize, SoundClip& clip)
{
    WaveData wave;
    if (!LoadWaveFile(filePath, wave)) return;

    DSBUFFERDESC dsbd = {};
    dsbd.dwSize = sizeof(DSBUFFERDESC);
    dsbd.dwFlags = DSBCAPS_CTRLVOLUME | DSBCAPS_GLOBALFOCUS;
    dsbd.dwBufferBytes = wave.bufferSize;
    dsbd.lpwfxFormat = &wave.wfx;

    IDirectSoundBuffer* pTemp = nullptr;
	if (m_pDS == nullptr) return;
    if (FAILED(m_pDS->CreateSoundBuffer(&dsbd, &pTemp, NULL))) return;

    clip.Buffers.resize(poolSize);
    pTemp->QueryInterface(IID_IDirectSoundBuffer8, (void**)&clip.Buffers[0]);
    pTemp->Release();

    void* pWrite = nullptr;
    DWORD dwLen = 0;
    clip.Buffers[0]->Lock(0, wave.bufferSize, &pWrite, &dwLen, NULL, NULL, 0);
    memcpy(pWrite, wave.pBuffer, dwLen);
    clip.Buffers[0]->Unlock(pWrite, dwLen, NULL, 0);

    for (int i = 1; i < poolSize; ++i) 
    {
        m_pDS->DuplicateSoundBuffer(clip.Buffers[0], (IDirectSoundBuffer**)&clip.Buffers[i]);
    }
}

void USoundManager::PlaySFX(ESFX sfx)
{
    auto it = SfxClips.find(sfx);
    if (it == SfxClips.end() || it->second.Buffers.empty()) return;

    SoundClip& clip = it->second;
    IDirectSoundBuffer8* pTarget = clip.Buffers[clip.NextIndex];

    pTarget->SetCurrentPosition(0);
    pTarget->SetVolume((LONG)((1.f - clip.VolumeScale) * -10000));
    pTarget->Play(0, 0, 0);

    clip.NextIndex = (clip.NextIndex + 1) % clip.Buffers.size();
}

bool USoundManager::CreatePrimaryBuffer()
{
    if (!m_pDS) return false;

    DSBUFFERDESC dsbd = {};
    dsbd.dwSize = sizeof(DSBUFFERDESC);
    dsbd.dwFlags = DSBCAPS_PRIMARYBUFFER;
    dsbd.dwBufferBytes = 0;
    dsbd.lpwfxFormat = NULL;

    IDirectSoundBuffer* pPrimary = nullptr;
    if (FAILED(m_pDS->CreateSoundBuffer(&dsbd, &pPrimary, NULL))) return false;

    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 2;
    wfx.nSamplesPerSec = 44100;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = (wfx.wBitsPerSample / 8) * wfx.nChannels;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    pPrimary->SetFormat(&wfx);
    pPrimary->Release();
    return true;
}

void USoundManager::PlayBGM(EBGM bgm)
{
    if (m_currentBGM == bgm) return;
    
    StopBGM();
    auto it = BgmClips.find(bgm);
    if (it == BgmClips.end() || it->second.Buffers.empty()) return;

    it->second.Buffers[0]->SetCurrentPosition(0);
    it->second.Buffers[0]->Play(0, 0, DSBPLAY_LOOPING);

    m_currentBGM = bgm;
}

void USoundManager::StopBGM()
{
    for (auto& pair : BgmClips)
    {
        if (!pair.second.Buffers.empty() && pair.second.Buffers[0])
        {
            pair.second.Buffers[0]->Stop();
            pair.second.Buffers[0]->SetCurrentPosition(0);
        }
    }
}

void USoundManager::StopSFX()
{
    for (auto& pair : SfxClips)
    {
        for (auto* buffer : pair.second.Buffers)
        {
            if (buffer) {
                buffer->Stop();
                buffer->SetCurrentPosition(0);
            }
        }
    }
}

void USoundManager::LoadBGM(EBGM type, const std::string& path)
{
    if (BgmClips.find(type) != BgmClips.end()) return;    // 중복 로드 방지
    if (m_pDS == nullptr) return;

    SoundClip clip;
    CreateSoundClip(path, 1, clip);

    // [진단] 로드 성공 여부 확인   
    if (clip.Buffers.empty())
    {
        std::wstring wPath(path.begin(), path.end());
        std::wstring err = L"BGM 로드 실패: " + wPath + L"\n파일 존재 여부를 확인하세요.";
        MessageBoxW(NULL, err.c_str(), L"Sound Error", MB_OK);
        return;
    }
    BgmClips[type] = clip;
}

void USoundManager::LoadSFX(ESFX type, const std::string& path, int poolSize)
{
    if (SfxClips.find(type) != SfxClips.end()) return;    // 중복 로드 방지
    if (m_pDS == nullptr) return;

    SoundClip clip;
    // 효과음은 인자로 받은 poolSize만큼 버퍼를 생성합니다.
    CreateSoundClip(path, poolSize, clip);

    // 생성된 클립을 창고(SfxClips)에 저장
    if (!clip.Buffers.empty())
    {
        SfxClips[type] = clip;
    }
}

void USoundManager::SetBGMVolume(float volumeScale)
{
    if (volumeScale < 0.0f) volumeScale = 0.0f;
    if (volumeScale > 1.0f) volumeScale = 1.0f;

    for (auto& pair : BgmClips)
    {
        pair.second.VolumeScale = volumeScale;
        LONG dsVol = (LONG)((1.0f - volumeScale) * -10000);

        if (!pair.second.Buffers.empty() && pair.second.Buffers[0])
            pair.second.Buffers[0]->SetVolume(dsVol);
    }
}

void USoundManager::SetSFXVolume()
{
    for (auto& pair : SfxClips)
    {
        LONG dsVol = (LONG)((1.0f - pair.second.VolumeScale) * -10000);
        for (auto* buffer : pair.second.Buffers)
            if (buffer) buffer->SetVolume(dsVol);
    }
}

float USoundManager::GetBGMVolume()
{
    // 맵이 비어있지 않다면 첫 번째 BGM의 볼륨 스케일을 반환합니다.
    if (!BgmClips.empty())
        return BgmClips.begin()->second.VolumeScale;

    return 1.0f; // 기본값
}

float USoundManager::GetSFXVolume()
{
    if (!SfxClips.empty())
        return SfxClips.begin()->second.VolumeScale;

    return 1.0f;
}

void USoundManager::Dispose()
{
    for (auto& pair : BgmClips)
    {
        for (auto* buf : pair.second.Buffers) if (buf) buf->Release();
        pair.second.Buffers.clear();
    }
    for (auto& pair : SfxClips)
    {
        for (auto* buf : pair.second.Buffers) if (buf) buf->Release();
        pair.second.Buffers.clear();
    }
    if (m_pDS) { m_pDS->Release(); m_pDS = nullptr; }
}