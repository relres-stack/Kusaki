// Fill out your copyright notice in the Description page of Project Settings.

#include "HolyItem.h"
#include "Components/StaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "CableComponent.h"
#include "BaseMonster.h"
#include "BaseCharacter.h"
#include "CenserDamageType.h"
#include "CandleDamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/StaticMesh.h"
#include "Engine/EngineTypes.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/SoftObjectPath.h"
#include "NavModifierComponent.h"
#include "NavAreas/NavArea_Obstacle.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "DrawDebugHelpers.h"

AHolyItem::AHolyItem()
{
	// Ticks so bDrawDebugRadius can redraw HolyAuraRadius live each frame while testing (see Tick());
	// the check inside Tick() makes this effectively free when the flag is off.
	PrimaryActorTick.bCanEverTick = true;

	ItemMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ItemMesh"));
	RootComponent = ItemMesh;

	ItemMesh->SetVisibility(true);
	ItemMesh->SetSimulatePhysics(true);
	ItemMesh->SetNotifyRigidBodyCollision(true);
	ItemMesh->SetGenerateOverlapEvents(true);

	// Continuous Collision Detection: a thrown item moving fast enough can travel further than a thin
	// target's collision depth within a single physics substep, "tunneling" through it with no OnHit at
	// all — CCD is Unreal's standard fix for exactly this (fast small/thin dynamic body vs. a target it
	// must not be able to pass through), and costs nothing when the item isn't moving fast.
	ItemMesh->SetUseCCD(true);

	// Fall back to a basic engine shape so the item is never an invisible mesh-less actor;
	// a Blueprint subclass or instance can still override this with a real asset.
	if (!ItemMesh->GetStaticMesh())
	{
		static ConstructorHelpers::FObjectFinder<UStaticMesh> DefaultMeshFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
		if (DefaultMeshFinder.Succeeded())
		{
			ItemMesh->SetStaticMesh(DefaultMeshFinder.Object);
		}
	}

	ItemMesh->OnComponentHit.AddDynamic(this, &AHolyItem::OnItemHit);
	ItemMesh->OnComponentBeginOverlap.AddDynamic(this, &AHolyItem::OnItemBeginOverlap);

	LightComponent = CreateDefaultSubobject<UPointLightComponent>(TEXT("LightComponent"));
	LightComponent->SetupAttachment(RootComponent);
	LightComponent->SetVisibility(true);
	LightComponent->SetIntensity(3000.0f);
	LightComponent->SetLightColor(FLinearColor::Yellow);

	// No Niagara System asset assigned here — a Blueprint subclass (e.g. BP_Censer/BP_Candle) assigns
	// the actual flame VFX in its Details panel. The component itself always exists so Extinguish()
	// always has something valid to Deactivate(), whether or not a subclass ever assigns an asset.
	FlameParticleComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("FlameParticleComponent"));
	FlameParticleComponent->SetupAttachment(GetRootComponent());

	// Not attached to anything here — AttachChainToSocket() (Censer-only, called from PickUp()/
	// Extinguish()/LightUp()) attaches its START directly onto the holder's skeletal mesh at runtime, and
	// its END always tracks this actor's own ItemMesh via SetAttachEndTo(). Hidden until then, so it never
	// renders unattached at the world origin for a Candle (which never calls AttachChainToSocket() at all)
	// or before a Censer is first picked up.
	ChainVisualComponent = CreateDefaultSubobject<UCableComponent>(TEXT("ChainVisualComponent"));
	ChainVisualComponent->SetVisibility(false);
	ChainVisualComponent->CableWidth = 1.5f;
	ChainVisualComponent->NumSegments = 8;
	ChainVisualComponent->EndLocation = FVector::ZeroVector;

	HolyAuraSphere = CreateDefaultSubobject<USphereComponent>(TEXT("HolyAuraSphere"));
	HolyAuraSphere->SetupAttachment(RootComponent);
	HolyAuraSphere->SetSphereRadius(HolyAuraRadius);

	// Keep the aura's WORLD radius exactly equal to HolyAuraRadius no matter what scale ItemMesh has
	// (e.g. a candle/censer mesh scaled down to fit the hand). Without this, HolyAuraSphere silently
	// inherits that scale too — GetScaledSphereRadius() (and the sphere's actual physics overlap
	// extent) shrinks right along with the mesh, even though HolyAuraRadius itself still reads as the
	// configured value. This is exactly what caused the light-protection math to fail: a scaled-down
	// item's real aura radius was a fraction of what HolyAuraRadius said it was.
	HolyAuraSphere->SetUsingAbsoluteScale(true);

	// QueryOnly, ignore everything except explicitly OVERLAP (never Block) the Pawn channel — this is
	// the one channel a Character's capsule is (by engine default) actually an object of, so it's what
	// reliably catches the monster regardless of its capsule's own response setup, while never hard-
	// stopping anything's movement/pathing the way a Block response would.
	HolyAuraSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	HolyAuraSphere->SetCollisionObjectType(ECC_WorldDynamic);
	HolyAuraSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	HolyAuraSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	// Explicit even though SetCollisionResponseToAllChannels(ECR_Ignore) above already covers it —
	// belt-and-braces so the aim trace (ECC_Visibility, also already Ignore by the same blanket call)
	// and the third-person spring arm's own camera-collision probe (which defaults to ECC_Camera) can
	// never be blocked/pulled in by this sphere even if something later narrows the blanket ignore.
	HolyAuraSphere->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	HolyAuraSphere->SetGenerateOverlapEvents(true);
	HolyAuraSphere->OnComponentBeginOverlap.AddDynamic(this, &AHolyItem::OnAuraOverlap);

	// HolyAuraSphere must stay large (see HolyAuraRadius's comment) but must NOT drive the NavMesh
	// avoidance area's size — UNavModifierComponent unions the bounds of every nav-relevant collision
	// component on this actor, so without this, the avoidance ring would match HolyAuraSphere's full
	// radius instead of the small NavAvoidanceSphere below, and (with two items held at once) two such
	// full-radius avoidance rings can overlap into a region so costly the monster can't approach at all.
	HolyAuraSphere->SetCanEverAffectNavigation(false);

	// Exists purely so LightNavModifier has something small to size the avoidance ring from — no
	// collision responses/overlap events of its own; QueryOnly + CanEverAffectNavigation (default true)
	// is all UNavModifierComponent's bounds calculation needs to pick it up.
	NavAvoidanceSphere = CreateDefaultSubobject<USphereComponent>(TEXT("NavAvoidanceSphere"));
	NavAvoidanceSphere->SetupAttachment(RootComponent);
	NavAvoidanceSphere->SetSphereRadius(NavAvoidanceRadius);
	// Same reasoning as HolyAuraSphere above — the avoidance ring shouldn't shrink/grow with ItemMesh's
	// visual scale either.
	NavAvoidanceSphere->SetUsingAbsoluteScale(true);
	NavAvoidanceSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	NavAvoidanceSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	NavAvoidanceSphere->SetGenerateOverlapEvents(false);

	LightNavModifier = CreateDefaultSubobject<UNavModifierComponent>(TEXT("LightNavModifier"));

	// UNavArea_Obstacle (not the engine default UNavArea_Null): a very high pathing cost, not a hole cut
	// out of the NavMesh — normal Patrol pathfinding strongly prefers routing around it, but it remains
	// enterable, so a Stalking/Chasing monster's MoveTo the light boundary (or beyond, if it must) can
	// still resolve a path instead of failing outright and getting stuck.
	LightNavModifier->AreaClass = UNavArea_Obstacle::StaticClass();
}

void AHolyItem::BeginPlay()
{
	Super::BeginPlay();

	// ItemType defaults to Candle in the header (see its UPROPERTY) and NOTHING in C++ ever assigns it
	// Censer for a spawned/placed actor — a Blueprint subclass (e.g. BP_Censer) that never explicitly
	// overrides "Item Type" in its Class Defaults silently inherits that Candle default and behaves
	// exactly like a Candle at runtime (AoE stun instead of damage, etc.) with nothing else in the log
	// to suggest why. One-time, unmissable confirmation of which this actor actually resolved to.
	UE_LOG(LogTemp, Warning, TEXT("%s BeginPlay: ItemType = %s"),
		*GetName(), ItemType == EHolyItemType::Censer ? TEXT("Censer") : TEXT("Candle"));
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(218, 5.0f, FColor::Magenta,
			FString::Printf(TEXT("%s: ItemType = %s"), *GetName(), ItemType == EHolyItemType::Censer ? TEXT("Censer") : TEXT("Candle")));
	}

	if (LightComponent)
	{
		LightComponent->SetVisibility(true);
		LightComponent->SetIntensity(3000.0f);
		LightComponent->SetLightColor(ItemType == EHolyItemType::Candle ? FLinearColor::Red : FLinearColor::Yellow);
	}

	// Overrides ItemMesh/LightComponent's color/intensity/radius above for a Candle still showing the
	// generic placeholder — see its own comment. Deliberately AFTER the block above (not merged into it),
	// so its warm-orange/moderate-intensity/short-radius flame settings are what actually wins for a
	// Candle, rather than being immediately overwritten back to the flat Red/3000/engine-default set here.
	ApplyCandlePrototypeVisualsIfNeeded();
	ApplyCenserPrototypeVisualsIfNeeded();

	if (HolyAuraSphere)
	{
		// Re-applied here (not just the constructor) so a Blueprint subclass/instance override of
		// HolyAuraRadius — or of any of the collision/nav settings below, e.g. via the component's own
		// Details panel defaults — actually reaches the live component; SetSphereRadius() etc. in the
		// constructor only ever see the C++ default, since Blueprint property overrides apply after
		// construction.
		HolyAuraSphere->SetSphereRadius(HolyAuraRadius);
		HolyAuraSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		HolyAuraSphere->SetCollisionObjectType(ECC_WorldDynamic);
		HolyAuraSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
		HolyAuraSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		HolyAuraSphere->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		HolyAuraSphere->SetGenerateOverlapEvents(true);
		HolyAuraSphere->SetCanEverAffectNavigation(false);
	}

	// Same reasoning as HolyAuraSphere above: re-apply so a Blueprint override of NavAvoidanceRadius
	// actually reaches the live component.
	if (IsValid(NavAvoidanceSphere))
	{
		NavAvoidanceSphere->SetSphereRadius(NavAvoidanceRadius);
	}

	if (IsValid(ItemMesh))
	{
		BaseItemMeshScale = ItemMesh->GetRelativeScale3D();
	}
}

