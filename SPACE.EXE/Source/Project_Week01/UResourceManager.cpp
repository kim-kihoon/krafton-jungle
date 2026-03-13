#include "UResourceManager.h"

static const ImWchar full_korean_ranges[] =
{
	0x0020, 0x00FF,   // Basic Latin + Latin Supplement
	0x3131, 0x3163,   // Hangul Jamo
	0xAC00, 0xD7A3,   // Hangul Syllables
	0,
};

//	Manager의 Boot에서 Call
void UResourceManager::Initialize(ID3D11Device* device , ID3D11DeviceContext * deviceContext)
{
	this->Device = device;
	this->DeviceContext = deviceContext;

	/* Image Setting */

	// 기본 텍스처 로드 (경로는 프로젝트에 맞게 수정)
	 LoadTexture("Background", L"res/image/Background.png");
	 LoadTexture("InGamePanel", L"res/image/InGamePanel.png");
	 LoadTexture("LeaderBoardPanel", L"res/image/LeaderBoardPanel.png");
	 LoadTexture("ButtonSprite", L"res/image/ButtonSprite.png");
	 LoadTexture("JustWhite", L"res/image/White.png");



	 ImageLoadTexture(ImageName::ROCKET, L"res/image/rocket.png");
	 ImageLoadTexture(ImageName::PLANET1, L"res/image/planet1.png");
	 ImageLoadTexture(ImageName::PLANET2, L"res/image/planet2.png");
	 ImageLoadTexture(ImageName::PLANET3, L"res/image/planet3.png");
	 ImageLoadTexture(ImageName::PLANET4, L"res/image/planet4.png");
	 ImageLoadTexture(ImageName::PLANET5, L"res/image/planet5.png");
	 ImageLoadTexture(ImageName::PLANET6, L"res/image/planet6.png");
	 ImageLoadTexture(ImageName::PLANET7, L"res/image/planet7.png");
	 ImageLoadTexture(ImageName::PLANET8, L"res/image/planet8.png");
	 ImageLoadTexture(ImageName::PLANET9, L"res/image/planet9.png");
	 ImageLoadTexture(ImageName::PLANET10, L"res/image/planet10.png");
	 ImageLoadTexture(ImageName::PLANET11, L"res/image/planet11.png");
	 ImageLoadTexture(ImageName::PLANET13, L"res/image/planet13.png");
	 ImageLoadTexture(ImageName::SUN, L"res/image/sun.png");
	 ImageLoadTexture(ImageName::GOAL, L"res/image/goal.png");

	SRVBackground = GetTexture("Background");
	SRVInGamePanel = GetTexture("InGamePanel");
	SRVLeaderBoardPanel = GetTexture("LeaderBoardPanel");
	SRVButtonSprite = GetTexture("ButtonSprite");

	/* Fonts Setting */
	ImGuiIO& io = ImGui::GetIO();

	//	Logo Fonts
	FontLogo = io.Fonts->AddFontFromFileTTF
	(
		"res\\fonts\\GravityBold8.ttf",
		108.f,
		nullptr,
		io.Fonts->GetGlyphRangesDefault()
	);

	FontLogoSmall = io.Fonts->AddFontFromFileTTF
	(
		"res\\fonts\\GravityBold8.ttf",
		60.f,
		nullptr,
		io.Fonts->GetGlyphRangesDefault()
	);

	//	Default Fonts
	FontDefault = io.Fonts->AddFontFromFileTTF
	(
		"res\\fonts\\GravityRegular5.ttf",
		32.f,
		nullptr,
		io.Fonts->GetGlyphRangesDefault()
	);

	FontDefaultSmall = io.Fonts->AddFontFromFileTTF
	(
		"res\\fonts\\GravityRegular5.ttf",
		26.f,
		nullptr,
		io.Fonts->GetGlyphRangesDefault()
	);

	FontDefaultVerySmall = io.Fonts->AddFontFromFileTTF
	(
		"res\\fonts\\GravityRegular5.ttf",
		16.f,
		nullptr,
		io.Fonts->GetGlyphRangesDefault()
	);

	FontInfoBold = io.Fonts->AddFontFromFileTTF
	(
		"res\\fonts\\NEXONLv1GothicBold.ttf",
		60.f,
		nullptr,
		io.Fonts->GetGlyphRangesKorean()
	);

	FontInfoBoldSmall = io.Fonts->AddFontFromFileTTF
	(
		"res\\fonts\\NEXONLv1GothicBold.ttf",
		42.f,
		nullptr,
		io.Fonts->GetGlyphRangesKorean()
	);

	FontInfoRegular = io.Fonts->AddFontFromFileTTF
	(
		"res\\fonts\\NEXONLv1GothicRegular.ttf",
		36.f,
		nullptr,
		io.Fonts->GetGlyphRangesKorean()
	);

	FontInfoRegularSmall = io.Fonts->AddFontFromFileTTF
	(
		"res\\fonts\\NEXONLv1GothicRegular.ttf",
		22.f,
		nullptr,
		io.Fonts->GetGlyphRangesKorean()
	);

	FontInfoLight = io.Fonts->AddFontFromFileTTF
	(
		"res\\fonts\\NEXONLv1GothicLight.ttf",
		17.f,
		nullptr,
		io.Fonts->GetGlyphRangesKorean()
	);

	/* Tip List Setting */
	tipList.push_back("Building this in four days was quite a challenge.");
	tipList.push_back("To climb the rankings, you need to find the fastest path.");
	tipList.push_back("Not bad, right?");
	tipList.push_back("The food at Jungle is amazing.");
	tipList.push_back("Arrive 25 minutes after meal time starts and you won\'t have to wait in line.");
	tipList.push_back("Thursday team dinner is going to be fun.");
	tipList.push_back("Buy a dollar.");
	tipList.push_back("Fatal Error : Merge Conflict");

	/* Loading List Setting */
	loadingList.push_back("Loading");
	loadingList.push_back("Loading.");
	loadingList.push_back("Loading..");
	loadingList.push_back("Loading...");

	/* How To Play List Setting */
	howToPlayList.push_back("[How To Play]");
	//	TODO : Make how to play

	bIsInitialzed = true;
}

