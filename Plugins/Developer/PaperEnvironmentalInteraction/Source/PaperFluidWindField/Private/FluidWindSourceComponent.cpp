#include "FluidWindSourceComponent.h"
#include "FluidWindCompute.h"
// Engine headers
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#if WITH_EDITOR
#include "DrawDebugHelpers.h"
#endif

int32 GEnableDrawFluidWindSource = 0;
static FAutoConsoleVariableRef CVarEnableDrawFluidWindSource(TEXT("r.FluidWind.Debug.ShowSource"), GEnableDrawFluidWindSource, TEXT("Draw fluid wind source."));

UFluidWindSourceComponent::UFluidWindSourceComponent()
{
	Super::SetComponentTickEnabled(true);
	PrimaryComponentTick.bCanEverTick = true;
}

void UFluidWindSourceComponent::OnRegister()
{
	Super::OnRegister();

#if WITH_EDITORONLY_DATA
	UpdateShapeVisualization();
#endif
}

void UFluidWindSourceComponent::OnUnregister()
{
#if WITH_EDITORONLY_DATA
	DestroyShapeVisualization();
#endif

	Super::OnUnregister();
}

void UFluidWindSourceComponent::BeginPlay()
{
	Super::BeginPlay();
	InitializeWindSourceData();
	FFluidWindCompute::Get().AddWindSource(this);
}

void UFluidWindSourceComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
	FFluidWindCompute::Get().RemoveWindSource(this);
}

void UFluidWindSourceComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (SourceDataGenerated.Num() > 0)
	{
		// The update only updates the position and rotation of the source data.
		UpdateWindSourceData(DeltaTime);
		// Update wind source to prepare render for upcoming simulation step. 
		FFluidWindCompute::Get().UpdateWindSource(this);
	}

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
}

#if WITH_EDITOR
void UFluidWindSourceComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	// Force scale to (1,1,1) — wind field computation ignores scale
	if (GetRelativeScale3D() != FVector::OneVector)
	{
		SetRelativeScale3D(FVector::OneVector);
	}
#if WITH_EDITORONLY_DATA
	// Clear preview mesh when skeletal mesh mode is disabled
	if (!SourceData.bUseSkeletalMesh)
	{
		PreviewSkeletalMesh = nullptr;
	}
#endif
	
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateShapeVisualization();
}

bool UFluidWindSourceComponent::CanEditChange(const FProperty* InProperty) const
{
	if (InProperty)
	{
		const FName PropName = InProperty->GetFName();

		// Disable scale editing — wind field computation ignores scale
		if (PropName == USceneComponent::GetRelativeScale3DPropertyName())
		{
			return false;
		}

#if WITH_EDITORONLY_DATA
		// PreviewSkeletalMesh only makes sense when bUseSkeletalMesh is enabled
		if (PropName == GET_MEMBER_NAME_CHECKED(UFluidWindSourceComponent, PreviewSkeletalMesh))
		{
			return SourceData.bUseSkeletalMesh;
		}
#endif
	}
	return Super::CanEditChange(InProperty);
}
#endif

void UFluidWindSourceComponent::InitializeWindSourceData()
{
	SourceDataGenerated.Empty();
	if (SourceData.bUseSkeletalMesh)
	{
		TArray<USkeletalMeshComponent*> SkeletalMeshComps;
		GetOwner()->GetComponents<USkeletalMeshComponent>(SkeletalMeshComps);
		for (const auto& SkeletalMeshComp : SkeletalMeshComps)
		{
			if (SkeletalMeshComp->ComponentHasTag(SourceData.SkeletalMeshComponentTag))
			{
				SkeletalMeshCompCached = SkeletalMeshComp;
				break;
			}
			else
			{
				SkeletalMeshCompCached = nullptr;
			}
		}
		if (SkeletalMeshCompCached.IsValid() && SkeletalMeshCompCached->GetPhysicsAsset())
		{
			for (const UPhysicsAsset* PhysAsset = SkeletalMeshCompCached->GetPhysicsAsset();
				const auto& BodySetup : PhysAsset->SkeletalBodySetups)
			{
				const FKAggregateGeom& AggGeom = BodySetup->AggGeom;
				const FName BoneName = BodySetup->BoneName;

				// Sphere bodies
				for (const FKSphereElem& SphereElem : AggGeom.SphereElems)
				{
					FFluidWindSourceData NewData = SourceData;
					NewData.Shape = EFluidWindSourceShape::Sphere;
					NewData.Radius = SphereElem.Radius;
					NewData.BoneName = BoneName;
					NewData.LocalCenter = SphereElem.Center;
					NewData.LocalRotation = FRotator::ZeroRotator;
					SourceDataGenerated.Add(NewData);
				}

				// Box bodies
				for (const FKBoxElem& BoxElem : AggGeom.BoxElems)
				{
					FFluidWindSourceData NewData = SourceData;
					NewData.Shape = EFluidWindSourceShape::Box;
					NewData.Extent = FVector3f(BoxElem.X * 0.5f, BoxElem.Y * 0.5f, BoxElem.Z * 0.5f);
					NewData.BoneName = BoneName;
					NewData.LocalCenter = BoxElem.Center;
					NewData.LocalRotation = BoxElem.Rotation;
					SourceDataGenerated.Add(NewData);
				}

				// Capsule bodies
				for (const FKSphylElem& CapsuleElem : AggGeom.SphylElems)
				{
					FFluidWindSourceData NewData = SourceData;
					NewData.Shape = EFluidWindSourceShape::Capsule;
					NewData.Radius = CapsuleElem.Radius;
					NewData.Length = CapsuleElem.Length;
					NewData.BoneName = BoneName;
					NewData.LocalCenter = CapsuleElem.Center;
					NewData.LocalRotation = CapsuleElem.Rotation;
					SourceDataGenerated.Add(NewData);
				}
			}
		}
	}
	else
	{
		SourceDataGenerated.Add(SourceData);
	}
}

