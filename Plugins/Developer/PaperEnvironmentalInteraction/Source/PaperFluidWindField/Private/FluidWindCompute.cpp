#include "FluidWindCompute.h"
#include "ShaderParameterStruct.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "FluidWindSourceComponent.h"
#include "GlobalShader.h"
#include "SimpleMeshDrawCommandPass.h"
#include "TextureRenderTargetVolumeResource.h"

int32 GFluidWindBlurSubSteps = 5;
static FAutoConsoleVariableRef CVarFluidWindBlurSubSteps(TEXT("r.FluidWind.BlurSubSteps"), GFluidWindBlurSubSteps, TEXT("The number of blur sub steps per frame. Must be an odd number."), ECVF_RenderThreadSafe);

float GFluidWindVolumeWorldSize = 25.f;
static FAutoConsoleVariableRef CVarFluidWindVolumeSize(TEXT("r.FluidWind.VolumeWorldSize"), GFluidWindVolumeWorldSize, TEXT("The world size of fluid wind volume."), ECVF_RenderThreadSafe);

// GPU profiler stats 
DECLARE_GPU_STAT_NAMED(FluidWind,           TEXT("FluidWind"));
DECLARE_GPU_STAT_NAMED(FluidWindOffset,     TEXT("FluidWind.Offset"));
DECLARE_GPU_STAT_NAMED(FluidWindSource,     TEXT("FluidWind.Source"));
DECLARE_GPU_STAT_NAMED(FluidWindDiffusion,  TEXT("FluidWind.Diffusion"));
DECLARE_GPU_STAT_NAMED(FluidWindAdvection,  TEXT("FluidWind.Advection"));
DECLARE_GPU_STAT_NAMED(FluidWindProjection, TEXT("FluidWind.Projection"));
DECLARE_GPU_STAT_NAMED(FluidWindMerge,      TEXT("FluidWind.Merge"));


const TCHAR* TransientTextureNames[3] = {
	TEXT("RT.FluidWind.TransientVolume_Channel_X"),
	TEXT("RT.FluidWind.TransientVolume_Channel_Y"),
	TEXT("RT.FluidWind.TransientVolume_Channel_Z"),
};

const TCHAR* FrameTextureNames[3] = {
	TEXT("RT.FluidWind.FrameVolume_Channel_X"),
	TEXT("RT.FluidWind.FrameVolume_Channel_Y"),
	TEXT("RT.FluidWind.FrameVolume_Channel_Z"),
};

const TCHAR* SourceTypeNames[FFluidWindCompute::SourceTypeCount] = {
	TEXT("FluidWind.SourceBuffer_Directional"),
	TEXT("FluidWind.SourceBuffer_Omni"),
	TEXT("FluidWind.SourceBuffer_Vortex"),
	TEXT("FluidWind.SourceBuffer_Moving"),
};

