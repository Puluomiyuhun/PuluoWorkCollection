#include "GrassInteractionPlane.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Engine.h"
#include "GameFramework/Character.h"

AGrassInteractionPlane::AGrassInteractionPlane()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PostUpdateWork;

    Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
    RootComponent = Root;

    PlaneMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PlaneMesh"));
    PlaneMesh->SetupAttachment(RootComponent);
    
    PlaneMesh->SetVisibility(false);
    PlaneMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AGrassInteractionPlane::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    ClearRenderTarget(VelocityRT_RG, FLinearColor(0, 0, 0, 0));
    if (bUseBA) ClearRenderTarget(VelocityRT_BA, FLinearColor(0, 0, 0, 0));
}


void AGrassInteractionPlane::BeginPlay()
{
    Super::BeginPlay();
    PlaneWorldSize = PlaneMesh->GetRelativeScale3D().X * 100;

    InitializeRenderTargets();
    InitializeMaterialInstance();
    InitializeMPCParameters();

    if (TargetCharacter)
    {
        LastCharacterPosition = TargetCharacter->GetActorLocation();
    }
    else
    {
        /*TargetCharacter = GetOwner();
        LastCharacterPosition = GetActorLocation();*/
    	if (UGameplayStatics::GetPlayerCharacter(GetWorld(),0))
    	{
    		TargetCharacter = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
			LastCharacterPosition = TargetCharacter->GetActorLocation();
    	}
    }
    
    // 初始化累积偏移
    AccumulatedPixelOffset = FVector2D::ZeroVector;
}

void AGrassInteractionPlane::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

	if (!TargetCharacter)
	{
		if (UGameplayStatics::GetPlayerCharacter(GetWorld(), 0))
		{
			TargetCharacter = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
			LastCharacterPosition = TargetCharacter->GetActorLocation();
		}
		else return;
	}

    if (!VelocityRT_RG || !VelocityUpdateMID_RG)
        return;

    UpdatePlanePosition();
    UpdatePhysics(DeltaTime);
    UpdateMPCParameters();
}

void AGrassInteractionPlane::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    //if (TempVelocityRT)
    //{
        //TempVelocityRT->ConditionalBeginDestroy();
        //TempVelocityRT = nullptr;
    //}
    
    Super::EndPlay(EndPlayReason);
}

void AGrassInteractionPlane::InitializeRenderTargets()
{
    // 创建临时渲染目标（使用浮点格式支持负数）
    TempVelocityRT_RG = UKismetRenderingLibrary::CreateRenderTarget2D(
        GetWorld(), RT_Resolution, RT_Resolution, RTF_RGBA32f);

	if (bUseBA)
	{
		TempVelocityRT_BA = UKismetRenderingLibrary::CreateRenderTarget2D(
			GetWorld(), RT_Resolution, RT_Resolution, RTF_RGBA32f);
	}

    // 清空初始RT
    ClearRenderTarget(VelocityRT_RG, FLinearColor(0, 0, 0, 0));
    ClearRenderTarget(TempVelocityRT_RG, FLinearColor(0, 0, 0, 0));

	if (bUseBA)
	{
		ClearRenderTarget(VelocityRT_BA, FLinearColor(0, 0, 0, 0));
		ClearRenderTarget(TempVelocityRT_BA, FLinearColor(0, 0, 0, 0));
	}
}

void AGrassInteractionPlane::InitializeMaterialInstance()
{
    if (VelocityUpdateMaterial_RG)
    	VelocityUpdateMID_RG = UMaterialInstanceDynamic::Create(VelocityUpdateMaterial_RG, this);
	if (VelocityUpdateMaterial_BA && bUseBA)
		VelocityUpdateMID_BA = UMaterialInstanceDynamic::Create(VelocityUpdateMaterial_BA, this);
}

void AGrassInteractionPlane::InitializeMPCParameters()
{
    if (!GrassMPC) return;

    UKismetMaterialLibrary::SetVectorParameterValue(
        GetWorld(), GrassMPC, "CharacterPosition", FLinearColor(0, 0, 0, 0));
    
    UKismetMaterialLibrary::SetScalarParameterValue(
        GetWorld(), GrassMPC, "PlaneWorldSize", PlaneWorldSize);
    
    UKismetMaterialLibrary::SetScalarParameterValue(
        GetWorld(), GrassMPC, "RTResolution", RT_Resolution);
}

