// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseCharacter.h"
#include "PriestCharacter.generated.h"

class AHolyItem;
class UCameraComponent;
class UAnimationAsset;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class USkeletalMeshComponent;
class UResourceStockpileComponent;
class USoundBase;
class UVisionFireSourceComponent;

// The Priest: uses Holy Candles, which create Holy Protection Zones that damage/repel monsters (see
// AHolyItem::ItemType == EHolyItemType::Candle, and ABaseMonster::IsLocationProtected()). No new
// mechanics of its own beyond ABaseCharacter's existing generic item pickup/throw/aim system — every
// AHolyItem already carries its own Candle-vs-Censer behavior — so this class exists purely to give the
// Priest a distinct type in the world/Blueprint hierarchy, plus an optional starting-Candle convenience.
UCLASS()
class MYCHARACTERS_API APriestCharacter : public ABaseCharacter
{
	GENERATED_BODY()

public:
	APriestCharacter();

	// Optional starting Candle class — if set, BeginPlay() spawns one and immediately picks it up, so a
	// level doesn't need a Candle pre-placed for the Priest to start equipped. Left unset (None) by
	// default: purely a designer convenience hook, not a hard requirement.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Priest")
	TSubclassOf<AHolyItem> StartingCandleClass;

	// Candle reserve — see UResourceStockpileComponent's own header comment. Spend/refill logic is a
	// separate future task; for now just holds the starting amount and feeds RefreshCandleStockpileSlot()'s
	// HUD readout.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Priest")
	TObjectPtr<UResourceStockpileComponent> CandleStockpile;

	// Per-hand chance a lit candle breaks for good (not pocketed) when EnterPanicState() extinguishes it,
	// rolled independently for each hand — see EnterPanicState()'s own comment.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Priest")
	float CandleBreakChanceOnPanic = 0.2f;

	// "Воспалённое зрение" — see UVisionFireSourceComponent's own header comment.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Priest")
	TObjectPtr<UVisionFireSourceComponent> VisionFireSourceComponent;

	// First-person camera used only while "Воспалённое зрение" is active. In third person the fire's
	// direction (driven by camera yaw, via UVisionFireSourceComponent::UpdateFirePlacement()) can visibly
	// diverge from the body's own facing (driven by movement, not mouse look — see ABaseCharacter's
	// bOrientRotationToMovement), which reads as the fire shining somewhere the character visibly isn't
	// looking. Swapping to an embedded head camera (and hiding the mesh from its own view — see
	// SetVisionFirstPersonActive()) makes "camera forward" and "what the character sees" the same thing by
	// construction, since there's no longer a third-person body on screen to visibly disagree with it — the
	// trace math in UpdateFirePlacement() itself is unchanged. Inactive by default (SetActive(false) in the
	// constructor); nothing but SetVisionFirstPersonActive() ever toggles it. Doesn't touch
	// ABaseCharacter's own CameraBoomComponent/GetFollowCamera() at all — AActor::CalcCamera() already
	// picks whichever of this actor's camera components has IsActive() true, so no further wiring is
	// needed for the view to actually switch.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Priest")
	TObjectPtr<UCameraComponent> VisionFirstPersonCamera;

	// Bone VisionFirstPersonCamera attaches to — "head" on this project's Simple Mannequin skeletons, same
	// lowercase UE5 Mannequin bone naming already used by RightHandSocket/LeftHandSocket.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Priest")
	FName VisionFirstPersonCameraSocket = TEXT("head");

	// Swaps between ABaseCharacter's normal third-person GetFollowCamera() and VisionFirstPersonCamera, and
	// hides this character's own mesh from its own camera (GetMesh()->SetOwnerNoSee()) while first-person —
	// otherwise the head camera would render looking out from inside the character's own head mesh. Called
	// by UVisionFireSourceComponent::ToggleVision() on every activate/deactivate of "Воспалённое зрение";
	// public so that component (not a subclass of this class) can reach it. Safe to call redundantly with
	// the same bActive value.
	// bShowRaisedArm = true is the "3D hand" mode of the vision (UVisionFireSourceComponent::bUse3DHand): the
	// mesh is NOT hidden from the head camera any more — only the "head" bone is (so the camera is not inside
	// the head), and bVisionArmRaised goes true so the AnimBP can lift the right arm into the camera's view.
	UFUNCTION(BlueprintCallable, Category = "Priest")
	void SetVisionFirstPersonActive(bool bActive, bool bShowRaisedArm = false);