void AHolyItem::ApplyCandlePrototypeVisualsIfNeeded()
{
	if (ItemType != EHolyItemType::Candle || !IsValid(ItemMesh))
	{
		return;
	}

	// Only replace the placeholder — never clobber a real candle mesh a designer has since assigned in
	// this Blueprint's own Class Defaults. Compared by asset path, not just "is it non-null", since the
	// constructor's own fallback (see AHolyItem()) always assigns SOMETHING (the same placeholder Cube).
	static const FSoftObjectPath PlaceholderCubePath(TEXT("/Engine/BasicShapes/Cube.Cube"));
	const UStaticMesh* CurrentMesh = ItemMesh->GetStaticMesh();
	const bool bStillUsingPlaceholder = !IsValid(CurrentMesh) || FSoftObjectPath(CurrentMesh) == PlaceholderCubePath;
	if (!bStillUsingPlaceholder)
	{
		return;
	}

	if (UStaticMesh* CylinderMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
	{
		ItemMesh->SetStaticMesh(CylinderMesh);
	}
	ItemMesh->SetRelativeScale3D(CandlePrototypeScale);

	if (!IsValid(LightComponent))
	{
		return;
	}

	LightComponent->SetLightColor(CandleFlameLightColor);
	LightComponent->SetIntensity(CandleFlameLightIntensity);
	LightComponent->SetAttenuationRadius(CandleFlameAttenuationRadius);

	// Positioned near the top tip of the (now-scaled) cylinder, derived from the mesh ASSET's own actual
	// bounds rather than a flat guess — same "compute from real dimensions, not a flat guess" reasoning
	// this project already uses elsewhere (e.g. UMountRiderComponent::MountRiderHeightBuffer). UStaticMesh
	// bounds are in the asset's own LOCAL (unscaled) space; LightComponent's relative location is expressed
	// in that same unscaled space too, since it's a child of ItemMesh (this actor's RootComponent) — the
	// parent's CandlePrototypeScale above is applied automatically when composing the final world
	// transform, so this correctly lands near the tip regardless of CandlePrototypeScale's exact value.
	// 0.85 (not 1.0) keeps the light just inside the tip rather than exactly on the mesh surface.
	if (const UStaticMesh* FinalMesh = ItemMesh->GetStaticMesh())
	{
		const float TopOffsetZ = FinalMesh->GetBounds().BoxExtent.Z * 0.85f;
		LightComponent->SetRelativeLocation(FVector(0.0f, 0.0f, TopOffsetZ));
	}
}

void AHolyItem::ApplyCenserPrototypeVisualsIfNeeded()
{
	if (ItemType != EHolyItemType::Censer || !IsValid(ItemMesh))
	{
		return;
	}

	// Same "only replace the still-placeholder Cube" guard as ApplyCandlePrototypeVisualsIfNeeded() — never
	// clobbers a real Censer mesh already assigned in a Blueprint's Class Defaults.
	static const FSoftObjectPath PlaceholderCubePath(TEXT("/Engine/BasicShapes/Cube.Cube"));
	const UStaticMesh* CurrentMesh = ItemMesh->GetStaticMesh();
	const bool bStillUsingPlaceholder = !IsValid(CurrentMesh) || FSoftObjectPath(CurrentMesh) == PlaceholderCubePath;
	if (!bStillUsingPlaceholder)
	{
		return;
	}

	if (UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")))
	{
		ItemMesh->SetStaticMesh(SphereMesh);
	}
	ItemMesh->SetRelativeScale3D(CenserPrototypeScale);
}

void AHolyItem::AttachChainToSocket(USkeletalMeshComponent* HeldMesh, FName SocketName)
{
	if (ItemType != EHolyItemType::Censer || !IsValid(ChainVisualComponent) || !IsValid(HeldMesh))
	{
		return;
	}

	ChainVisualComponent->AttachToComponent(HeldMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);
	ChainVisualComponent->SetAttachEndTo(this, TEXT("ItemMesh"));
	ChainVisualComponent->SetVisibility(true);
}

void AHolyItem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Independent of, and must run regardless of, bDrawDebugRadius's early-out below.
	if (bIsThrown && ItemType == EHolyItemType::Censer)
	{
		CheckCenserFlightPathForMonster();
	}

	// Censer-only, foot-vs-mounted protection radius: full HolyAuraRadius while the holder rides a mount,
	// the smaller FootProtectionRadius otherwise. IsProtectedByHeldItem()/CheckAuraProximity() in
	// BaseMonster.cpp already read HolyAuraSphere->GetScaledSphereRadius() live, so just keeping the
	// sphere's actual radius in sync here is enough for both to pick this up automatically.
	if (ItemType == EHolyItemType::Censer && IsValid(HolyAuraSphere))
	{
		const ABaseCharacter* HolderCharacter = Cast<ABaseCharacter>(HoldingCharacter.Get());
		const float DesiredRadius = (IsValid(HolderCharacter) && HolderCharacter->IsMounted()) ? HolyAuraRadius : FootProtectionRadius;
		if (!FMath::IsNearlyEqual(HolyAuraSphere->GetUnscaledSphereRadius(), DesiredRadius))
		{
			HolyAuraSphere->SetSphereRadius(DesiredRadius);
		}
	}

	// Fuel drain — independent of bDrawDebugRadius, same as the checks above. FuelDrainRate == 0 means
	// effectively infinite fuel (skipped entirely, not just a no-op division). Extinguish() itself sets
	// bIsLit false, so this naturally stops draining the tick after it empties rather than needing an
	// extra guard here.
	if (bIsLit && FuelDrainRate > 0.0f && FuelRemaining > 0.0f)
	{
		FuelRemaining = FMath::Max(0.0f, FuelRemaining - FuelDrainRate * DeltaTime);
		if (FuelRemaining <= 0.0f)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s: ran out of fuel — extinguishing"), *GetName());
			Extinguish();
		}
	}

	// Censer-only, mounted "lasso" swing: continuous circular orbit around the ordinary held offset while
	// mounted AND lit, independent of PerformMeleeSwing()'s own separate knockback trigger. On-foot logic
	// (CheckAuraProximity()'s leg swipe, FootProtectionRadius switch above, etc.) is untouched — this only
	// ever reacts to IsMounted(). Gated on HoldingCharacter.IsValid() — both SetActorRelativeLocation()
	// calls below are relative to this actor's attach parent (the hand/belt socket while held), but a
	// Censer resting on the ground has no attach parent at all, so "relative" location there is really
	// WORLD location — without this guard, a lit-but-dropped Censer got its Transform stomped to
	// BaseOffset in world space every single tick.
	if (ItemType == EHolyItemType::Censer && bIsLit && HoldingCharacter.IsValid())
	{
		const ABaseCharacter* HolderCharacter = Cast<ABaseCharacter>(HoldingCharacter.Get());
		const bool bShouldSwing = IsValid(HolderCharacter) && HolderCharacter->IsMounted();
		const FVector BaseOffset = bInvertProtectionForward ? EquipLocationOffsetLeftHand : EquipLocationOffset;
		const FRotator BaseRotation = bInvertProtectionForward ? EquipRotationOffsetLeftHand : EquipRotationOffset;
		USkeletalMeshComponent* HeldMesh = CachedHeldMesh.Get();

		if (bShouldSwing && !bIsAttachedToSwingSocket && IsValid(HeldMesh))
		{
			// Entering the mounted swing pose — (re-)attach to the dedicated grip socket so ALL of the
			// circular motion comes from the swing animation moving that bone, not from any offset we set
			// here. DoesSocketExist() falls back to the ordinary carry socket if the dedicated grip socket
			// hasn't been created in the Skeleton Tree yet (or the configured name is wrong) — same
			// defensive pattern ABaseMonster::GetMountSocketName() already uses for its own socket name.
			const FName DesiredSwingSocket = bInvertProtectionForward ? SwingSocketNameLeftHand : SwingSocketName;
			const bool bDesiredSwingSocketExists = HeldMesh->DoesSocketExist(DesiredSwingSocket);
			const FName ResolvedSwingSocket = bDesiredSwingSocketExists ? DesiredSwingSocket : CachedHandSocketName;

			// Where the BOWL itself sits — prefer a dedicated socket (placed visually in Persona, exactly
			// like ResolvedSwingSocket above) over the manual SwingItemLocationOffset nudge, so the hang
			// distance is authored against the real mesh instead of eyeballed blind. NAME_None (not created
			// yet) counts as "doesn't exist" here too.
			const FName DesiredItemSocket = bInvertProtectionForward ? SwingItemSocketNameLeftHand : SwingItemSocketName;
			const bool bDesiredItemSocketExists = DesiredItemSocket != NAME_None && HeldMesh->DoesSocketExist(DesiredItemSocket);

			if (bDesiredItemSocketExists)
			{
				// Precise path: snap the bowl straight onto its own dedicated socket, zero extra offset.
				AttachToComponent(HeldMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, DesiredItemSocket);
				SetActorRelativeLocation(FVector::ZeroVector);
				SetActorRelativeRotation(FRotator::ZeroRotator);
			}
			else
			{
				// Fallback path: no dedicated bowl socket yet — hang off the grip socket with a manual nudge.
				// NOT zero — zero would sit the bowl exactly on top of the socket the chain's start is ALSO
				// attached to below, collapsing the visible chain to ~0 length ("folded in half").
				AttachToComponent(HeldMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, ResolvedSwingSocket);
				SetActorRelativeLocation(SwingItemLocationOffset);
				SetActorRelativeRotation(SwingItemRotationOffset);
			}
			AttachChainToSocket(HeldMesh, ResolvedSwingSocket);
			bIsAttachedToSwingSocket = true;
		}
		else if (!bShouldSwing && bIsAttachedToSwingSocket)
		{
			// Dismounting — snap straight back to the ordinary held socket/offset, same mechanism
			// EndTossVisual() already uses to return the item to the hand.
			TrailSpawnTimer = 0.0f;
			if (IsValid(HeldMesh) && CachedHandSocketName != NAME_None)
			{
				AttachToComponent(HeldMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, CachedHandSocketName);
				SetActorRelativeLocation(BaseOffset);
				SetActorRelativeRotation(BaseRotation);
				AttachChainToSocket(HeldMesh, CachedHandSocketName);
			}
			bIsAttachedToSwingSocket = false;
		}

		if (bShouldSwing)
		{
			TrailSpawnTimer += DeltaTime;
			if (IsValid(SwingTrailEffect) && TrailSpawnTimer >= TrailSpawnInterval)
			{
				TrailSpawnTimer = 0.0f;
				UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, SwingTrailEffect, GetActorLocation(), GetActorRotation());
			}
		}
	}

	// Purely visual toss (see BeginTossVisual()/EndTossVisual()) — independent of, and unrelated to, the
	// orbit swing block above; bIsPlayingTossVisual only ever becomes true after BeginTossVisual() has
	// already DetachFromActor()'d this item, so nothing else should be driving its Transform at the same
	// time. Deliberately placed AFTER the orbit block: while a toss is in progress, HoldingCharacter is
	// still valid (BeginTossVisual() never clears it) but this item is detached, so the orbit block's own
	// "not orbiting" branch just wrote a now-meaningless relative offset (see its own comment on relative-
	// location-with-no-parent) — this SetActorLocation() call (an absolute WORLD move, not relative)
	// unconditionally overwrites that with the actual toss position every tick, so the final rendered
	// position is always correct regardless.
	if (bIsPlayingTossVisual)
	{
		TossVisualElapsed += DeltaTime;
		const float Alpha = FMath::Clamp(TossVisualElapsed / TossVisualDuration, 0.0f, 1.0f);
		const AActor* Holder = HoldingCharacter.Get();
		const FVector TargetLocation = IsValid(Holder) ? Holder->GetActorLocation() : TossVisualStartLocation;
		SetActorLocation(FMath::Lerp(TossVisualStartLocation, TargetLocation, Alpha));
	}

	// Also independent of bDrawDebugRadius — this is a permanent atmospheric indicator (own toggle,
	// bShowProtectionBoundary, defaults ON), not the bulky tuning wireframe below (defaults OFF).
	DrawProtectionBoundary();

	if (!bDrawDebugRadius || !IsValid(HolyAuraSphere))
	{
		return;
	}

	// Candle vs. Censer get visually distinct colors so both are unambiguous when testing together.
	// A small LifeTime (rather than the default -1/persistent) makes each frame's sphere replace the
	// last instead of accumulating, while still comfortably outlasting a dropped frame so it never
	// visibly flickers — DrawDebugSphere() itself is a no-op in Shipping builds (ENABLE_DRAW_DEBUG).
	const FColor DebugColor = (ItemType == EHolyItemType::Candle) ? FColor::Orange : FColor::Cyan;
	DrawDebugSphere(GetWorld(), GetActorLocation(), HolyAuraRadius, 32, DebugColor, false, 0.05f, 0, 1.5f);

	// When directional, also draw the actual protection cone (not just the full-radius sphere above) so
	// the cone's facing and half-angle are visible at a glance, in the same color as the sphere. Uses
	// the same forward-vector source as IsLocationInProtectionCone() — the holding character's facing
	// while held, so the drawn cone never disagrees with what's actually being checked.
	if (bUseConeProtection && ProtectionAngle < 360.0f)
	{
		const AActor* HolderActor = HoldingCharacter.Get();
		const FVector HolderOrOwnForward = IsValid(HolderActor) ? HolderActor->GetActorForwardVector() : GetActorForwardVector();
		const FVector ConeForward = bInvertProtectionForward ? (HolderOrOwnForward * -1.0f) : HolderOrOwnForward;
		const float HalfAngleRad = FMath::DegreesToRadians(ProtectionAngle * 0.5f);
		DrawDebugCone(GetWorld(), GetActorLocation(), ConeForward, HolyAuraRadius, HalfAngleRad, HalfAngleRad, 24, DebugColor, false, 0.05f, 0, 1.5f);
	}
}

