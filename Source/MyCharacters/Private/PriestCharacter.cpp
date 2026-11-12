// Fill out your copyright notice in the Description page of Project Settings.

#include "PriestCharacter.h"
#include "BaseMonster.h"
#include "ControlHint.h"
#include "CandleDamageType.h"
#include "HolyItem.h"
#include "InventoryComponent.h"
#include "ResourceStockpileComponent.h"
#include "Animation/AnimationAsset.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/InputComponent.h"
#include "Components/VisionFireSourceComponent.h"
#include "Engine/Engine.h"
#include "InputCoreTypes.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "TimerManager.h"

APriestCharacter::APriestCharacter()
{
	CandleStockpile = CreateDefaultSubobject<UResourceStockpileComponent>(TEXT("CandleStockpile"));
	CandleStockpile->ResourceName = TEXT("Candles");
	CandleStockpile->MaxAmount = 10;
	CandleStockpile->CurrentAmount = 0;

	VisionFireSourceComponent = CreateDefaultSubobject<UVisionFireSourceComponent>(TEXT("VisionFireSourceComponent"));

	VisionFirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("VisionFirstPersonCamera"));
	VisionFirstPersonCamera->SetupAttachment(GetMesh(), VisionFirstPersonCameraSocket);
	VisionFirstPersonCamera->bUsePawnControlRotation = true;
	VisionFirstPersonCamera->SetActive(false);

	// The 3D hand glued to the camera (see the header). Mesh asset and pose are assigned when the vision starts.
	VisionArmsMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("VisionArmsMesh"));
	VisionArmsMesh->SetupAttachment(VisionFirstPersonCamera);
	VisionArmsMesh->SetOnlyOwnerSee(true);
	VisionArmsMesh->SetCastShadow(false);
	VisionArmsMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisionArmsMesh->SetGenerateOverlapEvents(false);
	VisionArmsMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	VisionArmsMesh->SetVisibility(false);
}

void APriestCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Optional designer convenience — see StartingCandleClass's own comment. No-op if left unset.
	if (StartingCandleClass)
	{
		if (AHolyItem* StartingCandle = GetWorld()->SpawnActor<AHolyItem>(StartingCandleClass, GetActorLocation(), GetActorRotation()))
		{
			PickUp(StartingCandle);
		}
	}

	RefreshCandleStockpileSlot();
}

void APriestCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Q is free on this class — the base "MeleeSwing" Q binding no longer exists at all (see
	// ABaseCharacter::SetupPlayerInputComponent()'s own comment) — same direct BindKey() pattern as F above.
	PlayerInputComponent->BindKey(EKeys::Q, IE_Pressed, this, &APriestCharacter::CycleCandleHands);

	// V is free — not claimed by any mechanic in CLAUDE.md's registry — same direct BindKey() pattern as
	// F/Q above.
	PlayerInputComponent->BindKey(EKeys::V, IE_Pressed, this, &APriestCharacter::OnVisionFireTogglePressed);

	// "Scatter Prayer" — same direct BindKey() reasoning as ARiderCharacter::TryHolyExplosion()'s own X
	// binding. X is free on this class.
	PlayerInputComponent->BindKey(EKeys::X, IE_Pressed, this, &APriestCharacter::TryScatterPrayer);
}

void APriestCharacter::GetControlHints(TArray<FControlHint>& OutHints) const
{
	// Keep in sync with SetupPlayerInputComponent() above.
	OutHints.Add(FControlHint::Section(INVTEXT("ПРИСТ")));
	OutHints.Emplace(INVTEXT("Q"), INVTEXT("свечи по кругу: зажечь правую, затем левую, затем убрать в карман"));
	OutHints.Emplace(INVTEXT("ПКМ + ЛКМ"), INVTEXT("бросить свечу (свеча теряется)"));
	OutHints.Emplace(INVTEXT("E (у свечи)"), INVTEXT("подобрать свечу в карман"));
	OutHints.Emplace(INVTEXT("V"), INVTEXT("воспалённое зрение вкл / выкл (сам закрывает глаза)"));
	OutHints.Emplace(INVTEXT("X"), INVTEXT("молитва: отбросить монстров рядом (перезарядка 20 с)"));

	Super::GetControlHints(OutHints);
}

void APriestCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Raised-arm values for the AnimBP (see the header): smoothed 0..1 alpha, pose * alpha, plus a gentle sway.
	VisionArmRaiseAlpha = FMath::FInterpTo(VisionArmRaiseAlpha, bVisionArmRaised ? 1.0f : 0.0f, DeltaTime, VisionArmRaiseSpeed);
	if (VisionArmRaiseAlpha < 0.001f)
	{
		VisionArmRaiseAlpha = 0.0f;
	}
	const float SwayTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	const float Sway = FMath::Sin(SwayTime * VisionArmBobSpeed * 6.2831853f) * VisionArmRaiseAlpha;
	VisionUpperArmRotation = VisionUpperArmPose * VisionArmRaiseAlpha + VisionArmBobAmount * Sway;
	VisionLowerArmRotation = VisionLowerArmPose * VisionArmRaiseAlpha;
	VisionHandRotation = VisionHandPose * VisionArmRaiseAlpha + VisionArmBobAmount * (Sway * 0.5f);

	// 3D hand glued to the camera: line its head bone up with the camera (so the body is behind/below the view and
	// only the reaching arm shows), then add the user's offset, the rise-in travel and the gentle sway.
	if (IsValid(VisionArmsMesh) && VisionArmsMesh->GetSkeletalMeshAsset())
	{
		const bool bShowArms = bVisionArmRaised || VisionArmRaiseAlpha > 0.01f;
		VisionArmsMesh->SetVisibility(bShowArms);
		if (bShowArms)
		{
			const FRotator ArmsRotation = VisionArmsRotation;
			const FVector HeadInMesh = VisionArmsMesh->GetSocketTransform(VisionFirstPersonCameraSocket, RTS_Component).GetLocation();
			const FVector Travel = VisionArmsRaiseTravel * (1.0f - VisionArmRaiseAlpha);
			const FVector Bob = VisionArmsBobOffset * Sway;
			VisionArmsMesh->SetRelativeRotation(ArmsRotation);
			VisionArmsMesh->SetRelativeLocation(VisionArmsOffset + Travel + Bob - ArmsRotation.RotateVector(HeadInMesh));
		}
	}

	// Natural burnout (FuelRemaining hit 0 in AHolyItem::Tick(), which already called its own
	// Extinguish()) vs. a deliberate CycleCandleHands() extinguish: the deliberate path already clears
	// EquippedRightHand/EquippedLeftHand itself and pockets the candle, so by the time this runs a
	// still-assigned-but-unlit hand can only mean the candle burned out on its own. Nothing goes back
	// into the pocket here — it burned out for good — this just frees the hand so CycleCandleHands() has
	// somewhere to put the next one.
	if (IsValid(EquippedRightHand) && !EquippedRightHand->bIsLit)
	{
		EquippedRightHand->Destroy();
		EquippedRightHand = nullptr;
		if (IsValid(InventoryComponent))
		{
			InventoryComponent->RefreshLightSourceSlot();
		}
		OnHeldItemsChanged();
	}
	if (IsValid(EquippedLeftHand) && !EquippedLeftHand->bIsLit)
	{
		EquippedLeftHand->Destroy();
		EquippedLeftHand = nullptr;
		if (IsValid(InventoryComponent))
		{
			InventoryComponent->RefreshLightSourceSlot();
		}
		OnHeldItemsChanged();
	}
}

void APriestCharacter::OnVisionFireTogglePressed()
{
	if (IsValid(VisionFireSourceComponent))
	{
		VisionFireSourceComponent->ToggleVision();
	}
}

