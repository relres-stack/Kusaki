// Fill out your copyright notice in the Description page of Project Settings.

#include "DrunkNPC.h"
#include "DrunkNPCSpawner.h"
#include "BaseCharacter.h"
#include "BaseMonster.h"
#include "MoonshineComponent.h"
#include "AIController.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Engine.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TimerManager.h"

ADrunkNPC::ADrunkNPC()
{
	PrimaryActorTick.bCanEverTick = true;

	NPCMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("NPCMesh"));
	RootComponent = NPCMesh;
}

void ADrunkNPC::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (State != EDrunkNPCState::Luring)
	{
		return;
	}

	TArray<AActor*> NearbyActors;
	UKismetSystemLibrary::SphereOverlapActors(this, GetActorLocation(), EatProximityRadius,
		TArray<TEnumAsByte<EObjectTypeQuery>>{ UEngineTypes::ConvertToObjectType(ECC_Pawn) },
		ABaseMonster::StaticClass(), TArray<AActor*>{}, NearbyActors);

	const bool bWasEmptyBeforeThisTick = MonstersCurrentlyEating.Num() == 0;

	for (AActor* NearbyActor : NearbyActors)
	{
		ABaseMonster* Monster = Cast<ABaseMonster>(NearbyActor);
		if (!IsValid(Monster) || Monster->IsPendingKillPending() || Monster->Health <= 0.0f)
		{
			continue;
		}

		if (MonstersCurrentlyEating.Contains(Monster))
		{
			continue;
		}

		if (UCharacterMovementComponent* MonsterMove = Monster->GetCharacterMovement())
		{
			MonsterMove->DisableMovement();
		}
		MonstersCurrentlyEating.Add(Monster);

		UE_LOG(LogTemp, Warning, TEXT("%s: %s started eating the decoy"), *GetName(), *Monster->GetName());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(340, 3.0f, FColor::Green,
				FString::Printf(TEXT("Decoy: %s is eating"), *Monster->GetName()));
		}
	}

	// The clock only starts once the FIRST monster actually arrives — not from the throw itself, so a
	// slow approach doesn't eat into the decoy's own "health".
	if (bWasEmptyBeforeThisTick && MonstersCurrentlyEating.Num() > 0)
	{
		GetWorldTimerManager().SetTimer(SurviveTimerHandle, this, &ADrunkNPC::OnSurviveTimeExpired, ResolvedSurviveDuration, false);

		UE_LOG(LogTemp, Warning, TEXT("%s: first monster arrived — survive timer started (%.1fs)"), *GetName(), ResolvedSurviveDuration);
	}
}

bool ADrunkNPC::TryGetDrunk(UMoonshineComponent* FromComponent)
{
	if (State != EDrunkNPCState::Idle)
	{
		return false;
	}

	float AmountConsumed = 0.0f;
	if (!IsValid(FromComponent) || !FromComponent->TryConsumeForNPC(MinSipAmount, MaxSipAmount, AmountConsumed))
	{
		return false;
	}

	ResolvedLureRadius = BaseLureRadius + AmountConsumed * LureRadiusPerUnitConsumed;
	ResolvedSurviveDuration = BaseSurviveDuration + AmountConsumed * SurviveDurationPerUnitConsumed;
	State = EDrunkNPCState::Carried;
	SetActorEnableCollision(false);

	if (ABaseCharacter* Carrier = Cast<ABaseCharacter>(FromComponent->GetOwner()))
	{
		if (USkeletalMeshComponent* CarrierMesh = Carrier->GetMesh())
		{
			AttachToComponent(CarrierMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, CarrySocketName);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("%s: NPC drank %.1f units — LureRadius=%.1f, SurviveDuration=%.1f"),
		*GetName(), AmountConsumed, ResolvedLureRadius, ResolvedSurviveDuration);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1008, 3.0f, FColor::Cyan,
			FString::Printf(TEXT("NPC drank %.1f units — LureRadius=%.1f, SurviveDuration=%.1f"),
				AmountConsumed, ResolvedLureRadius, ResolvedSurviveDuration));
	}
	return true;
}

void ADrunkNPC::ThrowTo(const FVector& TargetPoint)
{
	if (State != EDrunkNPCState::Carried)
	{
		return;
	}

	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	SetActorEnableCollision(true);
	SetActorLocation(TargetPoint);
	State = EDrunkNPCState::Luring;
	StartLuring();
}

void ADrunkNPC::StartLuring()
{
	TArray<AActor*> NearbyActors;
	UKismetSystemLibrary::SphereOverlapActors(this, GetActorLocation(), ResolvedLureRadius,
		TArray<TEnumAsByte<EObjectTypeQuery>>{ UEngineTypes::ConvertToObjectType(ECC_Pawn) },
		ABaseMonster::StaticClass(), TArray<AActor*>{}, NearbyActors);

	int32 CalledCount = 0;
	for (AActor* NearbyActor : NearbyActors)
	{
		ABaseMonster* Monster = Cast<ABaseMonster>(NearbyActor);
		if (!IsValid(Monster) || Monster->IsPendingKillPending() || Monster->Health <= 0.0f)
		{
			continue;
		}

		if (AAIController* AICon = Cast<AAIController>(Monster->GetController()))
		{
			Monster->RequestMoveToLocation(AICon, GetActorLocation());
			++CalledCount;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("%s: landed — called %d monster(s) within %.1f units"),
		*GetName(), CalledCount, ResolvedLureRadius);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1009, 3.0f, FColor::Yellow,
			FString::Printf(TEXT("Decoy landed — %d monster(s) called"), CalledCount));
	}
}

void ADrunkNPC::OnSurviveTimeExpired()
{
	int32 ReleasedCount = 0;
	for (const TWeakObjectPtr<ABaseMonster>& WeakMonster : MonstersCurrentlyEating)
	{
		if (ABaseMonster* Monster = WeakMonster.Get())
		{
			if (UCharacterMovementComponent* MonsterMove = Monster->GetCharacterMovement())
			{
				MonsterMove->SetMovementMode(MOVE_Walking);
			}
			++ReleasedCount;
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("%s: decoy consumed — %d monster(s) released back to patrol"), *GetName(), ReleasedCount);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1010, 3.0f, FColor::Red,
			FString::Printf(TEXT("Decoy consumed — %d monster(s) released"), ReleasedCount));
	}

	State = EDrunkNPCState::Consumed;
	if (OwningSpawner.IsValid())
	{
		OwningSpawner->OnNPCConsumed();
	}
	Destroy();
}
