#include "Scene/SceneTypes.h"
#include "Scene/SceneData.h"
#include "Math/Frustum.h"
#include <Core/AppTypes.h>
#include <algorithm>
#include <stack>
#include <vector>

namespace Scene
{
    void FSceneBVH::Build(const FSceneDataSOA& InSceneData)
    {
        this->SceneData = &InSceneData;

        Nodes.clear();
        ObjectIndices.clear();

        uint32_t Count = SceneData->TotalObjectCount;
        if (Count == 0) return;

        ObjectIndices.reserve(Count);
        for (uint32_t i = 0; i < Count; ++i) ObjectIndices.push_back(i);

        Nodes.reserve(Count * 2);
        Nodes.emplace_back();

        struct BuildState { uint32_t NodeIndex; uint32_t ObjStart; uint32_t ObjCount; };
        std::vector<BuildState> Stack;
        Stack.push_back({ 0, 0, Count });

        while (!Stack.empty())
        {
            BuildState State = Stack.back();
            Stack.pop_back();

            // Calculate bounds
            Math::FBox Bounds;
            for (uint32_t i = 0; i < State.ObjCount; ++i)
            {
                uint32_t ObjIdx = ObjectIndices[State.ObjStart + i];
                Bounds.Expand({ SceneData->MinX[ObjIdx], SceneData->MinY[ObjIdx], SceneData->MinZ[ObjIdx] });
                Bounds.Expand({ SceneData->MaxX[ObjIdx], SceneData->MaxY[ObjIdx], SceneData->MaxZ[ObjIdx] });
            }
            Nodes[State.NodeIndex].Bounds = Bounds;
            Nodes[State.NodeIndex].ObjectIndex = 0;
            Nodes[State.NodeIndex].ObjectCount = 0;

            if (State.ObjCount <= 16) // Max 16 objects per leaf
            {
                Nodes[State.NodeIndex].ObjectIndex = State.ObjStart;
                Nodes[State.NodeIndex].ObjectCount = State.ObjCount;
                continue;
            }

            // Split
            float SizeX = Bounds.Max.x - Bounds.Min.x;
            float SizeY = Bounds.Max.y - Bounds.Min.y;
            float SizeZ = Bounds.Max.z - Bounds.Min.z;
            int Axis = (SizeX > SizeY && SizeX > SizeZ) ? 0 : (SizeY > SizeZ ? 1 : 2);

            float SplitPos = 0.0f;
            for (uint32_t i = 0; i < State.ObjCount; ++i)
            {
                uint32_t ObjIdx = ObjectIndices[State.ObjStart + i];
                float Center = (Axis == 0) ? (SceneData->MinX[ObjIdx] + SceneData->MaxX[ObjIdx]) * 0.5f :
                               (Axis == 1) ? (SceneData->MinY[ObjIdx] + SceneData->MaxY[ObjIdx]) * 0.5f :
                                             (SceneData->MinZ[ObjIdx] + SceneData->MaxZ[ObjIdx]) * 0.5f;
                SplitPos += Center;
            }
            SplitPos /= (float)State.ObjCount;

            auto It = std::partition(ObjectIndices.begin() + State.ObjStart, ObjectIndices.begin() + State.ObjStart + State.ObjCount,
                [&](uint32_t ObjIdx) {
                    float Center = (Axis == 0) ? (SceneData->MinX[ObjIdx] + SceneData->MaxX[ObjIdx]) * 0.5f :
                                   (Axis == 1) ? (SceneData->MinY[ObjIdx] + SceneData->MaxY[ObjIdx]) * 0.5f :
                                                 (SceneData->MinZ[ObjIdx] + SceneData->MaxZ[ObjIdx]) * 0.5f;
                    return Center < SplitPos;
                });

            uint32_t LeftCount = static_cast<uint32_t>(std::distance(ObjectIndices.begin() + State.ObjStart, It));

            if (LeftCount == 0 || LeftCount == State.ObjCount) LeftCount = State.ObjCount / 2;

            uint32_t LeftIdx = (uint32_t)Nodes.size();
            Nodes.emplace_back();
            uint32_t RightIdx = (uint32_t)Nodes.size();
            Nodes.emplace_back();

            Nodes[State.NodeIndex].LeftChild = LeftIdx;
            Nodes[State.NodeIndex].RightChild = RightIdx;

            Stack.push_back({ RightIdx, State.ObjStart + LeftCount, State.ObjCount - LeftCount });
            Stack.push_back({ LeftIdx, State.ObjStart, LeftCount });
        }
    }

