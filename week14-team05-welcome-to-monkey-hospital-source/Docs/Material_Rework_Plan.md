# 머티리얼 시스템 개편 설계/계획 (Material Rework Plan)

> 작업 브랜치: `feature/material-rework` (main 머지 완료)
> 담당: P3 (렌더)
> 상태: **Phase 1~5 전부 구현 완료(빌드 통과) + 코드리뷰 수정 + 에디터 아이콘 오버레이 패스. main 머지 완료.** — 남은 것은 **에디터 런타임 시각 검증**(아이콘/반투명/정렬 회귀).
> 최종 갱신: 2026-05-26
> 관련: 머지된 `feature/serialize-template-method` (UObject::Serialize Template Method)

이 문서는 머티리얼 시스템의 문제들 — 반투명 블렌드 처리, **저수준 렌더상태 커플링**, **셰이더↔지오메트리 커플링**, JSON 직렬화, 에디터 — 를 진단하고, **"머티리얼은 의도(intent)만 선언하고 엔진이 렌더상태와 셰이더를 모두 도출"** 하는 하나의 일관된 개편으로 해결하는 설계와 작업 순서를 정리한다.

---

## 1. 핵심 원칙

머티리얼은 **의도(intent)만** 선언한다 — `Domain` + `BlendMode` + (shader-agnostic 기본) + 파라미터/텍스처. 엔진이 그로부터 **렌더상태와 셰이더를 모두 도출**한다. 도출로 표현 불가한 특수 케이스만 **명시적 override(escape hatch)** 로 둔다.

| 머티리얼이 선언(intent) | 엔진이 도출 | 특수 override |
|---|---|---|
| `Domain` (Surface/PostProcess/UI/Decal) | RenderPass | — |
| `BlendMode` (Opaque/Masked/Translucent/Additive/Modulate) | BlendState + DepthStencil | Rasterizer override(스프라이트 NoCull 등) |
| (shader-agnostic) | **Shader** = Resolve(Domain, **VertexFactory**, Pass, ViewMode) | **Custom shader** override(명시적 경로) |
| 파라미터/텍스처 | — | — |

---

## 2. 현재 상태 진단

### 2.1 Translucent 패스 / 블렌드 — 패스 구조는 건전, 선언 방식이 문제
- 반투명은 **단일 패스**(`Render/RenderPass/TranslucentPass.cpp:6`)에서 모든 블렌드를 처리하고 back-to-front 정렬(`Render/Command/DrawCommand.h:135`)을 쓴다. **이 구조는 올바르며 유지한다.**
- 블렌드는 per-DrawCommand로 `Material->GetBlendState()` → `ApplyMaterialRenderState`(`DrawCommandBuilder.cpp:136`) → StateCache(`DrawCommandList.cpp:173`). 불명확한 건 패스가 아니라 **머티리얼이 블렌드를 선언하는 방법**(→2.2).

### 2.2 저수준 렌더상태 커플링 (Phase 2 대상)
- `UMaterial`이 저수준 4개를 직접 소유(`Materials/Material.h`): `ERenderPass`/`EBlendState`/`EDepthStencilState`/`ERasterizerState`.
- 같은 정보가 머티리얼·Proxy·JSON·에디터에 **중복**. `MaterialManager::StringTo*`(`MaterialManager.cpp:249-317`)가 Pass 기반 기본값을 부분 도출하나 JSON에 Pass를 직접 기입해야 동작.
- UE식 **Material Domain 개념 부재** — `ERenderPass`(16개)가 "머티리얼 종류"와 "파이프라인 단계"를 뭉뚱그림.