	// True while the vision wants the right arm raised in front of the face. READ THIS IN THE ANIMBP
	// (Cast to APriestCharacter) and blend the raised-arm pose in/out with it (e.g. Layered blend per bone
	// from clavicle_r, or Blend Poses by bool with a short blend time). Nothing in C++ moves the bones.
	UPROPERTY(BlueprintReadOnly, Category = "Priest|Vision Arm")
	bool bVisionArmRaised = false;

	// ---- 3D hand glued to the camera ("как та картинка, но 3D") ----
	// While the vision is on (and UVisionFireSourceComponent::bUse3DHand), this second skeletal mesh — the same
	// mesh asset as the character, attached to VisionFirstPersonCamera and visible only to the owner — plays the
	// one-frame pose VisionArmsAnimation. Because it is a child of the camera it stays put on the screen whatever
	// way you look, exactly like the old flat picture. Its head is lined up with the camera, so only the arm that
	// reaches forward comes into view; the rest of the body is behind/below the camera. The character's own mesh
	// stays hidden from the camera as before.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Priest|Vision Arm")
	TObjectPtr<USkeletalMeshComponent> VisionArmsMesh;

	// The pose to play on VisionArmsMesh (your AS_VisionHand_Pose). Set it in the Priest Blueprint's Class Defaults.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Priest|Vision Arm")
	TObjectPtr<UAnimationAsset> VisionArmsAnimation;

	// (The hand is deliberately plain: the vision's post-process paints it black like everything near the camera.
	// A glow/material for it was tried and dropped on 12.11.2026.)

	// Where the arms mesh sits relative to the camera, in the camera's own axes (X forward, Y right, Z up, cm),
	// ON TOP of lining its head up with the camera. This is the main knob for "where is the hand on the screen":
	// X pushes the hand away from / toward the eye, Y moves it right/left, Z up/down. Tunable live in PIE.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Priest|Vision Arm")
	FVector VisionArmsOffset = FVector(0.0f, 0.0f, 0.0f);

	// Extra turn of the mesh relative to the camera. Default (0,-90,0) makes the mannequin face where the camera looks.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Priest|Vision Arm")
	FRotator VisionArmsRotation = FRotator(0.0f, -90.0f, 0.0f);

	// While the hand rises (or lowers) it is shifted by this much (camera axes, cm) at alpha 0 — by default
	// 40 cm down, so the hand slides up into view when the vision starts and sinks back when it ends.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Priest|Vision Arm")
	FVector VisionArmsRaiseTravel = FVector(0.0f, 0.0f, -40.0f);

	// Gentle sway of the hand: how far it drifts (cm, camera axes) at the peak, at VisionArmBobSpeed swings/second.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Priest|Vision Arm")
	FVector VisionArmsBobOffset = FVector(0.0f, 0.0f, 1.2f);

