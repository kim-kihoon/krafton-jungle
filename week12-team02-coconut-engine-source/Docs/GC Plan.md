# Implementation Plan

담당자: KimKenneth

# Reflection layer: linear implementation order (4 steps)

## Context

The engine has a working UObject-side reflection chain (`UObject → UField → UStruct → UClass`) and a codegen-driven property layer with ~17 `FProperty` subclasses, but is missing four things that interlock:

1. **FObjectProperty / FClassProperty** — stubs exist under `Source/Engine/Core/Property/FObjectPropertyBase/` but are incomplete. Without them, UObject references can't be serialized correctly, GC has nothing to traverse, and UFunction can't handle UObject* parameters.
2. **FField + TFieldIterator** — `FProperty` is standalone and `UStruct::ChildProperties` is a `TArray<FProperty*>`. Two TODOs anticipate the linked-list shape: [UField.h:32](https://www.notion.so/KraftonEngine/Source/Engine/Object/UField.h:32) and [UStruct.h:80](https://www.notion.so/KraftonEngine/Source/Engine/Object/UStruct.h:80). Modern UE splits this as `FField` (lightweight, non-UObject base for properties) vs `UField` (heavy GC'd base for UStruct/UClass/UFunction).
3. **CDO + GC** — no per-class default instance and no mark-and-sweep. Without CDO there's no "Reset to Default", no scene-file delta serialization, and no canonical root for GC to start from. Without GC, UObject lifetimes are ad-hoc and asset references in defaults can dangle.
4. **UFunction** — codegen parses `UFUNCTION(...)` and emits Lua bindings ([GenerateCode.py:978-998](https://www.notion.so/Scripts/GenerateCode.py:978)) but provides no C++ call-by-name or function-level metadata.

These four must land in a specific order. Each piece is independently shippable in the sense that the engine remains in a strictly-better state if a later step slips. The ordering below is chosen to (a) respect hard dependencies, (b) bound risk by deferring the largest scope last, and (c) avoid throwaway scaffolding between steps.

Intended outcome: a reflection layer that supports correct UObject reference serialization, linked-list property/function iteration, delta-against-default scene saves, mark-and-sweep GC, and reflected callables — landing in four mergeable increments.

---

## Step 1 — Solidify FObjectProperty + FClassProperty, plant GC reference-emission hook (3–5 days)

Goal: UObject references serialize, deserialize, and enumerate correctly. Add the GC reference-emission API to `FProperty` so step 3 doesn't have to retrofit it across all subclasses.

### Critical files

- [Source/Engine/Core/Property/FObjectPropertyBase/](https://www.notion.so/KraftonEngine/Source/Engine/Core/Property/FObjectPropertyBase/) — finish `FObjectPropertyBase`, `FObjectProperty`, `FClassProperty`, `FSoftObjectProperty`. Audit `Serialize`/`Deserialize` for cycle handling and forward-reference resolution during scene load.
- [PropertyTypes.h](https://www.notion.so/KraftonEngine/Source/Engine/Core/Property/PropertyTypes.h) — add to `FProperty` base:
Default `EmitReferences` is a no-op; FObjectProperty/FClassProperty/FSoftObjectProperty override to push their reference. FArrayProperty and FStructProperty override to recurse.
    
    ```cpp
    virtual void EmitReferences(const void* Instance, class FReferenceCollector& Collector) const {}
    virtual bool Identical(const void* A, const void* B) const = 0;  // also seeds CDO
    ```
    
- New file `Source/Engine/Object/FReferenceCollector.h` — minimal interface: `void AddReferencedObject(UObject*&)`. Stub-only in this step; step 3 implements it.

### Why first

- Already partially in-tree; finishing it is bounded.
- Orthogonal to FField (FField changes the container; FObjectProperty internals are unaffected).
- Three downstream steps silently break if this is wrong: CDO compares object defaults, GC walks object refs, UFunction marshals UObject* params.
- Immediate payoff: scene serialization of UObject references gets safer the day this lands.

### Verification

1. Place an actor referencing another actor (e.g. via `USceneComponent*` parent), save, reload — pointer reconnects to the same actor.
2. Reference an asset (mesh/material) on a component — round-trips through save/load.
3. Unit-check `Identical()` for each new subclass against same-pointer / different-pointer / null cases.

---

## Step 2 — FField + TFieldIterator (2–3 days)

Goal: `FProperty : public FField`, `UStruct::ChildProperties` becomes `FField* ChildProperties` head pointer. `UField` is not touched in this step — its `Next` field stays a TODO until Step 4.

### New types

- `Source/Engine/Core/Field/FField.h` — non-UObject base. Holds `Name`, `Next`, `Owner` back-pointer. RTTI via the existing `FProperty::GetType()` enum in v1; FFieldClass deferred.
- `Source/Engine/Core/Field/TFieldIterator.h` — template iterator walking a `FField*` head (or, in Step 4, a `UField*` head) and filtering by type.

### Critical files

- [PropertyTypes.h](https://www.notion.so/KraftonEngine/Source/Engine/Core/Property/PropertyTypes.h) — `class FProperty : public FField`. The `Name` field moves from FProperty to FField. All 17 subclass constructors compile-touch only — they already forward `InName` to `FProperty(...)`.
- [UStruct.h](https://www.notion.so/KraftonEngine/Source/Engine/Object/UStruct.h) / [UStruct.cpp](https://www.notion.so/KraftonEngine/Source/Engine/Object/UStruct.cpp) — replace `TArray<FProperty*> ChildProperties` with `FField* ChildProperties = nullptr;`. Rewrite `AddProperty` (tail-append to preserve declaration order), `GetProperty`, `GetAllProperties`, `FindPropertyByName`, `GetEditableProperties`, `GetNonTransientProperties`, `HideInheritedProperty`. **Keep the `GetAllProperties(TArray<const FProperty*>& OutProps)` façade** so editor and serialization call sites stay unchanged.
- [Scripts/GenerateCode.py](https://www.notion.so/Scripts/GenerateCode.py) — `emit_property_registrar` ([line 862-890](https://www.notion.so/Scripts/GenerateCode.py:862)) still calls `Cls->AddProperty(...)`; underlying implementation changes only.

### Iteration-site sweep

Hand-written sites: [SceneSaveManager.cpp](https://www.notion.so/KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp), [EditorPropertyWidget.cpp](https://www.notion.so/KraftonEngine/Source/Editor/UI/EditorPropertyWidget.cpp), [AnimSequenceEditorWidget.cpp](https://www.notion.so/KraftonEngine/Source/Editor/UI/Asset/AnimSequenceEditorWidget.cpp), [AActor.cpp](https://www.notion.so/KraftonEngine/Source/Engine/GameFramework/AActor.cpp), [ActorComponent.cpp](https://www.notion.so/KraftonEngine/Source/Engine/Component/ActorComponent.cpp), [LuaScriptComponent.cpp](https://www.notion.so/KraftonEngine/Source/Engine/Component/LuaScriptComponent.cpp), [LightComponent.cpp](https://www.notion.so/KraftonEngine/Source/Engine/Component/Light/LightComponent.cpp), [PrimitiveComponent.cpp](https://www.notion.so/KraftonEngine/Source/Engine/Component/PrimitiveComponent.cpp), [TextRenderComponent.cpp](https://www.notion.so/KraftonEngine/Source/Engine/Component/TextRenderComponent.cpp), [SubUVComponent.cpp](https://www.notion.so/KraftonEngine/Source/Engine/Component/SubUVComponent.cpp), [BillboardComponent.cpp](https://www.notion.so/KraftonEngine/Source/Engine/Component/BillboardComponent.cpp), [Notify_PlaySound.cpp](https://www.notion.so/KraftonEngine/Source/Engine/Animation/Notify_PlaySound.cpp), [FStructProperty.h](https://www.notion.so/KraftonEngine/Source/Engine/Core/Property/FStructProperty.h), [Object.cpp](https://www.notion.so/KraftonEngine/Source/Engine/Object/Object.cpp). Most go through the array façade and need zero changes; direct `GetProperties()` callers switch to `TFieldIterator<FProperty>`.

### Verification

1. Codegen output unchanged; only implementation differs.
2. Editor property panel: every UPROPERTY appears in the same base-to-derived order.
3. Scene save: JSON byte-identical to pre-change save.
4. Reload: round-trip equal. `HideInheritedProperty` still hides.

---

## Step 3 — CDO + mark-and-sweep GC (1.5–2 weeks, bundled)

Goal: every UClass has a default object; GC traverses the reference graph using Step 1's `EmitReferences` and Step 2's iteration; scene serialization writes only deltas against CDO; editor gets "Reset to Default".

### Why bundle CDO and GC

They share both dependencies (FObjectProperty correctness + FField iteration) and infrastructure (CDOs *are* the canonical permanent root set; GC is the system that keeps CDO-referenced assets alive). Shipping them separately would either leave the GC without a clean root set or leave CDOs at risk of pointing into freed memory. The two are ~50% shared code at the integration layer.

### CDO pieces

- [UClass.h](https://www.notion.so/KraftonEngine/Source/Engine/Object/UClass.h) — add `UObject* ClassDefaultObject = nullptr;`. Construct via placement-new after class registration drains, with `bIsCDO` flag set on the instance.
- [Source/Engine/Object/Object.h](https://www.notion.so/KraftonEngine/Source/Engine/Object/Object.h) — add `bool bIsCDO : 1;` to UObject flags. Constructors that touch world/Lua/asset-manager guard with `if (bIsCDO) return;` after their base init.
- Per-FProperty `Identical()` (already added in Step 1) and new `CopyComplete(void* Dst, const void* Src) const` — used for delta detection and reset-to-default.
- [SceneSaveManager.cpp](https://www.notion.so/KraftonEngine/Source/Engine/Serialization/SceneSaveManager.cpp) — optional delta path: `if (CDO && Prop->Identical(Instance, CDO_Instance)) continue;` Gated behind a save-format version bump for backwards compatibility.
- [EditorPropertyWidget.cpp](https://www.notion.so/KraftonEngine/Source/Editor/UI/EditorPropertyWidget.cpp) — per-field "Reset to Default" button calls `Prop->CopyComplete(Instance, CDO)`. Bold/gray field labels based on `!Identical(Instance, CDO)`.

### GC pieces

- `Source/Engine/Object/FReferenceCollector.h` — promote stub from Step 1 to a real implementation backing the mark phase (push to a worklist).
- `Source/Engine/Object/UObjectGC.h` / `.cpp` — single-threaded mark-and-sweep. Mark phase: walk root set, for each live UObject call `GetClass()->EmitReferences(Instance, Collector)` which walks `TFieldIterator<FProperty>` and dispatches per-subclass `EmitReferences`. Sweep phase: integrate with the existing [UObjectManager](https://www.notion.so/KraftonEngine/Source/Engine/Object/Object.h) allocation list. Run on demand (`CollectGarbage()`) — incremental/parallel GC is **out of scope**.
- Root set: **CDOs** (every UClass's default), **UWorld** + active levels, editor selection, currently-loaded assets, Lua-held references (sol2 already tracks these via its registry — exposable as a root callback). Audit [FUObjectArray::DeferStaticObject](https://www.notion.so/KraftonEngine/Source/Engine/Object/UField.h:21) to make sure registration semantics align.
- Weak refs out of scope; `TWeakObjectPtr` is a v2 concern. Document that today's raw pointers must be cleared explicitly on destruction or live inside reflected properties.

### Constructor-headless safety audit

CDO construction runs at static-init drain with no world live. Audit `Source/Engine/Component/` (~30 files). Add the `bIsCDO` early-out anywhere a constructor touches: Lua state, asset manager, scene globals, delegate subscriptions. Trivial to grep; pattern lives in one place once established.

### Verification

1. Save scene with default-valued actor → JSON contains only non-default fields. Reload reconstructs equivalent state.
2. Editor: change a property, "Reset to Default" returns to CDO value.
3. Allocate a UObject without rooting it, call `CollectGarbage()`, verify it's freed. Repeat while rooted via a CDO-referenced asset → stays alive.
4. Load a scene with mesh references → meshes survive a GC pass because the actor's `UStaticMeshComponent::StaticMesh` FObjectProperty emits the reference.
5. Stress: 1000 allocs/frees per frame, no leaks per `UObjectManager` stats.

---

## Step 4 — UFunction (1 week baseline, +2–3 weeks production-ready)

Goal: `UFUNCTION()`-annotated methods become reflected `UFunction` objects on a separate `UField* Children` list, callable by name from C++ with parameter and return metadata. UFunction inherits from UStruct so it can own its parameter list as `FField* ChildProperties` — same machinery from Step 2.

### Critical files

- New `Source/Engine/Object/UFunction.h` / `.cpp` — `class UFunction : public UStruct`. Stores `FuncFlags`, `NumParms`, `ParmsSize`, `ReturnValueOffset`, and a thunk pointer `void(*)(UObject*, FFrame&, void*)`.
- New `Source/Engine/Object/FFrame.h` — minimal arg-buffer + cursor. Walks the UFunction's own ChildProperties via `TFieldIterator<FProperty>`.
- Activate `UField* Next` ([UField.h:32](https://www.notion.so/KraftonEngine/Source/Engine/Object/UField.h:32)). Add `UField* Children = nullptr;` to UStruct — separate from `ChildProperties`, holds UFunctions and (future) nested types.
- [UClass.h](https://www.notion.so/KraftonEngine/Source/Engine/Object/UClass.h) — `FindFunctionByName` walks `Children` via `TFieldIterator<UFunction>`. Add fast-path UFunction cast flag to `ClassCastFlags` to avoid `dynamic_cast`.
- [Object.h](https://www.notion.so/KraftonEngine/Source/Engine/Object/Object.h) — `DECLARE_CLASS` befriends `_FunctionRegistrar`.
- [Scripts/GenerateCode.py](https://www.notion.so/Scripts/GenerateCode.py) — extend `parse_function` ([line 498-500](https://www.notion.so/Scripts/GenerateCode.py:498)) for typed parameter lists. Add `emit_function_registrar` mirroring `emit_property_registrar` ([line 862-890](https://www.notion.so/Scripts/GenerateCode.py:862)). Emit per-function thunk. Forbid reflected overloads in v1.
- GC interaction: UFunction's ChildProperties list (its parameter signature) holds *types*, not live UObject refs — no GC traversal needed for the metadata itself. `FFrame` stack buffers are short-lived and held by C++ stack callers; not GC-traced. Document this.

### Parameter marshalling scope

- **v1 (1 week)**: scalar, `FVector`, `FName`, `FString`, `UObject*` (uses Step 1's FObjectProperty). Void or scalar return.
- **v2 (+2 weeks)**: `FStructProperty`, `FArrayProperty`, const methods, return-by-value structs.

### Verification

1. Annotate `UActionComponent::HitStop` ([ActionComponent.h:22-40](https://www.notion.so/KraftonEngine/Source/Engine/Component/ActionComponent.h:22)) with `UFUNCTION()`.
2. Rebuild — `UActionComponent_FunctionRegistrar`, `Thunk_UActionComponent_HitStop`, and a static `UFunction` instance appear in `Intermediate/Generated/Source/ActionComponent.gen.cpp`.
3. `UClass::FindByName("UActionComponent")->FindFunctionByName("HitStop")->Invoke(...)` — side effect matches direct call.
4. Existing `UFUNCTION(Lua)` bindings still work; `CarController.lua` against a Character actor — no regression.
5. `TFieldIterator<UFunction>` over a UClass enumerates exactly the reflected methods.
6. GC pass during a UFunction call doesn't collect UObject* arguments (caller holds the references; document the contract).

---

## Sequencing rationale, condensed

```
1. FObjectProperty + FClassProperty   ── 3–5 days     ── unblocks GC + CDO + UFunction param marshalling
2. FField + TFieldIterator            ── 2–3 days     ── unblocks CDO walking + UFunction param list
3. CDO + GC (bundled)                 ── 1.5–2 weeks  ── CDOs are GC roots; both need 1 & 2
4. UFunction                          ── 1–4 weeks    ── largest scope, lowest dependency-pressure on what's left
```

Every step is independently mergeable. If priorities shift after any step, the engine is strictly better than the previous state. Total: ~4–8 weeks for one engineer, depending on UFunction parameter-marshalling depth.

### Two judgment calls worth flagging

- **Swap 1 ↔ 2 if FObjectProperty is further from done than expected.** Cost of swapping is small — FField is container-only and doesn't touch FObjectProperty internals. Make the call once you crack open `FObjectPropertyBase.h`.
- **Splitting CDO and GC into separate steps is possible but discouraged.** They share so much (root set, EmitReferences plumbing, constructor-headless audit) that two PRs end up cross-referencing each other heavily. Bundling is the cleaner shipping unit even though the merge is bigger.