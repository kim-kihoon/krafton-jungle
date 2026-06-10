#include "Editor/Subsystem/OverlayStatSystem.h"

#include "Editor/EditorEngine.h"
#include "Engine/Profiling/Time/Timer.h"
#include "Engine/Profiling/Stats/MemoryStats.h"
#include "Engine/Profiling/Stats/ShadowStats.h"
#include "Engine/Profiling/Stats/ParticleStats.h"
#include "Engine/Profiling/Stats/ClothCollisionStats.h"
#include "Engine/Profiling/Stats/Stats.h"
#include "GameFramework/World.h"
#include "Physics/IPhysicsScene.h"
#include "Physics/PhysicsRuntime.h"
#include "Physics/PhysicsStats.h"
#include "Engine/Profiling/GPUProfiler.h"
#include "Slate/SWindow.h"
#include "ImGui/imgui.h"
#include <algorithm>
#include <cstdio>
#include <cstring>

// バイト数を適切な単位 (B / KB / MB / GB) に変換して文字列化
static int FormatBytes(char* Buffer, int32 BufferSize, const char* Label, uint64 Bytes)
{
	const double B = static_cast<double>(Bytes);
	const double KB = B / 1024.0;
	const double MB = KB / 1024.0;
	const double GB = MB / 1024.0;

	if (GB >= 1.0)
		return snprintf(Buffer, BufferSize, "%s : %.2f GB", Label, GB);
	if (MB >= 1.0)
		return snprintf(Buffer, BufferSize, "%s : %.2f MB", Label, MB);
	if (KB >= 1.0)
		return snprintf(Buffer, BufferSize, "%s : %.2f KB", Label, KB);
	return snprintf(Buffer, BufferSize, "%s : %llu B", Label, static_cast<unsigned long long>(Bytes));
}

static const char* LexToString(EClothCollisionPrimitiveType Type)
{
	switch (Type)
	{
	case EClothCollisionPrimitiveType::Sphere:
		return "Sphere";
	case EClothCollisionPrimitiveType::Capsule:
		return "Capsule";
	case EClothCollisionPrimitiveType::Box:
		return "Box";
	}
	return "Unknown";
}

static void AppendSelectedWorldCollisionLines(
	TArray<FString>& OutLines,
	const char* Label,
	const TArray<FClothCollisionStatCandidate>& Candidates,
	uint32 MaxDisplayed)
{
	char Buffer[256] = {};
	uint32 DisplayedCandidates = 0;
	for (const FClothCollisionStatCandidate& Candidate : Candidates)
	{
		if (Candidate.State != EClothCollisionSelectState::Selected)
		{
			continue;
		}

		if (DisplayedCandidates == 0)
		{
			snprintf(Buffer, sizeof(Buffer), "Selected %s:", Label);
			OutLines.push_back(FString(Buffer));
		}

		snprintf(Buffer, sizeof(Buffer),
			"  C%u B%d S%d  %s  Center(%.1f %.1f %.1f) Ext(%.1f %.1f %.1f)",
			Candidate.OwnerComponentId,
			Candidate.BodyIndex,
			Candidate.ShapeIndex,
			LexToString(Candidate.Type),
			Candidate.BoundsCenter.X,
			Candidate.BoundsCenter.Y,
			Candidate.BoundsCenter.Z,
			Candidate.BoundsExtent.X,
			Candidate.BoundsExtent.Y,
			Candidate.BoundsExtent.Z);
		OutLines.push_back(FString(Buffer));

		++DisplayedCandidates;
		if (DisplayedCandidates >= MaxDisplayed)
		{
			break;
		}
	}

	if (DisplayedCandidates == 0)
	{
		snprintf(Buffer, sizeof(Buffer), "Selected %s : none", Label);
		OutLines.push_back(FString(Buffer));
	}
}

void FOverlayStatSystem::AppendLine(TArray<FOverlayStatLine>& OutLines, float Y, const FString& Text) const
{
	FOverlayStatLine Line;
	Line.Text = Text;
	Line.ScreenPosition = FVector2(Layout.StartX, Y);
	OutLines.push_back(std::move(Line));
}

void FOverlayStatSystem::RecordPickingAttempt(double ElapsedMs)
{
	LastPickingTimeMs = ElapsedMs;
	AccumulatedPickingTimeMs += ElapsedMs;
	++PickingAttemptCount;
}

