// Fill out your copyright notice in the Description page of Project Settings.

#include "RiderCharacter.h"
#include "MountRiderComponent.h"
#include "BaseMonster.h"
#include "HolyItem.h"
#include "HookPoint.h"
#include "Components/RiderHookComponent.h"
#include "Components/InkVisionComponent.h"
#include "InventoryComponent.h"
#include "ResourceStockpileComponent.h"
#include "Components/InputComponent.h"
#include "InputCoreTypes.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "CenserDamageType.h"
#include "Animation/AnimSequence.h"

ARiderCharacter::ARiderCharacter()
{
	MountRiderComponent = CreateDefaultSubobject<UMountRiderComponent>(TEXT("MountRiderComponent"));

	// Rider-exclusive hook grapple — see URiderHookComponent's own header comment. NOT universal (unlike
	// UHidingComponent/UProbeComponent on ABaseCharacter) — same "hero-specific ability component" precedent
	// as OilReserve below.
	HookComponent = CreateDefaultSubobject<URiderHookComponent>(TEXT("HookComponent"));

	// "Чернильное зрение" — see UInkVisionComponent's own header comment. Rider-exclusive, same
	// hero-specific precedent as HookComponent above.
	InkVisionComponent = CreateDefaultSubobject<UInkVisionComponent>(TEXT("InkVisionComponent"));

	OilReserve = CreateDefaultSubobject<UResourceStockpileComponent>(TEXT("OilReserve"));
	OilReserve->ResourceName = TEXT("CenserOil");
	OilReserve->MaxAmount = 10;
	OilReserve->CurrentAmount = 10;
}

void ARiderCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Optional designer convenience — see StartingCenserClass's own comment. No-op if left unset.
	if (StartingCenserClass)
	{
		if (AHolyItem* StartingCenser = GetWorld()->SpawnActor<AHolyItem>(StartingCenserClass, GetActorLocation(), GetActorRotation()))
		{
			PickUp(StartingCenser);
		}
	}

	RefreshOilReserveSlot();
}

void ARiderCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// "Mount"/"Hook" — single binding now (previously ALSO had a raw BindKey(EKeys::F, ...)
	// fallback here, harmless back when both pointed straight at the idempotent TryMount(), but
	// actively broken for this context-sensitive dispatcher: two calls per physical press meant
	// a successful hook grapple (bIsHanging -> true) was immediately undone by the second call
	// in the same frame seeing bIsHanging already true and dropping right back off. One binding
	// is enough — "Mount" is already mapped to F in DefaultInput.ini. Isolated here (not
	// ABaseCharacter) so ONLY ARiderCharacter can ever execute a mount — see MountRiderComponent's
	// own header comment.
	PlayerInputComponent->BindAction("Mount", IE_Pressed, this, &ARiderCharacter::OnMountOrHookPressed);

	// "Refill Censer" — no named Input Action mapping exists for this (same "direct BindKey(), no
	// project-settings dependency" reasoning as Mount's own F binding above). R is free project-wide.
	PlayerInputComponent->BindKey(EKeys::R, IE_Pressed, this, &ARiderCharacter::RefillCenser);

	// "Toggle Censer" — same direct BindKey() reasoning as Refill/Holy Explosion/Ink Vision above. C is
	// free project-wide.
	PlayerInputComponent->BindKey(EKeys::C, IE_Pressed, this, &ARiderCharacter::ToggleCenser);

	// "Holy Explosion" — same direct BindKey() reasoning as Mount/Refill Censer above. X is free project-wide.
	PlayerInputComponent->BindKey(EKeys::X, IE_Pressed, this, &ARiderCharacter::TryHolyExplosion);

	// "Ink Vision" — same direct BindKey() reasoning as Mount/Refill Censer/Holy Explosion above. V is
	// free on Rider (F/R/X already taken).
	PlayerInputComponent->BindKey(EKeys::V, IE_Pressed, this, &ARiderCharacter::OnInkVisionTogglePressed);
}

bool ARiderCharacter::IsMounted() const
{
	return IsValid(MountRiderComponent) && MountRiderComponent->bIsMounted;
}

ABaseMonster* ARiderCharacter::GetMountedMonster() const
{
	return IsValid(MountRiderComponent) ? MountRiderComponent->MountedMonster : nullptr;
}

void ARiderCharacter::Dismount()
{
	if (IsValid(MountRiderComponent))
	{
		MountRiderComponent->Dismount();
	}
}