bool APriestCharacter::GetVisionFingertipLocation(FVector& OutLocation) const
{
	const USkeletalMeshComponent* MeshComponent = (IsValid(VisionArmsMesh) && VisionArmsMesh->GetSkeletalMeshAsset() && VisionArmsMesh->IsVisible())
		? VisionArmsMesh.Get() : GetMesh();
	if (!IsValid(MeshComponent))
	{
		return false;
	}
	const FName Fallback(TEXT("hand_r"));
	if (MeshComponent->DoesSocketExist(VisionFingertipSocket))
	{
		OutLocation = MeshComponent->GetSocketLocation(VisionFingertipSocket);
		return true;
	}
	if (MeshComponent->DoesSocketExist(Fallback))
	{
		OutLocation = MeshComponent->GetSocketLocation(Fallback);
		return true;
	}
	return false;
}

bool APriestCharacter::GetVisionFingertipCameraLocal(FVector& OutLocal) const
{
	if (!IsValid(VisionArmsMesh) || !VisionArmsMesh->GetSkeletalMeshAsset() || !VisionArmsMesh->IsVisible())
	{
		return false;
	}
	FName Socket = VisionFingertipSocket;
	if (!VisionArmsMesh->DoesSocketExist(Socket))
	{
		Socket = TEXT("hand_r");
		if (!VisionArmsMesh->DoesSocketExist(Socket))
		{
			return false;
		}
	}
	const FVector BoneInMesh = VisionArmsMesh->GetSocketTransform(Socket, RTS_Component).GetLocation();
	OutLocal = VisionArmsMesh->GetRelativeLocation() + VisionArmsMesh->GetRelativeRotation().RotateVector(BoneInMesh);
	return true;
}

void APriestCharacter::SetVisionFirstPersonActive(bool bActive, bool bShowRaisedArm)
{
	if (IsValid(GetFollowCamera()))
	{
		GetFollowCamera()->SetActive(!bActive);
	}
	if (IsValid(VisionFirstPersonCamera))
	{
		VisionFirstPersonCamera->SetActive(bActive);
	}
	const bool bRaiseArm = bActive && bShowRaisedArm;
	bVisionArmRaised = bRaiseArm;
	if (IsValid(GetMesh()))
	{
		// The character's own body is always hidden from its own head camera; in 3D-hand mode the separate
		// VisionArmsMesh (glued to the camera) shows the hand instead.
		GetMesh()->SetOwnerNoSee(bActive);
	}
	if (IsValid(VisionArmsMesh) && bRaiseArm)
	{
		if (!VisionArmsMesh->GetSkeletalMeshAsset() && IsValid(GetMesh()))
		{
			VisionArmsMesh->SetSkeletalMesh(GetMesh()->GetSkeletalMeshAsset());
		}
		if (IsValid(VisionArmsAnimation) && VisionArmsMesh->GetSkeletalMeshAsset())
		{
			VisionArmsMesh->PlayAnimation(VisionArmsAnimation, true);
		}
	}
}

void APriestCharacter::TryScatterPrayer()
{
	if (bIsPanicking || GetWorldTimerManager().IsTimerActive(ScatterPrayerCooldownTimerHandle))
	{
		return;
	}

	TArray<AActor*> NearbyActors;
	UKismetSystemLibrary::SphereOverlapActors(this, GetActorLocation(), ScatterPrayerRadius,
		TArray<TEnumAsByte<EObjectTypeQuery>>{ UEngineTypes::ConvertToObjectType(ECC_Pawn) },
		ABaseMonster::StaticClass(), TArray<AActor*>{}, NearbyActors);

	int32 ScatteredCount = 0;
	for (AActor* NearbyActor : NearbyActors)
	{
		ABaseMonster* Monster = Cast<ABaseMonster>(NearbyActor);
		if (!IsValid(Monster) || Monster->IsPendingKillPending())
		{
			continue;
		}

		const FVector PushDirection = (Monster->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
		Monster->ApplyKnockback(PushDirection, ScatterPrayerKnockbackForce, ScatterPrayerUpwardForce);
		UGameplayStatics::ApplyDamage(Monster, 1.0f, GetController(), this, UCandleDamageType::StaticClass());
		++ScatteredCount;
	}

	// No windup, unlike ARiderCharacter::TryHolyExplosion() — resolves immediately, cooldown starts now.
	// Poll-only timer (no callback) — same reasoning as ARiderCharacter::HolyExplosionTimerHandle's own
	// IsTimerActive() guard, just without a resolve function to call later.
	GetWorldTimerManager().SetTimer(ScatterPrayerCooldownTimerHandle, ScatterPrayerCooldown, false);

	if (ScatterPrayerSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ScatterPrayerSound, GetActorLocation());
	}

	UE_LOG(LogTemp, Warning, TEXT("%s: Scatter Prayer resolved at %s — scattered %d monster(s) within %.0f units"),
		*GetName(), *GetActorLocation().ToCompactString(), ScatteredCount, ScatterPrayerRadius);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(420, 3.0f, FColor::Cyan,
			FString::Printf(TEXT("МОЛИТВА: %d монстр(ов) отброшено"), ScatteredCount));
	}
}

