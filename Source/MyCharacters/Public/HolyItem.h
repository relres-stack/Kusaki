// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HolyItem.generated.h"

class UStaticMeshComponent;
class UPointLightComponent;
class USphereComponent;
class USkeletalMeshComponent;
class ABaseMonster;

// Which holy weapon this item represents; determines the damage type/amount it deals to monsters.
UENUM(BlueprintType)
enum class EHolyItemType : uint8
{
	Censer,
	Candle
};

// A physical holy item (Censer or Candle) that damages any living ABaseMonster it hits, physically
// overlaps, or that enters its holy aura sphere.
UCLASS()
class MYCHARACTERS_API AHolyItem : public AActor
{
	GENERATED_BODY()

public:
	AHolyItem();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Holy Item")
	UStaticMeshComponent* ItemMesh;

	// Glowing point light marking the item as holy; color/intensity reflect ItemType.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Holy Item")
	UPointLightComponent* LightComponent;

	// Flame VFX riding the item, toggled alongside LightComponent by Extinguish()/bIsLit. No default
	// Niagara System is assigned here — assign one on the component (e.g. in a BP_Censer/BP_Candle
	// subclass's Details panel) for it to actually render anything; the component itself is always
	// created so Extinguish() has something to safely call Deactivate() on regardless. TObjectPtr (not a
	// raw pointer, unlike ItemMesh/LightComponent/HolyAuraSphere above) + Category = "Components" to
	// match LightNavModifier's own declaration style further down this class.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<class UNiagaraComponent> FlameParticleComponent;

	// Censer-only: visible chain between the holder's current attach point (hand while lit, belt/shoulder
	// while extinguished — see AttachChainToSocket()) and ItemMesh. Its START is attached directly to the
	// holder's skeletal mesh (not a child of ItemMesh); its END is aimed at ItemMesh via SetAttachEndTo(),
	// so it automatically follows the item's live position each frame — including during the orbiting
	// swing (see Tick()) — without any manual per-frame recompute here. Hidden by default; only made
	// visible for a Censer once AttachChainToSocket() actually runs (Candle never uses this).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Holy Item|Censer Chain")
	TObjectPtr<class UCableComponent> ChainVisualComponent;

	// Overlap-only trigger volume around the item; monsters entering it take damage without needing physical contact.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Holy Item")
	USphereComponent* HolyAuraSphere;

	// Radius (in units) of HolyAuraSphere: the gameplay "holy light" zone that damages/stuns monsters on
	// overlap or proximity (OnAuraOverlap()/CheckAuraProximity()) and the radius ABaseMonster's Stalk
	// state orbits just outside of. Also sizes LightNavModifier's NavMesh avoidance area, since
	// UNavModifierComponent derives its bounds from the union of all of this actor's nav-relevant
	// collision components — HolyAuraSphere is by far the largest of those, so this radius is effectively
	// also the avoidance-zone radius. Applied to HolyAuraSphere in the constructor and re-applied in
	// BeginPlay(), so a Blueprint subclass/instance override of this value actually takes effect.
	// Must stay meaningfully larger than ABaseMonster::CatchDistance (its kill/attack range, 150 units)
	// — otherwise a monster can be outside the light's protection but still within kill range at the
	// same time, letting it reach the player despite them holding a lit item.
	// NOTE: HolyAuraSphere itself no longer sizes the NavMesh avoidance area (see
	// NavAvoidanceSphere/NavAvoidanceRadius below) — it's excluded via SetCanEverAffectNavigation(false)
	// in the constructor, specifically so this radius can stay large for protection purposes without
	// also blowing up the red avoidance ring on the NavMesh.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item")
	float HolyAuraRadius = 220.0f;

	// Censer-only: HolyAuraSphere's actual radius while the holder is NOT mounted (see Tick(), which
	// switches the live sphere between this and HolyAuraRadius each frame based on the holder's
	// IsMounted() state) — full protection is reserved for riding a mount; on foot, the Censer's reach is
	// reduced to this instead. Must stay meaningfully larger than ABaseMonster::CatchDistance (150), same
	// invariant as HolyAuraRadius's own comment, for the same reason — otherwise a monster could be
	// outside this reduced radius but still within kill range at the same time.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item")
	float FootProtectionRadius = 180.0f;

	// Small sphere that exists purely to size LightNavModifier's NavMesh avoidance area, independently
	// of HolyAuraRadius (which must stay large — see above). HolyAuraSphere is excluded from navigation
	// entirely, so this is the only component driving the avoidance ring's size.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Holy Item")
	USphereComponent* NavAvoidanceSphere;

	// Radius (in units) of NavAvoidanceSphere / the NavMesh avoidance ring around this item. Keep this
	// small (roughly 120-150) so most of a typical room stays normal walkable NavMesh — LightNavModifier
	// also uses UNavArea_Obstacle (high pathing cost, not impassable), so this only needs to be a tight
	// ring monsters prefer to route around, not a hard barrier.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item")
	float NavAvoidanceRadius = 130.0f;

	// If true, protection (see IsLocationInProtectionCone()) is restricted to a forward-facing cone
	// instead of the full sphere — sneaking up from behind/the side is not protected. Candle-style
	// items typically want this on (a hand-held flame only "watches" one direction); Censer-style items
	// typically want this off (bUseConeProtection = false gives full 360-degree protection).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Protection Cone")
	bool bUseConeProtection = true;

