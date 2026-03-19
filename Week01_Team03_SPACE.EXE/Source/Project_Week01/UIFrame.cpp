#include "UIFrame.h"

//	Sprite 9-slice
namespace
{
	constexpr float SpriteSheetWidth = 80.0f;
	constexpr float SpriteSheetHeight = 128.0f;
	constexpr float SpriteCellSize = 16.0f;

	ImVec2 GetCenteredPos(const ImVec2& center, const ImVec2& size)
	{
		return ImVec2(
			center.x - size.x * 0.5f,
			center.y - size.y * 0.5f
		);
	}

	void CalcSpriteUV(int index, ImVec2& outUV0, ImVec2& outUV1)
	{
		const int columns = static_cast<int>(SpriteSheetWidth / SpriteCellSize);
		const int rows = static_cast<int>(SpriteSheetHeight / SpriteCellSize);
		const int maxCount = columns * rows;

		if (index < 0 || index >= maxCount)
		{
			outUV0 = ImVec2(0.0f, 0.0f);
			outUV1 = ImVec2(1.0f, 1.0f);
			return;
		}

		const int x = index % columns;
		const int y = index / columns;

		const float u0 = (x * SpriteCellSize) / SpriteSheetWidth;
		const float v0 = (y * SpriteCellSize) / SpriteSheetHeight;
		const float u1 = ((x + 1) * SpriteCellSize) / SpriteSheetWidth;
		const float v1 = ((y + 1) * SpriteCellSize) / SpriteSheetHeight;

		outUV0 = ImVec2(u0, v0);
		outUV1 = ImVec2(u1, v1);
	}

	void DrawNineSliceImage(
		ID3D11ShaderResourceView* texture,
		const ImVec2& pos,
		const ImVec2& size,
		const ImVec2& uv0,
		const ImVec2& uv1,
		float borderLeft,
		float borderRight,
		float borderTop,
		float borderBottom,
		ImU32 tint = IM_COL32(255, 255, 255, 255))
	{
		if (texture == nullptr)
			return;

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		ImTextureID texId = (ImTextureID)texture;

		float width = size.x;
		float height = size.y;

		if (width < borderLeft + borderRight)
			width = borderLeft + borderRight;
		if (height < borderTop + borderBottom)
			height = borderTop + borderBottom;

		const float x0 = pos.x;
		const float x1 = pos.x + borderLeft;
		const float x2 = pos.x + width - borderRight;
		const float x3 = pos.x + width;

		const float y0 = pos.y;
		const float y1 = pos.y + borderTop;
		const float y2 = pos.y + height - borderBottom;
		const float y3 = pos.y + height;

		const float du = uv1.x - uv0.x;
		const float dv = uv1.y - uv0.y;

		const float bu0 = uv0.x;
		const float bu1 = uv0.x + du * (borderLeft / SpriteCellSize);
		const float bu2 = uv1.x - du * (borderRight / SpriteCellSize);
		const float bu3 = uv1.x;

		const float bv0 = uv0.y;
		const float bv1 = uv0.y + dv * (borderTop / SpriteCellSize);
		const float bv2 = uv1.y - dv * (borderBottom / SpriteCellSize);
		const float bv3 = uv1.y;

		// 1
		drawList->AddImage(texId, ImVec2(x0, y0), ImVec2(x1, y1), ImVec2(bu0, bv0), ImVec2(bu1, bv1), tint);
		// 2
		drawList->AddImage(texId, ImVec2(x1, y0), ImVec2(x2, y1), ImVec2(bu1, bv0), ImVec2(bu2, bv1), tint);
		// 3
		drawList->AddImage(texId, ImVec2(x2, y0), ImVec2(x3, y1), ImVec2(bu2, bv0), ImVec2(bu3, bv1), tint);

		// 4
		drawList->AddImage(texId, ImVec2(x0, y1), ImVec2(x1, y2), ImVec2(bu0, bv1), ImVec2(bu1, bv2), tint);
		// 5
		drawList->AddImage(texId, ImVec2(x1, y1), ImVec2(x2, y2), ImVec2(bu1, bv1), ImVec2(bu2, bv2), tint);
		// 6
		drawList->AddImage(texId, ImVec2(x2, y1), ImVec2(x3, y2), ImVec2(bu2, bv1), ImVec2(bu3, bv2), tint);

		// 7
		drawList->AddImage(texId, ImVec2(x0, y2), ImVec2(x1, y3), ImVec2(bu0, bv2), ImVec2(bu1, bv3), tint);
		// 8
		drawList->AddImage(texId, ImVec2(x1, y2), ImVec2(x2, y3), ImVec2(bu1, bv2), ImVec2(bu2, bv3), tint);
		// 9
		drawList->AddImage(texId, ImVec2(x2, y2), ImVec2(x3, y3), ImVec2(bu2, bv2), ImVec2(bu3, bv3), tint);
	}
}




