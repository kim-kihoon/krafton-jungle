#pragma once

#include "Render/Types/RenderTypes.h"
#include "Render/Types/RenderStateTypes.h"
#include "Math/Vector.h"
#include "Core/Types/CoreTypes.h"
#include "Render/Types/MaterialTextureSlot.h"

class FShader;
class FConstantBuffer;
struct ID3D11ShaderResourceView;
struct ID3D11Buffer;

// DrawCommand용 통합 지오메트리 버퍼 — Static MeshBuffer와 Dynamic Buffer를 단일 타입으로 취급
// 버퍼 자원 + 드로우 범위(어디부터 얼마나 그릴 것인가)를 함께 보관
struct FDrawCommandBuffer
{
	ID3D11Buffer* VB       = nullptr;
	uint32        VBStride = 0;
	ID3D11Buffer* IB       = nullptr;

	uint32 FirstIndex  = 0;              // 인덱스 시작 오프셋
	uint32 IndexCount  = 0;              // DrawIndexed 인덱스 수
	uint32 VertexCount = 0;              // IB 없을 때 Draw(VertexCount, 0)
	int32  BaseVertex  = 0;              // DrawIndexed BaseVertexLocation

	// 인스턴싱 (Mesh particle 등) — InstanceCount > 0 이면 DrawIndexedInstanced/DrawInstanced 경로.
	// InstanceVB는 slot 1로 바인딩 (slot 0은 위 정적 VB).
	uint32        InstanceCount    = 0;
	ID3D11Buffer* InstanceVB       = nullptr;
	uint32        InstanceVBStride = 0;

	bool HasBuffers() const { return VB != nullptr; }
};

// 렌더 상태 — DepthStencil / Blend / Rasterizer를 한 단위로 묶어 비교·복사
struct FDrawCommandRenderState
{
	EDepthStencilState       DepthStencil = EDepthStencilState::Default;
	EBlendState              Blend        = EBlendState::Opaque;
	ERasterizerState         Rasterizer   = ERasterizerState::SolidBackCull;
	D3D11_PRIMITIVE_TOPOLOGY Topology     = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
};

// 셰이더 리소스 바인딩 — PerShaderCB + SRVs (per-material/section 단위로 갱신)

// t0~t7: material texture slots
// t8~t12: forward light / tile / cluster buffers
// t16 + : scene / system / shadow resources
// t14 ~ t15 : 비어있음
struct FDrawCommandBindings
{
	FConstantBuffer*          PerShaderCB[2] = {};                           // [0]=b2, [1]=b3
	FConstantBuffer*          MeshScalarOverlayCB = nullptr;                 // b6: ViewModeMesh scalar overlay
	ID3D11ShaderResourceView* SRVs[(int)(EMaterialTextureSlot::Max)] = {};   // t0 ~ t7
	ID3D11ShaderResourceView* SkinMatrixSRV = nullptr;						 // t13
};

/*
	FDrawCommand — 드로우콜 1개에 필요한 모든 정보를 캡슐화합니다.
	UE5의 FMeshDrawCommand 패턴을 차용하여,
	PSO 상태 + Geometry + Bindings + 정렬 키를 하나의 구조체로 통합합니다.
*/
struct FDrawCommand
{
	// ===== 핵심 식별 =====
	ERenderPass  Pass      = ERenderPass::Opaque;
	FShader*     Shader    = nullptr;

	// ===== 렌더 상태 =====
	FDrawCommandRenderState RenderState;

	// ===== Geometry =====
	FDrawCommandBuffer Buffer;                        // VB + IB + 드로우 범위 (HasBuffers() == false → SV_VertexID 기반 드로우)

	// ===== Bindings =====
	FConstantBuffer*    PerObjectCB = nullptr;        // b1: Model + Color (per-proxy)
	FDrawCommandBindings Bindings;                    // PerShaderCB + SRVs (per-material)

	// ===== Sort =====
	uint64 SortKey = 0;                              // 정렬 키 (Pass → Shader → MeshBuffer → SRV)

	// ===== Profiling =====
	bool bIsSkeletal = false;
	bool bIsGpuSkinned = false;

	// Fullscreen triangle 초기화 (PostProcess 등 SV_VertexID 기반 드로우)
	void InitFullscreenTriangle(FShader* InShader, ERenderPass InPass, const FDrawCommandRenderState& InRenderState)
	{
		Shader      = InShader;
		Pass        = InPass;
		RenderState = InRenderState;
		Buffer.VertexCount = 3;
	}

