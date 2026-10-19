#pragma once
#include "CoreMinimal.h"
#include "FluidWindDataTypes.generated.h"

//: Ideal wind source is a volume-free point, we use a radius to define the effective range. 
UENUM()
enum class EFluidWindSourceType : uint8
{
	Directional = 0,
	OmniDirectional = 1,
	Vortex = 2 ,
	Moving = 3
};

UENUM()
enum class EFluidWindSourceShape :uint8
{
	Box = 0,
	Sphere = 1,
	Capsule = 2,
};

/** GPU-side per-body transform data for PhysicalBody wind sources. */
struct FPhysicalBodyTransform
{
	FVector3f CurrentPosition;
	float Padding0;
	FVector3f PreviousPosition;
	float Padding1;
};

USTRUCT()
struct FFluidWindSourceData
{
	GENERATED_BODY()

	int32 DataId = -1;

	// -- Wind Source type --
	UPROPERTY(EditAnywhere)
	EFluidWindSourceType Type = EFluidWindSourceType::Directional;

	UPROPERTY(EditAnywhere, meta=(EditCondition="!bUseSkeletalMesh", EditConditionHides))
	EFluidWindSourceShape Shape = EFluidWindSourceShape::Sphere;

	// -- Wind Source Shape parameters --
	UPROPERTY(EditAnywhere, meta = (EditCondition = "Shape == EFluidWindSourceShape::Box", EditConditionHides))
	FVector3f Extent = FVector3f::ZeroVector;

	UPROPERTY(EditAnywhere, meta = (EditCondition = "Shape == EFluidWindSourceShape::Sphere || Shape == EFluidWindSourceShape::Capsule", EditConditionHides))
	float Radius = 0.f;

	UPROPERTY(EditAnywhere, meta = (EditCondition = "Shape == EFluidWindSourceShape::Capsule", EditConditionHides))
	float Length = 0.f;

	// -- Wind Source behavior parameters --
	UPROPERTY(EditAnywhere, meta = (EditCondition = "Type != EFluidWindSourceType::Directional", EditConditionHides))
	float Force = 0.f;
	
	UPROPERTY(EditAnywhere, meta = (EditCondition = "Type == EFluidWindSourceType::Directional", EditConditionHides))
	FVector3f V3Force = FVector3f::ZeroVector;
	
	UPROPERTY(EditAnywhere,meta = (EditCondition = "Type == EFluidWindSourceType::Vortex", EditConditionHides))
	FVector3f Axis = FVector3f::ZeroVector;

	UPROPERTY(EditAnywhere, meta = (EditCondition = "Type == EFluidWindSourceType::Moving ", EditConditionHides))
	float MaxMovingSpeed = 0.f;

	// -- PhysicalBody specific parameters, only used when bUseSkeletalMesh is true --
	UPROPERTY(EditAnywhere, Category = WindSource)
	bool bUseSkeletalMesh = false;

	UPROPERTY(EditAnywhere, Category = WindSource, meta=(EditCondition="bUseSkeletalMesh", EditConditionHides))
	FName SkeletalMeshComponentTag;

	// -- Runtime only data, not exposed to editor --
	float DeltaTime = 0.016f;				// Init to 60fps, avoid to divide by zero in shader.
	FVector Position = FVector::ZeroVector;
	FVector LastPosition = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	FRotator LastRotation = FRotator::ZeroRotator;

	// -- Runtime only data, only used for PhysicalBody type, not exposed to editor --
	FName BoneName = NAME_None;
	FVector LocalCenter = FVector::ZeroVector;
	FRotator LocalRotation = FRotator::ZeroRotator;

	FVector4f GetPosition() const
	{
		// Moving wind now uses the current position as the sweep origin.
		// The sweep path [Position, Position + Behavior.xyz] = [Position, LastPosition]
		// covers the same trajectory, but the shape is centered at the current position,
		// ensuring cells in front of the movement direction are properly affected.
		return FVector4f(FVector3f(Position), 0.f);
	}

	FMatrix44f GetRotationMatrix() const
	{
		// ~ in shader using column-major matrix, so the rotation matrix is effectively transposed to be row-major.
		return FMatrix44f(FRotationMatrix(Rotation));
	}

	FVector4f GetExtents() const
	{
		switch (Shape)
		{
		case EFluidWindSourceShape::Box:
			return FVector4f(Extent.X, Extent.Y, Extent.Z, 0.f);
		case EFluidWindSourceShape::Sphere:
			return FVector4f(Radius, Radius * Radius, 0.f, 0.f);
		case EFluidWindSourceShape::Capsule:
			return FVector4f(Radius, Length, Radius * Radius, 0.f);
		default:
			return FVector4f::Zero();
		}
	}

	FVector4f GetBehavior() const
	{
		switch (Type)
		{
		case EFluidWindSourceType::Directional:
			return FVector4f(V3Force, 0.f);
		case EFluidWindSourceType::OmniDirectional:
			return FVector4f(Force, 0.f, 0.f, 0.f);
		case EFluidWindSourceType::Vortex:
			return FVector4f(Axis, Force);
		case EFluidWindSourceType::Moving:
			{
				// Behavior.xyz = backward sweep vector (from current position to last position)
				// The sweep line in shader: [Position, Position + Behavior.xyz] = [Position, LastPosition]
				// The actual forward movement direction is -Behavior.xyz, computed in shader.
				const FVector SweepVector = LastPosition - Position;
				const float CurrentSpeed = SweepVector.Size() / DeltaTime;
				const float ScaledForce = Force * FMath::Clamp(CurrentSpeed / MaxMovingSpeed, 0.f, 1.f);
				return FVector4f(FVector3f(SweepVector), ScaledForce);
			}
		default:
			return FVector4f::Zero();
		}
	}

	FVector4f GetAngleSpeed() const
	{
		if (Type == EFluidWindSourceType::Moving)
		{
			const FRotator DeltaRot = Rotation - LastRotation;
			const FVector AngleSpeed = FVector(DeltaRot.Roll, DeltaRot.Pitch, DeltaRot.Yaw) / DeltaTime;
			return FVector4f(FVector3f(AngleSpeed), DeltaTime);
		}
		return FVector4f::Zero();
	}
};