void AHolyItem::DrawProtectionBoundary() const
{
	if (!bShowProtectionBoundary || !IsValid(GetWorld()))
	{
		return;
	}

	// Projected onto the actual ground beneath this item (a short downward trace), not just drawn at
	// the item's own height — a HELD item sits at hand/chest height, and "on the ground" should mean
	// the floor regardless of whether this item is currently held or resting. Falls back to this item's
	// own location if nothing is hit (e.g. no floor within range). Small upward lift on a hit avoids
	// z-fighting/being embedded in the floor mesh itself.
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	FHitResult GroundHit;
	const bool bFoundGround = GetWorld()->LineTraceSingleByChannel(GroundHit, GetActorLocation(),
		GetActorLocation() - FVector(0.0f, 0.0f, 500.0f), ECC_Visibility, QueryParams);
	const FVector GroundLocation = bFoundGround ? (GroundHit.ImpactPoint + FVector(0.0f, 0.0f, 2.0f)) : GetActorLocation();

	// Small, fixed LifeTime (not persistent) so each frame's line replaces the last rather than
	// accumulating, same reasoning as the bDrawDebugRadius sphere/cone above — comfortably outlasts a
	// dropped frame so it never visibly flickers. Thin (1.0) and non-intrusive by design: a clean tactical
	// indicator line, not a heavy solid shape.
	constexpr int32 NumSegments = 48;
	constexpr float LineThickness = 1.0f;
	constexpr float LineLifeTime = 0.1f;

	if (ItemType == EHolyItemType::Candle)
	{
		// Thin arc along the outer edge of the protection cone — warm orange. Falls back to a full
		// circle if this Candle is configured without cone restriction (bUseConeProtection false, or
		// ProtectionAngle >= 360), same condition the bDrawDebugRadius cone draw above already uses.
		// Same ConeForward source as IsLocationInProtectionCone() (the holding character's facing while
		// held) so this indicator never disagrees with the actual protection check.
		const AActor* HolderActor = HoldingCharacter.Get();
		const FVector HolderOrOwnForward = IsValid(HolderActor) ? HolderActor->GetActorForwardVector() : GetActorForwardVector();
		const FVector ConeForward = bInvertProtectionForward ? (HolderOrOwnForward * -1.0f) : HolderOrOwnForward;

		const FColor CandleBoundaryColor(255, 170, 0);
		if (bUseConeProtection && ProtectionAngle < 360.0f)
		{
			const float HalfAngleRad = FMath::DegreesToRadians(ProtectionAngle * 0.5f);
			DrawDebugCircleArc(GetWorld(), GroundLocation, HolyAuraRadius, ConeForward, HalfAngleRad, NumSegments,
				CandleBoundaryColor, false, LineLifeTime, 0, LineThickness);
		}
		else
		{
			DrawDebugCircle(GetWorld(), GroundLocation, HolyAuraRadius, NumSegments, CandleBoundaryColor, false, LineLifeTime, 0,
				LineThickness, FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), /*bDrawAxis=*/false);
		}
	}
	else
	{
		// Censer: thin full circular outline around the active protective radius — clean golden yellow.
		// YAxis/ZAxis (1,0,0)/(0,1,0) puts the circle flat in the world XY (ground) plane; bDrawAxis
		// false keeps it a clean ring with no extra cross-hair lines through the center.
		const FColor CenserBoundaryColor(255, 210, 40);
		DrawDebugCircle(GetWorld(), GroundLocation, HolyAuraRadius, NumSegments, CenserBoundaryColor, false, LineLifeTime, 0,
			LineThickness, FVector(1.0f, 0.0f, 0.0f), FVector(0.0f, 1.0f, 0.0f), /*bDrawAxis=*/false);
	}
}

void AHolyItem::OnItemHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit)
{
	const FString OtherActorName = OtherActor ? OtherActor->GetName() : TEXT("None");
	UE_LOG(LogTemp, Warning, TEXT("%s hit: %s"), *GetName(), *OtherActorName);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(214, 3.0f, FColor::White,
			FString::Printf(TEXT("%s hit: %s"), *GetName(), *OtherActorName));
	}

	if (bIsThrown)
	{
		// Belt-and-braces for the mounted-throw fix (see Throw()'s IgnoreActorWhenMoving call): if
		// ItemMesh's own ignore-list somehow still lets a hit through against the rider's OWN mount (or
		// any other currently-ridden monster), don't resolve the throw against it at all — no damage,
		// and bIsThrown stays true so the item keeps flying toward whatever the player actually aimed
		// at, instead of being extinguished on the very actor it launched from beside.
		ABaseMonster* HitMonster = Cast<ABaseMonster>(OtherActor);
		// Checked by IDENTITY (== ThrowerMountedMonster) as well as by bIsBeingRidden — two independent
		// signals, either one alone is sufficient to block. Belt-and-braces against the (unlikely, but
		// possible if some other system races the order of operations) case where bIsBeingRidden reads
		// false for a frame right at dismount, while this is still unambiguously the same monster this
		// throw launched from beside.
		const bool bIsThrowersMount = IsValid(HitMonster) && (HitMonster == ThrowerMountedMonster.Get());
		if (IsValid(HitMonster) && (HitMonster->bIsBeingRidden || bIsThrowersMount))
		{
			UE_LOG(LogTemp, Warning, TEXT("%s: ignoring hit against %s (bIsBeingRidden=%s, isThrowersMount=%s) — not consuming throw, no damage applied"),
				*GetName(), *HitMonster->GetName(), HitMonster->bIsBeingRidden ? TEXT("true") : TEXT("false"),
				bIsThrowersMount ? TEXT("true") : TEXT("false"));
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(219, 2.5f, FColor::Yellow,
					FString::Printf(TEXT("MOUNTED-THROW GUARD: ignored hit on %s (ridden=%s mount=%s)"),
						*HitMonster->GetName(), HitMonster->bIsBeingRidden ? TEXT("Y") : TEXT("N"), bIsThrowersMount ? TEXT("Y") : TEXT("N")));
			}
			return;
		}

		// Grace period: a large item's mesh (the Censer especially) can spawn already touching/
		// overlapping the floor at the throw origin, firing an immediate spurious OnHit before real
		// velocity ever carries it away — resolving THAT as the throw's impact would cancel it before
		// it had any chance to reach anything. Ends at whichever of ThrowGracePeriodDuration/
		// ThrowGraceDistance is reached first (see their comments), and NEVER applies to a direct
		// monster hit — a genuinely close-range throw that connects immediately still counts.
		const bool bDirectMonsterHit = IsValid(Cast<ABaseMonster>(OtherActor));
		const bool bWithinGraceWindow = !bDirectMonsterHit
			&& (GetWorld()->GetTimeSeconds() - ThrowStartTime) < ThrowGracePeriodDuration
			&& FVector::DistSquared(GetActorLocation(), ThrowStartLocation) < FMath::Square(ThrowGraceDistance);

		if (bWithinGraceWindow)
		{
			UE_LOG(LogTemp, Warning, TEXT("%s: ignoring grace-period spawn-clip hit against %s (%.2fs, %.0f units from throw origin)"),
				*GetName(), *OtherActorName, GetWorld()->GetTimeSeconds() - ThrowStartTime, FVector::Dist(GetActorLocation(), ThrowStartLocation));
			return;
		}

		// One-shot: cleared immediately so a second impact (e.g. bouncing off a second wall before
		// physics settles) can't re-apply Stun()/Die() or re-fire the hit feedback below.
		bIsThrown = false;

		if (ItemType == EHolyItemType::Candle)
		{
			// AoE, not "only if OtherActor itself is a monster": a thrown Candle's Holy Fire Stun goes
			// off wherever it lands — hitting the ground right next to a group still stuns all of them,
			// not just a monster directly struck. ActorsToIgnore left empty: this item (an AStaticMesh-
			// based actor, not an ABaseMonster) and the thrower never match the ABaseMonster class filter
			// anyway, so there's nothing that needs excluding.
			TArray<AActor*> NearbyActors;
			UKismetSystemLibrary::SphereOverlapActors(this, Hit.ImpactPoint, CandleThrowStunRadius,
				TArray<TEnumAsByte<EObjectTypeQuery>>{ UEngineTypes::ConvertToObjectType(ECC_Pawn) },
				ABaseMonster::StaticClass(), TArray<AActor*>{}, NearbyActors);

			int32 StunnedCount = 0;
			for (AActor* NearbyActor : NearbyActors)
			{
				ABaseMonster* NearbyMonster = Cast<ABaseMonster>(NearbyActor);
				if (IsValid(NearbyMonster) && !NearbyMonster->IsPendingKillPending() && !NearbyMonster->bIsBeingRidden)
				{
					// ThrownStunDuration defaults to 4.0f (see its declaration) rather than a hardcoded
					// literal here, so the design-doc's "4 second stun" stays a tunable per-item value.
					NearbyMonster->Stun(ThrownStunDuration);
					++StunnedCount;
				}
			}

			UE_LOG(LogTemp, Warning, TEXT("Thrown Candle landed at %s — stunned %d monster(s) within %.0f units for %.1fs"),
				*Hit.ImpactPoint.ToCompactString(), StunnedCount, CandleThrowStunRadius, ThrownStunDuration);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(1000, 2.5f, StunnedCount > 0 ? FColor::Green : FColor(128, 128, 128),
					FString::Printf(TEXT("HOLY FIRE STUN: %d target(s)"), StunnedCount));
			}

			// Thrown Candle — всегда безвозвратная потеря (не должна снова стать подбираемой и уйти в карман
			// Приста через Interact()). Уничтожаем сразу, вместо Extinguish()+остаться в мире.
			ThrowerMountedMonster = nullptr;
			Destroy();
			return;
		}
		else // Censer: direct-hit only, damage-based (see ABaseMonster::TakeDamage()'s Censer branch)
		{
			ABaseMonster* Monster = Cast<ABaseMonster>(OtherActor);

			// Explicit cast-result feedback, independent of the liveness check below — this is what
			// tells apart "OtherActor isn't an ABaseMonster at all" from "it is, but it's already dead"
			// at a glance in PIE, instead of both cases just silently producing no damage.
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(215, 3.0f, IsValid(Monster) ? FColor::Green : FColor::Red,
					IsValid(Monster)
						? FString::Printf(TEXT("Monster cast successful: %s"), *Monster->GetName())
						: FString(TEXT("Monster cast failed")));
			}
			UE_LOG(LogTemp, Warning, TEXT("%s"), IsValid(Monster)
				? *FString::Printf(TEXT("Monster cast successful: %s"), *Monster->GetName())
				: TEXT("Monster cast failed"));

			const bool bHitLiveMonster = IsValid(Monster) && !Monster->IsPendingKillPending();

			if (bHitLiveMonster)
			{
				// Monster here can never be a ridden monster / ThrowerMountedMonster — the top-of-block
				// guard above already returned before reaching this point for either case. Logged anyway
				// so a live PIE session can directly confirm that invariant instead of assuming it.
				UE_LOG(LogTemp, Warning, TEXT("Thrown Censer struck %s (bIsBeingRidden=%s, isThrowersMount=%s) - APPLYING KNOCKBACK"),
					*Monster->GetName(), Monster->bIsBeingRidden ? TEXT("true") : TEXT("false"),
					Monster == ThrowerMountedMonster.Get() ? TEXT("true") : TEXT("false"));
				const FVector ThrowPushDirection = GetVelocity().GetSafeNormal2D();
				Monster->ApplyKnockback(ThrowPushDirection, ThrownCenserKnockbackForce, ThrownCenserUpwardForce);

				UE_LOG(LogTemp, Warning, TEXT("Censer knocked back %s!"), *Monster->GetName());
				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(1001, 2.5f, FColor::Red,
						FString::Printf(TEXT("Censer knocked back %s!"), *Monster->GetName()));
				}
			}
			else
			{
				// Fallback, not the primary path: the Censer's mesh is large enough that ItemMesh's own
				// collision can register OnHit against nearby floor/wall geometry a frame before its
				// origin would have reached the monster's capsule — OtherActor reports as that
				// StaticMeshActor/floor even though the throw clearly landed on target. Sphere-overlap
				// the actual impact point for any live ABaseMonster still in range before calling it a
				// miss, so the Censer's size alone doesn't defeat throws that visibly connect.
				TArray<AActor*> NearbyActors;
				UKismetSystemLibrary::SphereOverlapActors(this, Hit.ImpactPoint, CenserThrowAoERadius,
					TArray<TEnumAsByte<EObjectTypeQuery>>{ UEngineTypes::ConvertToObjectType(ECC_Pawn) },
					ABaseMonster::StaticClass(), TArray<AActor*>{}, NearbyActors);

				// Computed once, not per-iteration — this throw only ever has one travel direction, shared
				// by every monster this AoE fallback catches, same as the direct-hit branch above.
				const FVector ThrowPushDirection = GetVelocity().GetSafeNormal2D();

				int32 AoEHitCount = 0;
				for (AActor* NearbyActor : NearbyActors)
				{
					ABaseMonster* NearbyMonster = Cast<ABaseMonster>(NearbyActor);
					// Same double guard (bIsBeingRidden OR identity == ThrowerMountedMonster) as the direct-hit
					// path above — this AoE fallback's sphere-overlap is centered on the impact point, which
					// can land close enough to the rider's own mount to catch it even when the direct hit
					// itself didn't.
					if (!IsValid(NearbyMonster) || NearbyMonster->IsPendingKillPending()
						|| NearbyMonster->bIsBeingRidden || NearbyMonster == ThrowerMountedMonster.Get())
					{
						UE_LOG(LogTemp, Warning, TEXT("%s: Censer AoE fallback skipping %s (ridden=%s isThrowersMount=%s)"),
							*GetName(), IsValid(NearbyMonster) ? *NearbyMonster->GetName() : TEXT("invalid"),
							IsValid(NearbyMonster) && NearbyMonster->bIsBeingRidden ? TEXT("true") : TEXT("false"),
							IsValid(NearbyMonster) && NearbyMonster == ThrowerMountedMonster.Get() ? TEXT("true") : TEXT("false"));
						continue;
					}

					UE_LOG(LogTemp, Warning, TEXT("%s: Censer AoE fallback APPLYING knockback to %s"),
						*GetName(), *NearbyMonster->GetName());
					NearbyMonster->ApplyKnockback(ThrowPushDirection, ThrownCenserKnockbackForce, ThrownCenserUpwardForce);
					++AoEHitCount;

					UE_LOG(LogTemp, Warning, TEXT("Censer AoE knocked back %s!"), *NearbyMonster->GetName());
					if (GEngine)
					{
						GEngine->AddOnScreenDebugMessage(216, 2.5f, FColor::Orange,
							FString::Printf(TEXT("Censer AoE knocked back %s!"), *NearbyMonster->GetName()));
					}
				}

				if (AoEHitCount == 0)
				{
					UE_LOG(LogTemp, Warning, TEXT("Thrown Censer missed — hit %s, no monster within %.0f units of impact either"),
						OtherActor ? *OtherActor->GetName() : TEXT("nothing"), CenserThrowAoERadius);
					if (GEngine)
					{
						GEngine->AddOnScreenDebugMessage(212, 2.5f, FColor(128, 128, 128), TEXT("THROW MISSED!"));
					}
				}
			}
		}

		// Extinguish immediately — light/flame off, aura no longer protects/damages anything it happens
		// to still be touching — but leave the item itself in the world: physics keeps simulating so it
		// settles naturally on the floor/ground wherever it hit, instead of being destroyed, so it can
		// still be picked back up (PickUp() already re-lights it — see PickUp()'s own comment).
		Extinguish();
		ThrowerMountedMonster = nullptr;
		return;
	}

	ApplyItemDamage(OtherActor);
}