void UFluidWindSourceComponent::SetSourceDataInfoId(const int32 Index, const int32 InInfoId)
{
	if (SourceDataGenerated.IsValidIndex(Index))
	{
		SourceDataGenerated[Index].DataId = InInfoId;
	}
}

int32 UFluidWindSourceComponent::GetSourceDataInfoId(const int32 Index) const
{
	if (SourceDataGenerated.IsValidIndex(Index))
	{
		return SourceDataGenerated[Index].DataId;
	}
	return -1;
}

void UFluidWindSourceComponent::UpdateWindSourceData(float DeltaTime)
{
	if (SourceData.bUseSkeletalMesh)
	{
		// Update each source data's position and rotation based on the corresponding physical body transform.
		if (SkeletalMeshCompCached.IsValid())
		{
			for (FFluidWindSourceData& Data : SourceDataGenerated)
			{
				Data.DeltaTime = DeltaTime;
				Data.LastPosition = Data.Position;
				Data.LastRotation = Data.Rotation;
				
				if (const int32 BoneIndex = SkeletalMeshCompCached->GetBoneIndex(Data.BoneName);
					BoneIndex != INDEX_NONE)
				{
					const FTransform BoneWorldTransform = SkeletalMeshCompCached->GetBoneTransform(BoneIndex);
					const FTransform LocalShapeTransform(Data.LocalRotation.Quaternion(), Data.LocalCenter);
					const FTransform ShapeWorldTransform = LocalShapeTransform * BoneWorldTransform;

					Data.Position = ShapeWorldTransform.GetLocation();
					Data.Rotation = ShapeWorldTransform.Rotator();
				}
			}
		}
	}
	else
	{
		for (FFluidWindSourceData& Data : SourceDataGenerated)
		{
			Data.DeltaTime = DeltaTime;
			Data.LastPosition = Data.Position;
			Data.LastRotation = Data.Rotation;

			Data.Position = GetComponentLocation();
			Data.Rotation = GetComponentRotation();
		}
	}
	
#if WITH_EDITORONLY_DATA
	WindSourceShapeVisualization();
#endif
}

#if WITH_EDITORONLY_DATA
namespace WindSourceViewportVisualization
{
	/** Shared setup for every editor-only shape component. */
	static void ConfigureShapeComponent(UShapeComponent* Comp, const FColor& Color, USceneComponent* AttachParent)
	{
		Comp->ShapeColor = Color;
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetDepthPriorityGroup(SDPG_Foreground);
		Comp->SetHiddenInGame(true);
		Comp->bIsEditorOnly = true;
		Comp->SetAbsolute(false, false, true);
		Comp->CreationMethod = EComponentCreationMethod::Instance;

		AActor* OwnerActor = AttachParent->GetOwner();
		if (!OwnerActor || !OwnerActor->GetWorld())
		{
			return;
		}

		// Attach to parent before registration.
		// Guard: only attach if AttachParent is not the root, or if both are the same actor.
		// This prevents "root component cannot be attached" warnings.
		Comp->SetupAttachment(AttachParent);
		Comp->RegisterComponentWithWorld(OwnerActor->GetWorld());

		// After registration, if the component accidentally became the root (can happen with
		// stale serialized data), detach it so it doesn't interfere with the actor hierarchy.
		if (OwnerActor->GetRootComponent() == Comp)
		{
			OwnerActor->SetRootComponent(AttachParent);
			Comp->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
			Comp->AttachToComponent(AttachParent, FAttachmentTransformRules::KeepRelativeTransform);
		}
	}

