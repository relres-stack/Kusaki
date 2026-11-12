// Fill out your copyright notice in the Description page of Project Settings.

#include "Components/MonsterAudioComponent.h"
#include "BaseCharacter.h"
#include "BaseMonster.h"
#include "Components/AudioComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"

UMonsterAudioComponent::UMonsterAudioComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UMonsterAudioComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerMonster = Cast<ABaseMonster>(GetOwner());
	if (!OwnerMonster.IsValid())
	{
		return;
	}

	USkeletalMeshComponent* MonsterMesh = OwnerMonster->GetMesh();

	if (GroanSound)
	{
		GroanAudioComp = NewObject<UAudioComponent>(OwnerMonster.Get(), TEXT("GroanAudioComp"));
		GroanAudioComp->SetupAttachment(MonsterMesh);
		GroanAudioComp->RegisterComponent();
		GroanAudioComp->SetSound(GroanSound);
		GroanAudioComp->Play();
	}

	if (BreathingSound)
	{
		BreathingAudioComp = NewObject<UAudioComponent>(OwnerMonster.Get(), TEXT("BreathingAudioComp"));
		BreathingAudioComp->SetupAttachment(MonsterMesh);
		BreathingAudioComp->RegisterComponent();
		BreathingAudioComp->SetSound(BreathingSound);
	}

	if (GrowlSound)
	{
		GrowlAudioComp = NewObject<UAudioComponent>(OwnerMonster.Get(), TEXT("GrowlAudioComp"));
		GrowlAudioComp->SetupAttachment(MonsterMesh);
		GrowlAudioComp->RegisterComponent();
		GrowlAudioComp->SetSound(GrowlSound);
	}
}

void UMonsterAudioComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ABaseMonster* Monster = OwnerMonster.Get();
	if (!IsValid(Monster))
	{
		return;
	}

	ABaseCharacter* Player = Cast<ABaseCharacter>(UGameplayStatics::GetPlayerCharacter(GetWorld(), 0));

	if (IsValid(BreathingAudioComp))
	{
		const float Distance = IsValid(Player) ? FVector::Dist(Player->GetActorLocation(), Monster->GetActorLocation()) : MAX_FLT;
		const bool bShouldBreathe = Distance <= BreathingProximityRadius;
		if (bShouldBreathe != bWasBreathingLastTick)
		{
			bShouldBreathe ? BreathingAudioComp->Play() : BreathingAudioComp->Stop();
			bWasBreathingLastTick = bShouldBreathe;
		}
	}

	if (IsValid(GrowlAudioComp))
	{
		const bool bShouldGrowl = IsValid(Player) && Monster->IsScanningForPlayer(Player);
		if (bShouldGrowl != bWasGrowlingLastTick)
		{
			bShouldGrowl ? GrowlAudioComp->Play() : GrowlAudioComp->Stop();
			bWasGrowlingLastTick = bShouldGrowl;
		}
	}
}

void UMonsterAudioComponent::PlayAttackHitSound()
{
	ABaseMonster* Monster = OwnerMonster.Get();
	if (!IsValid(Monster) || !AttackHitSound)
	{
		return;
	}

	UGameplayStatics::PlaySoundAtLocation(this, AttackHitSound, Monster->GetActorLocation());
}
