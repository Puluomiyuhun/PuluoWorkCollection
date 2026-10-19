#pragma once
#include "FluidWindDataTypes.h"
#include "Engine/TextureRenderTargetVolume.h"

struct FWindInitResources
{
	UTextureRenderTargetVolume* FluidWindVolume;
};

struct FWindParams
{
	float DiffusionForce;
	float AdvectionForce;
	FIntVector AxisOffsetInCells;

	explicit FWindParams(const float InDiffusionForce = 0.f, const float InAdvectionForce = 0.f)  
		: DiffusionForce(InDiffusionForce)
		, AdvectionForce(InAdvectionForce)
		, AxisOffsetInCells(FIntVector::ZeroValue)
	{}
};


struct FWindSourceShaderData
{
	int32 InfoType;
	int32 Shape;
	int32 Padding0;
	int32 Padding1;
	
	FVector4f Position;
	
	// Wind Source Shape
	FVector4f Extents;

	// Wind Source Strength / Direction / Axis-Force
	FVector4f Behavior;

	// Only used for Moving source to store the current speed, for other source types, it will be set to zero and ignored in shader.
	FVector4f AngleSpeed;

	FMatrix44f RotationMatrix;

	FWindSourceShaderData& operator=(const FWindSourceShaderData& Other)
	{
		InfoType = Other.InfoType;
		Shape = Other.Shape;
		Position = Other.Position;
		RotationMatrix = Other.RotationMatrix;
		Extents = Other.Extents;
		Behavior = Other.Behavior;
		AngleSpeed = Other.AngleSpeed;
		return *this;
	}
};


class UFluidWindSourceComponent;

class FFluidWindCompute
{
public:
	// Todo: expose these values to config file or console command for better tweaking.
	// Add max source count limit to avoid GPU performance issue. If there are too many sources, we can consider sorting them by distance and only update the closest ones.
    constexpr static int32 MaxSourceCount = 10;
	constexpr static int32 GroupThreadSize = 4;
	constexpr static int32 SourceTypeCount = 4;
	constexpr static int32 FluidWindCellSizeX = 32;
	constexpr static int32 FluidWindCellSizeY = 32;
	constexpr static int32 FluidWindCellSizeZ = 16;
	constexpr static int32 FluidWindTotalVolumeSize = FluidWindCellSizeX * FluidWindCellSizeY * FluidWindCellSizeZ;
	
	
	static FFluidWindCompute& Get();
	
	FFluidWindCompute();

	void Init(const FWindInitResources& InitParams);
	void UpdateWindParams(const FWindParams& Params);

	void AddWindSource(UFluidWindSourceComponent* In);
	void RemoveWindSource(UFluidWindSourceComponent* In);
	void UpdateWindSource(UFluidWindSourceComponent* In);
	
	void RenderWind(float DeltaTime, const FVector& OffsetPosition, const FIntVector3& CellPosition, ERHIFeatureLevel::Type FeatureLevel);

	void ClearWind();

protected:
#if !(UE_BUILD_SHIPPING || UE_BUILD_TEST)
	friend class FVolumeVisualizationExtension;
#endif
	
	enum class ERefreshType
	{
		Update = 0,
		Add,
		Remove
	};
	
	struct FUpdateSourceCommand
	{
		int32 HashId;
		float DeltaTime;
		ERefreshType RefreshType;
		EFluidWindSourceType Type;
		FWindSourceShaderData SourceShaderData;
	};

	void UpdateWindSourceCommand(FUpdateSourceCommand& Command, const FFluidWindSourceData& SourceData);
	void UpdateWindSourceLists(const FUpdateSourceCommand& Command);
	void RenderWind_InThread(FRHICommandListImmediate& RHICmdList, float DeltaTime, const FVector& PivotPosition,const FIntVector3& CellPosition,  ERHIFeatureLevel::Type FeatureLevel);

	FIntVector3 VolumeSize;
	FIntVector3 VolumeSizeMinusOne;

	FWindParams RuntimeParams;

	FIntVector3 CurrentCellPosition;
	FIntVector3 LastCellPosition;
	

	TArray<FWindParams> UpdateParamsQueue;
	TArray<FUpdateSourceCommand> SourceCommandQueue;

	int32 SourceHashIdGenerator;
	TArray<TArray<FWindSourceShaderData>>              WindSourceLists;
	TArray<TArray<int32>>                        WindSourceHashIdLists;

	FTextureRHIRef VolumeTexRHIRefs[3];
	FTextureRHIRef TransientVolumeTexRHIRefs[3];

	FTextureRHIRef DivergenceTexRHIRef;
	FTextureRHIRef PressureTexRHIRef[2];
	
	FTextureRenderTargetResource* FluidWindVolume;

	bool bInitialized;
};
