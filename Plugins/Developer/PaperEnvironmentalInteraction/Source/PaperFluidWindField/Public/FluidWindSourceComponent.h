#pragma once
#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"
#include "Components/SceneComponent.h"
#include "FluidWindDataTypes.h"
#include "FluidWindSourceComponent.generated.h"

class USkeletalMeshComponent;
class USkeletalMesh;
class UShapeComponent;
class UBoxComponent;
class USphereComponent;
class UCapsuleComponent;


/** UFluidWindSourceComponent
 * PaperGames. Z2 Environmental Interaction. Author: HuJiaoFen. Created: 2026-03-26.
 * This component represents a wind source in the fluid wind field system.
 * It generates wind source data based on the user settings and feeds it to the FFluidWindCompute for simulation.
 * The wind source can be of different types (Directional, OmniDirectional, Vortex, Moving) and shapes (Box, Sphere, Capsule).
 */
UCLASS(ClassGroup=(FluidWind), meta=(BlueprintSpawnableComponent))
class PAPERFLUIDWINDFIELD_API UFluidWindSourceComponent: public USceneComponent
{
	GENERATED_BODY()
public:
	UFluidWindSourceComponent();

	void SetSourceDataInfoId(const int32 Index, const int32 InInfoId);
	int32 GetSourceDataInfoId(const int32 Index) const;
	
	const TArray<FFluidWindSourceData>& GetSourceData() const { return SourceDataGenerated; }
	
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual bool CanEditChange(const FProperty* InProperty) const override;
#endif

protected:
	friend class FFluidWindCompute;
	TArray<FFluidWindSourceData> SourceDataGenerated;
	
private:
	void InitializeWindSourceData();
	void UpdateWindSourceData(float DeltaTime);

	// This is the data that user can edit in editor, and it will be used to generate the real source data.
	// In most of the cases, only one data will be generated. But for the 'PhysicalBody' type, multiple instance will be generated based on the number of bodies in the physics asset.
	UPROPERTY(EditAnywhere, Category = WindSource)
	FFluidWindSourceData SourceData;
	
	UPROPERTY(Transient)
	TWeakObjectPtr<USkeletalMeshComponent> SkeletalMeshCompCached = nullptr;
	
	
#if WITH_EDITORONLY_DATA
	/** When enabled, shows the wind source shape in the editor viewport. */
	UPROPERTY(EditAnywhere, Category = "WindSource|Debug")
	bool bVisualizeShape = true;

	UPROPERTY(EditAnywhere, Category = "WindSource|Debug")
	TSoftObjectPtr<USkeletalMesh> PreviewSkeletalMesh = nullptr;
	
	/** The editor-only shape component used for viewport visualization. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UShapeComponent>> ShapeVisualizationComponents;

	// Editor-only visualization
	void UpdateShapeVisualization();
	void DestroyShapeVisualization();
	void WindSourceShapeVisualization();
#endif
	
};