    void FSceneBVH::QueryFrustum(const Math::FFrustum& Frustum, uint32_t* OutObjectIndices, uint32_t& OutCount, uint32_t MaxCount) const
    {
        if (Nodes.empty()) return;

        std::vector<uint32_t> Stack;
        Stack.reserve(256);
        Stack.push_back(0);

        while (!Stack.empty())
        {
            uint32_t NodeIdx = Stack.back();
            Stack.pop_back();

            const FSceneBVHNode& Node = Nodes[NodeIdx];

            Math::ECullingResult Result = Frustum.TestBox(Node.Bounds);

            if (Result == Math::ECullingResult::Outside) continue;

            if (Result == Math::ECullingResult::FullyInside)
            {
                // Optimistically add all objects in this subtree
                std::vector<uint32_t> SStack;
                SStack.reserve(128);
                SStack.push_back(NodeIdx);

                while (!SStack.empty())
                {
                    uint32_t SNodeIdx = SStack.back();
                    SStack.pop_back();

                    const FSceneBVHNode& SNode = Nodes[SNodeIdx];
                    if (SNode.IsLeaf())
                    {
                        for (uint32_t i = 0; i < SNode.ObjectCount; ++i)
                        {
                            uint32_t ObjIdx = ObjectIndices[SNode.ObjectIndex + i];

                            // 오브젝트 단위 Sphere Culling ---
                            if (Frustum.TestSphere(
                                SceneData->CenterX[ObjIdx],
                                SceneData->CenterY[ObjIdx],
                                SceneData->CenterZ[ObjIdx],
                                SceneData->Radius[ObjIdx]) != Math::ECullingResult::Outside)
                            {
                                if (OutCount < MaxCount) OutObjectIndices[OutCount++] = ObjIdx;
                            }
                        }
                    }
                    else
                    {
                        SStack.push_back(SNode.RightChild);
                        SStack.push_back(SNode.LeftChild);
                    }
                }
                continue;
            }

            if (Node.IsLeaf())
            {
                for (uint32_t i = 0; i < Node.ObjectCount; ++i)
                {
                    uint32_t ObjIdx = ObjectIndices[Node.ObjectIndex + i];
                    if (OutCount < MaxCount) OutObjectIndices[OutCount++] = ObjIdx;
                }
            }
            else
            {
                Stack.push_back(Node.RightChild);
                Stack.push_back(Node.LeftChild);
            }
        }
    }

    bool FSceneBVH::Raycast(const Math::FRay& Ray, float MaxDistance, uint32_t& OutHitIndex, float& OutHitDistance, NarrowPhaseFunc NarrowPhaseTest) const
    {
        if (Nodes.empty()) return false;

        float NearestT = MaxDistance;
        bool bHit = false;

        // [최적화] std::vector → 고정 크기 스택 배열로 교체.
        // 50k 오브젝트의 balanced BVH 최대 깊이 = log2(50000) ≈ 16.
        // 64 슬롯이면 충분하며, heap alloc / dealloc 비용을 완전히 제거한다.
        // MeshBVH(Renderer.cpp)도 동일한 방식(uint32_t Stack[64])을 이미 사용 중.
        uint32_t Stack[64];
        int32_t StackSize = 0;

        float RootT;
        Core::GPerformanceMetrics.BVHNodeTestCount++;
        if (!Ray.Intersects(Nodes[0].Bounds, RootT) || RootT > NearestT) return false;

        Stack[StackSize++] = 0;

        while (StackSize > 0)
        {
            uint32_t NodeIdx = Stack[--StackSize];

            const FSceneBVHNode& Node = Nodes[NodeIdx];

            if (Node.IsLeaf())
            {
                Core::GPerformanceMetrics.ObjectAABBTestCount += Node.ObjectCount;
                for (uint32_t i = 0; i < Node.ObjectCount; ++i)
                {
                    uint32_t ObjIdx = ObjectIndices[Node.ObjectIndex + i];

                    //  오브젝트 단위 Sphere 검사
                    const float dx = SceneData->CenterX[ObjIdx] - Ray.Origin.x;
                    const float dy = SceneData->CenterY[ObjIdx] - Ray.Origin.y;
                    const float dz = SceneData->CenterZ[ObjIdx] - Ray.Origin.z;

                    const float tca = (dx * Ray.Direction.x) + (dy * Ray.Direction.y) + (dz * Ray.Direction.z);
                    const float d2 = (dx * dx + dy * dy + dz * dz) - (tca * tca);
                    const float r2 = SceneData->Radius[ObjIdx] * SceneData->Radius[ObjIdx];

                    if (d2 > r2) continue;

                    const float thc = std::sqrt(r2 - d2);
                    const float t0 = tca - thc;
                    const float t1 = tca + thc;

                    if (t1 < 0.0f) continue;
                    const float tNear = (t0 < 0.0f) ? t1 : t0;

                    // 구체에 맞았고, 지금까지 찾은 거리보다 가깝다면 정밀 검사
                    if (tNear < NearestT)
                    {
                        float HitDist = NearestT;
                        if (NarrowPhaseTest(ObjIdx, HitDist))
                        {
                            if (HitDist < NearestT)
                            {
                                NearestT = HitDist;
                                OutHitIndex = ObjIdx;
                                bHit = true;
                            }
                        }
                    }
                }
            }
            else
            {
                float tL, tR;
                Core::GPerformanceMetrics.BVHNodeTestCount += 2;
                bool hitL = Ray.Intersects(Nodes[Node.LeftChild].Bounds, tL) && tL < NearestT;
                bool hitR = Ray.Intersects(Nodes[Node.RightChild].Bounds, tR) && tR < NearestT;

                if (hitL && hitR)
                {
                    if (tL < tR)
                    {
                        Stack[StackSize++] = Node.RightChild;
                        Stack[StackSize++] = Node.LeftChild;
                    }
                    else
                    {
                        Stack[StackSize++] = Node.LeftChild;
                        Stack[StackSize++] = Node.RightChild;
                    }
                }
                else if (hitL) Stack[StackSize++] = Node.LeftChild;
                else if (hitR) Stack[StackSize++] = Node.RightChild;
            }
        }

        if (bHit) OutHitDistance = NearestT;
        return bHit;
    }
}