UIFrame::UIFrame(const std::string& title) : title(title)
{
	position = ImVec2(0.0f, 0.0f);
	size = ImVec2(200.0f, 100.0f);
	backgroundColor = ImVec4(39.f / 255.f, 35.f / 255.f, 75.f / 255.f, 1.0f);
	borderLineTransparency = 1.0f;
	noTitleBar = false;
}


UIFrame::~UIFrame()
{
}

void UIFrame::AddButton(const std::string& label, const ImVec2& position, const ImVec2& size, std::function<void()> callback)
{
	ButtonInfo button;
	button.label = label;
	button.position = position;
	button.size = size;
	button.callback = callback;
	buttons.push_back(button);
}

void UIFrame::AddSelectableText(const std::string& label, const std::string& text, const ImVec2& position, ImFont* font, const ImVec4& color)
{
	TextInfo textInfo;
	textInfo.text = text;
	textInfo.position = position;
	textInfo.font = font;
	textInfo.color = color;

	selectableTexts[label] = std::make_unique<TextInfo>(textInfo);
}

void UIFrame::AddText(const std::string& text, const ImVec2& position, ImFont* font, const ImVec4& color)
{
	TextInfo textInfo;
	textInfo.text = text;
	textInfo.position = position;
	textInfo.font = font;
	textInfo.color = color;

	texts.push_back(textInfo);
}

void UIFrame::AddImage(ID3D11ShaderResourceView* texture, const ImVec2& position, const ImVec2& size)
{
	ImageInfo imageInfo;
	imageInfo.texture = texture;
	imageInfo.position = position;
	imageInfo.size = size;
	images.push_back(imageInfo);
}

void UIFrame::AddImage9(
	ID3D11ShaderResourceView* texture,
	const ImVec2& position,
	const ImVec2& size,
	float border)
{
	if (texture == nullptr)
		return;

	Image9Info imageInfo;
	imageInfo.texture = texture;
	imageInfo.position = position;
	imageInfo.size = size;
	imageInfo.borderLeft = border;
	imageInfo.borderRight = border;
	imageInfo.borderTop = border;
	imageInfo.borderBottom = border;

	images9.push_back(imageInfo);
}

void UIFrame::AddImageButton(const std::string& text, ID3D11ShaderResourceView* texture, const ImVec2& position, const ImVec2& size, std::function<void()> callback) {
	ImageButtonInfo imageButtonInfo;
	imageButtonInfo.label = text;
	imageButtonInfo.texture = texture;
	imageButtonInfo.position = position;
	imageButtonInfo.size = size;
	imageButtonInfo.callback = callback;


	imageButtons[text] = std::make_unique<ImageButtonInfo>(imageButtonInfo);
}


void UIFrame::AddSpriteButton(
	const std::string& text,
	ID3D11ShaderResourceView* texture,
	const ImVec2& position,
	const ImVec2& size,
	int index,
	std::function<void()> callback)
{
	if (texture == nullptr)
		return;

	SpriteButtonInfo spriteInfo;
	spriteInfo.label = text;
	spriteInfo.texture = texture;
	spriteInfo.position = position;
	spriteInfo.size = size;
	spriteInfo.callback = callback;

	CalcSpriteUV(index, spriteInfo.uv0, spriteInfo.uv1);

	spriteButtons.push_back(spriteInfo);
}

void UIFrame::AddSpriteManual(
	const std::string& text,
	ID3D11ShaderResourceView* texture,
	const ImVec2& textureSize,
	const int spriteSize,
	const ImVec2& position,
	const ImVec2& size,
	int index)
{
	if (texture == nullptr)
		return;

	SpriteInfo spriteInfo;
	spriteInfo.texture = texture;
	spriteInfo.position = position;
	spriteInfo.size = size;

	const int columns = static_cast<int>(textureSize.x / spriteSize);
	const int rows = static_cast<int>(textureSize.y / spriteSize);
	const int maxCount = columns * rows;

	//if (index < 0 || index >= maxCount)
	//{
	//	outUV0 = ImVec2(0.0f, 0.0f);
	//	outUV1 = ImVec2(1.0f, 1.0f);
	//	return;
	//}

	const int x = index % columns;
	const int y = index / columns;

	const float u0 = (x * spriteSize) / textureSize.x;
	const float v0 = (y * spriteSize) / textureSize.y;
	const float u1 = ((x + 1) * spriteSize) / textureSize.x;
	const float v1 = ((y + 1) * spriteSize) / textureSize.y;

	spriteInfo.uv0 = ImVec2(u0, v0);
	spriteInfo.uv1 = ImVec2(u1, v1);

	selectableSprite[text] = std::make_unique<SpriteInfo>(spriteInfo);;
}

