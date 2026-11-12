// Fill out your copyright notice in the Description page of Project Settings.

#include "BearTrap.h"
#include "BaseCharacter.h"
#include "BaseMonster.h"
#include "MountRiderComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

ABearTrap::ABearTrap()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = SceneRoot;

	TrapMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TrapMesh"));
	TrapMesh->SetupAttachment(RootComponent);
	TrapMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	TrapMesh->SetCollisionObjectType(ECC_WorldStatic);
	TrapMesh->SetCollisionResponseToAllChannels(ECR_Block);
	TrapMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	// Placeholder visual — flattened Cylinder reads as a rough trap silhouette lying on the ground, same
	// "working default before a real asset exists" precedent as AHolyItem's Candle placeholder (see
	// ApplyCandlePrototypeVisualsIfNeeded()'s own comment there).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMeshFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (CylinderMeshFinder.Succeeded())
	{
		TrapMesh->SetStaticMesh(CylinderMeshFinder.Object);
	}
	TrapMesh->SetRelativeScale3D(FVector(0.6f, 0.6f, 0.08f));

	TriggerCollision = CreateDefaultSubobject<USphereComponent>(TEXT("TriggerCollision"));
	TriggerCollision->SetupAttachment(RootComponent);
	TriggerCollision->SetSphereRadius(40.0f);
	TriggerCollision->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	TriggerCollision->SetCollisionObjectType(ECC_WorldDynamic);
	TriggerCollision->SetCollisionResponseToAllChannels(ECR_Ignore);
	TriggerCollision->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	TriggerCollision->SetGenerateOverlapEvents(true);
}

void ABearTrap::BeginPlay()
{
	Super::BeginPlay();

	TriggerCollision->OnComponentBeginOverlap.AddDynamic(this, &ABearTrap::OnTriggerOverlap);
}

void ABearTrap::OnTriggerOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	if (bHasCaughtSomeone)
	{
		return;
	}

	ABaseCharacter* Character = Cast<ABaseCharacter>(OtherActor);
	if (!IsValid(Character) || Character->bIsTrapped || Character->bIsDead)
	{
		// Not a valid ABaseCharacter catch — check for a ridden ABaseMonster instead (Roadmap Фаза 5, п.15:
		// a monster on its own doesn't fail this Cast<ABaseCharacter>, it just never passes it at all, so
		// this is a second, independent branch rather than a fallback within the one above). An unridden
		// monster stepping in the trap is deliberately left with no reaction — out of scope.
		ABaseMonster* Monster = Cast<ABaseMonster>(OtherActor);
		if (!IsValid(Monster) || !Monster->bIsBeingRidden)
		{
			return;
		}

		bHasCaughtSomeone = true;
		TriggerCollision->SetGenerateOverlapEvents(false);
		TrapMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

		if (UCharacterMovementComponent* MonsterMove = Monster->GetCharacterMovement())
		{
			MonsterMove->DisableMovement();
		}

		GetWorld()->GetTimerManager().SetTimer(
			RiddenMonsterReleaseTimerHandle,
			FTimerDelegate::CreateUObject(this, &ABearTrap::ReleaseRiddenMonster, TWeakObjectPtr<ABaseMonster>(Monster)),
			RiddenMonsterHoldDuration, false);
		return;
	}

	bHasCaughtSomeone = true;

	// Stop the trigger from firing again (belt-and-braces alongside bHasCaughtSomeone — this also frees
	// the trap from continuing to tick overlap queries it no longer needs) and let the character's own
	// mesh/capsule pass through the now-sprung trap instead of colliding with TrapMesh as an obstacle
	// while it's dragged around attached to a foot bone.
	TriggerCollision->SetGenerateOverlapEvents(false);
	TrapMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	USkeletalMeshComponent* CharacterMesh = Character->GetMesh();
	FName SocketToUse = NAME_None;
	if (IsValid(CharacterMesh))
	{
		if (CharacterMesh->DoesSocketExist(RightFootSocket) || CharacterMesh->GetBoneIndex(RightFootSocket) != INDEX_NONE)
		{
			SocketToUse = RightFootSocket;
		}
		else if (CharacterMesh->DoesSocketExist(LeftFootSocket) || CharacterMesh->GetBoneIndex(LeftFootSocket) != INDEX_NONE)
		{
			SocketToUse = LeftFootSocket;
		}
	}

	if (IsValid(CharacterMesh) && SocketToUse != NAME_None)
	{
		AttachToComponent(CharacterMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketToUse);
	}
	// Else: neither foot socket/bone exists on this mesh — the trap stays wherever it was in the world
	// (not ideal, but the escape mechanic itself doesn't depend on the trap's own transform at all, only
	// on ABaseCharacter's own state, so the QTE still works correctly without a visual foot-attach).

	Character->CaughtInTrap(this);
}

void ABearTrap::OnPlayerEscaped()
{
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	Destroy();
}

void ABearTrap::ReleaseRiddenMonster(TWeakObjectPtr<ABaseMonster> MonsterPtr)
{
	ABaseMonster* Monster = MonsterPtr.Get();
	if (IsValid(Monster))
	{
		if (UCharacterMovementComponent* MonsterMove = Monster->GetCharacterMovement())
		{
			MonsterMove->SetMovementMode(MOVE_Walking);
		}
		if (ABaseCharacter* Rider = Cast<ABaseCharacter>(Monster->CurrentRider.Get()))
		{
			if (UMountRiderComponent* MountComp = Rider->FindComponentByClass<UMountRiderComponent>())
			{
				MountComp->Dismount();
			}
		}
	}

	// Sprung trap is spent, same as OnPlayerEscaped() — the monster is now fully free.
	Destroy();
}