void AGrassInteractionPlane::UpdatePlanePosition()
{
    FVector CharacterLocation = TargetCharacter->GetActorLocation();
    FVector NewLocation = CharacterLocation;
    NewLocation.Z = GetActorLocation().Z;
    SetActorLocation(NewLocation);
    SetActorRotation(FRotator(0, 0, 0));
}
//PRAGMA_DISABLE_OPTIMIZATION
void AGrassInteractionPlane::UpdatePhysics(float DeltaTime)
{
    FVector CurrentCharacterPosition = TargetCharacter->GetActorLocation();
	FVector WorldMovement = CurrentCharacterPosition - LastCharacterPosition;
    FVector CharacterVelocity = TargetCharacter->GetVelocity();

	/*if (CharacterVelocity == FVector(0,0,0))
	{
		CharacterVelocity = WorldMovement / DeltaTime;
	}*/
    
    // 计算移动偏移
    FVector2D ThisFramePixelOffset = CalculatePixelOffset(WorldMovement);
    
    // 累积亚像素偏移
    AccumulatedPixelOffset += ThisFramePixelOffset;
    
    // 检查是否达到整像素偏移
    FVector2D IntegerOffset(0, 0);
    if (FMath::Abs(AccumulatedPixelOffset.X) >= 1.0f || 
        FMath::Abs(AccumulatedPixelOffset.Y) >= 1.0f)
    {
        // 取整像素部分
        IntegerOffset.X = FMath::RoundToFloat(AccumulatedPixelOffset.X);
        IntegerOffset.Y = FMath::RoundToFloat(AccumulatedPixelOffset.Y);
        
        // 保留亚像素部分
        AccumulatedPixelOffset.X -= IntegerOffset.X;
        AccumulatedPixelOffset.Y -= IntegerOffset.Y;
    }
    
    // 计算速度输入
    FVector2D VelocityInput = CalculateVelocityInput(CharacterVelocity);
    LastVelocityInput = VelocityInput;

    // 设置材质参数
    VelocityUpdateMID_RG->SetTextureParameterValue("PreviousVelocityRT_RG", VelocityRT_RG);
    if (bUseBA)VelocityUpdateMID_RG->SetTextureParameterValue("PreviousVelocityRT_BA", VelocityRT_BA);
    VelocityUpdateMID_RG->SetVectorParameterValue("PixelOffset", FLinearColor(IntegerOffset.X, IntegerOffset.Y, 0, 0));
    VelocityUpdateMID_RG->SetVectorParameterValue("VelocityInput", FLinearColor(VelocityInput.X, VelocityInput.Y, 0, 0));
    VelocityUpdateMID_RG->SetScalarParameterValue("DeltaTime", DeltaTime);
    VelocityUpdateMID_RG->SetScalarParameterValue("VelocityDecay", VelocityDecay);
    VelocityUpdateMID_RG->SetScalarParameterValue("Stiffness", Stiffness);
    VelocityUpdateMID_RG->SetScalarParameterValue("InteractionStrength", InteractionStrength);
    VelocityUpdateMID_RG->SetScalarParameterValue("RT_Resolution", RT_Resolution);
    UKismetRenderingLibrary::DrawMaterialToRenderTarget(GetWorld(), TempVelocityRT_RG, VelocityUpdateMID_RG);

	if (bUseBA)
	{
		// 设置材质参数
		VelocityUpdateMID_BA->SetTextureParameterValue("PreviousVelocityRT_RG", VelocityRT_RG);
		VelocityUpdateMID_BA->SetTextureParameterValue("PreviousVelocityRT_BA", VelocityRT_BA);
		VelocityUpdateMID_BA->SetVectorParameterValue("PixelOffset", FLinearColor(IntegerOffset.X, IntegerOffset.Y, 0, 0));
		VelocityUpdateMID_BA->SetVectorParameterValue("VelocityInput", FLinearColor(VelocityInput.X, VelocityInput.Y, 0, 0));
		VelocityUpdateMID_BA->SetScalarParameterValue("DeltaTime", DeltaTime);
		VelocityUpdateMID_BA->SetScalarParameterValue("VelocityDecay", VelocityDecay);
		VelocityUpdateMID_BA->SetScalarParameterValue("Stiffness", Stiffness);
		VelocityUpdateMID_BA->SetScalarParameterValue("InteractionStrength", InteractionStrength);
		VelocityUpdateMID_BA->SetScalarParameterValue("RT_Resolution", RT_Resolution);
		UKismetRenderingLibrary::DrawMaterialToRenderTarget(GetWorld(), TempVelocityRT_BA, VelocityUpdateMID_BA);
	}
	
	Swap(VelocityRT_RG, TempVelocityRT_RG);
	if (bUseBA)
		Swap(VelocityRT_BA, TempVelocityRT_BA);
		

    LastCharacterPosition = CurrentCharacterPosition;
}

void AGrassInteractionPlane::UpdateMPCParameters()
{
    if (!GrassMPC) return;

    FVector CharacterLocation = TargetCharacter->GetActorLocation();
    UKismetMaterialLibrary::SetVectorParameterValue(
        GetWorld(), GrassMPC, "CharacterPosition", 
        FLinearColor(CharacterLocation.X, CharacterLocation.Y, CharacterLocation.Z, 0));
    
    UKismetMaterialLibrary::SetScalarParameterValue(
        GetWorld(), GrassMPC, "PlaneWorldSize", PlaneWorldSize);
    
    UKismetMaterialLibrary::SetScalarParameterValue(
        GetWorld(), GrassMPC, "RTResolution", RT_Resolution);
}

FVector2D AGrassInteractionPlane::CalculatePixelOffset(const FVector& WorldMovement) const
{
    float PixelsPerCm = RT_Resolution / PlaneWorldSize;
    return FVector2D(WorldMovement.X * PixelsPerCm, WorldMovement.Y * PixelsPerCm);
}

FVector2D AGrassInteractionPlane::CalculateVelocityInput(const FVector& CharacterVelocity) const
{
    float SpeedScale = 0.1f;
    return FVector2D(CharacterVelocity.X * SpeedScale, CharacterVelocity.Y * SpeedScale);
}

void AGrassInteractionPlane::ClearRenderTarget(UTextureRenderTarget2D* RT, const FLinearColor& ClearColor)
{
    if (RT)
    {
        UKismetRenderingLibrary::ClearRenderTarget2D(GetWorld(), RT, ClearColor);
    }
}
//PRAGMA_ENABLE_OPTIMIZATION