UAnimSequence* ARiderCharacter::GetMountedRidingAnimation() const
{
	return IsValid(MountRiderComponent) ? MountRiderComponent->MountedRidingAnimation : nullptr;
}

void ARiderCharacter::OnKnockedOffMount(AActor* DamageCauser)
{
	if (IsValid(MountRiderComponent))
	{
		MountRiderComponent->HandleKnockedOffMount(DamageCauser);
	}
}

void ARiderCharacter::OnMountOrHookPressed()
{
	const bool bDiagHanging = IsValid(HookComponent) && HookComponent->IsHanging();
	const bool bDiagMounted = IsValid(MountRiderComponent) && MountRiderComponent->bIsMounted;
	UE_LOG(LogTemp, Warning, TEXT("OnMountOrHookPressed: ENTER — IsHanging=%s, bIsMounted=%s"),
		bDiagHanging ? TEXT("true") : TEXT("false"), bDiagMounted ? TEXT("true") : TEXT("false"));
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(295, 4.0f, FColor::Magenta,
			FString::Printf(TEXT("F pressed: IsHanging=%s, bIsMounted=%s"),
				bDiagHanging ? TEXT("TRUE") : TEXT("false"), bDiagMounted ? TEXT("TRUE") : TEXT("false")));
	}

	if (IsValid(HookComponent) && HookComponent->IsHanging())
	{
		HookComponent->DropFromHook();
		return;
	}

	if (IsValid(MountRiderComponent) && MountRiderComponent->bIsMounted)
	{
		FVector TargetPoint;
		FHitResult AimHit;
		TraceAimTarget(TargetPoint, AimHit);
		if (AHookPoint* Hook = Cast<AHookPoint>(AimHit.GetActor()))
		{
			HookComponent->TryGrapple(Hook);
		}
		// Mounted but not aiming at a hook — no-op, same as F already doing nothing while mounted before
		// this task.
		return;
	}

	if (IsValid(MountRiderComponent))
	{
		MountRiderComponent->TryMount();
	}
}

void ARiderCharacter::RefillCenser()
{
	if (!IsValid(OilReserve) || OilReserve->IsEmpty())
	{
		return;
	}

	AHolyItem* Censer = nullptr;
	if (IsValid(EquippedRightHand) && EquippedRightHand->ItemType == EHolyItemType::Censer)
	{
		Censer = EquippedRightHand;
	}
	else if (IsValid(EquippedLeftHand) && EquippedLeftHand->ItemType == EHolyItemType::Censer)
	{
		Censer = EquippedLeftHand;
	}

	if (!IsValid(Censer))
	{
		return;
	}

	const float SpaceInTank = Censer->MaxFuel - Censer->FuelRemaining;
	const int32 AmountToTransfer = FMath::Min(OilReserve->GetCurrentAmount(), FMath::FloorToInt(SpaceInTank));
	if (AmountToTransfer <= 0)
	{
		return;
	}

	OilReserve->TrySpend(AmountToTransfer);
	Censer->FuelRemaining = FMath::Min(Censer->MaxFuel, Censer->FuelRemaining + AmountToTransfer);
	Censer->LightUp();
	RefreshOilReserveSlot();

	UE_LOG(LogTemp, Warning, TEXT("%s: refilled Censer by %d (reserve %d/%d, tank %.0f/%.0f)"),
		*GetName(), AmountToTransfer, OilReserve->GetCurrentAmount(), OilReserve->GetMaxAmount(), Censer->FuelRemaining, Censer->MaxFuel);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(400, 3.0f, FColor::Green,
			FString::Printf(TEXT("Refilled Censer: +%d (tank %.0f/%.0f, reserve %d/%d)"), AmountToTransfer, Censer->FuelRemaining, Censer->MaxFuel, OilReserve->GetCurrentAmount(), OilReserve->GetMaxAmount()));
	}
}

void ARiderCharacter::ToggleCenser()
{
	AHolyItem* Censer = nullptr;
	if (IsValid(EquippedRightHand) && EquippedRightHand->ItemType == EHolyItemType::Censer)
	{
		Censer = EquippedRightHand;
	}
	else if (IsValid(EquippedLeftHand) && EquippedLeftHand->ItemType == EHolyItemType::Censer)
	{
		Censer = EquippedLeftHand;
	}

	if (!IsValid(Censer))
	{
		return;
	}

	if (Censer->bIsLit)
	{
		Censer->Extinguish();
	}
	else
	{
		Censer->LightUp();
	}
}