// All compute shaders for fluid wind field
namespace FluidWindShader
{
	/**  FOffsetPositionShader
	 *  This shader is used to offset the wind field position in the 3D texture volume.
	 *  Since the dynamic wind filed typically bind to the player character, when the player move, we need to offset the wind field position
	 *  to keep the wind field centered around the player.
	 **/
	class FOffsetPositionCS : public FGlobalShader
	{
	public:
		DECLARE_EXPORTED_SHADER_TYPE(FOffsetPositionCS, Global, PAPERFLUIDWINDFIELD_API);
		SHADER_USE_PARAMETER_STRUCT(FOffsetPositionCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
			SHADER_PARAMETER(FIntVector3,VolumeSizeMinusOne)
			SHADER_PARAMETER(FIntVector3,OffsetPosition)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, WindBufferInputX)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, WindBufferInputY)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, WindBufferInputZ)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, WindBufferOutputX)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, WindBufferOutputY)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, WindBufferOutputZ)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return true;	
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
		{
			Environment.SetDefine("COMPUTE_OFFSET_CS", 1);
			FGlobalShader::ModifyCompilationEnvironment(Parameters, Environment);
		}
	};

	/** FDiffusionShader
	 * 
	 **/
	class FDiffusionCS : public FGlobalShader
	{
	public:
		DECLARE_EXPORTED_SHADER_TYPE(FDiffusionCS, Global, PAPERFLUIDWINDFIELD_API);
		SHADER_USE_PARAMETER_STRUCT(FDiffusionCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
			SHADER_PARAMETER(FIntVector3,VolumeSizeMinusOne)
			SHADER_PARAMETER(float,DiffusionForce)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, BlurBufferInput)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, BlurBufferOutput)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return true;	
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			OutEnvironment.SetDefine("COMPUTE_BLUR_CS", 1);
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		}
	};

	/** FSourceSpeedCS
	 *  Applies all wind source forces (simple sources + PhysicalBody bone-driven sources) in a single dispatch.
	 **/
	class FSourceSpeedCS : public FGlobalShader
	{
	public:
		DECLARE_EXPORTED_SHADER_TYPE(FSourceSpeedCS, Global, PAPERFLUIDWINDFIELD_API);
		SHADER_USE_PARAMETER_STRUCT(FSourceSpeedCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
			SHADER_PARAMETER(float, WorldSize)
			SHADER_PARAMETER(FVector3f, VolumePosPivot)
			SHADER_PARAMETER(uint32, SourceBufferCount)
			SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<FWindSourceShaderData>, WindSourceBuffer)
			// Volume textures
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, WindBufferInputX)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, WindBufferInputY)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, WindBufferInputZ)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, WindBufferOutputX)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, WindBufferOutputY)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, WindBufferOutputZ)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return true;	
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		}
	};

	/** FAdvectionBase
	 * 
	 **/
	class FAdvectionBase : public FGlobalShader
	{
	public:
		BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
			SHADER_PARAMETER(float,DeltaTime)
			SHADER_PARAMETER(float,WorldSize)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, WindBufferInputX)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, WindBufferInputY)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, WindBufferInputZ)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<int3>, WindBufferTarget)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<int3>, WindBufferOutput)
			SHADER_PARAMETER(FIntVector3,VolumeSizeMinusOne)
			SHADER_PARAMETER(float,AdvectionForce)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return true;	
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			FGlobalShader::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		}
	};
	
	class FAdvectionSemiLagrangianCS : public FAdvectionBase
	{
		DECLARE_EXPORTED_SHADER_TYPE(FAdvectionSemiLagrangianCS, Global, PAPERFLUIDWINDFIELD_API);
		SHADER_USE_PARAMETER_STRUCT(FAdvectionSemiLagrangianCS, FAdvectionBase);

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return true;	
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutEnvironment)
		{
			OutEnvironment.SetDefine("ADVECTION_MODE", 0);
			FAdvectionBase::ModifyCompilationEnvironment(Parameters, OutEnvironment);
		}
	};

	/** FDivergenceCS
	 * 
	 **/
	class FDivergenceCS : public FGlobalShader
	{
	public:
		DECLARE_EXPORTED_SHADER_TYPE(FDivergenceCS, Global, PAPERFLUIDWINDFIELD_API);
		SHADER_USE_PARAMETER_STRUCT(FDivergenceCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
			SHADER_PARAMETER(float,WorldSize)
			SHADER_PARAMETER(FIntVector3,VolumeSizeMinusOne)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, VelocityInputX)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, VelocityInputY)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, VelocityInputZ)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, DivergenceOutput)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return true;	
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
		{
			Environment.SetDefine("COMPUTE_DIVERGENCE_CS", 1);
			FGlobalShader::ModifyCompilationEnvironment(Parameters, Environment);
		}
	};

	/** FPressureCS
	 * 
	 **/
	class FPressureCS : public FGlobalShader
	{
	public:
		DECLARE_EXPORTED_SHADER_TYPE(FPressureCS, Global, PAPERFLUIDWINDFIELD_API);
		SHADER_USE_PARAMETER_STRUCT(FPressureCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
			SHADER_PARAMETER(int,IterationIndex)
			SHADER_PARAMETER(float,Relaxation)
			SHADER_PARAMETER(float,WorldSize)
			SHADER_PARAMETER(float,DeltaTime)
			SHADER_PARAMETER(FIntVector3,VolumeSizeMinusOne)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, DivergenceInput)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, PressureInput)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, PressureOutput)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return true;	
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
		{
			Environment.SetDefine("COMPUTE_PRESSURE_CS", 1);
			FGlobalShader::ModifyCompilationEnvironment(Parameters, Environment);
		}
	};

	/** FVelocityRectificationCS
	 * 
	 **/
	class FVelocityRectificationCS : public FGlobalShader
	{
	public:
		DECLARE_EXPORTED_SHADER_TYPE(FVelocityRectificationCS, Global, PAPERFLUIDWINDFIELD_API);
		SHADER_USE_PARAMETER_STRUCT(FVelocityRectificationCS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
			SHADER_PARAMETER(float,WorldSize)
			SHADER_PARAMETER(FIntVector3,VolumeSizeMinusOne)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, VelocityInputX)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, VelocityInputY)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, VelocityInputZ)
			SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, PressureInput)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, VelocityOutputX)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, VelocityOutputY)
			SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float>, VelocityOutputZ)
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return true;	
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& Environment)
		{
			Environment.SetDefine("COMPUTE_RECTIFY_VELOCITY_CS", 1);
			FGlobalShader::ModifyCompilationEnvironment(Parameters, Environment);
		}
	};
	
	/** FMergeChannelCS
	 * 
	 **/
	class FMergeChannelCS : public FGlobalShader
	{
	public:
		DECLARE_EXPORTED_SHADER_TYPE(FMergeChannelCS, Global, PAPERFLUIDWINDFIELD_API);
		SHADER_USE_PARAMETER_STRUCT(FMergeChannelCS, FGlobalShader);
		BEGIN_SHADER_PARAMETER_STRUCT(FParameters,)
				SHADER_PARAMETER(float,WorldSize)
				SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, WindBufferInputX)
				SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, WindBufferInputY)
				SHADER_PARAMETER_RDG_TEXTURE_SRV(Texture3D<float>, WindBufferInputZ)
				SHADER_PARAMETER_RDG_TEXTURE_UAV(RWTexture3D<float3>, WindBufferOutput)
		END_SHADER_PARAMETER_STRUCT()
	
		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return true;	
		}

		static void ModifyCompilationEnvironment(const FGlobalShaderPermutationParameters& Parameters, FShaderCompilerEnvironment& OutComponent)
		{
			FAdvectionBase::ModifyCompilationEnvironment(Parameters, OutComponent);
		}
	};
}