void FOverlayStatSystem::BuildFPSLines(const UEditorEngine& Editor, TArray<FString>& OutLines) const
{
	const FTimer* Timer = Editor.GetTimer();
	if (Timer)
	{
		constexpr double FPSAverageWindowSeconds = 0.3;
		const double CurrentTime = Timer->GetTotalTime();

		if (!bFPSAverageInitialized)
		{
			FPSAverageWindowStartTime = CurrentTime;
			FPSAccumulatedFrameTimeMs = 0.0;
			FPSAccumulatedFrameCount = 0;
			bFPSAverageInitialized = true;
		}

		FPSAccumulatedFrameTimeMs += Timer->GetFrameTimeMs();
		++FPSAccumulatedFrameCount;

		const double WindowElapsed = CurrentTime - FPSAverageWindowStartTime;
		if (WindowElapsed >= FPSAverageWindowSeconds && FPSAccumulatedFrameCount > 0)
		{
			const float AverageMS = static_cast<float>(FPSAccumulatedFrameTimeMs / FPSAccumulatedFrameCount);
			const float AverageFPS = AverageMS > 0.0f ? 1000.0f / AverageMS : 0.0f;

			char Buffer[128] = {};
			snprintf(Buffer, sizeof(Buffer), "FPS : %.1f (%.2f ms)", AverageFPS, AverageMS);
			CachedFPSLine = Buffer;

			FPSAverageWindowStartTime = CurrentTime;
			FPSAccumulatedFrameTimeMs = 0.0;
			FPSAccumulatedFrameCount = 0;
		}
	}
	else
	{
		CachedFPSLine = "FPS : 0.0 (0.00 ms)";
		bFPSAverageInitialized = false;
		FPSAccumulatedFrameTimeMs = 0.0;
		FPSAccumulatedFrameCount = 0;
	}

	if (CachedFPSLine.empty())
	{
		CachedFPSLine = "FPS : 0.0 (0.00 ms)";
	}

	OutLines.push_back(CachedFPSLine);

	if (bShowPickingTime)
	{
		char Buffer[160] = {};
		snprintf(Buffer, sizeof(Buffer), "Picking Time %.5f ms : Num Attempts %d : Accumulated Time %.5f ms",
			LastPickingTimeMs,
			static_cast<int32>(PickingAttemptCount),
			AccumulatedPickingTimeMs);
		CachedPickingLine = Buffer;
		OutLines.push_back(CachedPickingLine);
	}
}

void FOverlayStatSystem::BuildMemoryLines(TArray<FString>& OutLines) const
{
	char Buffer[128] = {};

	// 할당 횟수 (단위 없음)
	snprintf(Buffer, sizeof(Buffer), "Allocation Count : %u", MemoryStats::GetTotalAllocationCount());
	OutLines.push_back(FString(Buffer));

	// 바이트 단위 메모리 — 자동 단위 변환 (B/KB/MB/GB)
	struct { const char* Label; uint64 Bytes; } MemEntries[] = {
		{ "Total Allocated",       MemoryStats::GetTotalAllocationBytes() },
		{ "PixelShader Memory",    MemoryStats::GetPixelShaderMemory() },
		{ "VertexShader Memory",   MemoryStats::GetVertexShaderMemory() },
		{ "VertexBuffer Memory",   MemoryStats::GetVertexBufferMemory() },
		{ "IndexBuffer Memory",    MemoryStats::GetIndexBufferMemory() },
		{ "StaticMesh CPU Memory", MemoryStats::GetStaticMeshCPUMemory() },
		{ "Texture Memory",        MemoryStats::GetTextureMemory() },
	};

	for (const auto& Entry : MemEntries)
	{
		FormatBytes(Buffer, sizeof(Buffer), Entry.Label, Entry.Bytes);
		OutLines.push_back(FString(Buffer));
	}
}

void FOverlayStatSystem::BuildShadowLines(TArray<FString>& OutLines) const
{
#if STATS
	char Buffer[128] = {};

	OutLines.push_back(FString("--- Shadow ---"));

	// Shadow map 메모리
	FormatBytes(Buffer, sizeof(Buffer), "Shadow Map Memory", FShadowStats::ShadowMapMemoryBytes);
	OutLines.push_back(FString(Buffer));

	// GPU 시간 (GPUProfiler snapshot에서 "ShadowMapPass" 검색)
	const TArray<FStatEntry>& GPUSnapshot = FGPUProfiler::Get().GetGPUSnapshot();
	double ShadowGpuMs = 0.0;
	for (const FStatEntry& Entry : GPUSnapshot)
	{
		if (Entry.Name && strcmp(Entry.Name, "ShadowMapPass") == 0)
		{
			ShadowGpuMs = Entry.LastTime * 1000.0;
			break;
		}
	}
	snprintf(Buffer, sizeof(Buffer), "Shadow GPU Time : %.3f ms", ShadowGpuMs);
	OutLines.push_back(FString(Buffer));

	// Shadow draw call 수
	snprintf(Buffer, sizeof(Buffer), "Shadow Draw Calls : %u", FShadowStats::ShadowDrawCallCount);
	OutLines.push_back(FString(Buffer));

	// 라이트별 shadow caster 수
	snprintf(Buffer, sizeof(Buffer), "Shadow Casters (Spot: %u  Point: %u  Dir: %u)",
		FShadowStats::SpotLightCasterCount,
		FShadowStats::PointLightCasterCount,
		FShadowStats::DirectionalLightCasterCount);
	OutLines.push_back(FString(Buffer));

	// Shadow-casting 라이트 수
	snprintf(Buffer, sizeof(Buffer), "Shadow Lights (Spot: %u  Point: %u  Dir: %u)",
		FShadowStats::SpotLightShadowCount,
		FShadowStats::PointLightShadowCount,
		FShadowStats::DirectionalLightShadowCount);
	OutLines.push_back(FString(Buffer));

	// directional light CSM Shadow map 해상도
	snprintf(Buffer, sizeof(Buffer), "CSM Shadow Map Resolution : %ux%u",
		FShadowStats::ShadowMapResolution, FShadowStats::ShadowMapResolution);
	OutLines.push_back(FString(Buffer));
#else
	OutLines.push_back(FString("Shadow stats unavailable (STATS=0)"));
#endif
}