	// Total cone angle (in degrees, centered on GetActorForwardVector()) that counts as protected when
	// bUseConeProtection is true. 90 means 45 degrees either side of forward. Ignored (full sphere)
	// when bUseConeProtection is false or this is >= 360.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Protection Cone", meta = (ClampMin = "0.0", ClampMax = "360.0", EditCondition = "bUseConeProtection"))
	float ProtectionAngle = 90.0f;

	// If true, the protection cone faces -GetActorForwardVector() of the holding character instead of
	// +GetActorForwardVector(). Exists because the player's held-item animations always pose both hands
	// forward (there is no distinct "second candle held behind the back" animation) — so a second,
	// off-hand candle's actual protection zone is made to face backward instead of duplicating the
	// primary candle's forward-facing zone. Set automatically by PickUp() based on which hand the item
	// goes into; also EditAnywhere so it can be previewed/tuned on a placed instance before pickup.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Protection Cone", meta = (EditCondition = "bUseConeProtection"))
	bool bInvertProtectionForward = false;

	// True if TargetLocation falls within this item's protection cone — purely an angular check,
	// independent of distance/HolyAuraRadius (callers combine this with their own radius check, e.g.
	// ABaseMonster::IsTargetInLight()). Always true when bUseConeProtection is false or ProtectionAngle
	// >= 360 (full sphere, no directional restriction).
	UFUNCTION(BlueprintCallable, Category = "Holy Item|Protection Cone")
	bool IsLocationInProtectionCone(FVector TargetLocation) const;

	// While true, draws a live debug sphere (and, when directional, the protection cone — see Tick())
	// each tick showing the actual HolyAuraRadius/ProtectionAngle in Play/PIE — orange for Candle, cyan
	// for Censer — so the protection zone can be seen and tested at a glance. Also gates
	// ABaseMonster::IsTargetInLight()'s own player-centered light-check sphere (checked there off
	// whichever hand's item this is), so a single flag controls every light-tracking debug visual, not
	// just this item's own. Purely a wireframe visual either way — DrawDebugSphere()/DrawDebugCone() are
	// no-ops in Shipping builds and don't affect collision/navigation/aim traces or the underlying
	// HolyAuraSphere overlap/light-tracking math at all (see HolyAuraSphere's own collision/nav settings
	// in the constructor/BeginPlay() for what actually controls that) — this only ever turns pixels off.
	// Defaulted OFF: these wireframes are a tuning aid, not something that should be on-screen by default
	// once the feature is done and being played rather than tuned.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Debug")
	bool bDrawDebugRadius = false;

	// While true, draws a thin, permanent atmospheric boundary indicator every tick — NOT a tuning aid
	// like bDrawDebugRadius above (which defaults off): this is meant to stay visible during normal
	// play, a clean tactical read on where protection actually ends. For a Candle: a thin arc along the
	// outer edge of the protection cone (warm orange). For a Censer: a thin full circle around the
	// active protective radius (golden yellow). Both are projected onto the actual ground beneath this
	// item (a short downward trace), independent of bDrawDebugRadius, and purely visual —
	// DrawDebugCircle()/DrawDebugCircleArc() don't affect the underlying HolyAuraSphere/
	// IsLocationInProtectionCone() protection math at all. See DrawProtectionBoundary().
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Debug")
	bool bShowProtectionBoundary = true;

	// Which holy weapon this item is; determines the damage type/amount dealt to monsters.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item")
	EHolyItemType ItemType = EHolyItemType::Candle;

	// Relative scale ItemMesh is set to when ApplyCandlePrototypeVisualsIfNeeded() replaces the generic
	// placeholder Cube with the engine Cylinder shape for a Candle-type item — slender and elongated
	// (default matches a held candle stick) rather than the Cylinder primitive's native stubby proportions.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Candle Prototype")
	FVector CandlePrototypeScale = FVector(0.1f, 0.1f, 0.4f);