IMPLEMENT_GLOBAL_SHADER(FluidWindShader::FOffsetPositionCS,  		 "/Shaders/FluidWindCompute.usf",	    "ComputeOffsetPosition", SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FluidWindShader::FDiffusionCS,		 		 "/Shaders/FluidWindCompute.usf",	    "ComputeDiffusion",      SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FluidWindShader::FDivergenceCS,				 "/Shaders/FluidWindCompute.usf",	    "ComputeDivergence",     SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FluidWindShader::FPressureCS,				 "/Shaders/FluidWindCompute.usf",	    "ComputePressure",     SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FluidWindShader::FVelocityRectificationCS,	 "/Shaders/FluidWindCompute.usf",	    "RectifyVelocity",       SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FluidWindShader::FSourceSpeedCS,	 		 "/Shaders/FluidWindSourceForce.usf",   "MainCS",				 SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FluidWindShader::FAdvectionSemiLagrangianCS, "/Shaders/FluidWindAdvection.usf",     "MainCS",				 SF_Compute);
IMPLEMENT_GLOBAL_SHADER(FluidWindShader::FMergeChannelCS,			 "/Shaders/FluidWindMergeChannel.usf",  "MainCS",				 SF_Compute);

using FOffsetPositionCompute = FluidWindShader::FOffsetPositionCS;
using FDiffusionCompute = FluidWindShader::FDiffusionCS;
using FDivergenceCompute = FluidWindShader::FDivergenceCS;
using FPressureCompute = FluidWindShader::FPressureCS;
using FVelocityRectificationCompute = FluidWindShader::FVelocityRectificationCS;
using FSourceSpeedCompute = FluidWindShader::FSourceSpeedCS;
using FAdvectionSemiLagrangianCompute = FluidWindShader::FAdvectionSemiLagrangianCS;
using FMergeChannelCompute = FluidWindShader::FMergeChannelCS;

FFluidWindCompute& FFluidWindCompute::Get()
{
	static FFluidWindCompute Instance;
	return Instance;
}

FFluidWindCompute::FFluidWindCompute()
	:VolumeSize(FluidWindCellSizeX, FluidWindCellSizeY, FluidWindCellSizeZ),
	VolumeSizeMinusOne(VolumeSize.X - 1, VolumeSize.Y - 1, VolumeSize.Z - 1),
	RuntimeParams(FWindParams())
{
	CurrentCellPosition = FIntVector3(0, 0, 0);
	LastCellPosition = FIntVector3(0, 0, 0);
	FluidWindVolume = nullptr;
	UpdateParamsQueue.Empty();
	SourceCommandQueue.Empty();
	SourceHashIdGenerator = 0;
	WindSourceHashIdLists.AddDefaulted(SourceTypeCount);
	WindSourceLists.AddDefaulted(SourceTypeCount);
	bInitialized = false;
}

void FFluidWindCompute::Init(const FWindInitResources& InitParams)
{
	ENQUEUE_RENDER_COMMAND(InitQueue)(
	[InitParams,this](FRHICommandListImmediate& RHICmdList)
	{
		FluidWindVolume = InitParams.FluidWindVolume->GetRenderTargetResource();
	
		VolumeSize = FIntVector3(FluidWindVolume->GetSizeX(), FluidWindVolume->GetSizeY(), FluidWindVolume->GetSizeZ());

		VolumeSizeMinusOne.X = VolumeSize.X - 1;
		VolumeSizeMinusOne.Y = VolumeSize.Y - 1;
		VolumeSizeMinusOne.Z = VolumeSize.Z - 1;

		FRHITextureCreateDesc PersistentVolumeTexDesc = FRHITextureCreateDesc::Create3D(
			TEXT("NA"),
			VolumeSize,
			PF_R32_FLOAT
		);
		PersistentVolumeTexDesc.SetFlags(
			ETextureCreateFlags::Dynamic | ETextureCreateFlags::AtomicCompatible | ETextureCreateFlags::RenderTargetable|
			ETextureCreateFlags::ShaderResource | ETextureCreateFlags::NoTiling | ETextureCreateFlags::UAV);
		
		for (int i = 0; i < 3; ++i)
		{
			PersistentVolumeTexDesc.SetDebugName(TransientTextureNames[i]);
			TransientVolumeTexRHIRefs[i] = RHICmdList.CreateTexture(PersistentVolumeTexDesc);
			PersistentVolumeTexDesc.SetDebugName(FrameTextureNames[i]);
			VolumeTexRHIRefs[i] = RHICmdList.CreateTexture(PersistentVolumeTexDesc);
		}

		PersistentVolumeTexDesc.SetDebugName(TEXT("FluidWind.DivergenceVolume"));
		DivergenceTexRHIRef = RHICmdList.CreateTexture(PersistentVolumeTexDesc);
		PersistentVolumeTexDesc.SetDebugName(TEXT("FluidWind.PressureVolume0"));
		PressureTexRHIRef[0] = RHICmdList.CreateTexture(PersistentVolumeTexDesc);
		PersistentVolumeTexDesc.SetDebugName(TEXT("FluidWind.PressureVolume1"));
		PressureTexRHIRef[1] = RHICmdList.CreateTexture(PersistentVolumeTexDesc);

		bInitialized = true;
	});
}

void FFluidWindCompute::UpdateWindParams(const FWindParams& Params)
{
	ENQUEUE_RENDER_COMMAND(UpdateParams)(
	[this,Params](FRHICommandListImmediate& RHICmdList)
	{
		UpdateParamsQueue.Add(Params);
	});
}

