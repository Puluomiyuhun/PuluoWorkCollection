#include "FluidWindFieldComponent.h"
#include "FluidWindCompute.h"
// Engine headers
#include "TextureResource.h"
#include "Engine/TextureRenderTargetVolume.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "Engine/Engine.h"
#include "Components/BoxComponent.h"

extern float GFluidWindVolumeWorldSize;

TWeakObjectPtr<UFluidWindFieldComponent> UFluidWindFieldComponent::ActiveInstance = nullptr;

int32 GEnableDrawFluidWindVolume = 0;
static FAutoConsoleVariableRef CVarEnableDrawFluidWindVoxel(TEXT("r.FluidWind.Debug.ShowVolume"), GEnableDrawFluidWindVolume, TEXT("Draw fluid wind voxels."));

UFluidWindFieldComponent::UFluidWindFieldComponent()
{
	Super::SetComponentTickEnabled(true);
	PrimaryComponentTick.bCanEverTick = true;
	FluidWindVolume = LoadObject<UTextureRenderTargetVolume>(nullptr, TEXT("/PaperEnvironmentalInteraction/FluidWindField/FluidWindVolume.FluidWindVolume"));
	MPCWindParams = LoadObject<UMaterialParameterCollection>(nullptr, TEXT("/PaperEnvironmentalInteraction/FluidWindField/MPC_FluidWind.MPC_FluidWind"));
	LastCellPosition = FIntVector::ZeroValue;
}

void UFluidWindFieldComponent::OnRegister()
{
	Super::OnRegister();

#if WITH_EDITORONLY_DATA
	UpdateFieldRangeBoxVisualization();
#endif
}

void UFluidWindFieldComponent::BeginPlay()
{
	Super::BeginPlay();

	// Check if another FluidWindFieldComponent is already active
	if (ActiveInstance.IsValid() && ActiveInstance.Get() != this)
	{
		bInvalidatedDuplicate = true;
		SetComponentTickEnabled(false);
#if WITH_EDITOR
		const FString WarningMessage = FString::Printf(
			TEXT("FluidWindFieldComponent: Duplicate instance on '%s' has been invalidated. Only one FluidWindFieldComponent is allowed at a time. Active instance is on '%s'."),
			*GetOwner()->GetName(),
			ActiveInstance.IsValid() ? *ActiveInstance->GetOwner()->GetName() : TEXT("Unknown"));

		UE_LOG(LogTemp, Error, TEXT("%s"), *WarningMessage);

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(INDEX_NONE, MAX_FLT, FColor::Red, WarningMessage);
		}
#endif
		return;
	}

	ActiveInstance = this;
	bInvalidatedDuplicate = false;
	
	RegisterComponent();
	
	checkf(FluidWindVolume, TEXT("FluidWindFieldComponent: Please assign CurrentVolume render target volume."));
	checkf(FluidWindVolume->SizeX == FFluidWindCompute::FluidWindCellSizeX &&
		   FluidWindVolume->SizeY == FFluidWindCompute::FluidWindCellSizeY &&
		   FluidWindVolume->SizeZ == FFluidWindCompute::FluidWindCellSizeZ,
		TEXT("FluidWindFieldComponent: FluidWindVolume size mismatch with GFluidWindCellSizeX/Y/Z."));
	
	FluidWindVolume->OverrideFormat = PF_FloatRGBA;
	FluidWindVolume->bSupportsUAV = true;
	FluidWindVolume->UpdateResource();
	IsInit = false;
}

void UFluidWindFieldComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (bInvalidatedDuplicate)
	{
		return;
	}

	if (ActiveInstance.Get() == this)
	{
		ActiveInstance = nullptr;
	}

	FFluidWindCompute::Get().ClearWind();
}

inline FIntVector3 ConvertFloatPointToCell(const FVector& Value)
{
	FIntVector3 OutValue;
	OutValue.X = Value.X < 0 ? FMath::CeilToInt32(Value.X) : FMath::FloorToInt32(Value.X);
	OutValue.Y = Value.Y < 0 ? FMath::CeilToInt32(Value.Y) : FMath::FloorToInt32(Value.Y);
	OutValue.Z = Value.Z < 0 ? FMath::CeilToInt32(Value.Z) : FMath::FloorToInt32(Value.Z);
	return OutValue;
}