### 2.3 셰이더 ↔ 지오메트리 커플링 (Phase 3 대상, 신규 진단)
- 머티리얼이 `ShaderPath`를 소유 → `FMaterialTemplate` → `FShader`. **`FShader`가 InputLayout을 VS 리플렉션으로 자체 소유**(`Render/Shader/Shader.cpp:267`)하므로 셰이더는 특정 정점 레이아웃에 묶인다(`FVertexPNCT` static vs `FVertexPNCTBW` skeletal vs 파티클 instance 정점, `Render/Types/VertexTypes.h`).
- 그래서 같은 머티리얼을 시스템 간 공유하기 어렵다. 현재 처리:
  - `SelectEffectiveShader`(`DrawCommandBuilder.cpp:106`)가 **UberLit일 때만** 특례로 `(LightingModel × VertexFactory{Static/Skeletal} × heatmap)` 퍼뮤테이션 교체(`ShaderManager.h:154-222`).
  - 비-UberLit 커스텀 셰이더는 **그냥 통과** → 레이아웃 불일치 시 깨짐.
  - 파티클은 머티리얼 셰이더를 **무시**하고 `FParticleVertexFactory`(Sprite/Mesh/Beam/Ribbon) 자체 셰이더 사용 → "셰이더 지정했는데 무시" 혼란.
- 즉 **VertexFactory(지오메트리)가 셰이더를 정해야 하는데 머티리얼이 경로를 들고 있어** 생기는 비대칭/혼란.

### 2.4 직렬화 — 머티리얼만 JSON, 그러나 바이너리 코드는 이미 존재 (Phase 4 대상)
- 머티리얼만 `.mat`(JSON): `SaveToJSON`(`MaterialManager.cpp:319`). 다른 에셋은 `.uasset`(`FWindowsBinWriter`+`FArchive`, 예 `Particle/ParticleSystemManager.cpp:73`).
- **`UMaterial::Serialize(FArchive&)`가 이미 존재**(CB+텍스처 경로, `Material.cpp:260-350`)하나 디스크엔 안 쓰임 — 직렬화 이원화. 바이너리 통합 = 이미 있는 경로를 디스크에 연결 + JSON 제거에 가깝다.

### 2.5 에디터 — JSON 텍스트에 직접 결합 (Phase 5 대상)
- `FEditorMaterialInspector`가 `CachedJson`을 직접 편집(`Editor/UI/Panel/EditorMaterialInspector.cpp:18`). 파라미터/텍스처만 가능, 렌더상태·셰이더·Domain은 편집 불가(JSON 수동). "Create Material" 팩토리 없음.

### 2.6 MaterialInstance (참고)
- `UMaterialInstance : UMaterial` 머지됨(`Materials/MaterialInstance.h`). `.mat`의 `Parent` 키로 자동 생성, Parent 복제 + override. **개편 시 같은 도출·직렬화 경로를 타야 함.**

---

## 3. 도출 모델 (단일 진실)

### 3.1 고수준 의도 enum (Phase 1 완료)
```cpp
enum class EMaterialDomain : uint8 { Surface, PostProcess, UI, Decal, MAX };
enum class EBlendMode      : uint8 { Opaque, Masked, Translucent, Additive, Modulate, MAX };
```
`ResolveMaterialRenderState(Domain, BlendMode) → FMaterialRenderState{Pass, Blend, DepthStencil, Rasterizer}` (`Materials/MaterialDomain.h`).

### 3.2 렌더상태 도출 규칙
| Domain | BlendMode | Pass | Blend | Depth | Raster |
|---|---|---|---|---|---|
| Surface | Opaque/Masked | Opaque | Opaque | Default | SolidBackCull |
| Surface | Translucent | Translucent | AlphaBlend | **DepthReadOnly** | SolidBackCull |
| Surface | Additive | Translucent | Additive | DepthReadOnly | SolidBackCull |
| Surface | Modulate | Translucent | Modulate | DepthReadOnly | SolidBackCull |
| PostProcess | — | PostProcess | (Opaque) | NoDepth | SolidNoCull |
| Decal | Opaque/Translucent | Decal | (Blend별) | DepthReadOnly | SolidNoCull |
| Decal | Additive | AdditiveDecal | Additive | DepthReadOnly | SolidNoCull |

