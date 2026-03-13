#pragma once
#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "ImGui/imgui_impl_win32.h"
#include "ImGui/imgui_impl_dx11.h"
#include <vector>
#include <functional>
#include <string>
#include "datatype.h"
#include "UResourceManager.h"



struct ButtonInfo
{
	std::string label;
	ImVec2 position;
	ImVec2 size;
	std::function<void()> callback;
};

struct ImageButtonInfo
{
	std::string label;
	ID3D11ShaderResourceView* texture;
	ImVec2 position;
	ImVec2 size;
	std::function<void()> callback;
};

struct ImageInfo
{
	ID3D11ShaderResourceView* texture;
	ImVec2 position;
	ImVec2 size;
};

struct Image9Info
{
	ID3D11ShaderResourceView* texture;
	ImVec2 position;
	ImVec2 size;
	float borderLeft;
	float borderRight;
	float borderTop;
	float borderBottom;
};

struct TextInfo
{
	std::string text;
	ImVec2 position;
	ImFont* font;
	ImVec4 color;
};

struct SpriteInfo
{
	ID3D11ShaderResourceView* texture;
	ImVec2 position;
	ImVec2 size;
	ImVec2 uv0;
	ImVec2 uv1;
};

struct SpriteButtonInfo
{
	std::string label;
	ID3D11ShaderResourceView* texture;
	ImVec2 position;
	ImVec2 size;
	ImVec2 uv0;
	ImVec2 uv1;
	std::function<void()> callback;
	
};

struct SpriteButton9Info
{
	std::string label;
	ID3D11ShaderResourceView* texture;
	ImVec2 position;
	ImVec2 size;
	ImVec2 uv0;
	ImVec2 uv1;
	float borderLeft;
	float borderRight;
	float borderTop;
	float borderBottom;
	std::function<void()> callback;
	bool enabled;
};

struct BlockerInfo
{
	std::string label;
	ImVec2 position;
	ImVec2 size;
};

class UIFrame
{
private:
	std::string title;
	ImVec2 position;
	ImVec2 size;
	ImVec4 backgroundColor;
	float borderLineTransparency;
	bool noTitleBar;

	std::vector<ButtonInfo> buttons;
	std::vector<TextInfo> texts;
	std::vector<ImageInfo> images;
	std::vector<Image9Info> images9;
	std::unordered_map<std::string, std::unique_ptr<ImageButtonInfo>> imageButtons;
	std::vector<SpriteButtonInfo> spriteButtons;
	std::vector<SpriteButton9Info> spriteButtons9;
	std::unordered_map<std::string, std::unique_ptr<TextInfo>> selectableTexts;
	std::unordered_map<std::string, std::unique_ptr<SpriteInfo>> selectableSprite;
	std::vector<BlockerInfo> blockers;

	int Layer = 0; // 렌더링 레이어, 필요에 따라 사용
	bool bNoBringToFrontOnFocus = false; // 포커스 시 앞으로 가져오지 않도록 설정하는 플래그

public:
	UIFrame(const std::string& title);
	~UIFrame();

	void AddButton(const std::string& label, const ImVec2& position, const ImVec2& size, std::function<void()> callback);
	void AddSelectableText(const std::string& label, const std::string& text, const ImVec2& position, ImFont* font, const ImVec4& color = ImVec4(1.f, 1.f, 1.f, 1.f));
	void AddText(const std::string& text, const ImVec2& position, ImFont* font, const ImVec4& color = ImVec4(1.f, 1.f, 1.f, 1.f));
	void AddImage(ID3D11ShaderResourceView* texture, const ImVec2& position, const ImVec2& size);
	void AddImage9(ID3D11ShaderResourceView* texture, const ImVec2& position, const ImVec2& size, float border);
	void AddImageButton(const std::string& text, ID3D11ShaderResourceView* texture, const ImVec2& position, const ImVec2& size, std::function<void()> callback);
	void AddSpriteButton(const std::string& text, ID3D11ShaderResourceView* texture, const ImVec2& position, const ImVec2& size, int index, std::function<void()> callback);
	void AddSpriteManual(const std::string& text, ID3D11ShaderResourceView* texture, const ImVec2& textureSize, const int spriteSize, const ImVec2& position, const ImVec2& size, int index);
	void AddSpriteButton9(
		const std::string& text,
		ID3D11ShaderResourceView* texture,
		const ImVec2& position,
		const ImVec2& size,
		int index,
		float border,
		std::function<void()> callback,
		bool enabled = true);
	void AddBlocker(const std::string& label, const ImVec2& position, const ImVec2& size);

	ImVec2 GetPosition() const;
	void SetPosition(const ImVec2& newPosition);
	void Render();

	int GetLayer() const { return Layer; }
	void SetLayer(int layer) { this->Layer = layer; }

	std::string GetTitle() const { return title; }
	UIFrame& NoBringToFrontOnFocus(bool value)
	{
		bNoBringToFrontOnFocus = value;
		return *this;
	}

	UIFrame& Position(ImVec2 newPosition);
	UIFrame& Size(ImVec2 newSize);
	UIFrame& BackgroundColor(ImVec4 newColor);
	UIFrame& NoTitleBar(bool noTitle);
	UIFrame& BorderLineTransparency(float transparency);

	ImageButtonInfo* GetImageButton(const std::string& label);
	TextInfo* GetSelectableText(const std::string& label);
	SpriteInfo* GetSelectableSprite(const std::string& label);


	bool bVisibility = true;
};