void ARiderCharacter::TryHolyExplosion()
{
	if (bIsPanicking || IsMounted() || GetWorldTimerManager().IsTimerActive(HolyExplosionTimerHandle))
	{
		return;
	}

	AHolyItem* Censer = nullptr;
	if (IsValid(EquippedRightHand) && EquippedRightHand->ItemType == EHolyItemType::Censer)
	{
		Censer = EquippedRightHand;
	}
	else if (IsValid(EquippedLeftHand) && EquippedLeftHand->ItemType == EHolyItemType::Censer)
	{
		Censer = EquippedLeftHand;
	}

	if (!IsValid(Censer) || Censer->FuelRemaining < HolyExplosionMinFuel)
	{
		return;
	}

	// Lit BEFORE the fuel is spent — LightUp() only refuels if FuelRemaining is already <= 0, and here
	// it's still >= HolyExplosionMinFuel, so no refuel happens; this is expected.
	Censer->LightUp();
	PendingHolyExplosionCenser = Censer;
	Censer->BeginTossVisual(HolyExplosionDelay);

	LaunchCharacter(FVector(0.0f, 0.0f, HolyExplosionJumpVelocity), false, true);

	GetWorldTimerManager().SetTimer(HolyExplosionTimerHandle, this, &ARiderCharacter::ResolveHolyExplosion, HolyExplosionDelay, false);

	UE_LOG(LogTemp, Warning, TEXT("%s: Holy Explosion activated — charging (%.1fs)"), *GetName(), HolyExplosionDelay);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(410, HolyExplosionDelay + 0.5f, FColor::Yellow, TEXT("HOLY EXPLOSION CHARGING..."));
	}
}

void ARiderCharacter::ResolveHolyExplosion()
{
	AHolyItem* Censer = PendingHolyExplosionCenser.Get();
	PendingHolyExplosionCenser = nullptr;

	if (!IsValid(Censer))
	{
		return;
	}

	// Ground-impact visual (see BeginTossVisual()'s own comment) resolves first, so the Censer's actual
	// world position matches the AoE kill below — same moment, not just the same math.
	Censer->EndTossVisual();

	TArray<AActor*> NearbyActors;
	UKismetSystemLibrary::SphereOverlapActors(this, GetActorLocation(), HolyExplosionRadius,
		TArray<TEnumAsByte<EObjectTypeQuery>>{ UEngineTypes::ConvertToObjectType(ECC_Pawn) },
		ABaseMonster::StaticClass(), TArray<AActor*>{}, NearbyActors);

	int32 KilledCount = 0;
	for (AActor* NearbyActor : NearbyActors)
	{
		ABaseMonster* Monster = Cast<ABaseMonster>(NearbyActor);
		if (!IsValid(Monster) || Monster->IsPendingKillPending() || Monster->bIsBeingRidden)
		{
			continue;
		}

		// Guaranteed kill — not tied to any particular monster's MaxHealth (unlike the old fixed
		// CenserThrowDamage), in case a tougher future monster shows up.
		UGameplayStatics::ApplyDamage(Monster, TNumericLimits<float>::Max(), GetController(), this, UCenserDamageType::StaticClass());
		++KilledCount;
	}

	Censer->FuelRemaining = 0.0f;
	Censer->Extinguish();

	UE_LOG(LogTemp, Warning, TEXT("%s: Holy Explosion resolved at %s — killed %d monster(s) within %.0f units"),
		*GetName(), *GetActorLocation().ToCompactString(), KilledCount, HolyExplosionRadius);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(411, 3.0f, FColor::Red,
			FString::Printf(TEXT("HOLY EXPLOSION: %d killed"), KilledCount));
	}
}

void ARiderCharacter::RefreshOilReserveSlot()
{
	if (!IsValid(OilReserve))
	{
		return;
	}
	UInventoryComponent* Inventory = FindComponentByClass<UInventoryComponent>();
	if (!IsValid(Inventory))
	{
		return;
	}
	FQuickSlotData NewData;
	NewData.SlotType = EQuickSlotType::Utility;
	NewData.DisplayName = FText::FromString(TEXT("Oil"));
	NewData.ChargeCount = OilReserve->GetCurrentAmount();
	NewData.MaxChargeCount = OilReserve->GetMaxAmount();
	Inventory->SetSlotData(2, NewData);
}

void ARiderCharacter::OnInkVisionTogglePressed()
{
	if (IsValid(InkVisionComponent))
	{
		InkVisionComponent->ToggleVision();
	}
}