void FOverlayStatSystem::BuildSkinningLines(TArray<FString>& OutLines) const
{
#if STATS
	char Buffer[160] = {};

	auto MarkStale = [](FSkinningOverlaySample& Sample)
		{
			Sample.bLive = false;
		};
	auto UpdateSample = [](FSkinningOverlaySample& Sample, const FStatEntry& Entry)
		{
			Sample.bValid = true;
			Sample.bLive = true;
			Sample.LastMs = Entry.LastTime * 1000.0;
			Sample.AvgMs = Entry.AvgTime * 1000.0;
			Sample.CallCount = Entry.CallCount;
		};
	auto AppendSample = [&](const char* Label, const FSkinningOverlaySample& Sample)
		{
			if (!Sample.bValid)
			{
				return;
			}

			snprintf(Buffer, sizeof(Buffer), "%s%s : %.3f ms  avg %.3f  calls %u",
				Label,
				Sample.bLive ? "" : " (last)",
				Sample.LastMs,
				Sample.AvgMs,
				Sample.CallCount);
			OutLines.push_back(FString(Buffer));
		};
	auto AppendModeTotal = [&](const char* Label, const FSkinningOverlaySample& A, const FSkinningOverlaySample& B)
		{
			if (!A.bValid || !B.bValid)
			{
				snprintf(Buffer, sizeof(Buffer), "%s : waiting for samples", Label);
				OutLines.push_back(FString(Buffer));
				return;
			}

			snprintf(Buffer, sizeof(Buffer), "%s : %.3f ms",
				Label,
				A.LastMs + B.LastMs);
			OutLines.push_back(FString(Buffer));
		};

	MarkStale(CPUVertexSkinSample);
	MarkStale(GPUMatrixUploadSample);
	MarkStale(SkeletalPreDepthCPUPathSample);
	MarkStale(SkeletalPreDepthGPUPathSample);

	const TArray<FStatEntry>& CPUSnapshot = FStatManager::Get().GetSnapshot();
	for (const FStatEntry& Entry : CPUSnapshot)
	{
		if (Entry.CallCount == 0)
		{
			continue;
		}
		if (!Entry.Category || strcmp(Entry.Category, "Skinning") != 0)
		{
			continue;
		}

		if (Entry.Name && strcmp(Entry.Name, "CPUSkinning_VertexSkin") == 0)
		{
			UpdateSample(CPUVertexSkinSample, Entry);
		}
		else if (Entry.Name && strcmp(Entry.Name, "GPUSkinning_MatrixUpload") == 0)
		{
			UpdateSample(GPUMatrixUploadSample, Entry);
		}
	}

	const TArray<FStatEntry>& GPUSnapshot = FGPUProfiler::Get().GetGPUSnapshot();
	for (const FStatEntry& Entry : GPUSnapshot)
	{
		if (Entry.CallCount == 0)
		{
			continue;
		}
		if (!Entry.Category || strcmp(Entry.Category, "Skinning") != 0)
		{
			continue;
		}

		if (Entry.Name && strcmp(Entry.Name, "SkeletalPreDepth_GPU_CPUPath") == 0)
		{
			UpdateSample(SkeletalPreDepthCPUPathSample, Entry);
		}
		else if (Entry.Name && strcmp(Entry.Name, "SkeletalPreDepth_GPU_GPUPath") == 0)
		{
			UpdateSample(SkeletalPreDepthGPUPathSample, Entry);
		}
	}

	OutLines.push_back(FString("--- CPU Skinning Mode ---"));
	AppendSample("CPU Vertex Skin", CPUVertexSkinSample);
	AppendSample("GPU Skeletal PreDepth (CPU Path)", SkeletalPreDepthCPUPathSample);
	AppendModeTotal("CPU Mode Total (CPU+GPU approx)", CPUVertexSkinSample, SkeletalPreDepthCPUPathSample);

	OutLines.push_back(FString("--- GPU Skinning Mode ---"));
	AppendSample("GPU Matrix Upload CPU", GPUMatrixUploadSample);
	AppendSample("GPU Skeletal PreDepth (GPU Path)", SkeletalPreDepthGPUPathSample);
	AppendModeTotal("GPU Mode Total (CPU+GPU approx)", GPUMatrixUploadSample, SkeletalPreDepthGPUPathSample);

	if (OutLines.empty())
	{
		OutLines.push_back(FString("No Skinning stats this frame"));
	}
#else
	OutLines.push_back(FString("Skinning stats unavailable (STATS=0)"));
#endif
}

