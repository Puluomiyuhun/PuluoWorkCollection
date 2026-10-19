// Copyright PaperGame. All Rights Reserved.

#include "KnockedFoliageActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "PhysicsEngine/BodyInstance.h"

AKnockedFoliageActor::AKnockedFoliageActor()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	MeshComponent = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("MeshComponent"));
	SetRootComponent(MeshComponent);

	// Ragdoll driven: physics is enabled on InitializeImpact. Collision is PhysicsOnly
	// so the ragdoll still lands, folds and stacks on the world (the physics scene uses
	// the physics response below), but it is invisible to gameplay query traces. This is
	// deliberate: as a QueryAndPhysics blocker it was picked up by climb-point detection
	// ("climbing point is occupied by ...") and the camera occlusion-fade trace, which
	// then held a material reference into a pooled/hidden mesh and crashed on release.
	// The skeletal mesh + physics asset are assigned on the Blueprint subclass (design a).
	MeshComponent->SetMobility(EComponentMobility::Movable);
	MeshComponent->SetSimulatePhysics(false);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
	MeshComponent->SetCollisionObjectType(ECC_PhysicsBody);
	MeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
	MeshComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	MeshComponent->SetCanEverAffectNavigation(false);
}

void AKnockedFoliageActor::InitializeImpact(const FFoliageImpactRequest& Request, const FFoliageImpactConfig& Config)
{
	ImpactConfig = Config;

	// Place the actor where the removed instance was. Only location + rotation are
	// taken; the instance's (possibly non-uniform / random) scale is ignored because
	// runtime-scaling a ragdoll is unstable - author SK_Fern to the field size.
	const FTransform& Xf = Request.InstanceWorldTransform;
	SetActorLocationAndRotation(Xf.GetLocation(), Xf.GetRotation());

	// The skeletal mesh + physics asset + materials are pre-set on the BP subclass,
	// so we do NOT swap meshes here (unlike the old static-mesh version).

	// Horizontal launch direction, with an optional random spread so a whole patch
	// fans out instead of flying as one block.
	FVector Dir = Request.ImpactDirection;
	Dir.Z = 0.f;
	if (!Dir.Normalize())
	{
		Dir = FVector::ForwardVector;
	}
	if (ImpactConfig.DirectionSpreadDegrees > 0.f)
	{
		const float SpreadDeg = FMath::FRandRange(-ImpactConfig.DirectionSpreadDegrees, ImpactConfig.DirectionSpreadDegrees);
		Dir = Dir.RotateAngleAxis(SpreadDeg, FVector::UpVector);
	}

	const float Strength = FMath::Max(0.f, Request.ImpactStrength);

	// Launch velocity = horizontal push + upward pop.
	const FVector LaunchVelocity = Dir * (ImpactConfig.LaunchSpeed * Strength)
		+ FVector::UpVector * (ImpactConfig.UpwardLaunchSpeed * Strength);

	// Tumble about the axis perpendicular to the (horizontal) launch direction so
	// it front-flips the way it was kicked, randomised a little per instance.
	FVector SpinAxis = FVector::CrossProduct(FVector::UpVector, Dir);
	if (!SpinAxis.Normalize())
	{
		SpinAxis = FVector::RightVector;
	}
	const float SpinScale = 1.f + FMath::FRandRange(-ImpactConfig.SpinRandomVariance, ImpactConfig.SpinRandomVariance);
	const float SpinRateRad = FMath::DegreesToRadians(ImpactConfig.SpinSpeed * SpinScale * Strength);

	// Turn the whole skeletal mesh into a ragdoll and hand it the initial state.
	// SetSimulatePhysics(true) simulates every body in the physics asset; the
	// constraints then produce the natural folding/flopping on impact.

	// Pooled-reuse safety: a recycled actor's physics bodies stay parked in world space
	// where the previous flight ended - SetForceRefPose + RefreshBoneTransforms only
	// resets the RENDER pose, not those residual bodies, so re-enabling simulation from
	// them still teleports the mesh far away. After forcing the reference pose at the
	// NEW transform, recreate the physics state: RecreatePhysicsState destroys the stale
	// bodies and re-instantiates fresh ones from the current component-space transforms
	// (ref pose) times the new ComponentToWorld - correct bodies at the correct location -
	// which then simulate + launch cleanly below. (The mesh has a physics asset assigned,
	// so the recreate has bodies to build and is safe.)
	MeshComponent->SetSimulatePhysics(false);
	MeshComponent->SetForceRefPose(true);
	MeshComponent->RefreshBoneTransforms();
	MeshComponent->SetForceRefPose(false);
	MeshComponent->RecreatePhysicsState();

	MeshComponent->SetSimulatePhysics(true);
	MeshComponent->SetEnableGravity(true);

	// Honour the Config damping on every body (component-level SetLinearDamping only
	// touches the root body, so iterate). Bodies also carry their own damping from
	// the physics asset - this just applies a uniform override on top.
	MeshComponent->ForEachBodyBelow(NAME_None, /*bIncludeSelf*/true, /*bSkipCustomType*/false,
		[&](FBodyInstance* BI)
		{
			BI->LinearDamping = ImpactConfig.LinearDamping;
			BI->AngularDamping = ImpactConfig.AngularDamping;
			BI->UpdateDampingProperties();
		});

	if (ImpactConfig.PhysicalMaterialOverride)
	{
		MeshComponent->SetPhysMaterialOverride(ImpactConfig.PhysicalMaterialOverride);
	}

	MeshComponent->SetAllPhysicsLinearVelocity(LaunchVelocity);
	MeshComponent->SetAllPhysicsAngularVelocityInRadians(SpinAxis * SpinRateRad);
	MeshComponent->WakeAllRigidBodies();

	// Start flying.
	CurrentPhase = EFoliageImpactPhase::Flying;
	PhaseElapsedTime = 0.f;
	SetActorHiddenInGame(false);
	SetActorTickEnabled(true);
}

void AKnockedFoliageActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	PhaseElapsedTime += DeltaSeconds;

	switch (CurrentPhase)
	{
	case EFoliageImpactPhase::Flying:
		TickFlying(DeltaSeconds);
		break;
	case EFoliageImpactPhase::Fading:
		TickFading(DeltaSeconds);
		break;
	case EFoliageImpactPhase::Finished:
		SetActorTickEnabled(false);
		break;
	default:
		break;
	}
}

void AKnockedFoliageActor::TickFlying(float DeltaSeconds)
{
	// Physics owns the motion during this phase; we only wait out the timer.
	if (PhaseElapsedTime >= ImpactConfig.FlyDuration)
	{
		TransitionToPhase(EFoliageImpactPhase::Fading);
	}
}

void AKnockedFoliageActor::TickFading(float DeltaSeconds)
{
	const float Alpha = FMath::Clamp(PhaseElapsedTime / ImpactConfig.FadeDuration, 0.f, 1.f);

	// Disappearance is driven purely by the material's "DissolveProgress" scalar
	// (0 = fully visible, 1 = fully dissolved). Physics keeps simulating (it has
	// usually settled / gone to sleep by now), so the ragdoll holds its rest pose.
	if (MeshComponent)
	{
		MeshComponent->SetScalarParameterValueOnMaterials(TEXT("DissolveProgress"), Alpha);
	}

	if (Alpha >= 1.f)
	{
		SetActorHiddenInGame(true);
		TransitionToPhase(EFoliageImpactPhase::Finished);
	}
}

void AKnockedFoliageActor::TransitionToPhase(EFoliageImpactPhase NewPhase)
{
	const EFoliageImpactPhase OldPhase = CurrentPhase;
	CurrentPhase = NewPhase;
	PhaseElapsedTime = 0.f;

	OnPhaseChanged(OldPhase, NewPhase);
}

void AKnockedFoliageActor::ResetForPooling()
{
	SetActorTickEnabled(false);

	if (MeshComponent)
	{
		// Stop the ragdoll (keeps the skeletal mesh assigned for reuse) and clear the
		// dissolve back to fully visible.
		MeshComponent->SetSimulatePhysics(false);
		MeshComponent->SetScalarParameterValueOnMaterials(TEXT("DissolveProgress"), 0.f);
	}

	// Reset runtime state.
	CurrentPhase = EFoliageImpactPhase::Flying;
	PhaseElapsedTime = 0.f;

	SetActorHiddenInGame(true);
}