> **depth 결정**: Translucent는 depth-write 안 하는 게 일반적으로 올바르므로 `DepthReadOnly`로 도출(파티클 .mat 명시값과 일치). 기존 에디터 반투명(라이트/포그 6종)은 Default→DepthReadOnly로 **의도된 동작 변경**(런타임 시각 확인 대상).
> **사용자 결정**: depth는 사용자 필드에서 도려내 도출. `.mat`의 `DepthStencilState`는 권위 아님.

### 3.3 셰이더 도출 (Phase 3 — 구현 완료)
- 머티리얼은 셰이더를 **모름**(shader-agnostic 기본). 셰이더 = `ResolveShader(Domain, EVertexFactoryType, Pass, ViewMode)`.
- `EVertexFactoryType`(StaticMesh/SkeletalMesh/ParticleSprite/Mesh/Beam/...)를 지오메트리(Proxy/Section/Factory)가 제공 → **셰이더 퍼뮤테이션의 축**. 기존 `SelectEffectiveShader`의 UberLit 특례를 **모든 표준 머티리얼의 기본 경로로 일반화**.
- **Custom shader override**: `bUseCustomShader` + `CustomShaderPath`. 설정 시 그 셰이더 강제(현재 통과 동작) — 작성자가 정점 레이아웃 락을 인지하는 특수 케이스(사용자 결정).
- **파티클 흡수**: "VertexFactory가 셰이더를 정한다"가 파티클에도 적용 → 기존 "머티리얼 셰이더 무시"가 **설계상 일관**이 되어 혼란 해소.

### 3.4 도출 불가 케이스 = 명시 override
- **Rasterizer**: 스프라이트 NoCull 등 지오메트리성 차이 → raster override(이상적으론 Phase 3에서 vertex factory로 이동).
- **CreateTransient**(Gizmo/Decal/Text/SubUV): Domain으로 표현 안 되는 고정 패스/상태 → 저수준 4개 모두 override로 정확 보존.

---

## 4. 작업 순서 (Phase)

각 Phase는 독립 빌드/검증 가능하도록 쪼갠다.

1. **Phase 1 ✅ — 의도 enum + 도출 함수** (`MaterialDomain.h`). 빌드 통과.
2. **Phase 2 ✅ — 렌더상태 도출**: `UMaterial`이 `Domain`+`BlendMode` 단일 소스로 Pass/Blend/Depth/Raster 도출. 도출 불가는 override(.mat=raster, CreateTransient=4개). 빌드 통과.
3. **Phase 3 ✅ — 셰이더 디커플링**: shader-agnostic 도출 `ResolveSectionShader(Mat, EVertexFactoryType, ViewMode, bGPUSkinning, bWeightBoneHeatMap)`(`DrawCommandBuilder.cpp`). `EVertexFactoryType`(StaticMesh/Skeletal/ParticleSprite/Mesh/Beam/Ribbon) 도입, custom shader override, 파티클 VF 전용 셰이더 흡수. 빌드 통과.
4. **Phase 4 ✅ — 바이너리(.uasset) 직렬화**: `UMaterial::Serialize`를 디스크에 연결, `FMaterialManager` Save/Load 바이너리화(`FWindowsBinWriter`+`FAssetPackageHeader`), **JSON(.mat)·SimpleJSON 의존 전부 제거**, 임포터 바이너리 출력, 하드코딩 경로·컨텐트브라우저·인스펙터 `.uasset`화. `.mat`→`.uasset` lazy 마이그레이션(경로 정규화 shim). 빌드 통과.
5. **Phase 5 ✅ — 에디터 재구축**: 객체 기반 인스펙터, Domain/BlendMode 드롭다운, custom-shader 토글 + **셰이더 선택 드롭다운(非UberLit 선택 시 자동 custom ON)**, **Two Sided 토글**, **Save 버튼**, **Create Material 팩토리**. 수기 ImGui 콤보라 UENUM/codegen 불필요. 빌드 통과.

---