void AHolyItem::OnItemBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	UE_LOG(LogTemp, Warning, TEXT("HolyItem (%s) overlapped %s"), *GetName(), OtherActor ? *OtherActor->GetName() : TEXT("None"));
	ApplyItemDamage(OtherActor);
}

void AHolyItem::OnAuraOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// HolyAuraSphere's collision (QueryOnly, Ignore-all-but-Pawn) already limits overlaps to
	// pawns, but that still includes the player (e.g. BP_MyPlayer) — only react to ABaseMonster.
	ABaseMonster* Monster = Cast<ABaseMonster>(OtherActor);
	if (!IsValid(Monster) || Monster->IsPendingKillPending())
	{
		return;
	}

	// A ridden monster ignores EVERY external HolyItem/light source, not just whatever the rider
	// happens to be holding — this is what covers walking near a ground-placed Candle/Censer while
	// mounted. ApplyAuraDamage() already guards this too (belt-and-braces), but checked directly here
	// as well so this function's own intent is explicit and doesn't rely on that internal detail.
	if (Monster->bIsBeingRidden)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("Candle light hit MONSTER: %s"), *Monster->GetName());
	ApplyAuraDamage(Monster);
}

void AHolyItem::ApplyAuraDamage(ABaseMonster* Monster)
{
	// A ridden monster ignores aura damage entirely, from whatever the rider happens to be holding —
	// covers OnAuraOverlap()'s physics-driven path (a held item's HolyAuraSphere can physically overlap
	// the very monster the rider is sitting on top of), independent of CheckAuraProximity()'s own guard.
	if (Monster->bIsBeingRidden)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s::ApplyAuraDamage: BLOCKED aura damage to ridden %s"), *GetName(), *Monster->GetName());
		return;
	}

	// Censer no longer deals ANY passive/ambient damage — its 500-damage hit is strictly a THROWN
	// effect (see OnItemHit()'s bIsThrown branch and CheckCenserFlightPathForMonster()). A held,
	// dropped, or otherwise idle Censer's aura no longer damages a monster that merely gets close to
	// it; the Candle's own passive aura tick below is unaffected and still applies.
	if (ItemType == EHolyItemType::Censer)
	{
		return;
	}

	// Must be UCandleDamageType (not the bare UHolyDamageType/UDamageType) so ABaseMonster::TakeDamage's
	// IsChildOf(UCandleDamageType) check still picks the stun branch in addition to accepting the
	// damage at all.
	UE_LOG(LogTemp, Warning, TEXT("%s::ApplyAuraDamage: APPLYING 1.0 aura damage to %s"), *GetName(), *Monster->GetName());
	UGameplayStatics::ApplyDamage(Monster, 1.0f, GetInstigatorController(), this, UCandleDamageType::StaticClass());
}

void AHolyItem::CheckAuraProximity(ABaseMonster* Monster)
{
	if (!IsValid(Monster) || Monster->IsPendingKillPending() || !IsValid(HolyAuraSphere) || !bIsLit)
	{
		return;
	}

	// A ridden monster is completely immune to whatever the rider happens to be holding — most acutely
	// the Censer's pushback below, since the rider sits directly on top of the very monster it's riding
	// while holding it, putting that monster continuously inside CenserPushbackRadius of its OWN rider's
	// hand. Every 0.1s call was landing an ApplyKnockback() with a near-degenerate (Monster location ==
	// item location, i.e. the rider's hand, are almost the same point) push direction — invisible before
	// bOrientRotationToMovement was enabled for mounted monsters, but visibly reads as rapid on-the-spot
	// spinning once the monster actually turns to face each chaotic knockback velocity.
	if (Monster->bIsBeingRidden)
	{
		return;
	}

	// Held Censer: continuous pushback, not once-per-approach like the damage tracking below — a
	// Chase-state monster's own AI keeps trying to path back in every tick, so this needs to keep
	// shoving every 0.1s (this function's own call cadence, from ABaseMonster::CheckForPlayer()) for as
	// long as it stays inside CenserPushbackRadius, not just once on first entry. IsValid(HoldingCharacter)
	// scopes this strictly to "currently held" — CheckAuraProximity() is itself only ever called for a
	// player's currently-equipped items (see CheckForPlayer()'s call sites), but the check is kept here
	// too as a direct, self-contained guard against the "When Held" part of the spec.
	// On-foot restriction: the passive pushback aura only pushes while the holder is mounted — on foot,
	// a held Censer only shrinks its protection radius (see Tick()'s FootProtectionRadius switch), it
	// never physically shoves anything. Cast<ABaseCharacter> (not just IsValid(HoldingCharacter.Get()))
	// so this can actually ask IsMounted().
	const ABaseCharacter* AuraHolderCharacter = Cast<ABaseCharacter>(HoldingCharacter.Get());
	if (ItemType == EHolyItemType::Censer && IsValid(AuraHolderCharacter) && AuraHolderCharacter->IsMounted())
	{
		const float PushbackRadiusSq = FMath::Square(CenserPushbackRadius);
		if (FVector::DistSquared(GetActorLocation(), Monster->GetActorLocation()) <= PushbackRadiusSq)
		{
			const FVector PushDir = (Monster->GetActorLocation() - GetActorLocation()).GetSafeNormal2D();
			Monster->ApplyKnockback(PushDir, CenserPushbackForce, CenserPushbackUpwardForce);
		}
	}

	// On-foot "leg swipe": a monster lingering inside the reduced foot-protection radius (see Tick()'s
	// FootProtectionRadius switch) occasionally reaches in past the reduced light and grazes the holder —
	// a light, non-panicking annoyance (mild knockback + fuel spill, occasional extinguish), NOT a real
	// hit; the actual attack is still fully blocked (see ABaseMonster::OnCapsuleHit()'s IsTargetInLight()
	// guard for Naskok). Gated to Censer + on-foot + lit, same scoping as the mounted pushback branch above.
	if (ItemType == EHolyItemType::Censer && IsValid(AuraHolderCharacter) && !AuraHolderCharacter->IsMounted() && bIsLit
		&& !GetWorldTimerManager().IsTimerActive(LegSwipeCooldownTimerHandle))
	{
		const float FootRadiusSq = FMath::Square(HolyAuraSphere->GetScaledSphereRadius());
		if (FVector::DistSquared(GetActorLocation(), Monster->GetActorLocation()) <= FootRadiusSq)
		{
			// Cooldown is armed unconditionally the moment a roll is attempted — regardless of whether the
			// roll below actually connects — so a lingering monster can only try again every
			// LegSwipeCooldown seconds instead of every 0.1s CheckAuraProximity() tick.
			GetWorldTimerManager().SetTimer(LegSwipeCooldownTimerHandle, LegSwipeCooldown, false);

			if (FMath::FRand() <= LegSwipeChance)
			{
				if (ACharacter* HolderCharacter = Cast<ACharacter>(HoldingCharacter.Get()))
				{
					const FVector PushDirection = (HolderCharacter->GetActorLocation() - Monster->GetActorLocation()).GetSafeNormal2D();
					HolderCharacter->LaunchCharacter(PushDirection * LegSwipeKnockbackForce, false, false);
				}

				FuelRemaining = FMath::Max(0.0f, FuelRemaining - LegSwipeFuelSpill);

				// Fuel hitting 0 extinguishes unconditionally, independent of the random roll — LegSwipeExtinguishChance
				// only governs the "still has fuel" case; an empty tank was never meant to keep burning.
				if (FuelRemaining <= 0.0f || FMath::FRand() <= LegSwipeExtinguishChance)
				{
					// Set BEFORE Extinguish() so ABaseMonster::IsProtectedByHeldItem() sees the awaiting-
					// relight flag already true the instant bIsLit goes false — no frame where the item
					// reads as a genuinely, unprotected-ly extinguished item.
					// Only actually "awaiting relight" if there's fuel left to relight from — a genuinely
					// empty tank gets no artificial protection window, same as running out via Tick()'s
					// normal burn-out.
					bIsAwaitingLegSwipeRelight = (FuelRemaining > 0.0f);
					Extinguish();
				}

				if (!bIsLit && FuelRemaining > 0.0f)
				{
					GetWorldTimerManager().SetTimer(LegSwipeRelightTimerHandle, this, &AHolyItem::LightUp, LegSwipeRelightDelay, false);
				}
			}
		}
	}

	const float RadiusSq = FMath::Square(HolyAuraSphere->GetScaledSphereRadius());
	const bool bInRange = FVector::DistSquared(GetActorLocation(), Monster->GetActorLocation()) <= RadiusSq;

	const TWeakObjectPtr<ABaseMonster> WeakMonster(Monster);
	const bool bWasInRange = MonstersInAuraRange.Contains(WeakMonster);

	if (bInRange && !bWasInRange)
	{
		MonstersInAuraRange.Add(WeakMonster);
		ApplyAuraDamage(Monster);
	}
	else if (!bInRange && bWasInRange)
	{
		MonstersInAuraRange.Remove(WeakMonster);
	}
}