void UIFrame::AddSpriteButton9(
	const std::string& text,
	ID3D11ShaderResourceView* texture,
	const ImVec2& position,
	const ImVec2& size,
	int index,
	float border,
	std::function<void()> callback,
	bool enabled)  // 추가
{
	SpriteButton9Info spriteInfo;
	spriteInfo.label = text;
	spriteInfo.texture = texture;
	spriteInfo.position = position;
	spriteInfo.size = size;
	spriteInfo.callback = callback;
	spriteInfo.enabled = enabled;

	spriteInfo.borderLeft = border;
	spriteInfo.borderRight = border;
	spriteInfo.borderTop = border;
	spriteInfo.borderBottom = border;

	CalcSpriteUV(index, spriteInfo.uv0, spriteInfo.uv1);

	spriteButtons9.push_back(spriteInfo);
}

void UIFrame::AddBlocker(const std::string& label, const ImVec2& position, const ImVec2& size)
{
	BlockerInfo blocker;
	blocker.label = label;
	blocker.position = position;
	blocker.size = size;
	blockers.push_back(blocker);
}

UIFrame& UIFrame::Position(ImVec2 newPosition)
{
	position = newPosition;
	return *this;
}

UIFrame& UIFrame::Size(ImVec2 newSize)
{
	size = newSize;
	return *this;
}

UIFrame& UIFrame::BackgroundColor(ImVec4 newColor)
{
	backgroundColor = newColor;
	return *this;
}

UIFrame& UIFrame::NoTitleBar(bool noTitle)
{
	noTitleBar = noTitle;
	return *this;
}

UIFrame& UIFrame::BorderLineTransparency(float transparency)
{
	borderLineTransparency = transparency;
	return *this;
}

ImageButtonInfo* UIFrame::GetImageButton(const std::string& label)
{
	return imageButtons[label].get();
}

TextInfo* UIFrame::GetSelectableText(const std::string& label)
{
	return selectableTexts[label].get();
}

SpriteInfo* UIFrame::GetSelectableSprite(const std::string& label)
{
	return selectableSprite[label].get();
}

void UIFrame::SetPosition(const ImVec2& newPosition)
{
	position = newPosition;
}

ImVec2 UIFrame::GetPosition() const
{
	return position;
}