void FOverlayStatSystem::BuildParticleLines(TArray<FString>& OutLines) const
{
#if STATS
	char Buffer[160] = {};

	// --- 개수 ---
	snprintf(Buffer, sizeof(Buffer), "Components : %u   Emitters : %u",
		FParticleStats::ComponentCount, FParticleStats::EmitterCount);
	OutLines.push_back(FString(Buffer));

	snprintf(Buffer, sizeof(Buffer), "Particles : %u  (Sprite %u  Mesh %u)",
		FParticleStats::TotalParticles, FParticleStats::SpriteParticles, FParticleStats::MeshParticles);
	OutLines.push_back(FString(Buffer));

	snprintf(Buffer, sizeof(Buffer), "Peak Particles : %u", FParticleStats::PeakTotalParticles);
	OutLines.push_back(FString(Buffer));

	// --- 이번 프레임 spawn/kill ---
	snprintf(Buffer, sizeof(Buffer), "Spawned : %u   Killed : %u",
		FParticleStats::ParticlesSpawned, FParticleStats::ParticlesKilled);
	OutLines.push_back(FString(Buffer));

	// --- 메모리 ---
	FormatBytes(Buffer, sizeof(Buffer), "GT Particle Mem", FParticleStats::GTMemoryBytes);
	OutLines.push_back(FString(Buffer));

	// 예약(Max) 대비 실사용(Active) — Precache·Address Align 튜닝 지표
	{
		const uint64 ActiveB = FParticleStats::ActiveDataBytes;
		const uint64 ReservedB = FParticleStats::ReservedDataBytes;
		const double UsedPct = ReservedB > 0 ? (100.0 * static_cast<double>(ActiveB) / static_cast<double>(ReservedB)) : 0.0;
		char ActiveStr[48] = {};
		char ReservedStr[48] = {};
		FormatBytes(ActiveStr, sizeof(ActiveStr), "Active", ActiveB);
		FormatBytes(ReservedStr, sizeof(ReservedStr), "Reserved", ReservedB);
		snprintf(Buffer, sizeof(Buffer), "%s / %s (%.1f%% used)", ActiveStr, ReservedStr, UsedPct);
		OutLines.push_back(FString(Buffer));
	}

	// --- CPU 시간 (FStatManager 스냅샷, category "Particles") ---
	double TickMs = 0.0;
	double PrepareMs = 0.0;
	uint32 TickCalls = 0;
	uint32 PrepareCalls = 0;
	const TArray<FStatEntry>& CPUSnapshot = FStatManager::Get().GetSnapshot();
	for (const FStatEntry& Entry : CPUSnapshot)
	{
		if (!Entry.Category || strcmp(Entry.Category, "Particles") != 0) continue;
		if (!Entry.Name) continue;
		if (strcmp(Entry.Name, "ParticleTick") == 0)
		{
			TickMs = Entry.LastTime * 1000.0;
			TickCalls = Entry.CallCount;
		}
		else if (strcmp(Entry.Name, "ParticlePrepareDraw") == 0)
		{
			PrepareMs = Entry.LastTime * 1000.0;
			PrepareCalls = Entry.CallCount;
		}
	}
	snprintf(Buffer, sizeof(Buffer), "CPU Tick : %.3f ms (x%u)   PrepareDraw : %.3f ms (x%u)",
		TickMs, TickCalls, PrepareMs, PrepareCalls);
	OutLines.push_back(FString(Buffer));

	// --- GPU 시간 (GPUProfiler 스냅샷, name "ParticleRender") ---
	// 실제 GPU draw는 per-section 라우팅(DrawCommandBuilder)에서 발생한다. draw dispatch
	// 지점이 정해지면 그 자리에 GPU_SCOPE_STAT_CAT("ParticleRender","Particles")만 추가하면
	// 아래 라인이 자동으로 값을 표시한다 (드로우콜 카운터와 동일 지점).
	double GpuMs = 0.0;
	bool bGpuFound = false;
	const TArray<FStatEntry>& GPUSnapshot = FGPUProfiler::Get().GetGPUSnapshot();
	for (const FStatEntry& Entry : GPUSnapshot)
	{
		if (Entry.Name && strcmp(Entry.Name, "ParticleRender") == 0)
		{
			GpuMs = Entry.LastTime * 1000.0;
			bGpuFound = true;
			break;
		}
	}
	if (bGpuFound)
	{
		snprintf(Buffer, sizeof(Buffer), "GPU Render : %.3f ms", GpuMs);
	}
	else
	{
		snprintf(Buffer, sizeof(Buffer), "GPU Render : (awaiting draw-site)");
	}
	OutLines.push_back(FString(Buffer));

	// --- 드로우콜: PrepareDrawBuffer에서 제출된 파티클 섹션 수 (emitter당 1 draw) ---
	snprintf(Buffer, sizeof(Buffer), "Draw Calls : %u (submitted)", FParticleStats::DrawCalls);
	OutLines.push_back(FString(Buffer));
	if (FParticleStats::SpriteRTInstances > 0 || FParticleStats::MeshRTInstances > 0)
	{
		snprintf(
			Buffer,
			sizeof(Buffer),
			"Sprite RT : inst %u  verts %u  idx %u   Mesh RT : inst %u  verts %u  idx %u",
			FParticleStats::SpriteRTInstances,
			FParticleStats::SpriteRTVertices,
			FParticleStats::SpriteRTIndices,
			FParticleStats::MeshRTInstances,
			FParticleStats::MeshRTVertices,
			FParticleStats::MeshRTIndices);
		OutLines.push_back(FString(Buffer));
	}
	if (FParticleStats::BeamRTStrips > 0)
	{
		snprintf(
			Buffer,
			sizeof(Buffer),
			"Beam RT : strips %u  verts %u  idx %u",
			FParticleStats::BeamRTStrips,
			FParticleStats::BeamRTVertices,
			FParticleStats::BeamRTIndices);
		OutLines.push_back(FString(Buffer));
	}
	if (FParticleStats::RibbonTrailBuilds > 0)
	{
		snprintf(
			Buffer,
			sizeof(Buffer),
			"Ribbon Trails : %u  Budget-Capped : %u  Max Effective Tess : %u",
			FParticleStats::RibbonTrailBuilds,
			FParticleStats::RibbonRuntimeCappedBuilds,
			FParticleStats::RibbonMaxEffectiveTessellation);
		OutLines.push_back(FString(Buffer));

		snprintf(
			Buffer,
			sizeof(Buffer),
			"Ribbon Ctrl Segments : %u  Sample Points : %u  Vertices : %u  Indices : %u",
			FParticleStats::RibbonControlSegments,
			FParticleStats::RibbonSamplePoints,
			FParticleStats::RibbonVertices,
			FParticleStats::RibbonIndices);
		OutLines.push_back(FString(Buffer));
	}
#else
	OutLines.push_back(FString("Particle stats unavailable (STATS=0)"));
#endif
}