bool AHolyItem::IsLocationInProtectionCone(FVector TargetLocation) const
{
	if (!bUseConeProtection || ProtectionAngle >= 360.0f)
	{
		// Full sphere: no directional restriction, so every location counts as within the cone.
		return true;
	}

	const FVector DirectionToTarget = (TargetLocation - GetActorLocation()).GetSafeNormal();
	if (DirectionToTarget.IsNearlyZero())
	{
		// TargetLocation is (almost) exactly at the item's own location — no meaningful direction to
		// test against, so don't let a degenerate case fail the check.
		return true;
	}

	// While held, face the cone using the holding character's forward vector, not the item's own — a
	// physics-enabled item (e.g. a Candle) can tip/tumble freely in the player's hand, which would
	// otherwise swing the cone up at the ceiling or down at the floor instead of wherever the player is
	// actually looking. Falls back to the item's own forward vector when not held (e.g. dropped on the
	// ground, where HoldingCharacter is unset and there's no holder to ask).
	const AActor* HolderActor = HoldingCharacter.Get();
	const FVector HolderOrOwnForward = IsValid(HolderActor) ? HolderActor->GetActorForwardVector() : GetActorForwardVector();

	// Second-candle case: the hold animation still poses this item forward like the primary hand, so
	// the actual protection direction is flipped 180 degrees to face behind the player instead (see
	// bInvertProtectionForward's comment). Combined with IsLocationInProtectionCone() being checked
	// per-hand (ABaseMonster::IsLocationProtected()), this is what lets a monster approaching from
	// behind be caught by the off-hand candle's rear cone even while the primary candle's forward cone
	// alone wouldn't cover it. Written as an explicit *-1.0f (rather than unary negation) and NOT run
	// through FMath::Abs anywhere in this function, so the sign flip actually reaches the DotProduct
	// below instead of being discarded.
	const FVector ForwardVector = bInvertProtectionForward ? (HolderOrOwnForward * -1.0f) : HolderOrOwnForward;

	const float AngleToTargetDegrees = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(ForwardVector, DirectionToTarget)));
	const bool bInCone = AngleToTargetDegrees <= (ProtectionAngle * 0.5f);

	UE_LOG(LogTemp, Warning, TEXT("%s::IsLocationInProtectionCone: bInvertProtectionForward=%s HolderForward=%s UsedDirection=%s AngleToTarget=%.1f (limit %.1f) -> %s"),
		*GetName(), bInvertProtectionForward ? TEXT("true") : TEXT("false"),
		*HolderOrOwnForward.ToCompactString(), *ForwardVector.ToCompactString(),
		AngleToTargetDegrees, ProtectionAngle * 0.5f, bInCone ? TEXT("PROTECTED") : TEXT("unprotected"));

	return bInCone;
}

void AHolyItem::PickUp(USkeletalMeshComponent* AttachToMesh, FName SocketName, bool bInvertForward)
{
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(213, 3.f, FColor::Cyan, FString::Printf(TEXT("PickUp called for socket: %s"), *SocketName.ToString()));
	}

	if (!IsValid(AttachToMesh))
	{
		return;
	}

	bInvertProtectionForward = bInvertForward;
	UE_LOG(LogTemp, Warning, TEXT("%s::PickUp: socket=%s bInvertForward param=%s -> bInvertProtectionForward now %s"),
		*GetName(), *SocketName.ToString(), bInvertForward ? TEXT("true") : TEXT("false"),
		bInvertProtectionForward ? TEXT("true") : TEXT("false"));

	// Physics must be off before attaching — a simulating body can't be snapped to a parent's socket
	// transform each frame. Collision itself, though, is now QueryOnly + Block-vs-Pawn (not NoCollision):
	// a held/carried item must physically hit a monster it's swung or walked into (OnComponentHit against
	// a Block response), not just affect it via HolyAuraSphere's overlap/CheckAuraProximity(). QueryOnly
	// (not QueryAndPhysics) because ItemMesh still isn't simulating while held — it registers hits from a
	// MONSTER's own sweep without physics forces being able to shove the item off the hand socket itself.
	// The self-collision guard below (holder capsule ignoring `this`) is what stops this from colliding
	// with the CARRIER'S own capsule.
	if (IsValid(ItemMesh))
	{
		ItemMesh->SetSimulatePhysics(false);
		ItemMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		ItemMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	}

	// Only ItemMesh's own collision is scoped down above — HolyAuraSphere and LightComponent must stay
	// fully active while the item is held, otherwise a held Candle/Censer would stop protecting the
	// player at all. (CheckAuraProximity(), called every 0.1s from ABaseMonster::CheckForPlayer(), is
	// the actual fix for monsters no longer reacting to a held item — physics overlaps on a component
	// riding an animated hand-socket bone aren't guaranteed to re-evaluate every frame — but this
	// reassertion stays too as insurance against future regressions here.) Also re-lights a spent item
	// (see Extinguish()) and refuels it if empty — see LightUp()'s own comment.
	LightUp();

	// Resolve the actual attach point before attaching — AttachToComponent() silently accepts an unknown
	// SocketName and just snaps to AttachToMesh's own origin instead (that origin sits at the character's
	// feet/capsule-base after ABaseCharacter's mesh alignment, which is exactly the "item stuck at Z=0"
	// bug this resolves). DoesSocketExist() ALONE isn't enough to detect that ahead of time: it only
	// checks the skeleton's named USkeletalMeshSocket entries, not plain bone names — and SocketName here
	// is usually a BONE name ("hand_r"/"hand_l"), not a custom socket (see RightHandSocket's own comment),
	// so this checks both.
	auto HasAttachPoint = [](const USkeletalMeshComponent* Mesh, FName Name)
	{
		return Mesh->DoesSocketExist(Name) || Mesh->GetBoneIndex(Name) != INDEX_NONE;
	};

	// Single, deliberately generic fallback bone — every mesh actually used by a Blueprint hero in this
	// project (SKM_Manny_Simple/SKM_Quinn_Simple, and the legacy SK_Mannequin) has a "hand_r" bone, so
	// this only ever matters if SocketName itself was some other, missing name (e.g. a Blueprint override
	// typo'd back to the old "Socket_Hand_R").
	static const FName FallbackHandBoneName(TEXT("hand_r"));

	FName ResolvedSocketName = SocketName;
	if (!HasAttachPoint(AttachToMesh, ResolvedSocketName))
	{
		if (HasAttachPoint(AttachToMesh, FallbackHandBoneName))
		{
			UE_LOG(LogTemp, Warning, TEXT("%s::PickUp: \"%s\" is not a socket or bone on %s — falling back to \"%s\""),
				*GetName(), *SocketName.ToString(), *AttachToMesh->GetName(), *FallbackHandBoneName.ToString());
			ResolvedSocketName = FallbackHandBoneName;
		}
		else
		{
			// Neither the requested name nor the generic fallback bone exists on this mesh at all (e.g. a
			// non-humanoid rig) — NAME_None tells the branch below to skip straight to the manual
			// hand-height offset instead of snapping onto a name AttachToComponent() would just silently
			// fail to resolve anyway.
			UE_LOG(LogTemp, Error, TEXT("%s::PickUp: neither \"%s\" nor fallback \"%s\" exist on %s — attaching to mesh origin with a manual hand-height offset instead"),
				*GetName(), *SocketName.ToString(), *FallbackHandBoneName.ToString(), *AttachToMesh->GetName());
			ResolvedSocketName = NAME_None;
		}
	}

	if (ResolvedSocketName != NAME_None)
	{
		AttachToComponent(AttachToMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, ResolvedSocketName);

		// SnapToTarget above zeroes this item's relative location/rotation to the socket exactly — since
		// ItemMesh's pivot is centered, that leaves half the mesh sticking out the back of the hand toward
		// the wrist/spine. EquipLocationOffset/EquipRotationOffset (both expressed in the socket/bone's own
		// local space, same as the fallback branch's manual offset below) nudge it forward into the palm
		// and align its long axis with the hand/arm direction instead — see their own header comments.
		//
		// bInvertForward selects the LEFT-hand pair instead — see EquipLocationOffsetLeftHand's own comment
		// on why hand_l needs genuinely separate values rather than a mirrored formula applied to the
		// right-hand ones (this project's actual skeleton doesn't mirror hand_r/hand_l as a clean single-
		// axis reflection).
		if (bInvertForward)
		{
			SetActorRelativeLocation(EquipLocationOffsetLeftHand);
			SetActorRelativeRotation(EquipRotationOffsetLeftHand);
		}
		else
		{
			SetActorRelativeLocation(EquipLocationOffset);
			SetActorRelativeRotation(EquipRotationOffset);
		}
	}
	else
	{
		// Last-resort fallback: attach to the mesh component itself (not the RootComponent/capsule, which
		// sits at the actor's feet) and nudge up to roughly hand height manually, so a held item is at
		// least in the right neighborhood instead of pinned at the ground.
		AttachToComponent(AttachToMesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, NAME_None);
		SetActorRelativeLocation(FVector(0.0f, 0.0f, 130.0f));
	}

	// Remember who's holding the item so DropItem() can ignore this specific character's capsule
	// when physics turns back on.
	HoldingCharacter = AttachToMesh->GetOwner();

	// The item is a SEPARATE actor from its holder — AttachToComponent() doesn't change ownership — so
	// the holder's own capsule sweep does NOT automatically ignore this now-Block-vs-Pawn held item the
	// way it would ignore its own components. Without this, the player's own capsule could be shoved/
	// blocked by the item riding their own hand — the same "fighting its own collision" problem
	// MountNearbyMonster() already solves for player-vs-mount. IgnoreActorWhenMoving() is called on the
	// CAPSULE (the component that actually performs the sweep), not on ItemMesh, per its own doc: it only
	// ignores actors the component itself moves/sweeps INTO, not the reverse — ItemMesh, riding a socket,
	// never itself performs a swept move while held. Cleared (capsule stops ignoring `this`) once the item
	// leaves this holder's hand — see DropItem()/Throw()'s matching IgnoreActorWhenMoving(..., false).
	if (const ACharacter* HolderCharacter = Cast<ACharacter>(HoldingCharacter.Get()))
	{
		if (UCapsuleComponent* HolderCapsule = HolderCharacter->GetCapsuleComponent())
		{
			HolderCapsule->IgnoreActorWhenMoving(this, true);
		}
	}

	// Collision is now Query-enabled above (not NoCollision), which reactivates ItemMesh's default
	// response to EVERY channel, not just the ECC_Pawn Block we set explicitly — so these need
	// overriding explicitly too, same reasoning HolyAuraSphere's constructor already documents:
	// - ECC_Visibility: the Interact() line trace runs on this channel specifically — ignoring it here
	//   is what stops a held item from blocking the trace meant to find a second item to pick up.
	// - ECC_Camera: USpringArmComponent's own collision-probe sweep defaults to this channel. Without
	//   ignoring it, ItemMesh's default channel response (Block, same "BlockAll"-style default that
	//   already blocks ECC_Pawn/ECC_WorldDynamic per Throw()'s own comment) pulls the camera boom in
	//   against the held item the instant it's equipped — the camera-clipping/"seeing your own neck" bug.
	if (IsValid(ItemMesh))
	{
		ItemMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		ItemMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}

	// While HELD, NavAvoidanceSphere must NOT affect navigation: it rides the hand socket everywhere
	// the player moves, so a nav-relevant obstacle here forms a moving ring around the player rather
	// than a fixed prop — and with a second item in the other hand, the two rings overlap into a
	// ~360-degree high-cost zone that seals the player off from every angle, freezing the monster
	// entirely. That defeats the actual design: a monster is meant to be kept out ONLY by the dynamic
	// light-cone/radius checks (ABaseMonster::IsTargetInLight()/MoveToLightBorder()), which already
	// let it flank to an unprotected side — the NavMesh was never supposed to be what blocks it.
	// DropItem()/Throw() turn this back on once the item is a stationary/settled world prop again,
	// where a normal small avoidance ring is the intended, harmless behavior.
	if (IsValid(NavAvoidanceSphere))
	{
		NavAvoidanceSphere->SetCanEverAffectNavigation(false);
	}

	// LightNavModifier needs no extra handling here for ORDINARY held-item gameplay: UNavModifierComponent
	// (see NavModifierComponent.h) binds directly to its owner's RootComponent (ItemMesh)
	// TransformUpdated delegate in OnRegister(), so it keeps following automatically as the item's
	// RootComponent moves with the hand socket. That's exactly the problem while MOUNTED though — see
	// DisableAllPhysicsAndCollision(), which explicitly silences it for that case.

	// Censer-only: remember which hand this item was actually picked up into (ResolvedSocketName, not the
	// raw SocketName parameter — the fallback logic above may have substituted it) so Extinguish()/
	// LightUp() can swap it between this hand and BeltSocketName later without needing to know which hand
	// that was ahead of time, and hook up the visible chain to match.
	if (ItemType == EHolyItemType::Censer)
	{
		CachedHeldMesh = AttachToMesh;
		CachedHandSocketName = ResolvedSocketName;
		AttachChainToSocket(AttachToMesh, ResolvedSocketName);
		bIsAttachedToSwingSocket = false;
	}
}