void UFluidWindFieldComponent::TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	if (!IsInit)
	{
		if (FluidWindVolume->GameThread_GetRenderTargetResource() && FluidWindVolume->GameThread_GetRenderTargetResource()->IsInitialized())
		{
			FWindInitResources InitResources;
			InitResources.FluidWindVolume = FluidWindVolume;
			FFluidWindCompute::Get().Init(InitResources);
			
			IsInit = true;
		}

		return;
	}

	// Determine 3D volume location based on follow mode
	FVector VolumeZeroBasedLocation = FVector::Zero();
	if (const AActor* Owner = GetOwner();Owner)
	{
		VolumeZeroBasedLocation = Owner->GetActorLocation();
	}
	
	if (FollowMode == EFieldFollowMode::CenterBased)
	{
		AxisOffsetInWorldX = 0.5f;
		AxisOffsetInWorldY = 0.5f;
		VolumeZeroBasedLocation -= FVector(
			FFluidWindCompute::FluidWindCellSizeX * GFluidWindVolumeWorldSize * AxisOffsetInWorldX,
			FFluidWindCompute::FluidWindCellSizeY * GFluidWindVolumeWorldSize * AxisOffsetInWorldY,
			FFluidWindCompute::FluidWindCellSizeZ * GFluidWindVolumeWorldSize * AxisOffsetInWorldZ);
	}
	else
	{
		VolumeZeroBasedLocation = VolumeZeroBasedLocation - FVector(
			FFluidWindCompute::FluidWindCellSizeX * GFluidWindVolumeWorldSize * AxisOffsetInWorldX,
			FFluidWindCompute::FluidWindCellSizeY * GFluidWindVolumeWorldSize * AxisOffsetInWorldY,
			FFluidWindCompute::FluidWindCellSizeZ * GFluidWindVolumeWorldSize * AxisOffsetInWorldZ);
	}
	
	const FIntVector CellPosition = ConvertFloatPointToCell(VolumeZeroBasedLocation / GFluidWindVolumeWorldSize);
	
	UpdateRuntimeParams(VolumeZeroBasedLocation, CellPosition - LastCellPosition);
	LastCellPosition = CellPosition;
	FFluidWindCompute::Get().RenderWind(DeltaTime, VolumeZeroBasedLocation, CellPosition, GetWorld()->GetFeatureLevel());

	// If enabled, draw fluid wind volume with Niagara
#if WITH_EDITOR
	if (GEnableDrawFluidWindVolume)
	{
		UE_LOG(LogTemp, Log, TEXT("FluidWindField Component Updated VolumeZeroBasedLocation: %f, %f, %f"), VolumeZeroBasedLocation.X, VolumeZeroBasedLocation.Y, VolumeZeroBasedLocation.Z);
		UE_LOG(LogTemp, Log, TEXT("FluidWindField Component Updated Cell: %d, %d, %d"), CellPosition.X, CellPosition.Y, CellPosition.Z);
		if (!DebugFluidWindNiagaraComponent)
		{
			if (const AActor* Owner = GetOwner();Owner)
			{
				UNiagaraSystem* NiagaraSystem = LoadObject<UNiagaraSystem>(nullptr, TEXT("/PaperEnvironmentalInteraction/FluidWindField/NS_FluidWindVisualization.NS_FluidWindVisualization"));
				DebugFluidWindNiagaraComponent = NewObject<UNiagaraComponent>(GetOwner());
				DebugFluidWindNiagaraComponent->SetAsset(NiagaraSystem);
				DebugFluidWindNiagaraComponent->RegisterComponentWithWorld(GetWorld());
				DebugFluidWindNiagaraComponent->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
				DebugFluidWindNiagaraComponent->Activate(true);
				DebugFluidWindNiagaraComponent->SetVariableTextureRenderTarget(FName(TEXT("WindVolumeRenderTarget")), FluidWindVolume);
				UE_LOG(LogTemp, Log, TEXT("FluidWindField Component: Created Debug Niagara Component for visualizing fluid wind volume."));
			}
		}

		// Manually update user parameters since the NPC is now user-configurable
		if (DebugFluidWindNiagaraComponent)
		{
			DebugFluidWindNiagaraComponent->SetVariableVec3(FName(TEXT("WorldPivotLocation")), VolumeZeroBasedLocation);
			DebugFluidWindNiagaraComponent->SetVariableFloat(FName(TEXT("WindScalar")), WindScaler);
			DebugFluidWindNiagaraComponent->SetVariableVec3(FName(TEXT("VolumeGridsOffset")), FVector(AxisOffsetInWorldX, AxisOffsetInWorldY, AxisOffsetInWorldZ));
			DebugFluidWindNiagaraComponent->SetVariableVec3(FName(TEXT("WorldGridExtents")), FVector(
				FFluidWindCompute::FluidWindCellSizeX * GFluidWindVolumeWorldSize,
				FFluidWindCompute::FluidWindCellSizeY * GFluidWindVolumeWorldSize,
				FFluidWindCompute::FluidWindCellSizeZ * GFluidWindVolumeWorldSize));
		}
	}
	else
	{
		if (DebugFluidWindNiagaraComponent)
		{
			DebugFluidWindNiagaraComponent->DestroyComponent();
			DebugFluidWindNiagaraComponent = nullptr;
		}
	}