void FOverlayStatSystem::BuildPhysicsLines(const UEditorEngine& Editor, TArray<FString>& OutLines) const
{
	char Buffer[192] = {};

	const UWorld* World = Editor.GetWorld();
	if (!World)
	{
		OutLines.push_back(FString("Physics world : unavailable"));
		return;
	}

	const IPhysicsScene* PhysicsScene = World->GetPhysicsScene();
	if (!PhysicsScene)
	{
		OutLines.push_back(FString("Physics scene : unavailable"));
		return;
	}

	const IPhysicsRuntime* Runtime = PhysicsScene->GetRuntime();
	if (!Runtime)
	{
		OutLines.push_back(FString("Physics runtime : unavailable"));
		return;
	}

	const FPhysicsStats Stats = Runtime->GetStats();

	snprintf(Buffer, sizeof(Buffer), "Bodies : %d  (Static %d  Dynamic %d  Kinematic %d)",
		Stats.NumBodies,
		Stats.NumStaticBodies,
		Stats.NumDynamicBodies,
		Stats.NumKinematicBodies);
	OutLines.push_back(FString(Buffer));

	snprintf(Buffer, sizeof(Buffer), "Active : %d   Sleeping : %d   Shapes : %d   Constraints : %d",
		Stats.NumActiveBodies,
		Stats.NumSleepingBodies,
		Stats.NumShapes,
		Stats.NumConstraints);
	OutLines.push_back(FString(Buffer));

	snprintf(Buffer, sizeof(Buffer), "Pairs : Contact %d   Trigger %d",
		Stats.NumContactPairs,
		Stats.NumTriggerPairs);
	OutLines.push_back(FString(Buffer));

	snprintf(Buffer, sizeof(Buffer), "Substeps : %d   Dropped : %d   Accum : %.4f s   Alpha : %.2f",
		Stats.NumSubsteps,
		Stats.NumDroppedSubsteps,
		Stats.AccumulatorSeconds,
		Stats.InterpolationAlpha);
	OutLines.push_back(FString(Buffer));

	snprintf(Buffer, sizeof(Buffer), "Commands : Pending %d   Applied %d   Deferred Destroy %d",
		Stats.NumPendingCommands,
		Stats.NumAppliedCommands,
		Stats.NumDeferredDestroys);
	OutLines.push_back(FString(Buffer));

	snprintf(Buffer, sizeof(Buffer), "Queries : Raycast %d   Sweep %d   Overlap %d",
		Stats.NumRaycasts,
		Stats.NumSweepQueries,
		Stats.NumOverlapQueries);
	OutLines.push_back(FString(Buffer));

	snprintf(Buffer, sizeof(Buffer), "Timing : Pre %.3f  Sim %.3f  Fetch %.3f  Post %.3f ms",
		Stats.PrePhysicsMs,
		Stats.SimulateMs,
		Stats.FetchResultsMs,
		Stats.PostPhysicsMs);
	OutLines.push_back(FString(Buffer));

	snprintf(Buffer, sizeof(Buffer), "Sync : Cmd %.3f  E->P %.3f  P->E %.3f  Snapshot %.3f ms",
		Stats.ApplyCommandsMs,
		Stats.SyncEngineToPhysicsMs,
		Stats.SyncPhysicsToEngineMs,
		Stats.BuildSnapshotMs);
	OutLines.push_back(FString(Buffer));

	snprintf(Buffer, sizeof(Buffer), "Events : Dispatch %.3f ms", Stats.DispatchEventMs);
	OutLines.push_back(FString(Buffer));

	if (Stats.NumVehicles > 0 || Stats.NumVehicleWheels > 0)
	{
		snprintf(Buffer, sizeof(Buffer), "Vehicles : %d   Wheels : %d   In Air : %d",
			Stats.NumVehicles,
			Stats.NumVehicleWheels,
			Stats.NumVehicleWheelInAir);
		OutLines.push_back(FString(Buffer));

		snprintf(Buffer, sizeof(Buffer), "Vehicle Timing : Raycast %.3f  Update %.3f ms",
			Stats.VehicleRaycastMs,
			Stats.VehicleUpdateMs);
		OutLines.push_back(FString(Buffer));
	}
}