void AHolyItem::DisableAllPhysicsAndCollision()
{
	TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
	GetComponents(PrimitiveComponents);
	for (UPrimitiveComponent* Component : PrimitiveComponents)
	{
		if (IsValid(Component))
		{
			Component->SetSimulatePhysics(false);
			Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}

	// The actual root cause of the mounted spinning: LightNavModifier (AreaClass=UNavArea_Obstacle)
	// keeps following ItemMesh's transform and marking a high-pathing-cost NavMesh region around it
	// continuously while held (see PickUp()'s comment) — completely independent of collision/physics,
	// so nothing above touches it. While mounted, the item rides right on/next to the very monster
	// carrying it, so that monster ends up sitting inside (or immediately next to) an obstacle region
	// generated by its OWN rider — a co-located, inescapable dynamic obstacle CharacterMovement's own
	// pathing/avoidance keeps trying to steer clear of every tick, which is what actually produced the
	// violent on-the-spot spin (independent of, and in addition to, all the stun/knockback/flee sources
	// already guarded elsewhere). ItemMesh itself has never had SetCanEverAffectNavigation(false) called
	// on it anywhere (unlike NavAvoidanceSphere/HolyAuraSphere, which PickUp() already scopes down) —
	// its default is true, so it was always contributing bounds to LightNavModifier regardless of held/
	// mounted state. SetNavigationRelevancy(false) (UNavRelevantComponent's actual public API for this —
	// UNavModifierComponent hides its inherited Activate()/Deactivate() via hidecategories=(Activation),
	// meaning that's not the intended toggle for this class) is what actually stops LightNavModifier
	// from contributing to the NavMesh at all, not merely narrowing its bounds.
	if (IsValid(ItemMesh))
	{
		ItemMesh->SetCanEverAffectNavigation(false);
	}
	if (IsValid(NavAvoidanceSphere))
	{
		NavAvoidanceSphere->SetCanEverAffectNavigation(false);
	}
	if (IsValid(LightNavModifier))
	{
		LightNavModifier->SetNavigationRelevancy(false);
	}
}

void AHolyItem::RestoreHeldPhysicsState()
{
	// Matches exactly what PickUp() itself establishes for a held item — not some other/default state,
	// since a held item should never have SIMULATED PHYSICS regardless of mount state, but (per PickUp()'s
	// own comment) DOES need QueryOnly + Block-vs-Pawn collision so it can physically hit a monster again
	// once dismounted, same as it could before mounting forced DisableAllPhysicsAndCollision()'s NoCollision.
	if (IsValid(ItemMesh))
	{
		ItemMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		ItemMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);

		// Explicitly reasserted (not just relying on PickUp() having set these once already and
		// DisableAllPhysicsAndCollision() never having touched per-channel responses, only
		// CollisionEnabled) — same belt-and-braces reasoning as this class's other re-applied settings.
		// ECC_Visibility: keeps the item from blocking Interact()'s pickup line trace. ECC_Camera: keeps
		// the spring arm's own collision probe (which defaults to this channel) from pulling the camera
		// in against the item the instant collision comes back on — see PickUp()'s matching comment for
		// the camera-clipping bug this prevents.
		ItemMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		ItemMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

		// Restored to true (ItemMesh's own baseline — this was never scoped down anywhere before mount
		// time), unlike NavAvoidanceSphere below, which PickUp() deliberately keeps false for as long as
		// the item stays held, mounted or not.
		ItemMesh->SetCanEverAffectNavigation(true);
	}
	if (IsValid(HolyAuraSphere))
	{
		HolyAuraSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
	if (IsValid(NavAvoidanceSphere))
	{
		NavAvoidanceSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
	if (IsValid(LightNavModifier))
	{
		LightNavModifier->SetNavigationRelevancy(true);
	}
}

void AHolyItem::DropItem(FVector DropLocation, FRotator DropRotation)
{
	if (!IsValid(this) || IsPendingKillPending())
	{
		return;
	}

	AActor* PreviousHolder = HoldingCharacter.Get();

	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	// Desired drop point: forward along the drop rotation's forward vector (and up slightly) so
	// ItemMesh's collision shape doesn't spawn embedded in the character's capsule — a deep overlap
	// at the instant physics turns on was what launched dropped items away hard enough to clip
	// straight through the floor.
	const FVector DesiredDropLocation = DropLocation + (DropRotation.Vector() * 70.0f) + FVector(0.0f, 0.0f, 15.0f);

	// Near a wall/corner/furniture, that blind forward offset can itself land inside solid geometry —
	// sweep a small sphere from the holder's eye/chest point toward DesiredDropLocation to catch that,
	// and if something blocks it, back off from the hit surface along its impact normal instead.
	FVector SafeDropLocation = DesiredDropLocation;
	if (IsValid(PreviousHolder))
	{
		FVector TraceStart = PreviousHolder->GetActorLocation() + FVector(0.0f, 0.0f, 40.0f);
		if (const APawn* HolderPawn = Cast<APawn>(PreviousHolder))
		{
			FRotator UnusedEyesRotation;
			HolderPawn->GetActorEyesViewPoint(TraceStart, UnusedEyesRotation);
		}

		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(PreviousHolder);
		QueryParams.AddIgnoredActor(this);

		FHitResult WallHit;
		const bool bBlocked = GetWorld()->SweepSingleByChannel(WallHit, TraceStart, DesiredDropLocation, FQuat::Identity,
			ECC_Visibility, FCollisionShape::MakeSphere(15.0f), QueryParams);

		if (bBlocked)
		{
			SafeDropLocation = WallHit.Location + (WallHit.ImpactNormal * 15.0f);
		}
	}

	// bSweep=false + TeleportPhysics: SafeDropLocation is already resolved clear of nearby geometry
	// above, so this is a deliberate relocation, not a physical move that should be blocked or swept.
	SetActorLocationAndRotation(SafeDropLocation, DropRotation, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

	if (IsValid(ItemMesh))
	{
		ItemMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		ItemMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		ItemMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);

		// Caps how hard the physics engine can shove ItemMesh out of any residual penetration (e.g. a
		// tight corner the sweep above couldn't fully clear) — without this, deep penetration can
		// launch the item hard enough to clip straight through the floor in a single tick.
		ItemMesh->SetMaxDepenetrationVelocity(NAME_None, 100.0f);

		// Belt-and-braces alongside the safe-location resolution above: while physics first settles,
		// ignore the character that just dropped it so any residual overlap can't add a launch impulse.
		if (IsValid(PreviousHolder))
		{
			ItemMesh->IgnoreActorWhenMoving(PreviousHolder, true);
		}

		ItemMesh->SetSimulatePhysics(true);
	}

	// Restore normal collision with the ex-holder once the item has settled, so it doesn't stay
	// permanently walk-through-able for whoever dropped it. Also clears the ex-holder's CAPSULE-side
	// ignore of `this` that PickUp() set (see its comment) — the dropped item is a normal collidable
	// world prop again, so the ex-holder's own movement should be able to bump into it like anything else.
	if (IsValid(PreviousHolder) && IsValid(ItemMesh))
	{
		TWeakObjectPtr<UStaticMeshComponent> WeakMesh(ItemMesh);
		TWeakObjectPtr<AActor> WeakHolder(PreviousHolder);
		TWeakObjectPtr<AHolyItem> WeakThis(this);
		TWeakObjectPtr<UCapsuleComponent> WeakHolderCapsule;
		if (const ACharacter* PreviousHolderCharacterForCapsule = Cast<ACharacter>(PreviousHolder))
		{
			WeakHolderCapsule = PreviousHolderCharacterForCapsule->GetCapsuleComponent();
		}
		GetWorldTimerManager().SetTimer(ReenableHolderCollisionTimerHandle, [WeakMesh, WeakHolder, WeakThis, WeakHolderCapsule]()
		{
			if (WeakMesh.IsValid() && WeakHolder.IsValid())
			{
				WeakMesh->IgnoreActorWhenMoving(WeakHolder.Get(), false);
			}
			// Capsule stops ignoring the ITEM actor (`this`) here — the reverse of what PickUp() set
			// (HolderCapsule->IgnoreActorWhenMoving(this, true)) — not the holder itself.
			if (WeakHolderCapsule.IsValid() && WeakThis.IsValid())
			{
				WeakHolderCapsule->IgnoreActorWhenMoving(WeakThis.Get(), false);
			}
		}, 0.5f, false);
	}

	// No longer riding a moving hand — back to being a normal stationary world prop, so its small
	// avoidance ring (see PickUp()'s comment on why this is off while held) is the intended behavior
	// again.
	if (IsValid(NavAvoidanceSphere))
	{
		NavAvoidanceSphere->SetCanEverAffectNavigation(true);
	}

	HoldingCharacter = nullptr;
}

void AHolyItem::Throw(FVector ThrowLocation, FVector LaunchVelocity)
{
	if (!IsValid(this) || IsPendingKillPending())
	{
		return;
	}

	AActor* PreviousHolder = HoldingCharacter.Get();

	// Resolved before the ItemMesh block below (but used both there AND further down, in the re-enable
	// timer), so it's declared at this scope rather than nested inside that if. See Throw()'s mounted-
	// throw fix comment further down for why this matters at all.
	const ABaseCharacter* PreviousHolderCharacter = Cast<ABaseCharacter>(PreviousHolder);
	ABaseMonster* PreviousHolderMount = (IsValid(PreviousHolderCharacter) && PreviousHolderCharacter->IsMounted())
		? PreviousHolderCharacter->GetMountedMonster()
		: nullptr;
	ThrowerMountedMonster = PreviousHolderMount;

	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	// bSweep=false + TeleportPhysics: ThrowLocation is the caller's already-resolved launch point
	// (e.g. ABaseCharacter::ThrowItem() pushes it out past the thrower's own eyes/capsule), so this is
	// a deliberate relocation, not a physical move that should be blocked or swept.
	SetActorLocation(ThrowLocation, /*bSweep=*/false, nullptr, ETeleportType::TeleportPhysics);

	if (IsValid(ItemMesh))
	{
		ItemMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		ItemMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

		// Explicit even though a monster's capsule already blocks WorldDynamic (ItemMesh's object type)
		// by engine default — belt-and-braces so a thrown item's OnComponentHit against ABaseMonster
		// can never silently stop firing just because something upstream (e.g. a Blueprint override)
		// narrows ItemMesh's own collision responses without this channel specifically in mind.
		ItemMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);

		// Same reasoning as DropItem(): ignore the thrower briefly so the item doesn't immediately
		// register a spurious hit against (or get shoved by depenetration from) the capsule it's
		// being launched from right next to.
		if (IsValid(PreviousHolder))
		{
			ItemMesh->IgnoreActorWhenMoving(PreviousHolder, true);
		}

		// Mounted-throw fix: the hand socket a throw launches from sits directly on top of the rider's
		// OWN mount, so without this, ItemMesh's very first physics tick registers a direct
		// OnComponentHit against that mount — resolving (and wasting) the throw's one-shot impact there
		// instead of carrying through to whatever the player actually aimed at. Ignoring the mount here
		// (same IgnoreActorWhenMoving mechanism used for PreviousHolder above, since ItemMesh is a
		// SimulatePhysics body, not a raycast/sweep query — FCollisionQueryParams::AddIgnoredActor()
		// doesn't apply here) makes the item pass straight through its own mount instead of colliding
		// with it at all. OnItemHit()/CheckCenserFlightPathForMonster() also guard against
		// ThrowerMountedMonster directly, belt-and-braces, in case this still somehow lets a hit through.
		if (IsValid(PreviousHolderMount))
		{
			ItemMesh->IgnoreActorWhenMoving(PreviousHolderMount, true);
			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(221, 2.5f, FColor::Yellow,
					FString::Printf(TEXT("Throw() while mounted: ignoring %s during flight"), *PreviousHolderMount->GetName()));
			}
		}

		ItemMesh->SetSimulatePhysics(true);

		// Set outright rather than an impulse, so the launch speed is exact and independent of the
		// item mesh's mass — matches the "configurable initial speed" the throw is specified in terms
		// of (ABaseCharacter::ThrowSpeed), not a force.
		ItemMesh->SetPhysicsLinearVelocity(LaunchVelocity);
	}

	// Marks this as a live throw so the next OnItemHit() applies one-shot Stun()/Die() + hit-feedback
	// behavior instead of ordinary ApplyItemDamage() — see bIsThrown's header comment.
	bIsThrown = true;

	// Seeds OnItemHit()'s grace-period check and CheckCenserFlightPathForMonster()'s first travel
	// segment — both measured from exactly here, the throw's actual start point/time.
	ThrowStartTime = GetWorld()->GetTimeSeconds();
	ThrowStartLocation = ThrowLocation;
	LastFlightTraceLocation = ThrowLocation;

	// No longer riding a moving hand — see PickUp()'s comment on why this is off while held; a thrown
	// item flies free and then settles as a normal stationary prop, same as a dropped one.
	if (IsValid(NavAvoidanceSphere))
	{
		NavAvoidanceSphere->SetCanEverAffectNavigation(true);
	}

	// Same as DropItem(): the item stays in the world after impact (extinguished, not destroyed — see
	// OnItemHit()), so it needs the same "restore normal collision with the thrower once it's settled"
	// as a dropped item, rather than permanently ignoring them. Also restores collision with
	// PreviousHolderMount (if any), for the same reason — see the mounted-throw fix above. And, same as
	// DropItem(), clears the THROWER's own capsule-side ignore of `this` that PickUp() set.
	if ((IsValid(PreviousHolder) || IsValid(PreviousHolderMount)) && IsValid(ItemMesh))
	{
		TWeakObjectPtr<UStaticMeshComponent> WeakMesh(ItemMesh);
		TWeakObjectPtr<AActor> WeakHolder(PreviousHolder);
		TWeakObjectPtr<AActor> WeakMount(PreviousHolderMount);
		TWeakObjectPtr<AHolyItem> WeakThis(this);
		TWeakObjectPtr<UCapsuleComponent> WeakHolderCapsule;
		if (const ACharacter* PreviousHolderCharacterForCapsule = Cast<ACharacter>(PreviousHolder))
		{
			WeakHolderCapsule = PreviousHolderCharacterForCapsule->GetCapsuleComponent();
		}
		GetWorldTimerManager().SetTimer(ReenableHolderCollisionTimerHandle, [WeakMesh, WeakHolder, WeakMount, WeakThis, WeakHolderCapsule]()
		{
			if (WeakMesh.IsValid() && WeakHolder.IsValid())
			{
				WeakMesh->IgnoreActorWhenMoving(WeakHolder.Get(), false);
			}
			if (WeakMesh.IsValid() && WeakMount.IsValid())
			{
				WeakMesh->IgnoreActorWhenMoving(WeakMount.Get(), false);
			}
			if (WeakHolderCapsule.IsValid() && WeakThis.IsValid())
			{
				WeakHolderCapsule->IgnoreActorWhenMoving(WeakThis.Get(), false);
			}
		}, 0.5f, false);
	}

	HoldingCharacter = nullptr;
}

