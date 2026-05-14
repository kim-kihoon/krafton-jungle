#pragma once
#include "Editor/UI/EditorWidget.h"
#include "Engine/Camera/Modifier/CameraShakeModifier.h"

class APlayerCameraManager;
class ULuaCameraModifier;

class FEditorCameraShakeWidget : public FEditorWidget
{
public:
    void Initialize(UEditorEngine* InEditorEngine) override;
    void Render(float DeltaTime) override;
    void SetOpen(bool bInOpen) { bIsOpen = bInOpen; }
    bool IsOpen() const { return bIsOpen; }

private:
    APlayerCameraManager* GetCameraManager() const;

    void SaveLuaScript();
    void LoadLuaScript();
    bool OpenSaveDialog(std::wstring& OutPath);
    bool OpenLoadDialog(std::wstring& OutPath);
    void GenerateLuaSource(std::string& OutSource) const;
    bool ParseLuaSource(const std::string& Source);

    FCameraShakeParams PreviewParams;

    ULuaCameraModifier* LuaModifier = nullptr;
    std::string LoadedScriptPath;
    std::string LastError;
    bool bIsOpen = true;
};