void FOverlayStatSystem::BuildClothCollisionLines(TArray<FString>& OutLines) const
{
#if STATS
	char Buffer[256] = {};

	snprintf(Buffer, sizeof(Buffer), "Cloth : Ticks %u   Components %u   Sections %u/%u eligible, %u gathered",
		FClothCollisionStats::TickAttempts,
		FClothCollisionStats::ComponentCount,
		FClothCollisionStats::CollisionEligibleSections,
		FClothCollisionStats::EnabledClothSections,
		FClothCollisionStats::SectionCount);
	OutLines.push_back(FString(Buffer));

	snprintf(Buffer, sizeof(Buffer), "WorldStatic : Sections %u configured, %u enabled   Candidates %u selected / %u total",
		FClothCollisionStats::WorldStaticConfiguredSections,
		FClothCollisionStats::WorldStaticEnabledSections,
		FClothCollisionStats::DebugStats.SelectedWorldStatic,
		FClothCollisionStats::WorldStaticCandidateCount);
	OutLines.push_back(FString(Buffer));

	snprintf(Buffer, sizeof(Buffer), "WorldDynamic : Sections %u configured, %u enabled   Candidates %u selected / %u total",
		FClothCollisionStats::WorldDynamicConfiguredSections,
		FClothCollisionStats::WorldDynamicEnabledSections,
		FClothCollisionStats::DebugStats.SelectedWorldDynamic,
		FClothCollisionStats::WorldDynamicCandidateCount);
	OutLines.push_back(FString(Buffer));

	snprintf(Buffer, sizeof(Buffer), "WorldStatic Types : Selected S%u C%u B%u   Rejected %u   Truncated %u",
		FClothCollisionStats::WorldStaticSelectedSpheres,
		FClothCollisionStats::WorldStaticSelectedCapsules,
		FClothCollisionStats::WorldStaticSelectedBoxes,
		FClothCollisionStats::DebugStats.RejectedWorldStatic,
		FClothCollisionStats::DebugStats.TruncatedWorldStatic);
	OutLines.push_back(FString(Buffer));

	snprintf(Buffer, sizeof(Buffer), "WorldDynamic Types : Selected S%u C%u B%u   Rejected %u   Truncated %u",
		FClothCollisionStats::WorldDynamicSelectedSpheres,
		FClothCollisionStats::WorldDynamicSelectedCapsules,
		FClothCollisionStats::WorldDynamicSelectedBoxes,
		FClothCollisionStats::DebugStats.RejectedWorldDynamic,
		FClothCollisionStats::DebugStats.TruncatedWorldDynamic);
	OutLines.push_back(FString(Buffer));

	snprintf(Buffer, sizeof(Buffer), "Uploaded : Spheres %u   Capsules %u   Planes %u   Convexes %u",
		FClothCollisionStats::DebugStats.UploadedSpheres,
		FClothCollisionStats::DebugStats.UploadedCapsules,
		FClothCollisionStats::DebugStats.UploadedPlanes,
		FClothCollisionStats::DebugStats.UploadedConvexes);
	OutLines.push_back(FString(Buffer));

	const uint32 SkipCount =
		FClothCollisionStats::SkippedNoAsset +
		FClothCollisionStats::SkippedNoClothPayload +
		FClothCollisionStats::SkippedNonCPUSkinning +
		FClothCollisionStats::SkippedNoSkinnedVertices;
	const uint32 IssueCount =
		SkipCount +
		FClothCollisionStats::MissingPhysicsRuntimeSections +
		FClothCollisionStats::InvalidSectionBounds +
		FClothCollisionStats::WorldStaticRejectedBySectionBounds +
		FClothCollisionStats::WorldStaticSkippedFilter +
		FClothCollisionStats::WorldDynamicRejectedBySectionBounds +
		FClothCollisionStats::WorldDynamicSkippedFilter +
		FClothCollisionStats::DebugStats.SkippedNonUniformScale;
	if (IssueCount > 0)
	{
		const uint32 FilteredCount =
			FClothCollisionStats::WorldStaticSkippedFilter +
			FClothCollisionStats::WorldDynamicSkippedFilter;
		snprintf(Buffer, sizeof(Buffer), "Issues : EarlySkips %u   MissingRuntime %u   InvalidBounds %u   Filtered %u",
			SkipCount,
			FClothCollisionStats::MissingPhysicsRuntimeSections,
			FClothCollisionStats::InvalidSectionBounds,
			FilteredCount);
		OutLines.push_back(FString(Buffer));
	}

	AppendSelectedWorldCollisionLines(OutLines, "WorldStatic", FClothCollisionStats::RecentWorldStaticCandidates, 4);
	AppendSelectedWorldCollisionLines(OutLines, "WorldDynamic", FClothCollisionStats::RecentWorldDynamicCandidates, 4);
#else
	OutLines.push_back(FString("Cloth collision stats unavailable (STATS=0)"));
#endif
}