#endif

	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	
}

void UFluidWindFieldComponent::UpdateRuntimeParams(const FVector& PivotPosition, const FIntVector& AxisOffsetInCells)
{
	UMaterialParameterCollectionInstance* MPCWindParamsInstance = GetWorld()->GetParameterCollectionInstance(MPCWindParams);
	MPCWindParamsInstance->SetVectorParameterValue(FName("WorldPivotLocation"),PivotPosition);
	MPCWindParamsInstance->SetScalarParameterValue(FName("WindScalar"), WindScaler);
	MPCWindParamsInstance->SetVectorParameterValue(FName("VolumeGridsOffset"),FVector3f(AxisOffsetInWorldX, AxisOffsetInWorldY, AxisOffsetInWorldZ));
	MPCWindParamsInstance->SetVectorParameterValue(FName("WorldGridExtents"),FVector3f(FFluidWindCompute::FluidWindCellSizeX * GFluidWindVolumeWorldSize,
		FFluidWindCompute::FluidWindCellSizeY * GFluidWindVolumeWorldSize, FFluidWindCompute::FluidWindCellSizeZ * GFluidWindVolumeWorldSize));

	FWindParams WindParams;
	WindParams.DiffusionForce = DiffusionForce;
	WindParams.AdvectionForce = AdvectionForce;
	WindParams.AxisOffsetInCells = AxisOffsetInCells;
	FFluidWindCompute::Get().UpdateWindParams(WindParams);
}

#if WITH_EDITOR
void UFluidWindFieldComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);
	UpdateFieldRangeBoxVisualization();
}
#endif

void UFluidWindFieldComponent::DestroyFieldRangeBoxVisualization()
{
#if WITH_EDITORONLY_DATA
	if (FieldRangeBoxComponent)
	{
		FieldRangeBoxComponent->DestroyComponent();
		FieldRangeBoxComponent = nullptr;
	}
#endif
}

void UFluidWindFieldComponent::UpdateFieldRangeBoxVisualization()
{
#if WITH_EDITORONLY_DATA
	DestroyFieldRangeBoxVisualization();

	if (!VisualizeFieldRangeBox)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner || !Owner->GetRootComponent())
	{
		return;
	}

	// Grid full extent in world units
	const FVector GridExtent(
		FFluidWindCompute::FluidWindCellSizeX * GFluidWindVolumeWorldSize,
		FFluidWindCompute::FluidWindCellSizeY * GFluidWindVolumeWorldSize,
		FFluidWindCompute::FluidWindCellSizeZ * GFluidWindVolumeWorldSize);

	// The box half-extent (UBoxComponent uses half-extent)
	const FVector HalfExtent = GridExtent * 0.5;

	// Compute relative offset from the actor origin to the box center.
	// This mirrors the runtime logic in TickComponent:
	//   CenterBased  -> volume origin = actor - GridExtent * (0.5, 0.5, OffsetZ)
	//   ZeroBased    -> volume origin = actor - GridExtent * (OffsetX, OffsetY, OffsetZ)
	// Box center = volume origin + HalfExtent
	FVector RelativeCenter;
	if (FollowMode == EFieldFollowMode::CenterBased)
	{
		RelativeCenter = FVector(
			-GridExtent.X * 0.5 + HalfExtent.X,
			-GridExtent.Y * 0.5 + HalfExtent.Y,
			-GridExtent.Z * AxisOffsetInWorldZ + HalfExtent.Z);
	}
	else
	{
		RelativeCenter = FVector(
			-GridExtent.X * AxisOffsetInWorldX + HalfExtent.X,
			-GridExtent.Y * AxisOffsetInWorldY + HalfExtent.Y,
			-GridExtent.Z * AxisOffsetInWorldZ + HalfExtent.Z);
	}

	const FColor BoxColor = (FollowMode == EFieldFollowMode::CenterBased) ? FColor::Cyan : FColor::Yellow;

	FieldRangeBoxComponent = NewObject<UBoxComponent>(Owner, NAME_None, RF_Transactional);
	FieldRangeBoxComponent->SetBoxExtent(HalfExtent);
	FieldRangeBoxComponent->ShapeColor = BoxColor;
	FieldRangeBoxComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FieldRangeBoxComponent->SetHiddenInGame(true);
	FieldRangeBoxComponent->bIsEditorOnly = true;
	FieldRangeBoxComponent->SetAbsolute(false, false, true);
	FieldRangeBoxComponent->SetupAttachment(Owner->GetRootComponent());
	FieldRangeBoxComponent->SetRelativeLocation(RelativeCenter);
	FieldRangeBoxComponent->RegisterComponent();
#endif
}
