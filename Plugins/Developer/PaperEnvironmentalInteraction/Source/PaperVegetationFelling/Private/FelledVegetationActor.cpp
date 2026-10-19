// Copyright PaperGame. All Rights Reserved.

#include "FelledVegetationActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "PhysicsEngine/BodyInstance.h"
#include "PhysicsEngine/BodySetup.h"
#include "HAL/IConsoleManager.h"

// UInstancedSkinnedMeshComponent is UE_EXPERIMENTAL
#pragma push_macro("UE_EXPERIMENTAL")
#undef UE_EXPERIMENTAL
#define UE_EXPERIMENTAL(Version, Message)
#include "Components/InstancedSkinnedMeshComponent.h"
#pragma pop_macro("UE_EXPERIMENTAL")

AFelledVegetationActor::AFelledVegetationActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	LowerPartMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("LowerPartMesh"));
	LowerPartMesh->SetupAttachment(RootComponent);
	LowerPartMesh->SetSimulatePhysics(false);

	UpperPartMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("UpperPartMesh"));
	UpperPartMesh->SetupAttachment(RootComponent);
	UpperPartMesh->SetSimulatePhysics(false); // 初始化时关闭，InitializeFelling 中启用
}

void AFelledVegetationActor::BeginPlay()
{
	Super::BeginPlay();
}