void FOverlayStatSystem::BuildLines(const UEditorEngine& Editor, TArray<FOverlayStatLine>& OutLines) const
{
	OutLines.clear();

	uint32 EstimatedLineCount = 0;
	if (bShowFPS)
	{
		++EstimatedLineCount;
	}
	if (bShowPickingTime)
	{
		++EstimatedLineCount;
	}
	if (bShowMemory)
	{
		EstimatedLineCount += 8;
	}
	if (bShowShadow)
	{
		EstimatedLineCount += 8;
	}
	if (bShowSkinning)
	{
		EstimatedLineCount += 4;
	}
	if (bShowParticles)
	{
		EstimatedLineCount += 9;
	}
	if (bShowPhysics)
	{
		EstimatedLineCount += 11;
	}
	if (bShowClothCollision)
	{
		EstimatedLineCount += 12;
	}
	OutLines.reserve(EstimatedLineCount);

	TArray<FString> Lines;
	float CurrentY = Layout.StartY;
	auto AppendGroup = [&](const TArray<FString>& GroupLines)
		{
			for (const FString& Line : GroupLines)
			{
				AppendLine(OutLines, CurrentY, Line);
				CurrentY += Layout.LineHeight;
			}
			if (!GroupLines.empty())
			{
				CurrentY += Layout.GroupSpacing;
			}
		};

	if (bShowFPS)
	{
		Lines.clear();
		BuildFPSLines(Editor, Lines);
		AppendGroup(Lines);
	}

	if (bShowMemory)
	{
		Lines.clear();
		BuildMemoryLines(Lines);
		AppendGroup(Lines);
	}

	if (bShowShadow)
	{
		Lines.clear();
		BuildShadowLines(Lines);
		AppendGroup(Lines);
	}

	if (bShowSkinning)
	{
		Lines.clear();
		BuildSkinningLines(Lines);
		AppendGroup(Lines);
	}

	if (bShowParticles)
	{
		Lines.clear();
		BuildParticleLines(Lines);
		AppendGroup(Lines);
	}

	if (bShowPhysics)
	{
		Lines.clear();
		BuildPhysicsLines(Editor, Lines);
		AppendGroup(Lines);
	}

	if (bShowClothCollision)
	{
		Lines.clear();
		BuildClothCollisionLines(Lines);
		AppendGroup(Lines);
	}
}

TArray<FOverlayStatLine> FOverlayStatSystem::BuildLines(const UEditorEngine& Editor) const
{
	TArray<FOverlayStatLine> Result;
	BuildLines(Editor, Result);
	return Result;
}