	/** Create a shape component with RF_Transient so it won't be serialized. */
	template<typename T>
	static T* CreateTransientShapeComponent(AActor* OwnerActor)
	{
		return NewObject<T>(OwnerActor, NAME_None, RF_Transient);
	}

	static UShapeComponent* CreateShapeFromSourceData(
		USceneComponent* Outer,
		const FFluidWindSourceData& Data,
		const FColor& Color,
		const FVector& RelativeOffset,
		const FRotator& RelativeRotation)
	{
		AActor* OwnerActor = Outer->GetOwner();
		if (!OwnerActor || !OwnerActor->GetWorld())
		{
			return nullptr;
		}

		UShapeComponent* Result = nullptr;
		switch (Data.Shape)
		{
		case EFluidWindSourceShape::Box:
			{
				UBoxComponent* Box = CreateTransientShapeComponent<UBoxComponent>(OwnerActor);
				Box->SetBoxExtent(FVector(Data.Extent.X, Data.Extent.Y, Data.Extent.Z));
				ConfigureShapeComponent(Box, Color, Outer);
				Box->SetRelativeLocation(RelativeOffset);
				Box->SetRelativeRotation(RelativeRotation);
				Result = Box;
			}
			break;
		case EFluidWindSourceShape::Sphere:
			{
				USphereComponent* Sphere = CreateTransientShapeComponent<USphereComponent>(OwnerActor);
				Sphere->SetSphereRadius(Data.Radius);
				ConfigureShapeComponent(Sphere, Color, Outer);
				Sphere->SetRelativeLocation(RelativeOffset);
				Result = Sphere;
			}
			break;
		case EFluidWindSourceShape::Capsule:
			{
				UCapsuleComponent* Capsule = CreateTransientShapeComponent<UCapsuleComponent>(OwnerActor);
				Capsule->SetCapsuleSize(Data.Radius, Data.Length * 0.5f + Data.Radius);
				ConfigureShapeComponent(Capsule, Color, Outer);
				Capsule->SetRelativeLocation(RelativeOffset);
				Capsule->SetRelativeRotation(RelativeRotation);
				Result = Capsule;
			}
			break;
		default:
			break;
		}
		return Result;
	}
}

void UFluidWindSourceComponent::DestroyShapeVisualization()
{
#if WITH_EDITORONLY_DATA
	for (UShapeComponent* Comp : ShapeVisualizationComponents)
	{
		if (Comp)
		{
			Comp->DestroyComponent();
		}
	}
	ShapeVisualizationComponents.Empty();

	// fix "root component cannot be attached" warnings.
	AActor* Owner = GetOwner();
	if (Owner)
	{
		TArray<UShapeComponent*> StaleShapes;
		Owner->GetComponents<UShapeComponent>(StaleShapes);
		for (UShapeComponent* Comp : StaleShapes)
		{
			if (!Comp || !Comp->bIsEditorOnly)
			{
				continue;
			}
			// Match stale visualization components: either attached to this, or became root unexpectedly
			if (Comp->GetAttachParent() == this || Owner->GetRootComponent() == Comp)
			{
				// If this stale component is root, restore the proper root first
				if (Owner->GetRootComponent() == Comp)
				{
					Owner->SetRootComponent(this);
				}
				Comp->DestroyComponent();
			}
		}
	}
#endif
}


