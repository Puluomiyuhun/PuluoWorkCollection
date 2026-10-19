#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Components/ActorComponent.h"
#include "FluidWindFieldComponent.generated.h"

class UBoxComponent;
class UMaterialParameterCollection;

UENUM()
enum class EFieldFollowMode : uint8
{
	ZeroBased = 0,
	CenterBased = 1,
};

UCLASS(ClassGroup=(FluidWind), meta=(BlueprintSpawnableComponent))
class PAPERFLUIDWINDFIELD_API UFluidWindFieldComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	
	UFluidWindFieldComponent();
	
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnRegister() override;

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	void UpdateRuntimeParams(const FVector& PivotPosition, const FIntVector& AxisOffsetInCells);

	/** Create / recreate the editor-only box component that visualises the field range. */
	void UpdateFieldRangeBoxVisualization();
	/** Destroy the editor-only box visualisation component if it exists. */
	void DestroyFieldRangeBoxVisualization();

	/** The active instance in the world. Only one FluidWindFieldComponent is allowed at a time. */
	static TWeakObjectPtr<UFluidWindFieldComponent> ActiveInstance;

	/** Whether this instance was invalidated because another instance was already active. */
	bool bInvalidatedDuplicate = false;
	
	UPROPERTY(VisibleAnywhere, Category="FluidWind|Runtime")
	class UTextureRenderTargetVolume* FluidWindVolume;
	
	UPROPERTY(EditAnywhere, Category="FluidWind|Runtime")
	float DiffusionForce = 0.5f;
	
	UPROPERTY(EditAnywhere, Category="FluidWind|Runtime")
	float AdvectionForce = 0.5f;
	
	UPROPERTY(EditAnywhere, Category="FluidWind|Runtime")
	float WindScaler = 1.f;

	UPROPERTY(EditAnywhere, Category="FluidWind|Runtime")
	EFieldFollowMode FollowMode = EFieldFollowMode::CenterBased;
	
	UPROPERTY(EditAnywhere, Category="FluidWind|Runtime", meta=(Range="-1.0,1.0"))
	float AxisOffsetInWorldZ = 0.f;

	UPROPERTY(EditAnywhere, Category="FluidWind|Runtime", meta=(EditCondition="FollowMode == EFieldFollowMode::ZeroBased", EditConditionHides, Range="-1.0,1.0"))
	float AxisOffsetInWorldX = 0.f;

	UPROPERTY(EditAnywhere, Category="FluidWind|Runtime", meta=(EditCondition="FollowMode == EFieldFollowMode::ZeroBased", EditConditionHides, Range="-1.0,1.0"))
	float AxisOffsetInWorldY = 0.f;
	
	UPROPERTY(EditAnywhere, Category="FluidWind|Runtime")
	UMaterialParameterCollection* MPCWindParams = nullptr;
	
	bool IsInit;

	FIntVector LastCellPosition;
	
#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<class UNiagaraComponent> DebugFluidWindNiagaraComponent;

	UPROPERTY(EditAnywhere, Category="FluidWind|Debug")
	bool VisualizeFieldRangeBox = true;

	/** Editor-only box component for visualising the field range in the viewport. */
	UPROPERTY(Transient)
	TObjectPtr<UBoxComponent> FieldRangeBoxComponent = nullptr;
#endif
};