void AFelledVegetationActor::InitializeFelling(
	const FVegetationFellingRequest& Request,
	const FTransform& InstanceTransform)
{
	CachedRequest = Request;
	SetActorTransform(InstanceTransform);

	// ---- Resolve the skeletal mesh to display ----
	// Prefer the BP-authored post-cut mesh so a plant placed as a static ISM can still
	// be cut; fall back to the source ISKM's own skeletal mesh (bamboo authored as ISKM).
	USkeletalMesh* WorkingMesh = CutSkeletalMesh;
	if (!WorkingMesh && Request.SourceISKM)
	{
		WorkingMesh = Cast<USkeletalMesh>(Request.SourceISKM->GetSkinnedAsset());
	}

	// Decisive entry log: tells us which branch InitializeFelling will take.
	UE_LOG(LogTemp, Warning, TEXT("FelledVegetation InitializeFelling: CutBone='%s' SourceISKM=%s WorkingMesh=%s"),
		*Request.CutBoneName.ToString(),
		Request.SourceISKM ? TEXT("valid") : TEXT("NULL"),
		WorkingMesh ? *WorkingMesh->GetName() : TEXT("NULL"));

	if (WorkingMesh)
	{
		UpperPartMesh->SetSkeletalMeshAsset(WorkingMesh);
		LowerPartMesh->SetSkeletalMeshAsset(WorkingMesh);

		// Copy materials from the source ISKM only when the mesh came from it. A
		// BP-authored CutSkeletalMesh (static ISM source) uses its own materials.
		if (Request.SourceISKM)
		{
			for (int32 i = 0; i < Request.SourceISKM->GetNumMaterials(); ++i)
			{
				UMaterialInterface* Mat = Request.SourceISKM->GetMaterial(i);
				if (Mat)
				{
					UpperPartMesh->SetMaterial(i, Mat);
					LowerPartMesh->SetMaterial(i, Mat);
				}
			}
		}

		// Enable collision so physics bodies are created from the PhysicsAsset
		UpperPartMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		UpperPartMesh->SetCollisionProfileName(TEXT("Ragdoll"));

		// Force physics state recreation after mesh assignment
		UpperPartMesh->RecreatePhysicsState();

		// Force animation initialization so bone visibility states are set up
		UpperPartMesh->InitAnim(true);
		LowerPartMesh->InitAnim(true);

		// LowerPartMesh: no physics needed, but ensure render state is initialized
		LowerPartMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		LowerPartMesh->SetVisibility(true);
		LowerPartMesh->MarkRenderStateDirty();
	}

	// ---- Hide bones to avoid overlap between upper/lower parts ----
	if (Request.CutBoneName != NAME_None && WorkingMesh)
	{
		const FReferenceSkeleton& RefSkeleton = WorkingMesh->GetRefSkeleton();
		const int32 CutBoneIndex = RefSkeleton.FindBoneIndex(Request.CutBoneName);

		UE_LOG(LogTemp, Warning, TEXT("FelledVegetation: CutBone=%s Index=%d, LowerBoneVis=%d, UpperBoneVis=%d, TotalBones=%d"),
			*Request.CutBoneName.ToString(), CutBoneIndex,
			LowerPartMesh->GetBoneVisibilityStates().Num(),
			UpperPartMesh->GetBoneVisibilityStates().Num(),
			RefSkeleton.GetNum());

		if (CutBoneIndex != INDEX_NONE)
		{
			// For LowerPartMesh: hide cut bone and all descendants (the upper part).
			// HideBone cascades to descendants (RebuildVisibilityArray marks every
			// child of an explicitly-hidden bone as BVS_HiddenByParent), so hiding
			// the cut bone alone already hides its whole subtree - leaving only the
			// ancestor bones (the stump) visible. This direction works correctly.
			int32 UpperBoneCount = 0; // cut bone + descendants = the falling upper part
			for (int32 i = 0; i < RefSkeleton.GetNum(); ++i)
			{
				if (i == CutBoneIndex || RefSkeleton.BoneIsChildOf(i, CutBoneIndex))
				{
					LowerPartMesh->HideBone(i, PBO_None);
					++UpperBoneCount;
				}
			}

			// IMPORTANT: We deliberately do NOT hide the ancestor bones on the
			// UpperPartMesh. Bone-visibility hiding cascades to ALL descendants of
			// a hidden bone, and the ancestor chain includes the root bone - hiding
			// it would cascade down through the cut bone and hide the entire upper
			// subtree we want to keep (this was the "upper disappears" bug). Instead,
			// the UpperPartMesh keeps every bone visible: only the cut bone and below
			// are simulated (see physics setup), so the ancestor bones stay frozen at
			// the ref pose (coincident with the LowerPartMesh stump) while the cut
			// subtree falls away. The LowerPartMesh remains as the persistent stump.

			// Compute the cut bone's local-space Z (height along the bamboo). This is
			// the clip plane fed to the UpperPartMesh material: the material discards
			// pixels whose pre-skinned local Z is below CutZ, so the UpperPartMesh
			// renders ONLY the part above the cut (hiding the stump duplicate and the
			// stretching seam). Pre-skinned local position is fixed per vertex, so the
			// clip tracks the geometry no matter how the upper tumbles.
			FTransform CutCS = FTransform::Identity;
			for (int32 idx = CutBoneIndex; idx != INDEX_NONE; idx = RefSkeleton.GetParentIndex(idx))
			{
				CutCS = CutCS * FTransform(RefSkeleton.GetRefBonePose()[idx]);
			}
			const float CutBoneLocalZ = CutCS.GetLocation().Z;
			// The clip plane is the cut bone's local Z plus a hand-tuned bias. The
			// stretching seam is the cross-bone-skinned transition band around the cut
			// joint; raise CutClipHeightBias until the seam is fully hidden.
			const float ClipZ = CutBoneLocalZ + FellingConfig.CutClipHeightBias;
			UE_LOG(LogTemp, Warning, TEXT("FelledVegetation: UpperBoneCount=%d, CutBoneLocalZ=%.1f, ClipZ=%.1f"),
				UpperBoneCount, CutBoneLocalZ, ClipZ);

			// Drive the material clip on the UpperPartMesh only (the LowerPartMesh keeps
			// its default CutZ so the stump renders fully).
			for (int32 MatIndex = 0; MatIndex < UpperPartMesh->GetNumMaterials(); ++MatIndex)
			{
				if (UMaterialInstanceDynamic* MID = UpperPartMesh->CreateAndSetMaterialInstanceDynamic(MatIndex))
				{
					MID->SetScalarParameterValue(TEXT("CutZ"), ClipZ);
				}
			}
		}

		LowerPartMesh->MarkRenderStateDirty();
		UpperPartMesh->MarkRenderStateDirty();
	}

	// ---- Configure lower part (stump, static) ----
	LowerPartMesh->SetSimulatePhysics(false);

	// ---- Configure upper part: fly away as ONE free rigid body ----
	// We deliberately do NOT physically cut or detach the upper mesh. Detaching a
	// smoothly-skinned mesh always leaves the cut-boundary vertices stretched
	// between the falling bones and the (static) stump bones they are ALSO weighted
	// to - the "bottom stays glued to the stump" artefact. Instead we simulate the
	// ENTIRE upper mesh as a single free rigid body so NO bone stays behind (zero
	// stretch), and rely purely on rendering to create the cut:
	//   - LowerPartMesh renders only the stump (cut bone + descendants hidden).
	//   - UpperPartMesh material-clips everything below CutZ.
	// The upper's invisible below-cut geometry flies along with it but is never
	// drawn; the persistent stump is the separate LowerPartMesh.
	// REQUIREMENT: the physics asset must drive the whole mesh as one rigid body -
	// a single collision body on the ROOT bone (one body drives root + all
	// descendants rigidly via PerformBlendPhysicsBones). A body on a mid/top bone
	// would leave its ancestor bones frozen at the ref pose and the lower part
	// wouldn't fly.
	UpperPartMesh->SetSimulatePhysics(true);
	UpperPartMesh->SetLinearDamping(FellingConfig.LinearDamping);
	UpperPartMesh->SetAngularDamping(FellingConfig.AngularDamping);

	// Force full physics drive + gravity on every body. Without this the upper can
	// "float and drift down slowly": if a body's physics blend weight is < 1 the
	// bone stays partly pinned to the frozen ref pose, and if a body has gravity
	// disabled in the physics asset it never falls. Set both explicitly.
	//
	// ALSO disable collision on the below-cut bodies. The upper is simulated IN
	// PLACE, so its invisible below-cut geometry (everything physically below the
	// cut plane) still extends down onto the ground/stump. Leaving those collision
	// bodies on makes the whole rigid body REST on the ground and refuse to fall -
	// it only slowly tips over. We keep those bodies in the rigid group (they still
	// add mass) but turn their collision OFF, so nothing supports the upper and it
	// drops at once. The bodies above the cut keep collision so the upper still
	// lands correctly and collides with surrounding bamboo.
	UpperPartMesh->SetAllBodiesPhysicsBlendWeight(1.0f);
	const FReferenceSkeleton* PhysRefSkeleton = WorkingMesh ? &WorkingMesh->GetRefSkeleton() : nullptr;
	const int32 PhysCutBoneIndex = PhysRefSkeleton ? PhysRefSkeleton->FindBoneIndex(Request.CutBoneName) : INDEX_NONE;

	// Compute the cut plane's local Z (component-space height along the bamboo) by
	// walking the cut bone up to the root and accumulating ref-pose transforms. We
	// classify bodies GEOMETRICALLY against this plane rather than by bone hierarchy:
	// a physics asset can put the whole trunk in one big body whose OWNING bone sits
	// above the cut yet whose collision shape extends far below it, and segment
	// bodies can hang off bones that aren't strict ancestors of the cut bone. The
	// only thing that matters for "does this collision hold the upper up" is where
	// the body physically is, so we disable every body whose bone origin is below
	// the cut plane (plus a small margin so a body straddling the plane still counts
	// as below-cut and gets disabled).
	float PhysCutPlaneZ = 0.f;
	if (PhysRefSkeleton && PhysCutBoneIndex != INDEX_NONE)
	{
		FTransform CutCS = FTransform::Identity;
		for (int32 idx = PhysCutBoneIndex; idx != INDEX_NONE; idx = PhysRefSkeleton->GetParentIndex(idx))
		{
			CutCS = CutCS * FTransform(PhysRefSkeleton->GetRefBonePose()[idx]);
		}
		PhysCutPlaneZ = CutCS.GetLocation().Z;
	}

	UE_LOG(LogTemp, Warning, TEXT("FelledVegetation: body scan - CutBoneIndex=%d, CutPlaneZ=%.1f, TotalBodies=%d"),
		PhysCutBoneIndex, PhysCutPlaneZ, UpperPartMesh->Bodies.Num());

	// Enable the engine's per-body collision filter hack path (off by default). For
	// skeletal meshes FBodyInstance IGNORES per-body collision responses: BuildBodyFilterData
	// rebuilds the shape filter from the (shared, untouchable) BodySetup->CollisionReponse
	// and forces Block, so SetResponseToAllChannels/SetCollisionResponse have no effect.
	// The engine's sanctioned per-instance override is bHACK_DisableCollisionResponse,
	// but it is only honoured when this CVar is set. Turning it on has no effect on
	// bodies that don't set the hack flag, so it is safe to leave enabled.
	if (IConsoleVariable* CVarPerBodyHacks = IConsoleManager::Get().FindConsoleVariable(TEXT("p.EnableDynamicPerBodyFilterHacks")))
	{
		CVarPerBodyHacks->Set(1);
	}

	int32 DisabledCollisionBodies = 0;
	// Iterate by INDEX and re-read Bodies[i] each pass. We must NEVER call anything
	// that recreates the physics state (e.g. FBodyInstance::SetCollisionEnabled with
	// a physics-enabled<->disabled transition) while walking Bodies: that calls
	// UPrimitiveComponent::RecreateInstanceBody -> RecreatePhysicsState, which
	// destroys and reallocates the ENTIRE Bodies array (TermArticulated deletes every
	// FBodyInstance). A range-for over Bodies would then dereference freed memory ->
	// intermittent EXCEPTION_ACCESS_VIOLATION crash. To turn a below-cut body's
	// collision off WITHOUT recreating it we flag bHACK_DisableCollisionResponse and
	// refresh the shape filter data (no recreate). The body stays in the ragdoll
	// (still adds mass, moves as one) but generates no contacts, so it can't rest on
	// the ground and hold the upper up.
	for (int32 BodyIdx = 0; BodyIdx < UpperPartMesh->Bodies.Num(); ++BodyIdx)
	{
		FBodyInstance* BI = UpperPartMesh->Bodies[BodyIdx];
		if (!BI)
		{
			continue;
		}
		BI->SetEnableGravity(true);

		if (PhysRefSkeleton && PhysCutBoneIndex != INDEX_NONE)
		{
			const UBodySetup* BS = BI->GetBodySetup();
			const int32 BoneIdx = BS ? PhysRefSkeleton->FindBoneIndex(BS->BoneName) : INDEX_NONE;
			if (BoneIdx != INDEX_NONE)
			{
				// Body's ref-pose component-space Z (height along the bamboo).
				FTransform BoneCS = FTransform::Identity;
				for (int32 idx = BoneIdx; idx != INDEX_NONE; idx = PhysRefSkeleton->GetParentIndex(idx))
				{
					BoneCS = BoneCS * FTransform(PhysRefSkeleton->GetRefBonePose()[idx]);
				}
				const float BoneZ = BoneCS.GetLocation().Z;

				// The collision SHAPE's actual component-space vertical span (not just
				// the bone origin). CalcAABB transforms the body's aggregate geometry
				// by the bone's ref-pose transform, so ShapeMinZ/ShapeMaxZ are the
				// lowest/highest points of the capsule/box in bamboo-local space.
				const FBox ShapeBox = BS->AggGeom.CalcAABB(BoneCS);
				const float ShapeMinZ = ShapeBox.Min.Z;
				const float ShapeMaxZ = ShapeBox.Max.Z;

				// Classify by the collision SHAPE against the render clip plane, NOT by
				// the bone origin. A body is "below-cut" (safe to disable) only when its
				// ENTIRE shape sits below the clip plane, i.e. it is fully invisible.
				// Such a body is never drawn, so it must not collide: if kept it would
				// prop the in-place-simulated upper up while standing. But a body that
				// STRADDLES or sits above the cut has VISIBLE geometry - we MUST keep its
				// collision, otherwise the fallen trunk's visible bottom (the section just
				// above the cut) has nothing to rest on and sinks through the ground (the
				// "最下沿少了一块" artefact). This keeps the lowest visible body (which
				// straddles the cut) colliding for ground support, while still disabling
				// the fully-buried body whose shape reaches the ground and would prop.
				// CutCollisionMargin nudges the plane UP to also disable a straddling body
				// if it still props while standing (default 0 = disable only fully-hidden).
				const float DisableBelowZ = PhysCutPlaneZ + FellingConfig.CutClipHeightBias + FellingConfig.CutCollisionMargin;
				const bool bBelowCut = ShapeMaxZ < DisableBelowZ;

				UE_LOG(LogTemp, Warning, TEXT("  body bone='%s' idx=%d Z=%.1f shapeZ=[%.1f..%.1f] belowCut=%d"),
					*BS->BoneName.ToString(), BoneIdx, BoneZ, ShapeMinZ, ShapeMaxZ, bBelowCut ? 1 : 0);


				if (bBelowCut)
				{
					// Per-instance skeletal-body collision disable, no physics-state
					// recreate. See note above: for skeletal meshes only this hack
					// flag (gated by p.EnableDynamicPerBodyFilterHacks, set earlier)
					// actually takes effect - it forces the body to PhysicsOnly +
					// all-channels-Ignore. UpdatePhysicsFilterData applies it to the
					// live shapes without tearing down the body.
					BI->bHACK_DisableCollisionResponse = true;
					BI->UpdatePhysicsFilterData();
					++DisabledCollisionBodies;
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("  body bone='%s' idx=NONE (unmapped, left enabled)"),
					BS ? *BS->BoneName.ToString() : TEXT("<null setup>"));
			}
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("FelledVegetation: disabled collision on %d below-cut bodies"),
		DisabledCollisionBodies);

	// NOTE: we intentionally do NOT touch the constraints here. Constraint stiffness
	// is authored per-joint in the PhysicsAsset (PhAT): the trunk/stump segment
	// joints are set to Locked (rigid, no deform) and the branch joints are left
	// Limited/Soft or given an Angular Drive (spring) so foliage compresses and
	// rebounds on impact. SetSimulatePhysics initialises the constraints from those
	// authored profiles, so blanket-locking them in code would just overwrite the
	// asset. Tune the feel in PhAT, not here.

	// Topple like a felled tree instead of dropping straight down. The upper is a
	// vertical rigid body simulated in place; a pure downward free-fall onto its
	// vertical collision capsules makes it drop in place and land upright ("直上直下
	// 跳下去还站着"). To break that, spin the WHOLE rigid body about the horizontal
	// axis perpendicular to the (horizontal) strike direction so its top rotates
	// over toward the strike direction, then add a gentle linear push. Angular
	// velocity rotates about the free body's centre of mass, which reads as a
	// natural topple. Use only the horizontal component of the strike so a mostly-
	// vertical strike still topples sideways rather than doing nothing.
	FVector StrikeDir = Request.StrikeDirection;
	StrikeDir.Z = 0.f;
	if (!StrikeDir.Normalize())
	{
		StrikeDir = FVector::ForwardVector;
	}

	// Axis = Up x StrikeDir. Rotation about it tips the top (+Z) toward StrikeDir.
	FVector ToppleAxis = FVector::CrossProduct(FVector::UpVector, StrikeDir);
	if (!ToppleAxis.Normalize())
	{
		ToppleAxis = FVector::RightVector;
	}
	if (FellingConfig.ToppleAngularSpeed > 0.f)
	{
		// Spin the WHOLE upper (every body), not just the root: SetPhysicsAngular*
		// only sets the root body's spin, so the other segments start at zero and
		// the constraints fight the root - the topple ends up weak/barely visible.
		// SetAllPhysics* gives every body the same spin so the whole thing tips
		// over toward the strike direction like a felled tree. No degrees variant
		// exists for "All", so convert to radians here.
		const FVector ToppleVelRad = ToppleAxis * FMath::DegreesToRadians(FellingConfig.ToppleAngularSpeed);
		UpperPartMesh->SetAllPhysicsAngularVelocityInRadians(ToppleVelRad);
	}

	// Knock the WHOLE upper away along the (horizontal) strike direction. Use
	// SetAllPhysicsLinearVelocity so every body flies at the same speed - setting
	// only the root body lets the other bodies lag behind and the constraints drag
	// the small push out, which is why it looked like it just dropped in place.
	if (FellingConfig.LaunchSpeed > 0.f)
	{
		UpperPartMesh->SetAllPhysicsLinearVelocity(StrikeDir * FellingConfig.LaunchSpeed);
	}

	UE_LOG(LogTemp, Warning, TEXT("FelledVegetation post-setup: UpperVel=%s BoundsExtent=%s"),
		*UpperPartMesh->GetPhysicsLinearVelocity().ToString(),
		*UpperPartMesh->Bounds.BoxExtent.ToString());

	// Start Tick
	CurrentPhase = EVegetationFellingPhase::Falling;
	PhaseElapsedTime = 0.f;
	SetActorTickEnabled(true);
}

void AFelledVegetationActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	PhaseElapsedTime += DeltaSeconds;

	switch (CurrentPhase)
	{
	case EVegetationFellingPhase::Falling:
		TickFalling(DeltaSeconds);
		break;
	case EVegetationFellingPhase::Dissolving:
		TickDissolving(DeltaSeconds);
		break;
	case EVegetationFellingPhase::Finished:
		SetActorTickEnabled(false);
		break;
	default:
		break;
	}
}