	// Cmd의 Pass/Shader/Buffer.VB/Bindings.SRVs[Diffuse]로부터 SortKey 자동 생성.
	// Pass == Transparent면 depth-first 정렬 키 (back-to-front), 그 외엔 상태 그룹핑 키.
	// CameraDistSquared는 Transparent 경로에서만 사용 — 다른 패스에선 0 전달해도 무방.
	void BuildSortKey(uint16 UserBits = 0, float CameraDistSquared = 0.0f)
	{
		if (Pass == ERenderPass::Transparent)
		{
			SortKey = ComputeTransparentSortKey(Pass, Shader, CameraDistSquared, UserBits);
		}
		else
		{
			SortKey = ComputeSortKey(Pass, Shader, Buffer.VB,
				Bindings.SRVs[(int)EMaterialTextureSlot::Diffuse], UserBits);
		}
	}

	// ===== SortKey 생성 유틸리티 (정적) =====
	// 일반 패스: Pass(5) | ShaderHash(16) | MeshHash(16) | SRVHash(16) | UserBits(11)
	// 상태 전환(셰이더/메시/텍스처) 그룹핑 위주.
	static uint64 ComputeSortKey(ERenderPass InPass, const FShader* InShader,
		const void* InMeshId, const ID3D11ShaderResourceView* InSRV,
		uint16 UserBits = 0)
	{
		auto PtrHash16 = [](const void* Ptr) -> uint16
		{
			// 포인터를 16비트로 축소 — 상태 전환 그룹핑용이므로 충돌 허용
			uintptr_t Val = reinterpret_cast<uintptr_t>(Ptr);
			return static_cast<uint16>((Val >> 4) ^ (Val >> 20));
		};

		uint64 Key = 0;
		Key |= (static_cast<uint64>(InPass) & 0x1F) << 59;          // [63:59] Pass (5비트, 최대 32패스)
		Key |= (static_cast<uint64>(PtrHash16(InShader))) << 43;     // [58:43] Shader
		Key |= (static_cast<uint64>(PtrHash16(InMeshId))) << 27;      // [42:27] MeshBuffer
		Key |= (static_cast<uint64>(PtrHash16(InSRV))) << 11;        // [26:11] SRV
		Key |= (static_cast<uint64>(UserBits) & 0x7FF);              // [10:0]  User (11비트)
		return Key;
	}

	// Transparent 전용: Pass(5) | DepthBucket(28) | UserBits(15) | ShaderHash(16)
	// DepthBucket = MAX_28BIT - quantize(CameraDistSquared) — 멀수록 작은 키 → 먼저 그림 (back-to-front).
	// 같은 깊이 버킷 안에서 UserBits를 ShaderHash보다 먼저 둬서 겹친 transparent section의 순서를 강제할 수 있다.
	static uint64 ComputeTransparentSortKey(ERenderPass InPass, const FShader* InShader,
		float CameraDistSquared, uint16 UserBits = 0)
	{
		auto PtrHash16 = [](const void* Ptr) -> uint16
		{
			uintptr_t Val = reinterpret_cast<uintptr_t>(Ptr);
			return static_cast<uint16>((Val >> 4) ^ (Val >> 20));
		};

		// 거리² → 28-bit 양자화. 100,000 unit (10^5) 이상은 saturate.
		// MaxDistSq = (10^5)² = 10^10
		constexpr uint32 MAX_BUCKET = 0x0FFFFFFFu;
		constexpr float  MAX_DIST_SQ = 1.0e10f;
		const float Scaled = CameraDistSquared * (static_cast<float>(MAX_BUCKET) / MAX_DIST_SQ);
		const uint32 Q = Scaled >= static_cast<float>(MAX_BUCKET)
			? MAX_BUCKET
			: (CameraDistSquared <= 0.0f ? 0u : static_cast<uint32>(Scaled));
		const uint32 Bucket = MAX_BUCKET - Q;  // 멀면 작은 키 → 먼저 그림

		uint64 Key = 0;
		Key |= (static_cast<uint64>(InPass) & 0x1F) << 59;                // [63:59] Pass (5비트)
		Key |= (static_cast<uint64>(Bucket) & 0x0FFFFFFFull) << 31;        // [58:31] DepthBucket (역양자화)
		Key |= (static_cast<uint64>(UserBits) & 0x7FFF) << 16;             // [30:16] UserBits (15비트)
		Key |= static_cast<uint64>(PtrHash16(InShader));                   // [15:0]  ShaderHash
		return Key;
	}
};