void FOverlayStatSystem::RenderImGui(const UEditorEngine& Editor, const FRect& ViewportRect) const
{
	if (ViewportRect.Width <= 1.0f || ViewportRect.Height <= 1.0f)
	{
		return;
	}

	constexpr float PaddingX = 10.0f;
	constexpr float PaddingY = 30.0f;
	constexpr float WindowGap = 6.0f;
	constexpr float ColumnGap = 8.0f;
	const float ViewportLeft = ViewportRect.X;
	const float ViewportTop = ViewportRect.Y;
	const float ViewportRight = ViewportRect.X + ViewportRect.Width;
	const float ViewportBottom = ViewportRect.Y + ViewportRect.Height;

	float CurrentX = ViewportLeft + PaddingX;
	float CurrentY = ViewportTop + PaddingY;
	float CurrentColumnWidth = 0.0f;

	ImGuiWindowFlags Flags =
		ImGuiWindowFlags_NoDecoration |
		ImGuiWindowFlags_AlwaysAutoResize |
		ImGuiWindowFlags_NoSavedSettings |
		ImGuiWindowFlags_NoFocusOnAppearing |
		ImGuiWindowFlags_NoNav |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoInputs;

	auto RenderWindow = [&](const char* WindowID, const char* Title, const ImVec4& BgColor, const TArray<FString>& Lines)
		{
			if (Lines.empty())
			{
				return;
			}

			const float EstimatedHeight =
				ImGui::GetTextLineHeightWithSpacing() * (static_cast<float>(Lines.size()) + 1.0f) +
				ImGui::GetStyle().WindowPadding.y * 2.0f;
			if (CurrentY > ViewportTop + PaddingY && CurrentY + EstimatedHeight > ViewportBottom - PaddingY)
			{
				CurrentX += CurrentColumnWidth + ColumnGap;
				CurrentY = ViewportTop + PaddingY;
				CurrentColumnWidth = 0.0f;
			}
			CurrentX = (std::max)(ViewportLeft + PaddingX, (std::min)(CurrentX, ViewportRight - PaddingX - 40.0f));

			ImGui::SetNextWindowPos(ImVec2(CurrentX, CurrentY), ImGuiCond_Always);
			ImGui::SetNextWindowBgAlpha(BgColor.w);
			ImGui::PushStyleColor(ImGuiCol_WindowBg, BgColor);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.0f);

			ImGui::Begin(WindowID, nullptr, Flags);
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 0.95f), "%s", Title);
			ImGui::Separator();
			for (const FString& Line : Lines)
			{
				ImGui::TextUnformatted(Line.c_str());
			}
			const ImVec2 WindowSize = ImGui::GetWindowSize();
			ImGui::End();

			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor();

			CurrentY += WindowSize.y + WindowGap;
			CurrentColumnWidth = (std::max)(CurrentColumnWidth, WindowSize.x);
		};

	TArray<FString> Lines;
	if (bShowFPS)
	{
		BuildFPSLines(Editor, Lines);
		RenderWindow("##StatFPSOverlay", "Stat FPS", ImVec4(0.05f, 0.09f, 0.12f, 0.62f), Lines);
	}

	if (bShowMemory)
	{
		Lines.clear();
		BuildMemoryLines(Lines);
		RenderWindow("##StatMemoryOverlay", "Stat Memory", ImVec4(0.10f, 0.07f, 0.04f, 0.62f), Lines);
	}

	if (bShowShadow)
	{
		Lines.clear();
		BuildShadowLines(Lines);
		RenderWindow("##StatShadowOverlay", "Stat Shadow", ImVec4(0.08f, 0.05f, 0.12f, 0.62f), Lines);
	}

	if (bShowSkinning)
	{
		Lines.clear();
		BuildSkinningLines(Lines);
		RenderWindow("##StatSkinningOverlay", "Stat Skinning", ImVec4(0.05f, 0.10f, 0.08f, 0.62f), Lines);
	}

	if (bShowParticles)
	{
		Lines.clear();
		BuildParticleLines(Lines);
		RenderWindow("##StatParticlesOverlay", "Stat Particles", ImVec4(0.04f, 0.08f, 0.10f, 0.62f), Lines);
	}

	if (bShowPhysics)
	{
		Lines.clear();
		BuildPhysicsLines(Editor, Lines);
		RenderWindow("##StatPhysicsOverlay", "Stat Physics", ImVec4(0.09f, 0.08f, 0.05f, 0.62f), Lines);
	}

	if (bShowClothCollision)
	{
		Lines.clear();
		BuildClothCollisionLines(Lines);
		RenderWindow("##StatClothCollisionOverlay", "Stat Cloth Collision", ImVec4(0.08f, 0.06f, 0.04f, 0.62f), Lines);
	}
}
