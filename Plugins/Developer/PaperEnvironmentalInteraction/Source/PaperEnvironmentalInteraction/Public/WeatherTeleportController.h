#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WeatherTeleportController.generated.h"

class ACharacter;

UCLASS()
class PAPERENVIRONMENTALINTERACTION_API AWeatherTeleportController : public AActor
{
	GENERATED_BODY()

public:
	AWeatherTeleportController();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EnableLocalInput();

	void TeleportTargetToTag(FName TagName);

	void Weather_O();
	void Weather_P();
	void Weather_I();
	void Weather_U();

	void Teleport_0();
	void Teleport_1();
	void Teleport_2();
	void Teleport_3();
	void Teleport_4();
	void Teleport_5();
	void Teleport_6();
	void Teleport_7();
	void Teleport_8();
	void Teleport_9();
	void Teleport_Z();
	void Teleport_X();
	void Teleport_C();
	void Teleport_V();
	void Teleport_B();
	void Teleport_N();
	void Teleport_M();

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "References")
	ACharacter* TargetCharacter = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "References")
	AActor* UDSActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "References")
	AActor* LookdevActor = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "References")
	double TimeRunSpeed = 1.0f;

private:
	double TimeOfDay = 960.0f;
	bool bTimeRun = false;
};