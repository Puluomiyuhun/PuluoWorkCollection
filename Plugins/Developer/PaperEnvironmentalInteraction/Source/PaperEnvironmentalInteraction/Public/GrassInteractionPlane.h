#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Materials/MaterialParameterCollection.h"
#include "GrassInteractionPlane.generated.h"

UCLASS()
class PAPERENVIRONMENTALINTERACTION_API AGrassInteractionPlane : public AActor
{
    GENERATED_BODY()
    
public:
    AGrassInteractionPlane();
	
	// 组件
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* PlaneMesh;
    
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	USceneComponent* Root;

	// 渲染目标
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass Interaction")
	UTextureRenderTarget2D* VelocityRT_RG;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass Interaction")
	UTextureRenderTarget2D* VelocityRT_BA;

	// 材质参数集
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass Interaction")
	UMaterialParameterCollection* GrassMPC;

	// 更新材质
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass Interaction")
	UMaterialInterface* VelocityUpdateMaterial_RG;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass Interaction")
	UMaterialInterface* VelocityUpdateMaterial_BA;

	// 配置参数
    
	// RT分辨率
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass Interaction")
	int32 RT_Resolution = 128;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass Interaction")
	float InteractionStrength = 50.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass Interaction")
	float VelocityDecay = 0.92f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass Interaction")
	float Stiffness = 0.15f;

	// 跟随的目标角色
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass Interaction")
	ACharacter* TargetCharacter;

	// 跟随的目标角色
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grass Interaction")
	bool bUseBA;

protected:
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaTime) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
    // 初始化函数
    void InitializeRenderTargets();
    void InitializeMaterialInstance();
    void InitializeMPCParameters();
    
    // 更新函数
    void UpdatePlanePosition();
    void UpdatePhysics(float DeltaTime);
    void UpdateMPCParameters();
    
    // 工具函数
    FVector2D CalculatePixelOffset(const FVector& WorldMovement) const;
    FVector2D CalculateVelocityInput(const FVector& CharacterVelocity) const;
    
    // 清空RT
	void ClearRenderTarget(UTextureRenderTarget2D* RT, const FLinearColor& ClearColor);

	
	// 内部状态
	UPROPERTY(Transient)
	UMaterialInstanceDynamic* VelocityUpdateMID_RG;
	UPROPERTY(Transient)
	UMaterialInstanceDynamic* VelocityUpdateMID_BA;
    
	UPROPERTY(Transient)
	UTextureRenderTarget2D* TempVelocityRT_RG;
	
	UPROPERTY(Transient)
	UTextureRenderTarget2D* TempVelocityRT_BA;

	FVector LastCharacterPosition;
	FVector2D LastVelocityInput;
	FVector2D AccumulatedPixelOffset;  // 累积的亚像素偏移
	float PlaneWorldSize = 400.0f;
};