void APriestCharacter::RefreshCandleStockpileSlot()
{
	if (!IsValid(CandleStockpile))
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
	NewData.DisplayName = FText::FromString(TEXT("Candles"));
	NewData.ChargeCount = CandleStockpile->GetCurrentAmount();
	NewData.MaxChargeCount = CandleStockpile->GetMaxAmount();
	Inventory->SetSlotData(2, NewData);
}

void APriestCharacter::Interact()
{
	// Trapped, or nothing highlighted (either drop-from-hand or a genuine no-op) — ordinary base behavior,
	// nothing Priest-specific applies. Also covers a highlighted non-Candle item (e.g. a stray Censer) —
	// only ItemType == Candle gets pocketed below.
	if (bIsTrapped || !IsValid(HighlightedItem) || HighlightedItem->ItemType != EHolyItemType::Candle)
	{
		Super::Interact();
		return;
	}

	if (!IsValid(CandleStockpile) || CandleStockpile->GetCurrentAmount() >= CandleStockpile->GetMaxAmount())
	{
		// Pocket already full (or no stockpile at all) — falls through to the ordinary hand-equip path,
		// same as before this override existed.
		Super::Interact();
		return;
	}

	AHolyItem* PickedCandle = HighlightedItem;
	const float PickedFuel = PickedCandle->FuelRemaining;
	if (CandleStockpile->Add(1) > 0)
	{
		PocketCandleFuel.Add(PickedFuel);
	}
	RefreshCandleStockpileSlot();

	// Same "clear the highlight immediately, don't wait for the next 0.1s scan tick" reasoning as
	// ABaseCharacter::PickUp()'s own matching block.
	PickedCandle->DisableHighlight();
	HighlightedItem = nullptr;
	OnHighlightedItemChanged.Broadcast(nullptr);

	PickedCandle->Destroy();
}

void APriestCharacter::EnterPanicState(ABaseMonster* AttackingMonster)
{
	Super::EnterPanicState(AttackingMonster);

	// Same "pocket at current fuel level" logic as CycleCandleHands()'s own extinguish branches, applied
	// unconditionally to both hands here instead of the alternating single-hand cycle — except each hand
	// now independently rolls a chance to break for good instead of being pocketed, per this task.
	if (IsValid(EquippedRightHand) && EquippedRightHand->bIsLit)
	{
		if (FMath::FRand() <= CandleBreakChanceOnPanic)
		{
			// Не повезло — сломалась безвозвратно, в карман не возвращается.
			EquippedRightHand->Destroy();
		}
		else if (PocketCandleFuel.Num() < CandleStockpile->GetMaxAmount())
		{
			// Повезло — тухнет целой, уходит в карман с текущим остатком топлива.
			PocketCandleFuel.Add(EquippedRightHand->FuelRemaining);
			CandleStockpile->Add(1);
			EquippedRightHand->Destroy();
		}
		else
		{
			// Повезло, но в кармане и так уже нет места — как и в текущей версии, просто теряется.
			EquippedRightHand->Destroy();
		}
		EquippedRightHand = nullptr;
	}
	if (IsValid(EquippedLeftHand) && EquippedLeftHand->bIsLit)
	{
		if (FMath::FRand() <= CandleBreakChanceOnPanic)
		{
			EquippedLeftHand->Destroy();
		}
		else if (PocketCandleFuel.Num() < CandleStockpile->GetMaxAmount())
		{
			PocketCandleFuel.Add(EquippedLeftHand->FuelRemaining);
			CandleStockpile->Add(1);
			EquippedLeftHand->Destroy();
		}
		else
		{
			EquippedLeftHand->Destroy();
		}
		EquippedLeftHand = nullptr;
	}

	RefreshCandleStockpileSlot();
	if (IsValid(InventoryComponent))
	{
		InventoryComponent->RefreshLightSourceSlot();
	}
	OnHeldItemsChanged();
}