//	Manager의 Shutdown에서 Call
void UResourceManager::Release()
{
	if (!bIsInitialzed) return;

	SRVBackground->Release();
	SRVInGamePanel->Release();
	SRVLeaderBoardPanel->Release();
	SRVButtonSprite->Release();

	//	Font는 ImGui Release 시 자동으로 정리됨.
	//	따로 nullptr 초기화 안함.

	for (int i = 0; i < ImageName::COUNT; i++)
	{
		if (TextureResources[i]) {
			TextureResources[i] = nullptr;
			TextureResources[i] -> Release();
		}
	}

}

bool UResourceManager::LoadTexture(const std::string& key, const wchar_t* filePath)
{
	if (!Device || !filePath)
		return false;

	auto found = TextureMap.find(key);
	if (found != TextureMap.end() && found->second)
		return true;

	ID3D11ShaderResourceView* srv = nullptr;
	HRESULT hr = DirectX::CreateWICTextureFromFile(Device, filePath, nullptr, &srv);
	if (FAILED(hr) || !srv)
		return false;

	TextureMap[key] = srv;
	return true;
}

ID3D11ShaderResourceView* UResourceManager::GetTexture(const std::string& key) const
{
	auto found = TextureMap.find(key);
	if (found == TextureMap.end())
		return nullptr;

	return found->second;
}

std::string UResourceManager::GetRandomTips() const
{
	if (!bIsInitialzed) return "[UResourceManager] Please initialize first.";

	std::random_device rd;
	std::mt19937 mt(rd());
	std::uniform_int_distribution<int> dist(0, tipList.size() - 1);

	return tipList[dist(mt)];
}

// texture 읽기
HRESULT UResourceManager::ImageLoadTexture(ImageName type, std::wstring filePath)
{
	ID3D11ShaderResourceView* srv = nullptr;
	HRESULT hr = DirectX::CreateWICTextureFromFile(Device, DeviceContext, filePath.c_str(), nullptr, &srv);

	if (SUCCEEDED(hr))
	{
		if (TextureResources[type]) TextureResources[type]->Release();
		TextureResources[type] = srv;
	}

	return hr;
}



ID3D11ShaderResourceView* UResourceManager::GetTexture(ImageName type)
{
	return TextureResources[type];
}