void FFluidWindCompute::AddWindSource(UFluidWindSourceComponent* In)
{
	if (In != nullptr)
	{
		// Here we set the DataID by the 'friend class' 
		for (int i = 0; i < In->SourceDataGenerated.Num(); ++i)
		{
			FUpdateSourceCommand AddCommand;
			AddCommand.HashId = ++SourceHashIdGenerator;
			AddCommand.RefreshType = ERefreshType::Add;
			In->SourceDataGenerated[i].DataId = AddCommand.HashId;
			UpdateWindSourceCommand(AddCommand, In->SourceDataGenerated[i]);
		}
	}
}

void FFluidWindCompute::RemoveWindSource(UFluidWindSourceComponent* In)
{
	for (const FFluidWindSourceData &SourceData: In->GetSourceData())
	{
		FUpdateSourceCommand RemoveCommand;
		RemoveCommand.HashId = SourceData.DataId;
		RemoveCommand.RefreshType = ERefreshType::Remove;
		RemoveCommand.Type = SourceData.Type;
	
		ENQUEUE_RENDER_COMMAND(UpdateParams)(
			[this,RemoveCommand](FRHICommandListImmediate& RHICmdList)
			{
				SourceCommandQueue.Add(RemoveCommand);
			});
	}
}

void FFluidWindCompute::UpdateWindSource(UFluidWindSourceComponent* In)
{
	for (const FFluidWindSourceData &SourceData: In->GetSourceData())
	{
		FUpdateSourceCommand UpdateCommand;
		UpdateCommand.RefreshType = ERefreshType::Update;
		UpdateWindSourceCommand(UpdateCommand, SourceData);
	}
}

void FFluidWindCompute::UpdateWindSourceCommand(FUpdateSourceCommand& Command, const FFluidWindSourceData& SourceData)
{
	Command.Type = SourceData.Type;
	Command.HashId = SourceData.DataId;
	Command.SourceShaderData.InfoType = StaticCast<int32>(SourceData.Type);
	Command.SourceShaderData.Shape = StaticCast<int32>(SourceData.Shape);
	Command.SourceShaderData.Position = SourceData.GetPosition();
	Command.SourceShaderData.RotationMatrix = SourceData.GetRotationMatrix();
	Command.SourceShaderData.Extents = SourceData.GetExtents();
	Command.SourceShaderData.Behavior = SourceData.GetBehavior();
	Command.SourceShaderData.AngleSpeed = SourceData.GetAngleSpeed();
	Command.SourceShaderData.Padding0 = 0;
	Command.SourceShaderData.Padding1 = 0;
	
	ENQUEUE_RENDER_COMMAND(UpdateParams)(
	[this,Command](FRHICommandListImmediate& RHICmdList)
	{
		SourceCommandQueue.Add(Command);
	});
}

void FFluidWindCompute::UpdateWindSourceLists(const FUpdateSourceCommand& Command)
{
	// Common handling for all wind source types.
	// For PhysicalBody type, we also need to update the separate body transform list
	if (Command.HashId == -1)
	{
		return;
	}
	const int32 SourceTypeIndex = StaticCast<int32>(Command.Type);
	if (Command.RefreshType == ERefreshType::Remove || Command.RefreshType == ERefreshType::Update)
	{
		if (const int Index = WindSourceHashIdLists[SourceTypeIndex].IndexOfByKey(Command.HashId); Index != -1)
		{
			if (Command.RefreshType == ERefreshType::Remove)
			{
				WindSourceLists[SourceTypeIndex].RemoveAtSwap(Index);
				WindSourceHashIdLists[SourceTypeIndex].RemoveAtSwap(Index);
			}
			else
			{
				WindSourceLists[SourceTypeIndex][Index] = Command.SourceShaderData;
			}
		}
	}
	else
	{
		WindSourceHashIdLists[SourceTypeIndex].Add(Command.HashId);
		WindSourceLists[SourceTypeIndex].Add(Command.SourceShaderData);
	}
}

void FFluidWindCompute::RenderWind(float DeltaTime, const FVector& PivotPosition, const FIntVector3& CellPosition, ERHIFeatureLevel::Type FeatureLevel)
{
	ENQUEUE_RENDER_COMMAND(RenderWind)(
	[=,this](FRHICommandListImmediate& RHICmdList)
	{
		RenderWind_InThread(RHICmdList, DeltaTime, PivotPosition, CellPosition, FeatureLevel);
	});
}

