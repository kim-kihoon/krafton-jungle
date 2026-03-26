#include "ParticleSubUVComponent.h"
#include "ResourceManager.h"
#include "Renderer/RenderObject.h"
#include "World.h"
#include "Renderer/Renderer.h"

UParticleSubUVComp::UParticleSubUVComp(const wchar_t* InTexture)
{
	type = EPrimitiveType::ParticleSubUV;
	ResourceManager* RM = ResourceManager::GetInstance();
	VResource = &RM->GetGeometry("ParticleSubUV");

	LocalBoundingBox = FBoundingBox::FromPoints(VResource->Vertices);
	Name = FName("ParticleSubUV");
    SetTexture(InTexture);
}

UParticleSubUVComp::~UParticleSubUVComp()
{
    for (auto& renderObj : RenderObjs)
    {
        delete renderObj;
        renderObj = nullptr;
    }
}

static void QueryTextureDimensions(ID3D11ShaderResourceView* SRV, int& OutWidth, int& OutHeight)
{
    OutWidth = OutHeight = 0;
    if (!SRV) return;
    ID3D11Resource* Res = nullptr;
    SRV->GetResource(&Res);
    if (!Res) return;
    ID3D11Texture2D* Tex = nullptr;
    Res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&Tex));
    if (Tex)
    {
        D3D11_TEXTURE2D_DESC Desc;
        Tex->GetDesc(&Desc);
        OutWidth  = static_cast<int>(Desc.Width);
        OutHeight = static_cast<int>(Desc.Height);
        Tex->Release();
    }
    Res->Release();
}

void UParticleSubUVComp::SetTexture(const wchar_t* path)
{
    TexturePath = path ? path : L"";
    ElapsedTime = 0.0f;
    SubUVData.CurrentFrame = 0;

    TComPtr<ID3D11ShaderResourceView> NewSRV = ResourceManager::GetInstance()->LoadTextureSRV(TexturePath.c_str());
    QueryTextureDimensions(NewSRV.Get(), SubUVData.TextureWidth, SubUVData.TextureHeight);

    for (auto& renderObj : RenderObjs)
    {
        if (renderObj && renderObj->SubUV)
        {
            renderObj->SubUV->SRV = NewSRV;
            renderObj->SubUV->TextureWidth  = SubUVData.TextureWidth;
            renderObj->SubUV->TextureHeight = SubUVData.TextureHeight;
        }
    }
}

void UParticleSubUVComp::Update(float DeltaTime)
{
    UPrimitiveComponent::Update(DeltaTime);
    TickComponent(DeltaTime);
}

void UParticleSubUVComp::TickComponent(float DeltaTime)
{
    if (!bLoop && LastFrame)
        return;
    SubUVData.Columns = Columns;
    SubUVData.Rows = Rows;
    ElapsedTime += DeltaTime;
    SubUVData.CurrentFrame = static_cast<int32>(ElapsedTime * PlayRate) % (Columns * Rows);

    LastFrame = false;
    if (SubUVData.CurrentFrame == Columns * Rows - 1)
    {
        LastFrame = true;
    }
}

void UParticleSubUVComp::CreateRenderObjects()
{
    URenderer* Renderer = GetWorld().GetRenderer();
    if (!Renderer)
        return;

    ResourceManager* RM = ResourceManager::GetInstance();
    RenderObject* renderObj = new RenderObject();
    renderObj->Material = &RM->GetMaterial(L"Asset/Shader/ShaderSubUV.hlsl");
    renderObj->Geometry = VResource;
    renderObj->World = GetRelativeMatrix();
    renderObj->bDepthEnabled = true;

    SubUVData.CB = &Renderer->SubUVConstantBuffer;
    SubUVData.Sampler = Renderer->SamplerState.Get();
    SubUVData.SRV = ResourceManager::GetInstance()->LoadTextureSRV(TexturePath.c_str());
    QueryTextureDimensions(SubUVData.SRV.Get(), SubUVData.TextureWidth, SubUVData.TextureHeight);
    renderObj->SubUV = &SubUVData;

	RenderObjs.push_back(renderObj);
}

json::JSON UParticleSubUVComp::Serialize()
{
    json::JSON json = UPrimitiveComponent::Serialize();
    json["Columns"] = Columns;
    json["Rows"] = Rows;
    json["PlayRate"] = PlayRate;
    json["bLoop"] = bLoop;
    json["TexturePath"] = std::string(TexturePath.begin(), TexturePath.end());
    return json;
}

void UParticleSubUVComp::Deserialize(json::JSON json)
{
    UPrimitiveComponent::Deserialize(json);
    Columns = json["Columns"].ToInt();
    Rows = json["Rows"].ToInt();
    PlayRate = json["PlayRate"].ToFloat();
    bLoop = json["bLoop"].ToBool();
    std::string texPath = json["TexturePath"].ToString();
    SetTexture(std::wstring(texPath.begin(), texPath.end()).c_str());
}

REGISTER_CLASS(UParticleSubUVComp);