	// ---- OLD (AnimBP bone nodes) values below: not needed by the camera-attached hand, kept so the AnimBP that reads them still compiles ----
	// Raised-arm pose, tunable live in PIE (Details of the Priest → Priest|Vision Arm) ----
	// The AnimBP only has to feed the three "Rotation Out" values below into three Transform (Modify) Bone nodes
	// (upperarm_r, lowerarm_r, hand_r; Mode = Add to Existing, Space = Bone Space). The numbers are degrees added
	// on top of the normal animation when the arm is fully raised; the pose itself is found by trying values.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Priest|Vision Arm")
	FRotator VisionUpperArmPose = FRotator(0.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Priest|Vision Arm")
	FRotator VisionLowerArmPose = FRotator(0.0f, 0.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Priest|Vision Arm")
	FRotator VisionHandPose = FRotator(0.0f, 0.0f, 0.0f);

	// Gentle up-and-down sway of the raised arm: degrees on each axis at the peak (added to the upper arm, half of it
	// to the hand) and how many full swings per second. Set the axis that lifts the arm; 0 = no sway.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Priest|Vision Arm")
	FRotator VisionArmBobAmount = FRotator(0.0f, 0.0f, 3.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Priest|Vision Arm", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float VisionArmBobSpeed = 0.6f;

	// How fast the arm rises/lowers (higher = quicker).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Priest|Vision Arm", meta = (ClampMin = "0.5", ClampMax = "20.0"))
	float VisionArmRaiseSpeed = 5.0f;

	// 0..1, smoothed version of bVisionArmRaised.
	UPROPERTY(BlueprintReadOnly, Category = "Priest|Vision Arm")
	float VisionArmRaiseAlpha = 0.0f;

	// The ready-made values for the AnimBP: pose * alpha (+ sway). Plug into the Rotation pin of each bone node.
	UPROPERTY(BlueprintReadOnly, Category = "Priest|Vision Arm")
	FRotator VisionUpperArmRotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Priest|Vision Arm")
	FRotator VisionLowerArmRotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Priest|Vision Arm")
	FRotator VisionHandRotation = FRotator::ZeroRotator;

	// Bone/socket of the fingertip where the vision's fire is aimed from while the 3D hand is used. If it does
	// not exist on the skeleton, RightHandSocket-like fallback "hand_r" is used.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Priest|Vision Arm")
	FName VisionFingertipSocket = TEXT("middle_03_r");

	// World position of the raised hand's fingertip (falls back to the hand bone). False if there is no mesh.
	bool GetVisionFingertipLocation(FVector& OutLocation) const;

	// Same fingertip but in the first-person camera's own space (cm; X forward, Y right, Z up), computed from the
	// arms mesh's local transform only — no world position involved, so a flame attached to the camera at this point
	// never lags or jumps while the camera/character moves. False if the arms mesh is not showing.
	bool GetVisionFingertipCameraLocal(FVector& OutLocal) const;

	// Radius (in units), centered on the Priest, TryScatterPrayer() finds live ABaseMonster in — same
	// SphereOverlapActors() call ARiderCharacter::ResolveHolyExplosion() uses, just non-lethal here.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Priest|Scatter Prayer")
	float ScatterPrayerRadius = 500.0f;

	// Seconds TryScatterPrayer() blocks itself from re-activating — checked via IsTimerActive() only, no
	// resolve callback (see the .cpp).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Priest|Scatter Prayer")
	float ScatterPrayerCooldown = 20.0f;

	// Outward launch speed (units/sec) ApplyKnockback() gives each monster caught in ScatterPrayerRadius —
	// same order of magnitude as AHolyItem::CenserPushbackForce, see that field's own comment for reference.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Priest|Scatter Prayer")
	float ScatterPrayerKnockbackForce = 1000.0f;

	// Upward component (units/sec) ApplyKnockback() gives each monster caught in ScatterPrayerRadius — same
	// order of magnitude as AHolyItem::CenserPushbackUpwardForce.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Priest|Scatter Prayer")
	float ScatterPrayerUpwardForce = 50.0f;

	// Optional feedback sound, played at the Priest's location on activation — no default (safe no-op if
	// unset), same "designer convenience hook" reasoning as StartingCandleClass above. Real VFX for this
	// ability is a separate future task.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Priest|Scatter Prayer")
	TObjectPtr<USoundBase> ScatterPrayerSound;

	// "Раскидывание" — the Priest's non-lethal counterpart to ARiderCharacter::TryHolyExplosion(): pushes
	// and stuns every live ABaseMonster within ScatterPrayerRadius, radially outward from the Priest, via
	// the same public ABaseMonster::ApplyKnockback() + UCandleDamageType stun hit AHolyItem's own aura
	// already uses — never kills. Doesn't spend CandleStockpile. Silent no-op while panicking or already on
	// cooldown. Bound to X in SetupPlayerInputComponent().
	UFUNCTION(BlueprintCallable, Category = "Priest|Scatter Prayer")
	void TryScatterPrayer();

protected:
	virtual void BeginPlay() override;

	virtual void Tick(float DeltaTime) override;

	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	// Crouches while holding a lit Candle in either hand, stands back up once none remain — called from
	// every point EquippedRightHand/EquippedLeftHand actually change (PickUp()/ThrowItem()/Interact()'s
	// drop branches, see ABaseCharacter::OnHeldItemsChanged()'s own comment). Requires
	// GetCharacterMovement()->NavAgentProps.bCanCrouch = true — set in ABaseCharacter's own constructor
	// (every hero can crouch via 'C' now, not just the Priest) — ACharacter::Crouch() silently no-ops
	// without it. Can briefly fight ToggleCrouch()'s own 'C' binding if both land on the same frame (e.g.
	// picking up a lit Candle while already mid-crouch-toggle) — same known class of edge case as this
	// project's other independent MaxWalkSpeed modifiers stacking unexpectedly (see ToggleSprint()'s own
	// comment), not something this override tries to fully resolve.
	virtual void OnHeldItemsChanged() override;

	// Priest-only pickup behavior: a highlighted Candle goes into CandleStockpile (pocket) instead of a
	// hand, if there's room — see the .cpp for the full branching. Everything else (nothing highlighted,
	// trapped, a non-Candle item, or the pocket already full) falls through to Super::Interact() exactly
	// as before.
	virtual void Interact() override;

	// Panic extinguishes both hands: any lit candle is snuffed and returned to the pocket at its current
	// fuel level (same "no loss" behavior as a manual CycleCandleHands() extinguish), rather than being
	// left burning in a hand while the character is knocked down. Super::EnterPanicState() runs first and
	// unchanged — this only adds the candle cleanup after it.
	virtual void EnterPanicState(ABaseMonster* AttackingMonster) override;

private:
	// Bound directly to V — free key, not claimed by any mechanic in CLAUDE.md's registry. Thin wrapper
	// because BindKey() needs an exact void(void) member signature, which
	// UVisionFireSourceComponent::ToggleVision() already matches, but the wrapper keeps this class's input
	// handlers consistent either way.
	UFUNCTION()
	void OnVisionFireTogglePressed();

	// Parallel to CandleStockpile: holds the remaining fuel of each pocketed candle, in pickup order.
	// CandleStockpile's own int32 count only tracks HOW MANY are in the pocket — this is what lets
	// CycleCandleHands() bring a partially-burned candle back out at the same fuel level it was
	// extinguished at, instead of always spawning a full one.
	TArray<float> PocketCandleFuel;

	// Bound directly to Q — free on this class (the old base-class "MeleeSwing" Q binding no longer
	// exists at all, see ABaseCharacter::SetupPlayerInputComponent()'s own comment). Cycles candles
	// between the pocket and both hands: empty hands light one from the pocket (right first, then
	// left); both hands lit extinguishes the right one back into the pocket (then the left on the next
	// press) at its current fuel level. No-op if the pocket is empty (when trying to light) or already
	// full (when trying to pocket) — see the .cpp.
	UFUNCTION()
	void CycleCandleHands();

	// Pushes CandleStockpile's current/max amount into the HUD's Utility slot (index 2, key "3") via
	// UInventoryComponent::SetSlotData() — see that function's own comment on why Utility slots are the
	// designer-populated ones. Called once from BeginPlay() for now; future spend/refill tasks will call
	// it again after CandleStockpile actually changes.
	void RefreshCandleStockpileSlot();

	// Guards TryScatterPrayer() against re-activating during ScatterPrayerCooldown — polled via
	// IsTimerActive() only, never fires a callback (see TryScatterPrayer()'s own comment).
	FTimerHandle ScatterPrayerCooldownTimerHandle;
};
