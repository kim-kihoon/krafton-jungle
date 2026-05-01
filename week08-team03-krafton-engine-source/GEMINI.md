# GEMINI.md: Shadow Mapping Rendering Pipeline

## 1. 필수 구현 과제 (Core Implementation)

### 1.1 기본 렌더링 요건
* **조건부 렌더링:** `ULightComponentBase::bCastShadows`가 `true`일 때만 PSM(Perspective Shadow Map) 기반 그림자 렌더링 수행.
* **ViewMode 대응:** `Unlit` 모드에서는 그림자 연산 및 렌더링을 완전히 비활성화.
* **동적 렌더링 (Mobility):** 모든 라이트와 오브젝트는 `Movable`로 간주 (Pre-computed/Baked Shadow 사용 안 함. 100% 실시간 연산).

### 1.2 광원 지원 (Multi-Light Support)
* **동시 처리:** 1개의 Directional Light + $n$개의 Point Light + $n$개의 Spot Light 환경 대응.
* **Point Light 대응:** 전방위 그림자 처리를 위한 `TextureCubeArray` 및 Cube Map 렌더링 로직 필요.

### 1.3 에디터 및 디버깅 기능
* **Light Perspective Override:** 광원의 시점(Light's perspective)으로 카메라 뷰를 전환하는 기능 구현 (절두체 및 섀도우 맵 정렬 디버깅 용도).
* **Depth Map 시각화:** Light Property 창에 현재 광원의 PSM Depth Map을 실시간으로 표시 (`Visualize Depth Map`).
* **해상도 제어:** 에디터 상에서 `ShadowResolutionScale` 변수를 통해 섀도우 맵 해상도 동적 조절.
* **ShowFlag & Stat:** 사용된 섀도우 맵 메모리량, 현재 활성화된 라이트 종류별 개수 등을 화면에 출력.

## 2. 그림자 품질 및 아티팩트 제어 (Quality Control)

### 2.1 그림자 여드름(Shadow Acne) 및 피터팬 현상 제어
다음 변수들을 활용하여 Z-Test 정밀도 오차 보정:
* `ShadowBias`: Constant Bias 적용 (균일한 깊이 오프셋).
* `ShadowSlopeBias`: Slope-Scaled Bias 적용 (표면 기울기에 비례하는 오프셋).

### 2.2 안티앨리어싱 및 필터링
* **PCF (Percentage-Closer Filtering):** 그림자 경계선을 부드럽게(Soft Edge) 처리하는 기본 필터 구현.
* **VSM (Variance Shadow Map) - *선택 학습*:** * 깊이의 평균과 분산을 저장하여 확률적으로 그림자를 판정하는 고급 필터링.
    * `ULightComponent::ShadowSharpen` 변수와 연동하여 그림자 선명도 제어.
    * 콘솔 명령어 (`shadow_filter VSM`, `shadow_filter PCF`)를 통한 런타임 스위칭 구현.

## 3. 핵심 렌더링 수식 설계 (Bouding Box & Matrix)

### 3.1 광원 시점 좌표계 정렬 (Light-Space Alignment)
* **목적:** 섀도우 맵(2D 텍스처)의 모눈 격자(Grid)와 광원의 시선 축을 일치시켜 해상도 낭비 원천 봉쇄.
* **수행:** 월드 축 AABB가 아닌, 메인 카메라 절두체(MCF)의 정점들을 **광원 뷰 공간(Light Space)으로 변환 후 생성한 AABB**를 기반으로 직교/원근 투영 행렬을 구성함.

### 3.2 PSM (Perspective Shadow Map) 파이프라인
* 메인 카메라의 원근 변환이 적용된 NDC 공간 데이터를 광원의 시점으로 가로채어 투영함.
* 카메라에 가까운 프래그먼트가 섀도우 맵의 더 많은 텍셀을 점유하도록 공간을 왜곡(Warping)하여 근거리 해상도 확보.

## 4. 심화 아키텍처 (Advanced Architecture - 선택 학습)

### 4.1 Shadow Atlas
* **목적:** 다수의 광원(Point, Spot 등)이 각각 개별 텍스처를 할당받는 것을 방지.
* **구현:** 하나의 거대한 텍스처 맵(Atlas)을 구역별로 쪼개어 여러 라이트의 Depth Map을 한 번에 저장.
* **효과:** Pixel Shader로 전달하는 SRV(Shader Resource View)의 개수를 최소화하여 렌더링 부하 감소 및 State Change 비용 절감.

### 4.2 CSM (Cascaded Shadow Map)
* **목적:** Directional Light의 원거리/근거리 해상도 불균형 해소.
* **구현:** 메인 카메라 절두체를 Z축(깊이) 기준으로 분할(Cascade)하고, 각 구역마다 타이트한 Light-Space AABB를 씌워 여러 장의 섀도우 맵을 렌더링.
* 콘솔 명령어를 통한 제어 및 캐스케이드 관련 Stat 출력 기능 포함.