void FFluidWindCompute::RenderWind_InThread(FRHICommandListImmediate& RHICmdList, float DeltaTime, const FVector& PivotPosition, const FIntVector3& CellPosition, ERHIFeatureLevel::Type FeatureLevel)
{
	if (!bInitialized) 
	{
		return;
	}
	FRDGBuilder GraphBuilder(RHICmdList);
	TArray<FWindSourceShaderData> ShaderDataCollections;
	
	// 1. Update Runtime Params / Source Commands
	{
		if (UpdateParamsQueue.Num() > 0)
		{
			const auto& Param = UpdateParamsQueue.Last();
			RuntimeParams = Param;

			UpdateParamsQueue.Empty();
		}

		if (SourceCommandQueue.Num() > 0)
		{
			for (int32 i = 0; i < SourceCommandQueue.Num(); ++i)
			{
				const FUpdateSourceCommand& Param = SourceCommandQueue[i];
				UpdateWindSourceLists(Param);
			}
			// Collect all sources info 
			for (int i = 0; i < SourceTypeCount; ++i)
			{
				ShaderDataCollections.Append(WindSourceLists[i]);
			}
			SourceCommandQueue.Empty();
		}
	}

	// 2. Register Textures

	if (DivergenceTexRHIRef == nullptr || PressureTexRHIRef[0] == nullptr || PressureTexRHIRef[1] == nullptr)
	{
		return;
	}
	
	if (FluidWindVolume && FluidWindVolume->GetTextureRHI() == nullptr)
	{
		return;
	}
	
	FRDGTextureRef TransientVolumeTexRefs[3];
	FRDGTextureRef FrameVolumeTexRefs[3];
	for (int i = 0; i < 3; ++i)
	{
		if (TransientVolumeTexRHIRefs[i] == nullptr  || VolumeTexRHIRefs[i] == nullptr )
		{
			return;
		}
		
		TransientVolumeTexRefs[i] = RegisterExternalTexture(GraphBuilder, TransientVolumeTexRHIRefs[i], TransientTextureNames[i]);
		FrameVolumeTexRefs[i] = RegisterExternalTexture(GraphBuilder, VolumeTexRHIRefs[i], FrameTextureNames[i]);
	}

	const FRDGTextureRef DivergenceTexture = RegisterExternalTexture(GraphBuilder, DivergenceTexRHIRef, TEXT("RT.FluidWind.DivergenceVolume"));
	const FRDGTextureRef PressureTextures[2] = {
		RegisterExternalTexture(GraphBuilder, PressureTexRHIRef[0], TEXT("RT.FluidWind.PressureVolume0")),
		RegisterExternalTexture(GraphBuilder, PressureTexRHIRef[1], TEXT("RT.FluidWind.PressureVolume1")),
	};
	const FRDGTextureRef FluidWindVolumeTexture = RegisterExternalTexture(GraphBuilder, FluidWindVolume->GetTextureRHI(), TEXT("RT.FluidWind.VolumeVelocity"));

	// 3. Update offset position
	CurrentCellPosition = CellPosition;
	if (LastCellPosition.IsZero())
	{
		LastCellPosition = CurrentCellPosition;
	}
	const FIntVector3 OffsetPos = CurrentCellPosition - LastCellPosition;
	LastCellPosition = CurrentCellPosition;
	
	// Now, we can start the fluid wind update passes
	{
		RDG_EVENT_SCOPE(GraphBuilder, "FluidWind");
		//RDG_GPU_STAT_SCOPE(GraphBuilder, FluidWind);

		auto GroupCount = FComputeShaderUtils::GetGroupCount( VolumeSize, FFluidWindCompute::GroupThreadSize ); 
		// Pass 1.Move Fluid Wind Volume to Offset Position
		{
			RDG_EVENT_SCOPE(GraphBuilder, "FluidWind.Offset");
			//RDG_GPU_STAT_SCOPE(GraphBuilder, FluidWindOffset);
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(FrameVolumeTexRefs[0]), 0.0f);
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(FrameVolumeTexRefs[1]), 0.0f);
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(FrameVolumeTexRefs[2]), 0.0f);
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(PressureTextures[0]), 0.0f);
			AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(PressureTextures[1]), 0.0f);
			
			if (OffsetPos.IsZero())
			{
				for (int i = 0; i < 3; ++i)
				{
					AddCopyTexturePass(GraphBuilder, TransientVolumeTexRefs[i], FrameVolumeTexRefs[i]);
				}
			}
			else
			{
				// Note: TransientVolumeTexRefs now store the last frame data
				const FOffsetPositionCompute::FPermutationDomain Permutation;
				if (TShaderRef<FOffsetPositionCompute> ComputeShader = GetGlobalShaderMap(FeatureLevel)->GetShader<FOffsetPositionCompute>(Permutation);
					ComputeShader.IsValid())
				{
				
					FOffsetPositionCompute::FParameters* PassParameters = GraphBuilder.AllocParameters<FOffsetPositionCompute::FParameters>();
					PassParameters->OffsetPosition = OffsetPos;
					PassParameters->VolumeSizeMinusOne = VolumeSizeMinusOne;
					PassParameters->WindBufferInputX 		 = GraphBuilder.CreateSRV(TransientVolumeTexRefs[0]);
					PassParameters->WindBufferInputY 		 = GraphBuilder.CreateSRV(TransientVolumeTexRefs[1]);
					PassParameters->WindBufferInputZ 		 = GraphBuilder.CreateSRV(TransientVolumeTexRefs[2]);
					PassParameters->WindBufferOutputX 		 = GraphBuilder.CreateUAV(FrameVolumeTexRefs[0]);
					PassParameters->WindBufferOutputY 		 = GraphBuilder.CreateUAV(FrameVolumeTexRefs[1]);
					PassParameters->WindBufferOutputZ 		 = GraphBuilder.CreateUAV(FrameVolumeTexRefs[2]);
			
					GraphBuilder.AddPass(
						RDG_EVENT_NAME("FluidWind.OffsetPosition"),
						PassParameters,
						ERDGPassFlags::Compute,
						[PassParameters, ComputeShader, GroupCount](FRHICommandListImmediate& CmdList)
						{
							FComputeShaderUtils::Dispatch(CmdList, ComputeShader, *PassParameters, GroupCount);
						});
				
				}
			}
		}

		// Pass 2. Update all wind sources 
		{
			RDG_EVENT_SCOPE(GraphBuilder, "FluidWind.Source");
			//RDG_GPU_STAT_SCOPE(GraphBuilder, FluidWindSource);

			if (ShaderDataCollections.Num() > 0)
			{
				const FSourceSpeedCompute::FPermutationDomain PermutationBasedOnType;
				if (TShaderRef<FSourceSpeedCompute> ComputeShader = GetGlobalShaderMap(FeatureLevel)->GetShader<FSourceSpeedCompute>(PermutationBasedOnType);
					ComputeShader.IsValid())
				{
					// Build simple source buffer (at least 1 element to satisfy structured buffer requirements)
					const uint32 SourceBufferCount = FMath::RoundUpToPowerOfTwo(ShaderDataCollections.Num());

					const FRDGBufferRef SourceBuffer = CreateStructuredBuffer(
						GraphBuilder,
						TEXT("FluidWind.SourceBuffer"),
						ShaderDataCollections.GetTypeSize(),
						SourceBufferCount,
						ShaderDataCollections.GetData(),
						ShaderDataCollections.Num() * ShaderDataCollections.GetTypeSize());
					

					FSourceSpeedCompute::FParameters* PassParameters = GraphBuilder.AllocParameters<FSourceSpeedCompute::FParameters>();
					PassParameters->WorldSize = GFluidWindVolumeWorldSize;
					PassParameters->VolumePosPivot = FVector3f(PivotPosition);
					PassParameters->SourceBufferCount = ShaderDataCollections.Num();
					PassParameters->WindSourceBuffer = GraphBuilder.CreateSRV(SourceBuffer);
				
					PassParameters->WindBufferInputX = GraphBuilder.CreateSRV(FrameVolumeTexRefs[0]);
					PassParameters->WindBufferInputY = GraphBuilder.CreateSRV(FrameVolumeTexRefs[1]);
					PassParameters->WindBufferInputZ = GraphBuilder.CreateSRV(FrameVolumeTexRefs[2]);
					PassParameters->WindBufferOutputX = GraphBuilder.CreateUAV(TransientVolumeTexRefs[0]);
					PassParameters->WindBufferOutputY = GraphBuilder.CreateUAV(TransientVolumeTexRefs[1]);
					PassParameters->WindBufferOutputZ = GraphBuilder.CreateUAV(TransientVolumeTexRefs[2]);
					GraphBuilder.AddPass(
						RDG_EVENT_NAME("FluidWind.SourceUpdate"),
						PassParameters,
						ERDGPassFlags::Compute,
						[PassParameters, ComputeShader, GroupCount](FRHICommandListImmediate& CmdList)
						{
							FComputeShaderUtils::Dispatch(CmdList, ComputeShader, *PassParameters, GroupCount);
						});
				}
			}
			else
			{
				// No active sources: copy Frame → Transient so Diffusion reads correct data
				for (int i = 0; i < 3; ++i)
				{
					AddCopyTexturePass(GraphBuilder, FrameVolumeTexRefs[i], TransientVolumeTexRefs[i]);
				}
			}
		}
		
		// Pass 3. Here we use blur to instead of diffusion
		{
			RDG_EVENT_SCOPE(GraphBuilder, "FluidWind.Diffusion");
			//RDG_GPU_STAT_SCOPE(GraphBuilder, FluidWindDiffusion);

			const FDiffusionCompute::FPermutationDomain Permutation;
			if (TShaderRef<FDiffusionCompute> ComputeShader = GetGlobalShaderMap(FeatureLevel)->GetShader<FDiffusionCompute>(Permutation);
				ComputeShader.IsValid())
			{
				// The volume grid size more large, the diffusion force should be smaller to avoid instability
				// This is from Stam, Jos. "Real-time fluid dynamics for games." Proceedings of the game developer conference. Vol. 18. No. 11. 2003.
				const float VolumeScale = (GFluidWindVolumeWorldSize * GFluidWindVolumeWorldSize * GFluidWindVolumeWorldSize);
				// Ensure odd number of sub-steps so the final result lands in the correct buffer (Frame).
				// Diffusion ping-pong: k=0 reads Transient→writes Frame, k=1 reads Frame→writes Transient, ...
				// After an odd number of steps, the result is in Frame, which Advection expects to read from.
				const int32 BlurSubSteps = FMath::Max(1, GFluidWindBlurSubSteps | 1);
				for (int k = 0; k < BlurSubSteps; ++k)
				{
					// Here, we first read from FrameVolumeTexRefs and write to TransientVolumeTexRefs and then vice versa.
					// To make sure the following operations are correct, here 'k' must be an odd number.
					for (int i = 0; i < 3; ++i)
					{
						FDiffusionCompute::FParameters* PassParameters = GraphBuilder.AllocParameters<FDiffusionCompute::FParameters>();
						PassParameters->VolumeSizeMinusOne = VolumeSizeMinusOne;
						PassParameters->DiffusionForce = RuntimeParams.DiffusionForce * DeltaTime * VolumeScale;
						PassParameters->BlurBufferInput = GraphBuilder.CreateSRV(k % 2 == 0 ? TransientVolumeTexRefs[i]  : FrameVolumeTexRefs[i]);
						PassParameters->BlurBufferOutput = GraphBuilder.CreateUAV(k % 2 == 0 ? FrameVolumeTexRefs[i]: TransientVolumeTexRefs[i]);
			
						GraphBuilder.AddPass(
							RDG_EVENT_NAME("FluidWind.Blur%d SubSteps:%d", i, k),
							PassParameters,
							ERDGPassFlags::Compute,
							[PassParameters, ComputeShader, GroupCount](FRHICommandListImmediate& CmdList)
							{
								FComputeShaderUtils::Dispatch(CmdList, ComputeShader, *PassParameters, GroupCount);
							});
					}
				}
			}
		}
		
		// Pass 4. Advection
		{
			RDG_EVENT_SCOPE(GraphBuilder, "FluidWind.Advection");
			//RDG_GPU_STAT_SCOPE(GraphBuilder, FluidWindAdvection);

			// Semi-Lagrangian method for advection. The x,y,z channels are advected separately.
			const FAdvectionSemiLagrangianCompute::FPermutationDomain Permutation;
			if (TShaderRef<FAdvectionSemiLagrangianCompute> ComputeShader = GetGlobalShaderMap(FeatureLevel)->GetShader<FAdvectionSemiLagrangianCompute>(Permutation);
				ComputeShader.IsValid())
			{
				for (int i = 0; i < 3; ++i)
				{
					FAdvectionSemiLagrangianCompute::FParameters* PassParameters = GraphBuilder.AllocParameters<FAdvectionSemiLagrangianCompute::FParameters>();
					PassParameters->DeltaTime = DeltaTime;
					PassParameters->WorldSize = GFluidWindVolumeWorldSize;
					PassParameters->VolumeSizeMinusOne = VolumeSizeMinusOne;
					PassParameters->AdvectionForce = RuntimeParams.AdvectionForce;
					
					PassParameters->WindBufferInputX = GraphBuilder.CreateSRV(FrameVolumeTexRefs[0]);
					PassParameters->WindBufferInputY = GraphBuilder.CreateSRV(FrameVolumeTexRefs[1]);
					PassParameters->WindBufferInputZ = GraphBuilder.CreateSRV(FrameVolumeTexRefs[2]);
					PassParameters->WindBufferTarget = GraphBuilder.CreateSRV(FrameVolumeTexRefs[i]);
					
					PassParameters->WindBufferOutput = GraphBuilder.CreateUAV(TransientVolumeTexRefs[i]);
			
					GraphBuilder.AddPass(
						RDG_EVENT_NAME("FluidWind.Advection%c", 'X'+i),
						PassParameters,
						ERDGPassFlags::Compute,
						[PassParameters, ComputeShader, GroupCount](FRHICommandListImmediate& CmdList)
						{
							FComputeShaderUtils::Dispatch(CmdList, ComputeShader, *PassParameters, GroupCount);
						});
				}
			}
		}
		
		// Pass 5. Divergence & Pressure
		{
			RDG_EVENT_SCOPE(GraphBuilder, "FluidWind.Projection");
			//RDG_GPU_STAT_SCOPE(GraphBuilder, FluidWindProjection);

			// First compute divergence
			const FDivergenceCompute::FPermutationDomain Permutation;
			if (TShaderRef<FDivergenceCompute> ComputeShader = GetGlobalShaderMap(FeatureLevel)->GetShader<FDivergenceCompute>(Permutation);
				ComputeShader.IsValid())
			{
				FDivergenceCompute::FParameters* PassParameters = GraphBuilder.AllocParameters<FDivergenceCompute::FParameters>();
				PassParameters->WorldSize = GFluidWindVolumeWorldSize;
				PassParameters->VolumeSizeMinusOne = VolumeSizeMinusOne;
				PassParameters->VelocityInputX = GraphBuilder.CreateSRV(TransientVolumeTexRefs[0]);
				PassParameters->VelocityInputY = GraphBuilder.CreateSRV(TransientVolumeTexRefs[1]);
				PassParameters->VelocityInputZ = GraphBuilder.CreateSRV(TransientVolumeTexRefs[2]);
				PassParameters->DivergenceOutput = GraphBuilder.CreateUAV(DivergenceTexture);
		
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("FluidWind.Divergence"),
					PassParameters,
					ERDGPassFlags::Compute,
					[PassParameters, ComputeShader, GroupCount](FRHICommandListImmediate& CmdList)
					{
						FComputeShaderUtils::Dispatch(CmdList, ComputeShader, *PassParameters, GroupCount);
					});
				
			}
			
			const FPressureCompute::FPermutationDomain PressurePermutation;
			constexpr int32 PressureIterations = 6;
			// After ping-pong, the final result lands in PressureTextures[PressureIterations % 2]
			int32 FinalPressureIdx = PressureIterations % 2;
			if (TShaderRef<FPressureCompute> ComputeShader = GetGlobalShaderMap(FeatureLevel)->GetShader<FPressureCompute>(PressurePermutation);
				ComputeShader.IsValid())
			{
				for (int i = 0; i < PressureIterations; ++i)
				{
					const int32 SrcIdx = i % 2;
					const int32 DstIdx = 1 - SrcIdx;
					
					FPressureCompute::FParameters* PassParameters = GraphBuilder.AllocParameters<FPressureCompute::FParameters>();
					PassParameters->IterationIndex = i;
					PassParameters->Relaxation =0.75;
					PassParameters->DeltaTime = DeltaTime;
					PassParameters->WorldSize = GFluidWindVolumeWorldSize;
					PassParameters->VolumeSizeMinusOne = VolumeSizeMinusOne;
					PassParameters->PressureInput = GraphBuilder.CreateSRV(PressureTextures[SrcIdx]);
					PassParameters->DivergenceInput = GraphBuilder.CreateSRV(DivergenceTexture);
					PassParameters->PressureOutput = GraphBuilder.CreateUAV(PressureTextures[DstIdx]);
		
					GraphBuilder.AddPass(
						RDG_EVENT_NAME("FluidWind.Pressure%d", i),
						PassParameters,
						ERDGPassFlags::Compute,
						[PassParameters, ComputeShader, GroupCount](FRHICommandListImmediate& CmdList)
						{
							FComputeShaderUtils::Dispatch(CmdList, ComputeShader, *PassParameters, GroupCount);
						});
				}
			}
		
			const FVelocityRectificationCompute::FPermutationDomain PermutationRectify;
			if (TShaderRef<FVelocityRectificationCompute> ComputeShader = GetGlobalShaderMap(FeatureLevel)->GetShader<FVelocityRectificationCompute>(PermutationRectify);
				ComputeShader.IsValid())
			{
				FVelocityRectificationCompute::FParameters* PassParameters = GraphBuilder.AllocParameters<FVelocityRectificationCompute::FParameters>();
				PassParameters->WorldSize = GFluidWindVolumeWorldSize;
				PassParameters->VolumeSizeMinusOne = VolumeSizeMinusOne;
				PassParameters->VelocityInputX = GraphBuilder.CreateSRV(TransientVolumeTexRefs[0]);
				PassParameters->VelocityInputY = GraphBuilder.CreateSRV(TransientVolumeTexRefs[1]);
				PassParameters->VelocityInputZ = GraphBuilder.CreateSRV(TransientVolumeTexRefs[2]);
				PassParameters->PressureInput = GraphBuilder.CreateSRV(PressureTextures[FinalPressureIdx]);
				PassParameters->VelocityOutputX = GraphBuilder.CreateUAV(FrameVolumeTexRefs[0]);
				PassParameters->VelocityOutputY = GraphBuilder.CreateUAV(FrameVolumeTexRefs[1]);
				PassParameters->VelocityOutputZ = GraphBuilder.CreateUAV(FrameVolumeTexRefs[2]);
		
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("FluidWind.RectifyVelocity"),
					PassParameters,
					ERDGPassFlags::Compute,
					[PassParameters, ComputeShader, GroupCount](FRHICommandListImmediate& CmdList)
					{
						FComputeShaderUtils::Dispatch(CmdList, ComputeShader, *PassParameters, GroupCount);
					});
				
			}
		}
		
		// Pass 6. Merge all results
		{
			RDG_EVENT_SCOPE(GraphBuilder, "FluidWind.Merge");
			//RDG_GPU_STAT_SCOPE(GraphBuilder, FluidWindMerge);

			const FMergeChannelCompute::FPermutationDomain Permutation;
			if (TShaderRef<FMergeChannelCompute> ComputeShader = GetGlobalShaderMap(FeatureLevel)->GetShader<FMergeChannelCompute>(Permutation);
				ComputeShader.IsValid())
			{
				FMergeChannelCompute::FParameters* PassParameters = GraphBuilder.AllocParameters<FMergeChannelCompute::FParameters>();
				PassParameters->WorldSize = GFluidWindVolumeWorldSize;
				PassParameters->WindBufferInputX = GraphBuilder.CreateSRV(FrameVolumeTexRefs[0]);
				PassParameters->WindBufferInputY = GraphBuilder.CreateSRV(FrameVolumeTexRefs[1]);
				PassParameters->WindBufferInputZ = GraphBuilder.CreateSRV(FrameVolumeTexRefs[2]);
				PassParameters->WindBufferOutput = GraphBuilder.CreateUAV(FluidWindVolumeTexture);
		
				GraphBuilder.AddPass(
					RDG_EVENT_NAME("MergeChannel"),
					PassParameters,
					ERDGPassFlags::Compute,
					[PassParameters, ComputeShader, GroupCount](FRHICommandListImmediate& CmdList)
					{
						FComputeShaderUtils::Dispatch(CmdList, ComputeShader, *PassParameters, GroupCount);
					});

				
				AddCopyTexturePass(GraphBuilder, FrameVolumeTexRefs[0], TransientVolumeTexRefs[0]);
				AddCopyTexturePass(GraphBuilder, FrameVolumeTexRefs[1], TransientVolumeTexRefs[1]);
				AddCopyTexturePass(GraphBuilder, FrameVolumeTexRefs[2], TransientVolumeTexRefs[2]);
			}
		}
	} // end of RDG_EVENT_SCOPE / RDG_GPU_STAT_SCOPE block
	
	GraphBuilder.Execute();
}

void FFluidWindCompute::ClearWind()
{
	ENQUEUE_RENDER_COMMAND(ClearWind)(
	[this](FRHICommandListImmediate& RHICmdList)
	{
		UpdateParamsQueue.Empty();
		SourceCommandQueue.Empty();
		UpdateParamsQueue.Empty();

		FluidWindVolume = nullptr;
		PressureTexRHIRef[0].SafeRelease();
		PressureTexRHIRef[1].SafeRelease();
		DivergenceTexRHIRef.SafeRelease();
		for (int i = 0;i < 3; ++i)
		{
			VolumeTexRHIRefs[i].SafeRelease();
			TransientVolumeTexRHIRefs[i].SafeRelease();
		}

		for (int i = 0; i < SourceTypeCount; ++i)
		{
			WindSourceLists[i].Empty();
			WindSourceHashIdLists[i].Empty();
		}
	});
}