## 4b. 계획 외 추가 구현 / 수정 (진행 중 발견)

- **텍스처 t0~t7 셰이더 리플렉션**: `FShader::ExtractTextureBindings`(D3D `D3D_SIT_TEXTURE`, register t0~t7) → `FMaterialTemplate`/`UMaterial::GetTextureBindings`. 인스펙터가 셰이더 선언 텍스처 슬롯을 노출. `RebuildCachedSRVs`는 리플렉션 바인딩(이름→register, 없으면 슬롯 규칙명 폴백) 기반 — Billboard 셰이더의 `BillboardTex@t0` 처럼 셰이더 변수명과 저장 파라미터명(`DiffuseTexture`)이 달라도 정확히 바인딩.
- **인스펙터 텍스처 피커**: `Content/` 전체 재귀 스캔(아이콘 PNG가 `Content/Editor/Icons`에 위치) + 확장자 대소문자 무관(`.PNG` 등) + 드롭다운 hover 썸네일(`FEditorTextureManager`).
- **코드 리뷰 수정**: 리플렉션 `FMaterialParameterInfo` 누수 → `shared_ptr` 소유; `~FMaterialManager` 가드 정정(`if(Device)`); `MaterialTextureSlot::ToString`의 `throw` 제거; 죽은 `UMaterial::Bind()` 제거; sRGB 휴리스틱 단일화(`IsSRGBTextureSlot`); CB 업로드 이중 경로 제거(`FlushDirtyBuffers`에 위임); `SetTextureParameter`를 리플렉션 규칙과 일치.
- **에디터 아이콘 오버레이 패스 (`ERenderPass::EditorIcon`)**: 라이트/포그/데칼 아이콘 빌보드가 Translucent(DepthReadOnly, 불투명 이후)에서 깊이를 안 써 풀스크린 포그/합성에 덮여 **빈 배경에서 사라지던 회귀** 해결. 포스트프로세스/FXAA 이후 **NoDepth + AlphaBlend** 오버레이 패스 신설(`FEditorIconPass`, OverlayFont 미러). `FBillboardSceneProxy::GetRenderPass()`가 `EditorIcon` 반환(`PrimitiveSceneProxy::GetRenderPass()` virtual화)으로 라우팅. → 깊이/포그 분리, 소프트 엣지 유지.
- **SortKey Pass 필드 4→5비트 확장**(`DrawCommand.h`의 `ComputeSortKey`/`ComputeTranslucentSortKey`, UserBits 1비트 차용): `ERenderPass`가 16개로 꽉 차 17번째(EditorIcon) 추가가 불가하던 한도 해소(최대 32패스). GammaCorrection(16)도 안전 인코딩.

---

## 5. ⚠️ 설계 정정 (초기안 대비)

- 초기안의 "`Pass == Mat->GetRenderPass()` 조건 제거"는 **오판정**. 이 조건은 PreDepth/SelectionMask 등 유틸 패스에서 머티리얼 블렌드/뎁스를 적용하지 않기 위한 **올바른 게이트**(`DrawCommandBuilder.cpp:248`)이므로 **유지**한다. `GetRenderPass()`가 도출 Pass를 반환하게 해서 라우팅이 그대로 맞는다. (`FPrimitiveSceneProxy::GetRenderPass()`가 `SectionDraws[0].Material->GetRenderPass()`를 반환 = 머티리얼이 라우팅 권위, `PrimitiveSceneProxy.cpp:28`.)
- 초기안에 없던 **셰이더↔지오메트리 커플링**(§2.3)을 핵심 문제로 추가.
- **에디터 아이콘 = home-pass 게이트 의도적 우회**(§4b): 빌보드를 `EditorIcon` 패스로 라우팅하면 머티리얼 home pass(Translucent) ≠ EditorIcon 이라 `ApplyMaterialRenderState` 게이트가 **실패** → 패스의 `NoDepth/AlphaBlend`가 그대로 적용되고 머티리얼은 셰이더/텍스처/파라미터만 제공. 오버레이엔 의도된 동작(머티리얼별 blend/depth는 이 패스에서 비적용).