void UFluidWindSourceComponent::UpdateShapeVisualization()
{
#if WITH_EDITORONLY_DATA
	DestroyShapeVisualization();

	if (!bVisualizeShape)
	{
		return;
	}

	const FColor ShapeColor(0, 255, 50, 255);

	USkeletalMesh* SkelMesh = PreviewSkeletalMesh.LoadSynchronous();
	if (SourceData.bUseSkeletalMesh && SkelMesh && SkelMesh->GetPhysicsAsset())
	{
		const UPhysicsAsset* PhysAsset = SkelMesh->GetPhysicsAsset();
		const FReferenceSkeleton& RefSkeleton = SkelMesh->GetRefSkeleton();
		const TArray<FTransform>& RefBonePose = RefSkeleton.GetRefBonePose();

		// Helper: compute component-space transform for a bone by walking up the parent chain
		auto GetComponentSpaceBoneTransform = [&RefSkeleton, &RefBonePose](int32 BoneIdx) -> FTransform
		{
			FTransform ComponentSpaceTransform = FTransform::Identity;
			while (BoneIdx != INDEX_NONE)
			{
				ComponentSpaceTransform = ComponentSpaceTransform * RefBonePose[BoneIdx];
				BoneIdx = RefSkeleton.GetParentIndex(BoneIdx);
			}
			return ComponentSpaceTransform;
		};

		for (const TObjectPtr<USkeletalBodySetup>& BodySetup : PhysAsset->SkeletalBodySetups)
		{
			const FName BoneName = BodySetup->BoneName;
			const int32 BoneIndex = RefSkeleton.FindBoneIndex(BoneName);

			FTransform BoneTransform = FTransform::Identity;
			if (BoneIndex != INDEX_NONE)
			{
				BoneTransform = GetComponentSpaceBoneTransform(BoneIndex);
			}

			const FKAggregateGeom& AggGeom = BodySetup->AggGeom;

			// Sphere bodies
			for (const FKSphereElem& Elem : AggGeom.SphereElems)
			{
				FFluidWindSourceData TempData = SourceData;
				TempData.Shape = EFluidWindSourceShape::Sphere;
				TempData.Radius = Elem.Radius;

				const FVector Offset = BoneTransform.TransformPosition(Elem.Center);
				if (UShapeComponent* Comp = WindSourceViewportVisualization::CreateShapeFromSourceData(
						this, TempData, ShapeColor, Offset, FRotator::ZeroRotator))
				{
					ShapeVisualizationComponents.Add(Comp);
				}
			}

			// Box bodies
			for (const FKBoxElem& Elem : AggGeom.BoxElems)
			{
				FFluidWindSourceData TempData = SourceData;
				TempData.Shape = EFluidWindSourceShape::Box;
				TempData.Extent = FVector3f(Elem.X * 0.5f, Elem.Y * 0.5f, Elem.Z * 0.5f);

				const FVector Offset = BoneTransform.TransformPosition(Elem.Center);
				const FRotator ElemRotation = (BoneTransform.GetRotation() * Elem.Rotation.Quaternion()).Rotator();
				if (UShapeComponent* Comp = WindSourceViewportVisualization::CreateShapeFromSourceData(
						this, TempData, ShapeColor, Offset, ElemRotation))
				{
					ShapeVisualizationComponents.Add(Comp);
				}
			}

			// Capsule bodies (Sphyl)
			for (const FKSphylElem& Elem : AggGeom.SphylElems)
			{
				FFluidWindSourceData TempData = SourceData;
				TempData.Shape = EFluidWindSourceShape::Capsule;
				TempData.Radius = Elem.Radius;
				TempData.Length = Elem.Length;

				const FVector Offset = BoneTransform.TransformPosition(Elem.Center);
				const FRotator ElemRotation = (BoneTransform.GetRotation() * Elem.Rotation.Quaternion()).Rotator();
				if (UShapeComponent* Comp = WindSourceViewportVisualization::CreateShapeFromSourceData(
						this, TempData, ShapeColor, Offset, ElemRotation))
				{
					ShapeVisualizationComponents.Add(Comp);
				}
			}
		}
	}
	else
	{
		// Single source mode: visualize the SourceData shape at component origin
		if (UShapeComponent* Comp = WindSourceViewportVisualization::CreateShapeFromSourceData(
				this, SourceData, ShapeColor, FVector::ZeroVector, FRotator::ZeroRotator))
		{
			ShapeVisualizationComponents.Add(Comp);
		}
	}
#endif
}

void UFluidWindSourceComponent::WindSourceShapeVisualization()
{
	if (GEnableDrawFluidWindSource)
	{
		for (const FFluidWindSourceData& Data : SourceDataGenerated)
		{
			const FColor DebugColor = FColor::Green;
			const FQuat Quat = Data.Rotation.Quaternion();

			switch (Data.Shape)
			{
			case EFluidWindSourceShape::Box:
				DrawDebugBox(GetWorld(), Data.Position, FVector(Data.Extent.X, Data.Extent.Y, Data.Extent.Z),Quat, DebugColor, false, -1.f, 0, 1.5f);
				break;
			case EFluidWindSourceShape::Sphere:
				DrawDebugSphere(GetWorld(), Data.Position, Data.Radius, 12, DebugColor, false, -1.f, 0, 1.5f);
				break;
			case EFluidWindSourceShape::Capsule:
				DrawDebugCapsule(GetWorld(), Data.Position, Data.Length * 0.5f + Data.Radius, Data.Radius, Quat, DebugColor, false, -1.f, 0, 1.5f);
				break;
			default:
				break;
			}
		}
	}
}

#endif