void AFelledVegetationActor::TickFalling(float DeltaSeconds)
{
	// 溶解直接从斩断瞬间计时：物理全程模拟倒下过程，但我们不做任何落地判断/落地
	// 计时。PhaseElapsedTime 在 Falling 阶段就是"自斩断起经过的时间"（该阶段是
	// InitializeFelling 后的首个阶段，进入时已清零）。到点直接进入 Dissolving，
	// 无论上半段此刻是否已停稳。
	if (PhaseElapsedTime >= FellingConfig.TimeBeforeDissolve)
	{
		// 保留该蓝图回调作为"倒下结束、即将溶解"的时机（可播放 VFX/SFX）。
		OnGroundImpact();
		TransitionToPhase(EVegetationFellingPhase::Dissolving);
	}
}

void AFelledVegetationActor::TickDissolving(float DeltaSeconds)
{
	DissolveProgress = FMath::Clamp(PhaseElapsedTime / FellingConfig.DissolveDuration, 0.f, 1.f);

	// Dissolve ONLY the fallen upper part. The stump (LowerPartMesh) must stay
	// fully rendered so it can persist long-term. Best-effort: if the upper's
	// material has no "DissolveProgress" scalar this is a harmless no-op and the
	// upper simply hides when the dissolve completes below.
	if (UpperPartMesh)
	{
		UpperPartMesh->SetScalarParameterValueOnMaterials(TEXT("DissolveProgress"), DissolveProgress);
	}

	if (DissolveProgress >= 1.f)
	{
		// Retire the upper for good: stop physics and hide it. Do NOT touch the
		// stump. The subsystem keeps this Actor alive (see ShouldPersistStump) so
		// the stump stays in the scene.
		if (UpperPartMesh)
		{
			UpperPartMesh->SetSimulatePhysics(false);
			UpperPartMesh->SetVisibility(false, /*bPropagateToChildren=*/ true);
			UpperPartMesh->SetComponentTickEnabled(false);
		}
		TransitionToPhase(EVegetationFellingPhase::Finished);
	}
}