void UIFrame::Render()
{
	if (!bVisibility) return;

	ImGui::SetNextWindowPos(position, ImGuiCond_Always);
	ImGui::SetNextWindowSize(size, ImGuiCond_Always);

	ImGui::PushStyleColor(ImGuiCol_WindowBg, backgroundColor);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, borderLineTransparency);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

	auto defaultFlags =
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoScrollbar |
		ImGuiWindowFlags_NoScrollWithMouse |
		//ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus;

	if (bNoBringToFrontOnFocus)
	{
		defaultFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
	}

	auto flags = noTitleBar ? (defaultFlags | ImGuiWindowFlags_NoTitleBar) : defaultFlags;

	ImGui::Begin(title.c_str(),	nullptr,flags);
	//ImGui::InvisibleButton((title + "_Blocker").c_str(), size);


	for (const auto& image : images)
	{
		ImGui::SetCursorPos(image.position);
		ImGui::Image((ImTextureID)image.texture, image.size);
	}


	for (const auto& pair : selectableSprite)
	{
		auto image = *pair.second.get();

		ImGui::SetCursorPos(image.position);
		ImGui::Image((ImTextureID)image.texture, image.size, image.uv0, image.uv1);
	}

	for (const auto& image9 : images9)
	{
		ImVec2 pos = {
			image9.position.x - image9.size.x * 0.5f,
			image9.position.y - image9.size.y * 0.5f
		};

		ImGui::SetCursorPos(pos);

		ImVec2 screenPos = ImGui::GetCursorScreenPos();

		DrawNineSliceImage(
			image9.texture,
			screenPos,
			image9.size,
			ImVec2(0.0f, 0.0f),
			ImVec2(1.0f, 1.0f),
			image9.borderLeft,
			image9.borderRight,
			image9.borderTop,
			image9.borderBottom
		);

		ImGui::Dummy(image9.size);
	}

	for (const auto& button : buttons)
	{
		ImVec2 pos = {
			button.position.x - button.size.x * 0.5f,
			button.position.y - button.size.y * 0.5f
		};

		ImGui::SetCursorPos(pos);

		if (ImGui::Button(button.label.c_str(), button.size))
		{
			button.callback();
		}
	}

	for (const auto& pair : imageButtons)
	{

		auto imageButton = *pair.second.get();
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.2f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.3f));

		ImVec2 pos = {
			imageButton.position.x - imageButton.size.x * 0.5f,
			imageButton.position.y - imageButton.size.y * 0.5f
		};

		ImGui::SetCursorPos(pos);

		if (ImGui::ImageButton(imageButton.label.c_str(), (ImTextureID)(imageButton.texture), imageButton.size))
		{
			imageButton.callback();
		}

		ImGui::PopStyleColor(3);
	}

	for (const auto& spriteButton : spriteButtons)
	{
		ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1, 1, 1, 0.0f));
		ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1, 1, 1, 0.2f));
		ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1, 1, 1, 0.3f));

		ImGui::SetCursorPos(GetCenteredPos(spriteButton.position, spriteButton.size));

		

		if (ImGui::ImageButton(
			spriteButton.label.c_str(),
			(ImTextureID)(spriteButton.texture),
			spriteButton.size,
			spriteButton.uv0,
			spriteButton.uv1))
		{
			spriteButton.callback();
		}

		

		ImGui::PopStyleColor(3);
	}

	for (const auto& spriteButton : spriteButtons9)
	{
		ImVec2 pos = GetCenteredPos(spriteButton.position, spriteButton.size);

		ImGui::SetCursorPos(pos);

		ImGui::BeginDisabled(!spriteButton.enabled);

		if (ImGui::InvisibleButton(spriteButton.label.c_str(), spriteButton.size))
		{
			spriteButton.callback();
		}

		ImGui::EndDisabled();

		bool hovered = ImGui::IsItemHovered();
		bool active = ImGui::IsItemActive();

		ImU32 tint = IM_COL32(255, 255, 255, 255);
		if (active)
			tint = IM_COL32(220, 220, 220, 255);
		else if (hovered)
			tint = IM_COL32(240, 240, 240, 255);

		ImVec2 screenPos = ImGui::GetItemRectMin();

		DrawNineSliceImage(
			spriteButton.texture,
			screenPos,
			spriteButton.size,
			spriteButton.uv0,
			spriteButton.uv1,
			spriteButton.borderLeft,
			spriteButton.borderRight,
			spriteButton.borderTop,
			spriteButton.borderBottom,
			tint
		);
	}

	for (const auto& text : texts)
	{
		ImGui::PushFont(text.font);

		ImVec2 textSize = ImGui::CalcTextSize(text.text.c_str());

		ImVec2 pos = {
			text.position.x - textSize.x * 0.5f,
			text.position.y - textSize.y * 0.5f
		};

		ImGui::SetCursorPos(pos);

		ImGui::PushStyleColor(ImGuiCol_Text, text.color);
		ImGui::Text("%s", text.text.c_str());
		ImGui::PopStyleColor();

		ImGui::PopFont();
	}

	for (auto& pair : selectableTexts) 
	{
		auto text = *pair.second.get();

		ImGui::PushFont(text.font);

		ImVec2 textSize = ImGui::CalcTextSize(text.text.c_str());

		ImVec2 pos = {
			text.position.x - textSize.x * 0.5f,
			text.position.y - textSize.y * 0.5f
		};

		ImGui::SetCursorPos(pos);

		ImGui::PushStyleColor(ImGuiCol_Text, text.color);
		ImGui::Text("%s", text.text.c_str());
		ImGui::PopStyleColor();

		ImGui::PopFont();
	}

	for (int i = 0; i < (int)blockers.size(); i++)
	{
		const auto& blocker = blockers[i];

		ImGui::PushID(i);

		ImGui::SetCursorPos(blocker.position);
		ImGui::InvisibleButton(blocker.label.c_str(), blocker.size);

		ImGui::PopID();
	}

	ImGui::End();
	ImGui::PopStyleColor();
	ImGui::PopStyleVar(2);
}