void AHolyItem::Extinguish()
{
	bIsLit = false;

	if (IsValid(LightComponent))
	{
		LightComponent->SetVisibility(false);
	}
	if (IsValid(HolyAuraSphere))
	{
		HolyAuraSphere->SetGenerateOverlapEvents(false);
		HolyAuraSphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (IsValid(FlameParticleComponent))
	{
		// DeactivateImmediate() (not just Deactivate()) cuts already-emitted particles right now instead
		// of letting them finish their natural lifetime while the system merely stops emitting new ones —
		// SetVisibility(false) as belt-and-braces in case any residual particles would otherwise still
		// render for a frame.
		FlameParticleComponent->DeactivateImmediate();
		FlameParticleComponent->SetVisibility(false);
	}

	UE_LOG(LogTemp, Warning, TEXT("%s::Extinguish"), *GetName());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(224, 2.5f, FColor(128, 128, 128), FString::Printf(TEXT("%s extinguished"), *GetName()));
	}

	// Censer-only: move from the hand to the belt/shoulder while put out — only if this item is actually
	// currently held (CachedHeldMesh is only ever set by PickUp()); a Censer resting on the ground or
	// mid-throw has nothing to hang from and stays exactly where it is.
	if (ItemType == EHolyItemType::Censer && HoldingCharacter.IsValid() && CachedHeldMesh.IsValid())
	{
		AttachToComponent(CachedHeldMesh.Get(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, BeltSocketName);
		SetActorRelativeLocation(BeltEquipLocationOffset);
		SetActorRelativeRotation(BeltEquipRotationOffset);
		AttachChainToSocket(CachedHeldMesh.Get(), BeltSocketName);

		// Moved off whatever socket Tick()'s swing logic had us on (if any) — see bIsAttachedToSwingSocket's
		// own comment on why this must be reset here rather than left for Tick() to notice on its own.
		bIsAttachedToSwingSocket = false;
	}
}

void AHolyItem::LightUp()
{
	// Reset unconditionally, first thing — every path that lights (or attempts to light) this item goes
	// through here (pickup, R refill, the holy explosion's own reset, and this leg-swipe relight timer
	// itself), so this is the single point that closes the IsProtectedByHeldItem() grace window, whether
	// or not lighting actually succeeds below (see the FuelRemaining <= 0 early-out just after this).
	bIsAwaitingLegSwipeRelight = false;

	if (FuelRemaining <= 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s::LightUp: no fuel remaining — not lighting"), *GetName());
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(226, 2.5f, FColor(128, 128, 128), FString::Printf(TEXT("%s: out of fuel — won't light"), *GetName()));
		}
		return;
	}

	if (IsValid(HolyAuraSphere))
	{
		HolyAuraSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		HolyAuraSphere->SetGenerateOverlapEvents(true);
	}
	if (IsValid(LightComponent))
	{
		LightComponent->SetVisibility(true);
	}
	if (IsValid(FlameParticleComponent))
	{
		FlameParticleComponent->SetVisibility(true);
		FlameParticleComponent->Activate(true);
	}
	bIsLit = true;

	UE_LOG(LogTemp, Warning, TEXT("%s::LightUp"), *GetName());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(225, 2.5f, FColor::Yellow, FString::Printf(TEXT("%s lit up"), *GetName()));
	}

	// Censer-only: move back from the belt/shoulder to the original hand this item was picked up into
	// (CachedHeldMesh/CachedHandSocketName, set by PickUp()) — the "on" counterpart to Extinguish()'s belt
	// move. No-ops if this item isn't actually currently held, or hasn't been picked up into a hand yet
	// (e.g. the very first PickUp() call, which sets those caches itself right after calling LightUp()).
	if (ItemType == EHolyItemType::Censer && HoldingCharacter.IsValid() && CachedHeldMesh.IsValid() && CachedHandSocketName != NAME_None)
	{
		AttachToComponent(CachedHeldMesh.Get(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, CachedHandSocketName);
		SetActorRelativeLocation(bInvertProtectionForward ? EquipLocationOffsetLeftHand : EquipLocationOffset);
		SetActorRelativeRotation(bInvertProtectionForward ? EquipRotationOffsetLeftHand : EquipRotationOffset);
		AttachChainToSocket(CachedHeldMesh.Get(), CachedHandSocketName);

		// This lands on the ordinary carry socket, not the swing one — if the holder happens to already be
		// mounted (e.g. LightUp() from RefillCenser() while riding), Tick()'s own swing check will correctly
		// re-attach to the swing socket on the very next tick, same as any other mounted+lit transition.
		bIsAttachedToSwingSocket = false;
	}
}

