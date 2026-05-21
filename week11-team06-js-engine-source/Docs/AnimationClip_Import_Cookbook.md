# Animation Clip Import Cookbook

팀원이 애니메이션 import 결과를 바로 찾아 쓰기 위한 짧은 지도다.

## 핵심 구조체

```cpp
// Clip 하나. FBX AnimStack 하나가 보통 FAnimationClip 하나가 된다.
struct FAnimationClip
{
    TArray<FBoneAnimationTrack> BoneTracks;
    TArray<FShapeKeyAnimationTrack> ShapeKeyTracks;
    float DurationSeconds;
    float SourceFrameRate;
    int32 NumberOfKeys;
};

// Bone 하나의 transform track. runtime pose는 InternalTrack을 본다.
struct FBoneAnimationTrack
{
    FString BoneName;
    FRawAnimSequenceTrack InternalTrack;
};

// 샘플링된 bone transform key. 각 배열의 길이는 NumberOfKeys 또는 1개(상수인 경우)다.
struct FRawAnimSequenceTrack
{
    TArray<FVector> PosKeys;
    TArray<FQuat> RotKeys;
    TArray<FVector> ScaleKeys;
};

// Shape key 하나의 weight curve. 0~100 FBX weight는 0~1로 정규화되어 들어온다.
struct FShapeKeyAnimationTrack
{
    FString MeshNodeName;
    FString ShapeKeyName;
    FAnimationFloatCurve WeightCurve;
};
```

## 애니메이션을 로드하고 싶다

```cpp
// Path는 FBX 또는 animation clip path다.
UAnimationClipAsset* Asset = ResourceManager.LoadAnimationClip(Path);
// 실제 imported data는 UAnimationClipAsset이 소유한다.
const FAnimationClip* Clip = Asset ? Asset->GetClipData() : nullptr;
// clip 길이는 wrapper API로도 바로 볼 수 있다.
const float PlayLength = Asset ? Asset->GetPlayLength() : 0.0f;
```

## FBX에서 애니메이션을 직접 import하고 싶다

```cpp
// 기본 옵션은 bone transform과 shape key curve를 모두 import한다.
FAnimationImportOptions Options;
// FBX AnimStack들이 FAnimationClip 배열로 변환된다.
TArray<FAnimationClip*> Clips = FbxImporter.LoadAnimations(Path, Options);
// 사용 후 소유권 정리가 필요하다.
FAnimationClip* FirstClip = Clips.empty() ? nullptr : Clips[0];
```

## Bone transform key를 읽고 싶다

```cpp
// BoneTracks는 bone 이름별 transform animation track이다.
const FBoneAnimationTrack& Track = Clip->BoneTracks[TrackIndex];
// InternalTrack이 runtime pose용 sampled raw key다.
const FRawAnimSequenceTrack& Raw = Track.InternalTrack;
// Pos/Rot/Scale 배열은 NumberOfKeys개 또는 상수면 1개다.
const FVector Pos0 = Raw.PosKeys[0];
```

## 현재 시간의 bone key index를 구하고 싶다

```cpp
// SourceFrameRate 기준으로 import 때 샘플링했다.
const float Frame = TimeSeconds * Clip->SourceFrameRate;
// 아직 공용 evaluator가 없으므로 직접 clamp해서 key를 고른다.
const int32 KeyIndex = std::clamp(static_cast<int32>(Frame), 0, Clip->NumberOfKeys - 1);
// 상수 트랙은 1키만 있으니 배열별로 한 번 더 clamp한다.
const FVector Pos = Raw.PosKeys[std::min<int32>(KeyIndex, static_cast<int32>(Raw.PosKeys.size()) - 1)];
```

## Shape key curve를 읽고 싶다

```cpp
// ShapeKeyTracks는 mesh node와 shape key 이름을 같이 들고 있다.
const FShapeKeyAnimationTrack& ShapeTrack = Clip->ShapeKeyTracks[TrackIndex];
// WeightCurve는 shape key weight용 editable float curve다.
const FAnimationFloatCurve& Curve = ShapeTrack.WeightCurve;
// 값은 FBX 0~100에서 engine 0~1 범위로 정규화되어 저장된다.
const FString& ShapeName = ShapeTrack.ShapeKeyName;
```

## Binary cache를 저장하거나 읽고 싶다

```cpp
// SourcePath 기준으로 animation binary cache 경로를 만든다.
const FString BinPath = FAssetPathPolicy::MakeWritableAnimationClipCacheBinaryPath(SourcePath);
// FAnimationClip 전체를 binary cache로 저장한다.
AnimationClipSerializer.SaveAnimationClip(BinPath, SourcePath, *Clip);
// cache freshness 확인만 필요하면 header만 읽는다.
FAnimationClipBinaryHeader Header; AnimationClipSerializer.ReadAnimationClipHeader(BinPath, Header);
```

## 현재 구현 범위

```cpp
// 완료: FBX AnimStack을 FAnimationClip으로 변환한다.
// 완료: bone transform은 sampled raw track으로, shape key는 float curve로 import한다.
// 미완: runtime pose evaluator, skeleton 적용, skinning 반영, preview playback, retargeting, curve blending.
```