---

## 6. 리스크 / 오픈 이슈

### 남은 검증 (런타임 시각 확인 — 미완)
- **아이콘 오버레이 패스**: 라이트/포그/데칼 아이콘이 빈 배경에서도 보이고 소프트 엣지 정상인지.
- **SortKey 5비트 회귀**: 반투명 back-to-front 정렬, 파티클/데칼/기즈모/포그/감마 패스 순서가 정상인지 (Pass 비트 폭 변경 영향).
- **인스펙터**: 텍스처 드롭다운 Content/ 전체 PNG(대문자 포함)·hover 썸네일, Domain/Blend/Two Sided/custom-shader 토글 + Save round-trip.

### 잔존 이슈 / 후속 후보
- **Rasterizer override**: 스프라이트 NoCull은 지오메트리성 → 장기적으로 vertex factory로 이동 후보(현재 override 슬롯·Two Sided 토글로 처리).
- **Masked 미연결**: `BlendMode::Masked`는 렌더상태(Opaque)만 맞고 UberLit에 clip 퍼뮤테이션이 없어 실제 마스킹 미동작. 필요 시 `MASKED` define 퍼뮤테이션 + 알파 컷오프 파라미터 추가.
- **라이팅 모델이 ViewMode 종속**: 머티리얼이 셰이딩 모델(Unlit/Lit)을 고정 못 함(뷰가 결정). UE Shading Model과 차이 — custom 셰이더로 우회.
- **셰이더/텍스처 참조**: 문자열 경로 유지. GUID/AssetRegistry화는 범위 밖.
- **포맷 버전**: `FAssetPackageHeader::IsValid`가 버전 정확 일치 요구 → 직렬화 필드 추가 시 기존 `.uasset` 일괄 재저장 필요(graceful 다운그레이드 없음).

---

## 7. 리뷰 요청 포인트 (P1 / P2)

- **P1 (직렬화)**: Phase 4 `.uasset` 통합 — 마이그레이션 전략, `SerializeExtra` 훅 기반 CB blob 직렬화.
- **P2 (에디터)**: Phase 5 에디터 재구축 범위 — Domain/Blend UI, custom-shader 토글, Create 팩토리, JSON 의존 제거.
- **공통**: §3.2 도출 규칙이 현 동작과 일치하는지(특히 Decal/파티클), §3.3 셰이더 도출 모델 방향.

---

## 부록: 주요 참조 파일

| 영역 | 파일:라인 |
|---|---|
| 의도 enum + 도출 | `Materials/MaterialDomain.h` |
| UMaterial / getter | `Materials/Material.h`, `.cpp` |
| MaterialManager (역매핑/도출) | `Materials/MaterialManager.cpp` |
| MaterialInstance | `Materials/MaterialInstance.h/.cpp` |
| 셰이더 선택/퍼뮤테이션 | `Render/Command/DrawCommandBuilder.cpp:106`, `Render/Shader/ShaderManager.h:154` |
| 셰이더/InputLayout | `Render/Shader/Shader.cpp:267` |
| 정점 포맷 | `Render/Types/VertexTypes.h` |
| 파티클 vertex factory | `Render/Particle/ParticleVertexFactory.h` |
| 패스 라우팅 권위 | `Render/Proxy/PrimitiveSceneProxy.cpp:28`, home-게이트 `Render/Command/DrawCommandBuilder.cpp:248` |
| Translucent 패스/정렬 | `Render/RenderPass/TranslucentPass.cpp:6`, `Render/Command/DrawCommand.h:135` |
| 바이너리 에셋 저장 참조 | `Particle/ParticleSystemManager.cpp:73` |
| 머티리얼 에디터 | `Editor/UI/Panel/EditorMaterialInspector.cpp:18` |
| FBX 머티리얼 import | `Mesh/.../FbxMaterialImporter.cpp:237` |