void AHolyItem::EnableHighlight()
{
	if (IsValid(ItemMesh))
	{
		ItemMesh->SetRenderCustomDepth(true);

		// Temporary stand-in for the CustomDepth outline until a Post Process outline material is
		// set up in the scene — scaled relative to BaseItemMeshScale rather than a hardcoded 1.2 so
		// it still works correctly for a Blueprint that scales ItemMesh to something other than 1.0.
		ItemMesh->SetRelativeScale3D(BaseItemMeshScale * 1.2f);
	}
}

void AHolyItem::DisableHighlight()
{
	if (IsValid(ItemMesh))
	{
		ItemMesh->SetRenderCustomDepth(false);
		ItemMesh->SetRelativeScale3D(BaseItemMeshScale);
	}
}

void AHolyItem::ApplyItemDamage(AActor* OtherActor)
{
	ABaseMonster* Monster = Cast<ABaseMonster>(OtherActor);
	if (!IsValid(Monster) || Monster->IsPendingKillPending() || Monster->Health <= 0.0f)
	{
		return;
	}

	// A ridden monster ignores physical contact with ANY item too (e.g. brushing against a ground-
	// dropped Candle/Censer while mounted), same immunity as the aura paths above.
	if (Monster->bIsBeingRidden)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s::ApplyItemDamage: BLOCKED contact damage to ridden %s"), *GetName(), *Monster->GetName());
		return;
	}

	// Same restriction as ApplyAuraDamage() — a Censer's ordinary physical-contact damage (this is the
	// OnItemHit()/OnItemBeginOverlap() path for a HELD or otherwise-not-thrown item touching a monster,
	// e.g. bumping into one while carried) is removed entirely. Its 500 damage only ever fires via a
	// THROW's bIsThrown-gated impact logic.
	if (ItemType == EHolyItemType::Censer)
	{
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("%s::ApplyItemDamage: APPLYING 1.0 contact damage to %s"), *GetName(), *Monster->GetName());
	UGameplayStatics::ApplyDamage(Monster, 1.0f, nullptr, this, UCandleDamageType::StaticClass());
}

void AHolyItem::PerformMeleeSwing()
{
	if (ItemType != EHolyItemType::Censer || !bIsLit)
	{
		return;
	}

	AActor* Holder = HoldingCharacter.Get();
	if (!IsValid(Holder))
	{
		return;
	}

	// On-foot restriction: the melee swing is only available while the holder is mounted — same
	// invariant as CheckAuraProximity()'s own pushback gate above.
	const ABaseCharacter* SwingHolderCharacter = Cast<ABaseCharacter>(Holder);
	if (!IsValid(SwingHolderCharacter) || !SwingHolderCharacter->IsMounted())
	{
		return;
	}

	// Sweep centered a bit ahead of the holder (not right on top of them) so the knockback reads as a
	// forward swing rather than a radial pulse centered on the player's own capsule.
	const FVector SwingOrigin = Holder->GetActorLocation() + Holder->GetActorForwardVector() * (MeleeSwingRadius * 0.5f);

	TArray<AActor*> NearbyActors;
	UKismetSystemLibrary::SphereOverlapActors(this, SwingOrigin, MeleeSwingRadius,
		TArray<TEnumAsByte<EObjectTypeQuery>>{ UEngineTypes::ConvertToObjectType(ECC_Pawn) },
		ABaseMonster::StaticClass(), TArray<AActor*>{ Holder }, NearbyActors);

	int32 KnockedBackCount = 0;
	for (AActor* NearbyActor : NearbyActors)
	{
		ABaseMonster* Monster = Cast<ABaseMonster>(NearbyActor);
		// bIsBeingRidden excluded here too — if Holder is mounted and swings a Censer, the sweep is
		// centered right at/near Holder's own (mount-riding) location, and the ridden monster is an
		// ABaseMonster like any other target, so without this it could catch its own rider's knockback
		// swing. Same "don't let my own attack hit my own mount" immunity as every other HolyItem damage
		// path in this class.
		if (!IsValid(Monster) || Monster->IsPendingKillPending() || Monster->bIsBeingRidden)
		{
			continue;
		}

		// Uniform push along the HOLDER's forward vector (not radially away from each monster) — every
		// monster caught in the swing gets shoved the same way, reading as one directional strike rather
		// than an omnidirectional pulse. Routed through ABaseMonster::ApplyKnockback() (not a raw
		// LaunchCharacter() call) so this also staggers the monster, suppressing its own AI pathing for
		// a moment so the shove is actually visible instead of being immediately corrected against. No
		// damage here — this is pure knockback, per the Censer melee spec.
		const FVector PushDirection = Holder->GetActorForwardVector().GetSafeNormal2D();
		Monster->ApplyKnockback(PushDirection, MeleeSwingKnockbackForce, MeleeSwingUpwardForce);
		++KnockedBackCount;

		UE_LOG(LogTemp, Warning, TEXT("Knocked back %s with Censer swing!"), *Monster->GetName());
	}

	UE_LOG(LogTemp, Warning, TEXT("Censer melee swing at %s — knocked back %d monster(s) within %.0f units"),
		*SwingOrigin.ToCompactString(), KnockedBackCount, MeleeSwingRadius);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(1002, 2.0f, KnockedBackCount > 0 ? FColor::Cyan : FColor(128, 128, 128),
			FString::Printf(TEXT("CENSER SWING: %d knocked back"), KnockedBackCount));
	}

	// Fuel cost: 1 unit per swing that actually executes (reached this point — mounted + lit), regardless
	// of whether it connected with a monster. Reaching empty extinguishes immediately, same as the
	// ordinary burn-out path in Tick().
	FuelRemaining = FMath::Max(0.0f, FuelRemaining - 1.0f);
	if (FuelRemaining <= 0.0f)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s: ran out of fuel from melee swing — extinguishing"), *GetName());
		Extinguish();
	}
}

bool AHolyItem::ShouldPlaySwingPose() const
{
	if (ItemType != EHolyItemType::Censer || !bIsLit)
	{
		return false;
	}
	const ABaseCharacter* HolderCharacter = Cast<ABaseCharacter>(HoldingCharacter.Get());
	return IsValid(HolderCharacter) && HolderCharacter->IsMounted();
}

bool AHolyItem::ShouldPlayCarryPose() const
{
	if (ItemType != EHolyItemType::Censer || !bIsLit)
	{
		return false;
	}
	const ABaseCharacter* HolderCharacter = Cast<ABaseCharacter>(HoldingCharacter.Get());
	return IsValid(HolderCharacter) && !HolderCharacter->IsMounted();
}

void AHolyItem::BeginTossVisual(float Duration)
{
	if (bIsPlayingTossVisual)
	{
		return;
	}

	TossVisualStartLocation = GetActorLocation();
	TossVisualDuration = FMath::Max(Duration, 0.01f);
	TossVisualElapsed = 0.0f;
	bIsPlayingTossVisual = true;

	// Detach from the hand for the duration of the flight — Tick() above moves the item in WORLD space
	// itself from here on, instead of staying in the hand socket's relative space.
	DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
}

void AHolyItem::EndTossVisual()
{
	if (!bIsPlayingTossVisual)
	{
		return;
	}
	bIsPlayingTossVisual = false;

	if (IsValid(TossImpactEffect))
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, TossImpactEffect, GetActorLocation(), GetActorRotation());
	}

	// Return to the hand — the SAME mechanism LightUp() already uses to return from the belt (see its own
	// body): CachedHeldMesh/CachedHandSocketName, untouched here.
	if (ItemType == EHolyItemType::Censer && HoldingCharacter.IsValid() && CachedHeldMesh.IsValid() && CachedHandSocketName != NAME_None)
	{
		AttachToComponent(CachedHeldMesh.Get(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, CachedHandSocketName);
		SetActorRelativeLocation(bInvertProtectionForward ? EquipLocationOffsetLeftHand : EquipLocationOffset);
		SetActorRelativeRotation(bInvertProtectionForward ? EquipRotationOffsetLeftHand : EquipRotationOffset);
		AttachChainToSocket(CachedHeldMesh.Get(), CachedHandSocketName);
		bIsAttachedToSwingSocket = false;
	}
}

void AHolyItem::CheckCenserFlightPathForMonster()
{
	const FVector CurrentLocation = GetActorLocation();

	// Ignore the thrower's own mount (if any) for the entire flight, same reasoning as Throw()'s
	// IgnoreActorWhenMoving call — the flight path's early segments can still pass right by the rider's
	// mount, which would otherwise register here as a false "hit" before the item ever reaches its
	// actual target.
	TArray<AActor*> ActorsToIgnore{ this };
	if (ABaseMonster* MountToIgnore = ThrowerMountedMonster.Get())
	{
		ActorsToIgnore.Add(MountToIgnore);
	}

	FHitResult PathHit;
	const bool bPathHit = UKismetSystemLibrary::SphereTraceSingleForObjects(this, LastFlightTraceLocation, CurrentLocation,
		CenserFlightTraceRadius, TArray<TEnumAsByte<EObjectTypeQuery>>{ UEngineTypes::ConvertToObjectType(ECC_Pawn) },
		/*bTraceComplex=*/false, ActorsToIgnore, EDrawDebugTrace::None, PathHit, /*bIgnoreSelf=*/true);

	// Recorded regardless of whether this segment hit anything, so the NEXT tick's segment starts from
	// where this one ended rather than re-checking (or skipping) ground already swept.
	LastFlightTraceLocation = CurrentLocation;

	if (!bPathHit)
	{
		return;
	}

	ABaseMonster* Monster = Cast<ABaseMonster>(PathHit.GetActor());
	if (!IsValid(Monster) || Monster->IsPendingKillPending() || Monster->bIsBeingRidden || Monster == ThrowerMountedMonster.Get())
	{
		if (IsValid(Monster))
		{
			UE_LOG(LogTemp, Warning, TEXT("%s: flight-path trace skipping %s (ridden=%s isThrowersMount=%s) — no damage applied"),
				*GetName(), *Monster->GetName(), Monster->bIsBeingRidden ? TEXT("true") : TEXT("false"),
				Monster == ThrowerMountedMonster.Get() ? TEXT("true") : TEXT("false"));
		}
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("%s: flight-path trace APPLYING knockback to %s"), *GetName(), *Monster->GetName());
	const FVector ThrowPushDirection = GetVelocity().GetSafeNormal2D();
	Monster->ApplyKnockback(ThrowPushDirection, ThrownCenserKnockbackForce, ThrownCenserUpwardForce);

	UE_LOG(LogTemp, Warning, TEXT("Censer knocked back %s!"), *Monster->GetName());
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(217, 2.5f, FColor::Red,
			FString::Printf(TEXT("Censer knocked back %s!"), *Monster->GetName()));
	}

	// Resolved proactively here, not via OnItemHit() — replicate its one-shot extinguish/cleanup so a
	// later OnItemHit() (e.g. this Censer subsequently landing on the floor) falls through to the
	// ordinary ApplyItemDamage() contact path (a no-op against non-monster geometry) instead of
	// re-running the thrown-impact logic a second time for the same throw.
	bIsThrown = false;
	Extinguish();
	ThrowerMountedMonster = nullptr;
}