	// Warm flame color/intensity/range applied to LightComponent for a Candle-type item — see
	// ApplyCandlePrototypeVisualsIfNeeded(). Deliberately separate from the plain Candle-vs-Censer color
	// BeginPlay() otherwise applies (a flat Red for Candle) — a real flame reads as warm orange/yellow, not
	// red, and needs a shorter attenuation radius than that generic setup uses for a small, cozy glow
	// rather than a room-filling one.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Candle Prototype")
	FLinearColor CandleFlameLightColor = FLinearColor(1.0f, 0.65f, 0.2f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Candle Prototype")
	float CandleFlameLightIntensity = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Candle Prototype")
	float CandleFlameAttenuationRadius = 300.0f;

	// Censer counterpart to CandlePrototypeScale — replaces the generic placeholder-Cube ItemMesh falls
	// back to (see the constructor) with a small Sphere, so an un-assigned Censer reads as a round "bowl
	// on a chain" instead of a bare cube. See ApplyCenserPrototypeVisualsIfNeeded().
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Censer Prototype")
	FVector CenserPrototypeScale = FVector(0.2f, 0.2f, 0.2f);

	// Marks a navigation area (e.g. to discourage pathing through it) around the item on the NavMesh.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<class UNavModifierComponent> LightNavModifier;

	// Relative location offset applied (via SetActorRelativeLocation(), in the RIGHT hand socket/bone's
	// OWN local space) on top of PickUp()'s socket-snap attach — AttachToComponent()'s SnapToTarget rule
	// alone places this item's ORIGIN exactly at the socket with zero offset, and since ItemMesh's pivot
	// is centered, that puts HALF the mesh sticking out the back of the hand toward the wrist/spine.
	// Nudges the whole item forward into the palm/fingers instead. Tune per-Blueprint (e.g. BP_Candle vs
	// BP_Censer likely want different offsets for their differently-shaped/sized meshes).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Equip Offset")
	FVector EquipLocationOffset = FVector(8.0f, 0.0f, 0.0f);

	// Relative rotation offset applied (via SetActorRelativeRotation(), also in the RIGHT hand socket/
	// bone's own local space) alongside EquipLocationOffset above — aligns the item's long axis to point
	// forward along the hand/arm direction instead of whatever raw orientation the socket-snap alone
	// produces. Deliberately expressed entirely in the SOCKET's own local space (not corrected toward any
	// world-space axis) so it naturally rotates along with the hand as animations flex the arm/elbow.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Equip Offset")
	FRotator EquipRotationOffset = FRotator(90.0f, 0.0f, 0.0f);

	// LEFT hand equivalents of EquipLocationOffset/EquipRotationOffset above — NOT derived from them by a
	// formula. A runtime diagnostic of this project's actual skeleton (see PickUp()'s own comment) found
	// hand_l's local axes are NOT a clean single-axis mirror of hand_r's: local X flips sign almost
	// exactly between the two hands, but Y/Z don't — there's an additional twist baked into the left arm's
	// bone roll beyond a pure reflection. A single "negate one axis" formula can't correctly cover both
	// location AND rotation given that, so the left hand gets its own independently-tunable values instead.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Equip Offset")
	FVector EquipLocationOffsetLeftHand = FVector(-8.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Equip Offset")
	FRotator EquipRotationOffsetLeftHand = FRotator(90.0f, 0.0f, 0.0f);

	// Optional socket on THIS item's own static mesh (add it in the Static Mesh Editor: Window -> Socket Manager) that
	// marks where the hand holds the item. If the mesh has a socket with this name, PickUp() lines that socket up with
	// the hand's grip socket (instead of the mesh's pivot) — the socket's position AND rotation set which side of the
	// item sits in the palm and which way is "up". EquipLocationOffset/EquipRotationOffset (and the Left Hand pair)
	// then act as small extra adjustments ON TOP of that alignment. If the mesh has no such socket, nothing changes:
	// the item is attached by its pivot exactly as before. Lets any candle model be swapped in: just add a "Grip"
	// socket to the new model.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Equip Offset")
	FName GripSocketName = TEXT("Grip");

	// Censer-only: bone/socket on the holder's skeleton the item (and ChainVisualComponent, see below) is
	// moved to while EXTINGUISHED — hangs at the belt/shoulder like a put-away censer instead of staying in
	// the hand. PLACEHOLDER — verify against the actual skeleton (SKM_Manny_Simple/SKM_Quinn_Simple) via
	// the Skeleton Tree and correct if "spine_01" isn't the intended belt height.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Equip Offset")
	FName BeltSocketName = TEXT("spine_01");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Equip Offset")
	FVector BeltEquipLocationOffset = FVector::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Equip Offset")
	FRotator BeltEquipRotationOffset = FRotator::ZeroRotator;

	// Attaches the item to AttachToMesh's SocketName (e.g. a hand socket), disabling SIMULATED PHYSICS for
	// as long as it's held. Collision itself is QueryOnly + Block-vs-Pawn (NOT NoCollision) — a held item
	// must physically hit a monster it's carried/swung into, same as it would while thrown or dropped —
	// with a capsule-side IgnoreActorWhenMoving() guard against the holder's OWN capsule so this doesn't
	// self-collide with its carrier (see the .cpp). bInvertForward sets bInvertProtectionForward — pass
	// true for a secondary/off-hand item whose protection cone should face backward (see that property's
	// comment).
	UFUNCTION(BlueprintCallable, Category = "Holy Item")
	void PickUp(USkeletalMeshComponent* AttachToMesh, FName SocketName, bool bInvertForward = false);

	// Forcibly disables SimulatePhysics + sets NoCollision on EVERY UPrimitiveComponent on this actor —
	// not just ItemMesh, unlike PickUp()'s own (now Query-enabled, not fully disabled) collision. Covers
	// any additional physics bodies a Blueprint subclass might add (e.g. a swinging-chain physics asset on
	// BP_Censer) that PickUp() has no knowledge of, so nothing on this item can collide/depenetrate against
	// anything — including the ridden monster itself — while mounted. Used by
	// ABaseCharacter::MountNearbyMonster() so a held item can never physically interact with the monster's
	// mesh while the player rides it. Call RestoreHeldPhysicsState() to undo.
	UFUNCTION(BlueprintCallable, Category = "Holy Item")
	void DisableAllPhysicsAndCollision();

	// Restores this item's components to the same "held" state PickUp() itself establishes (ItemMesh:
	// QueryOnly + Block-vs-Pawn; HolyAuraSphere/NavAvoidanceSphere: QueryOnly) — NOT to some other/default
	// state, since a held item should never have SIMULATED PHYSICS regardless of mount state, but (per
	// PickUp()) DOES need to be able to physically hit a monster again once dismounted. Any extra
	// Blueprint-added components DisableAllPhysicsAndCollision() touched stay disabled; if a Blueprint
	// intentionally simulates physics on one of those while held (unlikely, but possible), override this
	// item's Blueprint-side handling separately rather than relying on this base implementation.
	UFUNCTION(BlueprintCallable, Category = "Holy Item")
	void RestoreHeldPhysicsState();

	// Detaches the item, moves it to DropLocation/DropRotation, and restores its physics/collision.
	UFUNCTION(BlueprintCallable, Category = "Holy Item")
	void DropItem(FVector DropLocation, FRotator DropRotation);

	// Detaches the item (if held), teleports it to ThrowLocation, and launches it as a physics
	// projectile with LaunchVelocity (direction * speed) — called by ABaseCharacter::ThrowItem().
	// Marks this item as a live throw (bIsThrown) so its next OnItemHit() applies one-shot impact
	// behavior (Stun()/Die() on a struck monster, on-screen hit feedback, then extinguishing this
	// item's light/aura) instead of the ordinary ApplyItemDamage() dealt by ordinary physical contact
	// (e.g. a dropped item resting against a monster). The item is NOT destroyed on impact — like
	// DropItem(), it's left in the world (extinguished, but otherwise intact) so it can be picked back
	// up, and, also like DropItem(), regains normal collision with the thrower once it's settled.
	UFUNCTION(BlueprintCallable, Category = "Holy Item")
	void Throw(FVector ThrowLocation, FVector LaunchVelocity);

	// True once Throw() has launched this item and until its first impact (OnItemHit()) resolves —
	// see Throw()'s comment.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Holy Item|Throw")
	bool bIsThrown = false;

	// Stun duration (seconds) applied to monsters caught by a THROWN Candle's impact — independent of
	// ABaseMonster::StunDuration (that one is for ordinary contact/aura damage). Ignored for a Censer.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Throw")
	float ThrownStunDuration = 4.0f;

	// Radius (in units), centered on the impact point, that a THROWN Candle's stun reaches — every live
	// ABaseMonster in range is stunned, not just one directly struck, so a throw that lands on the
	// ground near a group still stuns them (see OnItemHit()).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Throw")
	float CandleThrowStunRadius = 300.0f;

	// Damage a THROWN Censer deals to an ABaseMonster it directly strikes, via UGameplayStatics::
	// ApplyDamage()/UCenserDamageType (see OnItemHit()) — no longer an unconditional instant kill, so a
	// future boss subclass with MaxHealth above this can survive a single hit. Comfortably lethal
	// (500) against a regular 100-HP monster.
	// UNUSED BY THE THROW PATH as of the knockback-instead-of-kill change (see ThrownCenserKnockbackForce/
	// ThrownCenserUpwardForce below) — OnItemHit()/CheckCenserFlightPathForMonster() now call
	// ApplyKnockback() instead of ApplyDamage() for a thrown Censer. Left in place (not deleted) so any
	// already-tuned Blueprint value isn't lost if a damage-based throw path is ever reintroduced elsewhere.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Throw")
	float CenserThrowDamage = 500.0f;

	// Outward speed (units/sec) ApplyKnockback() gives a monster hit by a THROWN Censer (direct hit, AoE
	// fallback, or flight-path pre-check — see OnItemHit()/CheckCenserFlightPathForMonster()) — no longer
	// damage-based (see CenserThrowDamage's own updated comment), pure knockback like PerformMeleeSwing().
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Throw")
	float ThrownCenserKnockbackForce = 1200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Throw")
	float ThrownCenserUpwardForce = 250.0f;

	// Radius (in units), centered on the impact point, OnItemHit() falls back to searching for a live
	// ABaseMonster in when a thrown Censer's OtherActor ISN'T one directly — the Censer mesh is large
	// enough that it can register OnHit against nearby floor/wall geometry a frame before its origin
	// would have reached the monster's capsule, which otherwise reads as a clean miss despite the throw
	// clearly landing on target.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Throw")
	float CenserThrowAoERadius = 250.0f;

	// Radius (in units) of the sphere swept along the Censer's actual travel segment every tick while
	// bIsThrown (see CheckCenserFlightPathForMonster()) — catches a monster anywhere in the flight path
	// BEFORE any floor/wall OnHit can resolve first and cancel the throw, independently of (and checked
	// before) CenserThrowAoERadius's on-impact fallback above.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Throw")
	float CenserFlightTraceRadius = 80.0f;

	// Seconds after Throw() during which OnItemHit() ignores a hit against anything OTHER than a live
	// ABaseMonster (ThrowGraceDistance below must also still hold) — long enough to absorb a large
	// item's mesh spawning already touching/overlapping the floor at the throw origin (which otherwise
	// fires an immediate spurious OnHit and cancels the throw before it ever leaves the player), short
	// enough to never affect a real mid-flight impact. A direct monster hit is NEVER ignored, even
	// inside this window — only floor/wall/other geometry is.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Throw")
	float ThrowGracePeriodDuration = 0.2f;

	// Distance (in units) from the throw's start location within which ThrowGracePeriodDuration's grace
	// window applies. The window ends at whichever of the two limits — elapsed time or distance — is
	// reached first, so a slow-arcing throw still near the player after 0.2s doesn't stay floor-immune
	// indefinitely, and a fast throw that's already 150+ units out doesn't get an unnecessarily long grace.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Throw")
	float ThrowGraceDistance = 150.0f;

	// Radius (in units) of the Censer's melee swing knockback sweep — see PerformMeleeSwing().
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Melee")
	float MeleeSwingRadius = 250.0f;

	// Outward speed (units/sec) ABaseMonster::ApplyKnockback() gives each monster caught by
	// PerformMeleeSwing() — a "heavy" push, deliberately stronger than the passive aura's
	// CenserPushbackForce below.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Melee")
	float MeleeSwingKnockbackForce = 1000.0f;

	// Upward component (units/sec) added to the melee swing's knockback, on top of
	// MeleeSwingKnockbackForce's horizontal push.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Melee")
	float MeleeSwingUpwardForce = 200.0f;

	// Censer-only: sweeps MeleeSwingRadius in front of this item's holder and knocks back (via
	// ABaseMonster::ApplyKnockback(), no damage) every live ABaseMonster caught in it, all pushed
	// uniformly along the holder's own forward vector (a directional shove, not a radial one) — bound to
	// a dedicated melee-swing input on whichever hand holds a Censer (see
	// ABaseCharacter::SwingHeldCenser()). No-ops for a Candle, or if this item currently has no
	// HoldingCharacter.
	UFUNCTION(BlueprintCallable, Category = "Holy Item|Melee")
	void PerformMeleeSwing();

	// True while this Censer is held, lit, AND the holder is mounted — the exact same condition that
	// already drives Tick()'s own orbiting "lasso" swing (see Tick()), just exposed as a single BlueprintPure
	// bool so an AnimGraph (e.g. the Rider's ABP, picking between a "carry" and a "swing overhead" pose for
	// the held-Censer hand via Layered Blend Per Bone) doesn't need to reassemble that same chain of checks
	// from raw Get nodes. Read-only — doesn't affect, and isn't affected by, Tick()'s own copy of this check.
	UFUNCTION(BlueprintPure, Category = "Holy Item|Censer Swing")
	bool ShouldPlaySwingPose() const;

	// True while this Censer is held and lit, but the holder is NOT mounted (ordinary hand-carry, no
	// orbit) — same principle as ShouldPlaySwingPose(), just the inverse of its IsMounted() check.
	UFUNCTION(BlueprintPure, Category = "Holy Item|Censer Swing")
	bool ShouldPlayCarryPose() const;

	// Purely visual item toss — used by the Rider's Holy Explosion (see ARiderCharacter::
	// TryHolyExplosion()/ResolveHolyExplosion()), but not tied to it directly: just detaches this item
	// from the hand and smoothly moves it (see Tick()) toward HoldingCharacter's current world position
	// over Duration seconds — reads as "the Censer falls toward the landing spot" while the holder
	// jumps/falls. BeginTossVisual()/EndTossVisual() are the only public API — the rest of the mechanic
	// is private (see Tick()).
	UFUNCTION(BlueprintCallable, Category = "Holy Item|Toss Visual")
	void BeginTossVisual(float Duration);

	// Stops the toss (safe to call even if one isn't in progress), spawns TossImpactEffect at the item's
	// current position (= the landing spot, since Tick() has already carried it there) and returns the
	// item to the hand — the SAME mechanism LightUp() already uses to return from the belt (see its own
	// body; not reimplemented here).
	UFUNCTION(BlueprintCallable, Category = "Holy Item|Toss Visual")
	void EndTossVisual();

	// Ground-impact Niagara effect spawned by EndTossVisual() — same principle as SwingTrailEffect: left
	// unassigned (nullptr) by default, with no effect spawning and everything else still working.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Toss Visual")
	TObjectPtr<class UNiagaraSystem> TossImpactEffect;

	// Radius (in units), centered on this item, within which the Censer's passive aura pushes a live
	// ABaseMonster back — ONLY while this item is currently HELD (see CheckAuraProximity()). Deliberately
	// independent of HolyAuraRadius/HolyAuraSphere (which stay large for light-boundary/protection
	// purposes) so this physical repulsion can be tuned tighter on its own.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Melee")
	float CenserPushbackRadius = 200.0f;

	// Outward speed (units/sec) the held Censer's passive aura pushback gives a monster inside
	// CenserPushbackRadius — deliberately gentler than MeleeSwingKnockbackForce, a steady repulsion
	// rather than a heavy strike.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Melee")
	float CenserPushbackForce = 400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Melee")
	float CenserPushbackUpwardForce = 50.0f;

	// Minimum seconds between "leg swipe" attempts (see CheckAuraProximity()) — a lingering on-foot
	// monster only gets to roll for a graze this often, not every 0.1s CheckAuraProximity() tick.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Leg Swipe")
	float LegSwipeCooldown = 3.0f;

	// Chance [0,1] a leg swipe attempt actually connects once its cooldown allows a roll.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Leg Swipe")
	float LegSwipeChance = 0.5f;

	// Speed (units/sec) of the LaunchCharacter() push a connecting leg swipe gives the HOLDER (not the
	// monster) — deliberately mild, a graze/flinch, not a real hit.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Leg Swipe")
	float LegSwipeKnockbackForce = 250.0f;

	// Fuel units spilled from FuelRemaining on a connecting leg swipe.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Leg Swipe")
	float LegSwipeFuelSpill = 5.0f;

	// Chance [0,1], rolled independently on a connecting leg swipe, that it also extinguishes this item.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Leg Swipe")
	float LegSwipeExtinguishChance = 0.2f;

	// Seconds after a leg-swipe-caused extinguish before this item relights itself on its own, provided
	// FuelRemaining is still above 0 at that point.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Leg Swipe")
	float LegSwipeRelightDelay = 1.0f;

	// Continuous "lasso" swing: while mounted AND lit, Tick() re-attaches the Censer to a dedicated grip
	// socket on the holding hand (see Tick()) — NOT tied to any button, driven purely by
	// IsMounted() && bIsLit. All the actual circular motion comes from the swing animation moving that
	// socket's bone; this code no longer computes any orbit math itself (it used to — a hand-authored
	// sine/cosine circle — which double-counted once a real swing animation was wired in, since the bone
	// was already circling on its own). PerformMeleeSwing() (Aim+LMB)'s own knockback sweep is a separate,
	// independent trigger layered on top of whatever the swing pose is already doing visually.
	//
	// RightHandSocket/LeftHandSocket equivalents of the item's ordinary hand attach point, specifically for
	// this mounted swing pose — deliberately separate from CachedHandSocketName (the everyday carry point):
	// a socket positioned for a relaxed carry pose usually isn't where fingers actually wrap around the
	// item mid-swing. Default to the plain hand bones so this still works even before a dedicated socket
	// exists (see the DoesSocketExist() fallback in Tick()) — create a real "CenserGripSocket"-style socket
	// on hand_r/hand_l in the Skeleton Tree and point these at it once placed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Censer Swing")
	FName SwingSocketName = TEXT("HandGrip_R");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Censer Swing")
	FName SwingSocketNameLeftHand = TEXT("hand_l");

	// Relative offset (in the grip socket's OWN local space, same convention as EquipLocationOffset above)
	// applied to the item once it's snapped onto SwingSocketName/SwingSocketNameLeftHand. Deliberately NOT
	// zero: the grip socket marks where the FIST holds the chain, not where the bowl itself should hang —
	// zero offset here sits the whole item exactly on top of that socket, which is also where
	// ChainVisualComponent's start is attached, collapsing the visible chain to ~0 length ("folded in
	// half"). Tune this in the Blueprint's Class Defaults (no recompile needed) until the bowl hangs a
	// believable chain-length away from the fist — no code change required to adjust it.
	//
	// MakeEditWidget=true draws a draggable 3D move-arrow widget for this offset directly in the viewport
	// (Blueprint editor's preview, or the actual actor selected in the level/PIE) — drag it by eye instead
	// of typing numbers and re-testing blind, same idea as dragging a socket's gizmo in Persona.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Censer Swing", meta = (MakeEditWidget = true))
	FVector SwingItemLocationOffset = FVector(0.0f, 0.0f, -20.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Censer Swing")
	FRotator SwingItemRotationOffset = FRotator::ZeroRotator;

	// PREFERRED over SwingItemLocationOffset/RotationOffset above: a second dedicated socket — placed
	// visually in the Skeleton Tree exactly the same way SwingSocketName was (drag its gizmo against the
	// real hand mesh, no guessing) — marking exactly where the BOWL should hang, separate from the grip
	// socket the chain's start attaches to. When this socket exists, Tick() snaps the item straight onto it
	// (zero extra offset) instead of using the manual SwingItemLocationOffset nudge, so the hang distance is
	// authored precisely against the actual mesh rather than eyeballed against an empty preview. Left as
	// NAME_None by default (not created yet) — falls back to SwingItemLocationOffset until set.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Censer Swing")
	FName SwingItemSocketName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Censer Swing")
	FName SwingItemSocketNameLeftHand = NAME_None;

	// Niagara system spawned repeatedly along the orbit's arc while the swing is active (see Tick()) —
	// separate from FlameParticleComponent (the steady flame riding the bowl itself). Trail fade-out is
	// controlled entirely by the Niagara asset's own particle lifetime, not by any code here. Left
	// unassigned (nullptr) by default — without an asset, no trail spawns and everything else still works.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Censer Swing")
	TObjectPtr<class UNiagaraSystem> SwingTrailEffect;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|Censer Swing")
	float TrailSpawnInterval = 0.05f;

	// Turns on ItemMesh's CustomDepth outline (requires a Post Process outline setup in the scene).
	UFUNCTION(BlueprintCallable, Category = "Holy Item")
	void EnableHighlight();

	// Turns off ItemMesh's CustomDepth outline.
	UFUNCTION(BlueprintCallable, Category = "Holy Item")
	void DisableHighlight();

	// Distance-based backup for OnAuraOverlap(): a physics overlap between HolyAuraSphere and a
	// monster's capsule isn't guaranteed to re-evaluate every frame while this item is attached to and
	// riding an animated hand-socket bone (as opposed to sitting static, or physically simulating, on
	// the ground) — so ABaseMonster::CheckForPlayer() calls this every 0.1s for whichever item(s) the
	// player is currently holding. Applies aura damage once per approach, mirroring BeginOverlap, by
	// tracking which monsters are currently within range.
	UFUNCTION(BlueprintCallable, Category = "Holy Item")
	void CheckAuraProximity(ABaseMonster* Monster);

	// True while this item's light/aura/flame are active. False once Extinguish() has run — a thrown
	// item that has resolved its impact, or (per Extinguish()'s own comment) any other future "this item
	// stops being holy for now" trigger, e.g. a burn-out resource running out. Restored to true by
	// PickUp() (mirrors PickUp() already unconditionally re-enabling LightComponent/HolyAuraSphere), so
	// a spent item picked back up is lit again.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|State")
	bool bIsLit = true;

	// Universal extinguish: turns off LightComponent, disables HolyAuraSphere's
	// collision/overlap-generation, and deactivates FlameParticleComponent (if assigned), then sets
	// bIsLit false. Consolidates what OnItemHit()'s bIsThrown branch and
	// CheckCenserFlightPathForMonster() previously did ad hoc and separately whenever a throw resolves —
	// virtual so a Blueprint/C++ subclass can extend it (e.g. play an extinguish sound/VFX) without
	// having to reimplement the underlying light/aura/particle shutdown.
	UFUNCTION(BlueprintCallable, Category = "Holy Item|State")
	virtual void Extinguish();

	// Universal light-up: turns on LightComponent, re-enables HolyAuraSphere's overlap generation, activates
	// FlameParticleComponent (if assigned), and sets bIsLit true — the "on" counterpart to Extinguish(). Also
	// refuels to MaxFuel if FuelRemaining was already at/below 0, same "treated as freshly refueled" reasoning
	// PickUp() already applied inline before this was extracted from it — so this never re-lights stuck at an
	// empty gauge. Virtual so a subclass can extend it, same reasoning as Extinguish().
	UFUNCTION(BlueprintCallable, Category = "Holy Item|State")
	virtual void LightUp();

	// Remaining fuel/wax/oil, [0, MaxFuel] — purely a resource-tracking value the Horror HUD's quick-slot
	// bar reads to show "how much light is left" for whichever item currently occupies the Light Source
	// slot (see UInventoryComponent). Drains at FuelDrainRate units/sec whenever bIsLit is true (Tick())
	// — a Candle/Censer burns whether held, dropped, or thrown, same as a real flame would. Reaching 0
	// calls Extinguish().
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|State")
	float FuelRemaining = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|State")
	float MaxFuel = 100.0f;

	// Fuel units drained per second while bIsLit. Default (100 fuel / ~0.056 per sec) burns out in about
	// 30 real minutes — slow enough not to matter for a short play session, but a real, ticking-down
	// value rather than a HUD element that always reads 100%. Set to 0 for effectively infinite fuel.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Item|State")
	float FuelDrainRate = 0.0556f;

	// True from the moment a leg swipe extinguishes this item (see CheckAuraProximity()) until LightUp()
	// runs (either the scheduled self-relight succeeds, or the item is lit some other way in the
	// meantime) — NOT a general "is lit" substitute, purely a short grace window so
	// ABaseMonster::IsProtectedByHeldItem() keeps treating the player as protected across a leg-swipe
	// blackout instead of opening a real attack window during it. Every OTHER extinguish path (fuel
	// running out, a manual Extinguish() call, the holy explosion) leaves this false, so protection drops
	// immediately for those, exactly as before. Internal state only — not EditAnywhere; public (like
	// bIsLit above) so ABaseMonster::IsProtectedByHeldItem() can read it directly, same access pattern.
	bool bIsAwaitingLegSwipeRelight = false;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	// Damages the monster on physical collision (e.g. the item is thrown into it).
	UFUNCTION()
	void OnItemHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	// Damages the monster on overlap (e.g. the item is swept/carried into it).
	UFUNCTION()
	void OnItemBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	// Damages the monster when it enters HolyAuraSphere.
	UFUNCTION()
	void OnAuraOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

private:
	// Applies this item's holy damage (by ItemType) to OtherActor if it is a living ABaseMonster.
	void ApplyItemDamage(AActor* OtherActor);

	// Applies this item's holy aura damage (by ItemType) to Monster. Shared by OnAuraOverlap() (physics
	// overlap, for items on the ground) and CheckAuraProximity() (manual distance check, for held items).
	void ApplyAuraDamage(ABaseMonster* Monster);

	// Called from Tick() every frame while bIsThrown && ItemType == Censer. Sweeps a CenserFlightTraceRadius
	// sphere from LastFlightTraceLocation to the current location (i.e. along this tick's actual travel
	// segment) for a live ABaseMonster and, if found, resolves the throw's impact right here — applying
	// CenserThrowDamage and running the same one-shot extinguish/cleanup OnItemHit() would — instead of
	// waiting for a physics OnHit that a large mesh spawning/clipping against the floor may resolve first.
	void CheckCenserFlightPathForMonster();

	// Called from Tick() every frame (independent of bDrawDebugRadius) while bShowProtectionBoundary —
	// draws the thin ground-projected boundary indicator described on that property's own comment.
	void DrawProtectionBoundary() const;

	// Called once from BeginPlay(): if ItemType == Candle AND ItemMesh is still showing the generic
	// placeholder Cube the constructor falls back to (i.e. no real candle mesh has been assigned in this
	// Blueprint's own Class Defaults), replaces it with the engine Cylinder shape at CandlePrototypeScale,
	// and applies CandleFlameLightColor/Intensity/AttenuationRadius to LightComponent, repositioned near
	// the cylinder's top tip. No-ops entirely for a Censer, or for a Candle that already has a real mesh
	// assigned — never clobbers a legitimate custom mesh. Run from BeginPlay() (not the constructor)
	// because ItemType itself — set via this Blueprint's own Class Defaults — isn't reliably known until
	// after construction; same reasoning as HolyAuraRadius's own "applied in constructor AND re-applied in
	// BeginPlay" comment.
	void ApplyCandlePrototypeVisualsIfNeeded();

	// Censer counterpart to ApplyCandlePrototypeVisualsIfNeeded() — called once from BeginPlay() right
	// after it. No-ops entirely for a Candle, or for a Censer that already has a real mesh assigned (same
	// "only replace the still-placeholder Cube" guard, by asset path).
	void ApplyCenserPrototypeVisualsIfNeeded();

	// Censer-only: (re-)attaches ChainVisualComponent's START to SocketName on HeldMesh, and points its END
	// at this actor's own ItemMesh via SetAttachEndTo() — used by PickUp() (initial hand attach) and by
	// Extinguish()/LightUp() (switching between the hand socket and BeltSocketName as the item's lit state
	// changes). No-ops for a Candle.
	void AttachChainToSocket(USkeletalMeshComponent* HeldMesh, FName SocketName);

	// ItemMesh's relative scale as of BeginPlay, cached so DisableHighlight() can restore it exactly
	// instead of assuming a hardcoded 1.0 (which would be wrong for a Blueprint that scales the mesh).
	FVector BaseItemMeshScale = FVector::OneVector;

	// State for BeginTossVisual()/EndTossVisual()/Tick()'s own toss-visual block — see their comments.
	bool bIsPlayingTossVisual = false;
	float TossVisualElapsed = 0.0f;
	float TossVisualDuration = 1.0f;
	FVector TossVisualStartLocation = FVector::ZeroVector;

	// Cache of PickUp()'s own "hand" attach target — the LAST hand mesh/socket this item was actually
	// picked up into. Extinguish()/LightUp() use this to swap the item (and its chain) between the belt and
	// exactly the hand it started in, without either of them needing to know which hand that was ahead of
	// time.
	UPROPERTY()
	TWeakObjectPtr<USkeletalMeshComponent> CachedHeldMesh;
	FName CachedHandSocketName = NAME_None;

	// Whether this item is CURRENTLY attached to the dedicated swing-grip socket (SwingSocketName/
	// SwingSocketNameLeftHand) rather than its ordinary carry socket (CachedHandSocketName) — see Tick().
	// Lets Tick() detect the mounted+lit transition edge and only re-attach once, instead of calling
	// AttachToComponent() every single frame. Reset to false by Extinguish()/LightUp()/EndTossVisual()
	// whenever any of them move the item to a different socket on their own, so Tick() doesn't mistake a
	// stale "already on the swing socket" flag for reality after one of those runs.
	bool bIsAttachedToSwingSocket = false;

	// Seconds accumulated since the last SwingTrailEffect spawn while the orbit swing is active — see
	// Tick(). Reset to 0 whenever the swing stops, so it doesn't spawn a "backlog" burst the instant
	// mounting/lighting conditions are met again.
	float TrailSpawnTimer = 0.0f;

	// GetWorld()->GetTimeSeconds()/GetActorLocation() at the moment Throw() was last called — OnItemHit()
	// compares against these (with ThrowGracePeriodDuration/ThrowGraceDistance) to detect and ignore a
	// spurious spawn-clip hit against non-monster geometry. Also seeds LastFlightTraceLocation below.
	float ThrowStartTime = 0.0f;
	FVector ThrowStartLocation = FVector::ZeroVector;

	// This item's location as of the last CheckCenserFlightPathForMonster() call (or the throw's start
	// location, for the first tick after Throw()) — the sphere trace's start point for the NEXT tick's
	// travel segment.
	FVector LastFlightTraceLocation = FVector::ZeroVector;

	// The monster (if any) the thrower was riding at the moment Throw() launched this item. Thrown items
	// spawn/launch from a hand socket that sits directly on top of a ridden monster's own capsule, so
	// without this, the item's very first physics tick registers a direct OnComponentHit against the
	// thrower's OWN mount — immediately resolving (and wasting) the throw's one-shot impact against the
	// mount instead of whatever the player actually aimed at. Set in Throw(), cleared once the throw
	// resolves (OnItemHit()/CheckCenserFlightPathForMonster()) or on a fresh Throw(). Used to: (1) make
	// ItemMesh ignore this monster for the duration of the physics throw (see Throw()), and (2) as a
	// belt-and-braces guard in OnItemHit()/CheckCenserFlightPathForMonster() in case (1) somehow still
	// lets a hit through. UPROPERTY() for the same Live Coding re-instancing reason as HoldingCharacter
	// below.
	UPROPERTY()
	TWeakObjectPtr<ABaseMonster> ThrowerMountedMonster;

	// Monsters currently within HolyAuraSphere's radius per the last CheckAuraProximity() call, so aura
	// damage from that path fires once on approach (like BeginOverlap) instead of every 0.1s tick while
	// a monster lingers in range.
	UPROPERTY()
	TSet<TWeakObjectPtr<ABaseMonster>> MonstersInAuraRange;

	// Character actor currently holding this item, set in PickUp() from AttachToMesh's owner. DropItem()
	// uses this to tell ItemMesh to ignore that specific character's capsule for the first physics tick.
	// UPROPERTY so UHT/GC and Live Coding re-instancing track and update this reference correctly
	// instead of leaving stale raw memory behind a class reload.
	UPROPERTY()
	TWeakObjectPtr<AActor> HoldingCharacter;

	// Timer that restores normal collision between ItemMesh and HoldingCharacter shortly after dropping,
	// so a dropped item doesn't stay permanently walk-through-able for whoever dropped it.
	// UPROPERTY for the same reason as HoldingCharacter above.
	UPROPERTY()
	FTimerHandle ReenableHolderCollisionTimerHandle;

	// Gates how often CheckAuraProximity()'s on-foot "leg swipe" can roll — see LegSwipeCooldown.
	UPROPERTY()
	FTimerHandle LegSwipeCooldownTimerHandle;

	// Fires LightUp() after LegSwipeRelightDelay when a leg swipe extinguishes this item with fuel left.
	UPROPERTY()
	FTimerHandle LegSwipeRelightTimerHandle;
};