void AFelledVegetationActor::TransitionToPhase(EVegetationFellingPhase NewPhase)
{
	const EVegetationFellingPhase OldPhase = CurrentPhase;
	CurrentPhase = NewPhase;
	PhaseElapsedTime = 0.f;

	OnPhaseChanged(OldPhase, NewPhase);
}

void AFelledVegetationActor::ResetForPooling()
{
	// Stop physics simulation and unhide all bones
	if (UpperPartMesh)
	{
		UpperPartMesh->SetSimulatePhysics(false);
		UpperPartMesh->SetAllBodiesSimulatePhysics(false);
		UpperPartMesh->SetAllBodiesBelowPhysicsBlendWeight(NAME_None, 0.f);

		// CRITICAL for pooling: while the upper simulated as a free rigid body, the
		// physics drove UpperPartMesh's WORLD transform far away (and rotated it as
		// it tumbled). Since the mesh is attached to RootComponent, the engine
		// back-computed a large RELATIVE transform onto it. If we don't reset that,
		// the next InitializeFelling's SetActorTransform only moves the actor root -
		// the mesh keeps its drifted relative offset, so the reused upper spawns far
		// away / already lying down, and the drift accumulates every reuse. Snap the
		// component back onto the root now that physics is off.
		UpperPartMesh->SetRelativeTransform(FTransform::Identity);

		// Unhide all bones
		if (const USkeletalMesh* Mesh = UpperPartMesh->GetSkeletalMeshAsset())
		{
			for (int32 i = 0; i < Mesh->GetRefSkeleton().GetNum(); ++i)
			{
				UpperPartMesh->UnHideBone(i);
			}
		}
	}

	if (LowerPartMesh)
	{
		LowerPartMesh->SetSimulatePhysics(false);
		LowerPartMesh->SetRelativeTransform(FTransform::Identity);

		// Unhide all bones
		if (const USkeletalMesh* Mesh = LowerPartMesh->GetSkeletalMeshAsset())
		{
			for (int32 i = 0; i < Mesh->GetRefSkeleton().GetNum(); ++i)
			{
				LowerPartMesh->UnHideBone(i);
			}
		}
	}

	// Reset state
	CurrentPhase = EVegetationFellingPhase::Falling;
	PhaseElapsedTime = 0.f;
	DissolveProgress = 0.f;
	CachedRequest = FVegetationFellingRequest();

	// Stop Tick
	SetActorTickEnabled(false);

	// Hide Actor
	SetActorHiddenInGame(true);
}