void APriestCharacter::CycleCandleHands()
{
	if (!IsValid(CandleStockpile))
	{
		return;
	}

	const bool bRightLit = IsValid(EquippedRightHand) && EquippedRightHand->bIsLit;
	const bool bLeftLit = IsValid(EquippedLeftHand) && EquippedLeftHand->bIsLit;

	if (!bLeftLit)
	{
		// Left hand not lit — either both hands are free, or only the right is lit. Either way this is
		// the "light one from the pocket" half of the cycle: PickUp()'s own priority (right first, then
		// left) already sends the new candle to whichever of those two hands is actually free, so no
		// hand needs picking here.
		if (PocketCandleFuel.Num() == 0 || !StartingCandleClass)
		{
			return;
		}

		const float PoppedFuel = PocketCandleFuel.Pop();
		if (!CandleStockpile->TrySpend(1))
		{
			// Shouldn't happen (PocketCandleFuel and CandleStockpile's count are kept in lockstep), but
			// don't strand the popped fuel value if it ever does.
			PocketCandleFuel.Add(PoppedFuel);
			return;
		}

		if (AHolyItem* NewCandle = GetWorld()->SpawnActor<AHolyItem>(StartingCandleClass, GetActorLocation(), GetActorRotation()))
		{
			// Set before PickUp() so its own LightUp() call sees the pocketed fuel level, not the
			// freshly-spawned default.
			NewCandle->FuelRemaining = PoppedFuel;
			PickUp(NewCandle);
		}
	}
	else
	{
		// Left hand is lit — either both hands are lit (pocket the right one), or only the left is lit
		// (pocket that one). The other half of the cycle.
		if (PocketCandleFuel.Num() >= CandleStockpile->GetMaxAmount())
		{
			return;
		}

		AHolyItem*& HandToPocket = bRightLit ? EquippedRightHand : EquippedLeftHand;
		PocketCandleFuel.Add(HandToPocket->FuelRemaining);
		CandleStockpile->Add(1);
		HandToPocket->Destroy();
		HandToPocket = nullptr;
		if (IsValid(InventoryComponent))
		{
			InventoryComponent->RefreshLightSourceSlot();
		}
		OnHeldItemsChanged();
	}

	RefreshCandleStockpileSlot();
}

void APriestCharacter::OnHeldItemsChanged()
{
	// "Active" candle per this task's own wording: currently held (either hand) AND still lit — a thrown/
	// dropped/extinguished one no longer counts, matching the same bIsLit gate
	// UInventoryComponent::RefreshLightSourceSlot() already uses for "is this actually equipped light".
	const bool bHoldingLitCandle =
		(IsValid(EquippedRightHand) && EquippedRightHand->ItemType == EHolyItemType::Candle && EquippedRightHand->bIsLit) ||
		(IsValid(EquippedLeftHand) && EquippedLeftHand->ItemType == EHolyItemType::Candle && EquippedLeftHand->bIsLit);

	if (bHoldingLitCandle)
	{
		Crouch();
	}
	else
	{
		UnCrouch();
	}
}
