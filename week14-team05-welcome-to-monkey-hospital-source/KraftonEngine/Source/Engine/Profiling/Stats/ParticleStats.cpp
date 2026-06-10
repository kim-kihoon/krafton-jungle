#include "ParticleStats.h"

#if STATS
uint32 FParticleStats::SpriteParticles = 0;
uint32 FParticleStats::MeshParticles = 0;
uint32 FParticleStats::TotalParticles = 0;
uint32 FParticleStats::PeakTotalParticles = 0;
uint32 FParticleStats::EmitterCount = 0;
uint32 FParticleStats::ComponentCount = 0;
uint32 FParticleStats::ParticlesSpawned = 0;
uint32 FParticleStats::ParticlesKilled = 0;
uint32 FParticleStats::DrawCalls = 0;
uint64 FParticleStats::GTMemoryBytes = 0;
uint64 FParticleStats::ActiveDataBytes = 0;
uint64 FParticleStats::ReservedDataBytes = 0;
uint32 FParticleStats::SpriteRTInstances = 0;
uint32 FParticleStats::SpriteRTVertices = 0;
uint32 FParticleStats::SpriteRTIndices = 0;
uint32 FParticleStats::MeshRTInstances = 0;
uint32 FParticleStats::MeshRTVertices = 0;
uint32 FParticleStats::MeshRTIndices = 0;
uint32 FParticleStats::BeamRTStrips = 0;
uint32 FParticleStats::BeamRTVertices = 0;
uint32 FParticleStats::BeamRTIndices = 0;
uint32 FParticleStats::RibbonTrailBuilds = 0;
uint32 FParticleStats::RibbonRuntimeCappedBuilds = 0;
uint32 FParticleStats::RibbonMaxEffectiveTessellation = 0;
uint32 FParticleStats::RibbonControlSegments = 0;
uint32 FParticleStats::RibbonSamplePoints = 0;
uint32 FParticleStats::RibbonVertices = 0;
uint32 FParticleStats::RibbonIndices = 0;
#endif
