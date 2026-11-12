// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "InputActionValue.h"
#include "BaseCharacter.generated.h"

class AHolyItem;
class ABaseMonster;
class ABearTrap;
class USpringArmComponent;
class UCameraComponent;
class UCrosshairWidget;
class UHeartbeatAudioComponent;
class UHidingComponent;
class UInventoryComponent;
class UProbeComponent;
class UPlayerHUDWidget;
class UControlsHintWidget;
struct FControlHint;
class UInputMappingContext;
class UInputAction;
class UPostProcessComponent;

// Broadcast whenever Health actually changes (currently only TakeDamage() — no heal source exists yet)
// so UPlayerHUDWidget's health bar can update live instead of polling every frame.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnHealthChanged, float, NewHealth, float, MaxHealth);

// Broadcast whenever ScanForInteractable()'s HighlightedItem actually changes (only on a real change,
// not every 0.1s tick it runs) so the interaction prompt (see UCrosshairWidget's PromptText) can update
// without polling.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHighlightedItemChanged, AHolyItem*, NewHighlightedItem);

/**
 * Base character with a stealth "closed eyes" mechanic: while active, the
 * player's camera fades to black so the player can't see the world.
 */
UCLASS()
class MYCHARACTERS_API ABaseCharacter : public ACharacter
{
	GENERATED_BODY()

public:
	ABaseCharacter();

	// Starts closing the eyes: fades the camera to black.
	UFUNCTION(BlueprintCallable, Category = "Stealth")
	void CloseEyes();

	// Opens the eyes again: fades the camera back in.
	UFUNCTION(BlueprintCallable, Category = "Stealth")
	void OpenEyes();

	// Bound to LeftShift's IE_Pressed (see SetupPlayerInputComponent()) — TOGGLES bIsEyesClosed via
	// CloseEyes()/OpenEyes() rather than the old hold-to-close behavior, so the Nose-Breathing mini-game
	// (OnBreathPressed(), also Space) doesn't compete with the same hand having to hold Shift down the
	// whole time.
	UFUNCTION(BlueprintCallable, Category = "Stealth")
	void ToggleEyesClosed();

	// Forces the eyes open as a SIDE EFFECT of something else — currently only UMoonshineComponent::
	// UseMoonshine() (Roadmap 1.3: drinking moonshine forces the eyes open for the whole buff window, so
	// Rule 1.1's background threat is a real risk instead of something the player can just sit out with
	// eyes closed). Deliberately does NOT go through OpenEyes() itself: that function's unconditional
	// Blind Encounter instant-kill check (BlindEncounterMonster) is reserved for a genuinely VOLUNTARY
	// open (the player's own Shift press) — same convention already established by the private
	// ForceEyesOpenFromPanic() (see its own comment). This function is the public, non-panic-specific
	// equivalent of that same "involuntary open, no instant-kill check" pattern, for external callers like
	// UMoonshineComponent that can't reach ForceEyesOpenFromPanic() or bIsEyesClosed directly (both
	// private/protected). No proximity-kill roll either — ongoing exposure comes entirely from Rule 1.1
	// polling IsEyesClosed() for the caller's own buff duration, not a one-off check here. No-ops if the
	// eyes are already open.
	UFUNCTION(BlueprintCallable, Category = "Stealth")
	void ForceEyesOpen();

	bool IsEyesClosed() const { return bIsEyesClosed || bIsBlindedByAbility; }

	// True while an external ability (currently only ARiderCharacter's "Чернильное зрение" —
	// UInkVisionComponent) wants the owner to be treated as blind for detection purposes (Rule 1.1,
	// dodge/panic-strike checks, etc.) WITHOUT actually going through CloseEyes()/OpenEyes() — those carry
	// side effects (nose-breathing minigame, instant-kill-on-open-near-monster) that don't apply to this
	// kind of induced blindness. IsEyesClosed() above ORs this in, so every existing external caller (all
	// of them already go through that one getter, never the raw bIsEyesClosed field) is covered
	// automatically.
	UFUNCTION(BlueprintCallable, Category = "Stealth")
	void SetBlindedByAbility(bool bBlinded) { bIsBlindedByAbility = bBlinded; }

	// True while an external ability (currently only APriestCharacter's
	// "Воспалённое зрение" — UVisionFireSourceComponent) wants its own screen
	// effect visible even though bIsEyesClosed is true — Tick()
	// reads this so the eyes-closed darkening doesn't crush the ability's own post-process material. Does NOT affect
	// IsEyesClosed() or any monster-detection logic — Rule 1.1 still sees the
	// player as blind; this is purely a screen-rendering override. Always paired
	// on/off by whoever sets it (see SetEyesClosedDarknessSuppressed()).
	UFUNCTION(BlueprintCallable, Category = "Stealth")
	void SetEyesClosedDarknessSuppressed(bool bSuppressed) { bIsDarknessSuppressedByAbility = bSuppressed; }

	// Generic lock preventing CloseEyes() (and therefore ToggleEyesClosed()) from doing anything while
	// true — deliberately NOT Moonshine-specific: any future mechanic that needs to force the eyes to
	// stay open for a duration can reuse this same flag instead of ABaseCharacter knowing about it by
	// name (project convention: mechanics talk to ABaseCharacter through generic flags, not by having
	// this class reach into a specific component). Setting it does not itself open the eyes — callers
	// combine it with ForceEyesOpen() as needed.
	UFUNCTION(BlueprintCallable, Category = "Stealth")
	void SetEyesLockedOpen(bool bLocked) { bEyesLockedOpen = bLocked; }

	bool AreEyesLockedOpen() const { return bEyesLockedOpen; }

	// Mirror of SetEyesLockedOpen(): while true, a VOLUNTARY OpenEyes() (and therefore ToggleEyesClosed()
	// on closed eyes) does nothing. Same generic-flag convention — not named after the one mechanic that
	// sets it today (Priest's inflamed vision). Setting it does not itself close the eyes — callers
	// combine it with CloseEyes() as needed. Involuntary opens (ForceEyesOpen(), the panic path) are
	// deliberately NOT blocked by it — they are not a player choice.
	UFUNCTION(BlueprintCallable, Category = "Stealth")
	void SetEyesLockedClosed(bool bLocked) { bEyesLockedClosed = bLocked; }

	bool AreEyesLockedClosed() const { return bEyesLockedClosed; }

	// Generic monster-harm immunity — checked at the top of TakeDamage() (no-ops, Health untouched) and
	// EnterPanicState() (no-ops, no knockdown/breathing minigame). Deliberately NOT Moonshine-specific,
	// same "generic flag, not named after one caller" convention as SetEyesLockedOpen() above — currently
	// only UMoonshineComponent::UseMoonshine() sets this (Roadmap: Redneck can't be hurt or panicked by a
	// monster while the buff is active), but any future mechanic needing the same protection can reuse it.
	// Does NOT block the separate instant-kill paths that bypass Health/EnterPanicState entirely — a
	// monster's physical catch (ABaseMonster::OnKillPlayer()) and the Blind Encounter kill
	// (ResolveBlindEncounterKill()) are untouched by this flag; only damage and panic funnel through it.
	UFUNCTION(BlueprintCallable, Category = "Health")
	void SetImmuneToMonsterAttacks(bool bImmune) { bImmuneToMonsterAttacks = bImmune; }

	bool IsImmuneToMonsterAttacks() const { return bImmuneToMonsterAttacks; }

	// Called from CloseEyes()/OpenEyes() — empty base. CloseEyes()/OpenEyes() themselves never touch
	// Hunter's Focus's step markers at all (only a camera fade), so this hook exists purely so
	// ARedneckCharacter can forward the transition to its HunterFocusComponent, switching any currently-
	// spawned markers into their translucent, glowing "Mental Trail" state — visible even with the
	// camera faded near-black — while eyes are closed, and back while they're open. Same "virtual hook,
	// empty base" pattern as IsImmuneToMonsterCatch()/GetHunterFocusPromptText().
	UFUNCTION(BlueprintCallable, Category = "Hunter's Focus")
	virtual void OnEyesClosedChanged(bool bClosed) {}

	// Current health, [0, MaxHealth]. TakeDamage() decrements this and broadcasts OnHealthChanged, and
	// calls HandleDeath() once this reaches 0 — see that function's own comment. ABaseMonster::
	// OnKillPlayer() (the monster-catch-you kill) also calls HandleDeath() directly, independent of
	// Health/TakeDamage() entirely, since a "catch" is an instant kill rather than a damage-over-threshold
	// event.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Health")
	float Health = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Health")
	float MaxHealth = 100.0f;

	// True once HandleDeath() has run — guards against triggering the ragdoll/input-disable sequence
	// twice (e.g. TakeDamage() dropping Health to 0 in the same beat ABaseMonster::OnKillPlayer() also
	// calls it directly).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Death")
	bool bIsDead = false;

	// Replaces the old "instantly vanish" death: disables further movement/input, ragdolls the mesh (full-
	// body physics simulation via the engine's built-in "Ragdoll" collision profile — requires a Physics
	// Asset on this character's mesh to actually simulate per-bone, which every hero's Mannequin-derived
	// mesh has by default), and disables the capsule's own collision so it stops blocking monsters/other
	// actors as an invisible obstacle at its last standing spot once the mesh has physically fallen away
	// from it. Deliberately does NOT Destroy() or hide the actor — the whole point is that the ragdoll
	// stays visible on the ground; whatever calls this (currently only ABaseMonster::OnKillPlayer()) is
	// responsible for any actual respawn/level-reload logic, on its own delay so this has time to be seen
	// first. No-ops if already dead.
	UFUNCTION(BlueprintCallable, Category = "Death")
	void HandleDeath();

	// True while this character should be completely immune to a monster's catch/kill (checked by
	// ABaseMonster::CheckForPlayer() before its CatchDistance kill trigger, ahead of even calling
	// HandleDeath() — not a damage-reduction or a post-hit save, a full skip of the kill attempt itself).
	// Base implementation is never immune; ARedneckCharacter overrides this to cover its Moonshine Rage
	// Dash i-frames (see UMoonshineComponent::bIsMoonshineActive) — same "virtual hook, empty base"
	// pattern as OnHeldItemsChanged() below, so no other hero carries any dash-specific state here.
	UFUNCTION(BlueprintCallable, Category = "Death")
	virtual bool IsImmuneToMonsterCatch() const { return false; }

	// Height (Z, world units) above the death location the spectator camera settles at once detached in
	// HandleDeath() — see that function's own comment for the full death-camera behavior.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	float DeathCameraHeightOffset = 150.0f;

	// Horizontal distance (world units) the spectator camera pulls back from the death location, along the
	// boom's last live forward direction, once detached in HandleDeath().
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	float DeathCameraBackDistance = 250.0f;

	// True while caught in a Bear Trap (see ABearTrap/CaughtInTrap()) — severely restricts movement
	// (GetCharacterMovement()->MaxWalkSpeed clamped to TrappedMaxWalkSpeed) and blocks Moonshine
	// (UMoonshineComponent::UseMoonshine() checks this directly) until MashTrapEscape() has been pressed
	// TrapEscapePressesRequired times.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bear Trap")
	bool bIsTrapped = false;

	// [0, TrapEscapePressesRequired] — how many MashTrapEscape() presses have landed since CaughtInTrap().
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Bear Trap")
	int32 TrapEscapePresses = 0;

	// MashTrapEscape() presses required to break free.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bear Trap")
	int32 TrapEscapePressesRequired = 15;

	// GetCharacterMovement()->MaxWalkSpeed is clamped DOWN to this (never up — CaughtInTrap() only
	// lowers it) for as long as bIsTrapped is true, restored to whatever it was immediately beforehand
	// once EscapeTrap() runs.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bear Trap")
	float TrappedMaxWalkSpeed = 40.0f;

	// Small forward nudge (world units, along the actor's current forward vector, swept so it can't clip
	// through walls) applied on every MashTrapEscape() press while trapped — the attached ABearTrap
	// follows automatically since it's parented to the foot socket, so this is what actually "drags" it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Bear Trap")
	float TrapDragStepDistance = 8.0f;

	// Called by ABearTrap::OnTriggerOverlap() once it's attached itself to this character's foot socket —
	// applies the trapped state and resets TrapEscapePresses to 0. No-ops if already trapped (a second
	// trap's overlap shouldn't clobber CurrentTrap/PreTrapMaxWalkSpeed mid-escape).
	UFUNCTION(BlueprintCallable, Category = "Bear Trap")
	void CaughtInTrap(ABearTrap* Trap);

	// Bound directly to E (BindKey() — see SetupPlayerInputComponent(), no named Input Action needed) —
	// no-op unless bIsTrapped. Each call nudges the character TrapDragStepDistance forward and advances
	// TrapEscapePresses by one; reaching TrapEscapePressesRequired breaks free via EscapeTrap().
	UFUNCTION(BlueprintCallable, Category = "Bear Trap")
	void MashTrapEscape();

	// The monster currently threatening this character via a "blind touch" encounter (see
	// ABaseMonster::OnCapsuleHit()/EnterScanningState()/EnterPanicState() below) — set the instant
	// physical contact happens while bIsEyesClosed, cleared once the encounter resolves (monster loses
	// interest, the Nose-Breathing mini-game is won, or panic resolves one way or another). A deliberate/
	// voluntary eyes-open while this is set is an unconditional instant kill — see OpenEyes()'s own
	// check. Public (not wrapped in a setter) since ABaseMonster's own blind-encounter functions need to
	// read/write it directly — same "closely-related systems touch each other's public state directly"
	// convention EquippedRightHand/EquippedLeftHand already use with ABaseMonster elsewhere in this
	// project.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Panic")
	TWeakObjectPtr<ABaseMonster> BlindEncounterMonster;

	// Monotonically incremented every time a NEW panic episode starts (see EnterPanicState()) — lets a
	// "piling on" monster (see ABaseMonster::LastSeenPanicEpisodeId) tell a fresh episode apart from a
	// stale one it already attempted a strike against, purely by comparing integers, without this class
	// needing to know anything about monster-side bookkeeping.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Panic")
	int32 PanicEpisodeId = 0;

	// The monster that started the CURRENT panic episode (set alongside BlindEncounterMonster in
	// EnterPanicState()) — kept as its own field, separate from BlindEncounterMonster, since a panic
	// episode's "who started it" identity needs to persist for the whole episode even if
	// BlindEncounterMonster itself is later reassigned/cleared by an unrelated Blind Encounter resolution.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Panic")
	TWeakObjectPtr<ABaseMonster> PanicEpisodeOriginator;

	// GetWorld()->GetTimeSeconds() as of the most recent EnterPanicState() — other monsters piling onto
	// this episode (see ABaseMonster::PilingOnWindowDuration) use this to cap how long after the episode
	// began they're still allowed to arrive and take a swing.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Panic")
	float LastPanicEpisodeStartTime = -1000.0f;

	// True while lying on the ground after being knocked down (Scenario B of a blind encounter: kept
	// walking into a monster while eyes closed) and playing the Nose-Breathing mini-game (see PanicMeter's
	// own comment) — movement blocked, but NOT full DisableInput(), since OnBreathPressed() (Space) needs
	// to keep working the whole time.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Panic")
	bool bIsPanicking = false;

	// True from EndPanicSafely() (the panic meter cleared safely) until Move() sees a "forward" input edge
	// and calls RecoverFromKnockdown() — see that function's own comment. Split out from bIsPanicking itself
	// (which is already false by this point) so the character stays visibly down/still on the ground, not
	// standing back up automatically, until the player explicitly presses forward. Reset to false at the
	// start of EnterPanicState() too, in case a fresh hit lands (see the "piling on after recovery" path)
	// while a previous episode's get-up was never confirmed.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Panic")
	bool bIsAwaitingGetUp = false;

	// [0, PanicMeterMax] — 0 is fully calm (see EndPanicSafely()), PanicMeterMax is a fear-driven
	// involuntary eyes-open (see ForceEyesOpenFromPanic()). Set to InitialPanicMeter when EnterPanicState()
	// begins; OnBreathBeat() moves it by PanicReliefPerBreath/PanicPenaltyPerGasp every BreathRhythmInterval
	// depending on whether OnBreathPressed() (Space) was pressed at least once that cycle.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Panic")
	float PanicMeter = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panic")
	float PanicMeterMax = 100.0f;

	// Tuned down from an earlier 40 — playtesting showed the mini-game killing the player before they'd
	// had a chance to learn its rhythm at all. 25 plus the lowered PanicPenaltyPerGasp below gives roughly
	// 6 missed beats of runway before PanicMeterMax, instead of ~2.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panic")
	float InitialPanicMeter = 25.0f;

	// Seconds per Nose-Breathing "beat" cycle — the target moment (NextBreathBeatTime) OnBreathPressed()'s
	// timing is judged against, and what OnBreathWindowClosed() falls back to a mouth-gasp after if no
	// press ever landed in the window (see BreathToleranceWindow's own comment for the actual grace
	// window mechanics). Slowed from an earlier 1.2s to a calmer, easier-to-track rhythm.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panic")
	float BreathRhythmInterval = 2.0f;

	// Grace window (seconds, both before AND after NextBreathBeatTime) OnBreathPressed() counts as a
	// successful nose-breath — e.g. 0.3 means a press landing anywhere in [BeatTime - 0.3, BeatTime + 0.3]
	// hits. A press outside that window (too early/late) is judged immediately as a mouth-gasp, same as
	// never pressing at all — see OnBreathPressed()'s own comment. Widened from an earlier 0.15s; the
	// mini-game felt unfairly strict at that tolerance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panic")
	float BreathToleranceWindow = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panic")
	float PanicReliefPerBreath = 15.0f;

	// Lowered from an earlier 25 — see InitialPanicMeter's own comment on why the panic build-up rate
	// needed slowing down overall. A single missed beat should sting, not nearly end the encounter outright.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panic")
	float PanicPenaltyPerGasp = 12.0f;

	// PanicMeter jump (FMath::Min(PanicMeterMax, PanicMeter + this), same pattern RegisterMouthGasp() already
	// uses) applied by Move() the instant WASD input appears while bIsPanicking — movement itself is already
	// physically blocked (MaxWalkSpeed=0), but STRUGGLING against being pinned down is what actually costs
	// panic here. Deliberately large/"a nervous spike" rather than a per-tick drain — see Move()'s own
	// comment for why this fires once per press (edge-triggered), not continuously while a key is held.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panic")
	float PanicMovementPenalty = 30.0f;

	// GetWorld()->GetTimeSeconds() as of the most recent RegisterNoseBreath()/RegisterMouthGasp() — the
	// Breathing HUD (see UPlayerHUDWidget::PaintBreathingIndicator()) fades its hit/miss flash out over a
	// short window after this. -1 means no result has landed yet this panic episode.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Panic")
	float LastBreathResultTime = -1.0f;

	// True if the most recently resolved beat (LastBreathResultTime) was a successful nose-breath rather
	// than a mouth-gasp — the Breathing HUD reads this to flash green vs red.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Panic")
	bool bLastBreathWasHit = false;

	// Optional — nullptr by default, same "designer assigns a sound asset in the editor, code just plays
	// it if set" convention as ABaseMonster::BurnSound. A subtle inhale, played on every successful nose-
	// breath (RegisterNoseBreath()).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panic")
	class USoundBase* NoseBreathSound = nullptr;

	// A sharp gasp, played on every mouth-gasp (RegisterMouthGasp()) — including the one that gets this
	// character killed, since the sound is what a nearby monster is reacting to in the first place.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panic")
	class USoundBase* MouthGaspSound = nullptr;

	// Normalized [0, 1] proximity to the next breath beat — 1.0 exactly AT NextBreathBeatTime, 0.0 at the
	// midpoint between beats — read every frame by the Breathing HUD's pulsing rhythm indicator (see
	// UPlayerHUDWidget::PaintBreathingIndicator()) so it visually "converges" on each target beat. Always
	// 0 while not panicking.
	UFUNCTION(BlueprintCallable, Category = "Panic")
	float GetBreathBeatProximity() const;

	// Radius (world units) FindNearbyMonster() searches — used both by a missed breath beat (instant kill
	// if a monster's this close) and by ForceEyesOpenFromPanic()'s own "did everyone actually disperse"
	// check once PanicMeter maxes out.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panic")
	float MonsterProximityKillRadius = 500.0f;

	// Physical "knocked backward" impulse (LaunchCharacter(), same mechanism ABaseMonster::
	// ApplyKnockback() uses on monsters) applied once when EnterPanicState() begins, aimed away from the
	// monster that caused it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panic")
	float PanicKnockbackForce = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panic")
	float PanicKnockbackUpwardForce = 200.0f;

	// Played once (non-looping, via USkeletalMeshComponent::PlayAnimation() directly — NOT
	// PlaySlotAnimationAsDynamicMontage()/an authored Montage, since that path requires a matching Slot
	// node somewhere in this mesh's Animation Blueprint graph, which isn't guaranteed to exist; PlayAnimation
	// switches the mesh to AnimationSingleNode mode instead, bypassing the AnimBP entirely and so working
	// regardless) the instant EnterPanicState() knocks the character down. Not looping means the mesh just
	// holds its final frame once it finishes, which is what keeps the character visibly lying on the ground
	// for the rest of the Panic State. Defaults to one of the project's existing Mannequin "Death" anim
	// sequences (see the constructor) purely reused for its knockdown POSE — entirely unrelated to bIsDead/
	// HandleDeath(), which still ragdolls via physics simulation, not any animation asset at all.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panic")
	class UAnimSequence* KnockdownFallAnimation = nullptr;

	// Played once (non-looping) by RecoverFromKnockdown() when panic resolves safely, blending the
	// character back onto its feet before movement control is restored. Nullptr by default — no dedicated
	// get-up clip exists in this project yet (only fall/death poses do); if left unset, recovery just snaps
	// straight back to normal AnimBP-driven locomotion instead of playing anything.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Panic")
	class UAnimSequence* GetUpAnimation = nullptr;

	// Called by ABaseMonster's blind-encounter Scenario B resolution — knocks this character down and
	// starts the Nose-Breathing mini-game (see PanicMeter's own comment). No-ops if already dead/trapped/
	// panicking. Deliberately does NOT DisableInput() — that would also block the Space presses the mini-
	// game itself depends on; MaxWalkSpeed is zeroed instead to stop movement specifically, and the launch
	// impulse is applied BEFORE that so it isn't frozen mid-air.
	UFUNCTION(BlueprintCallable, Category = "Panic")
	virtual void EnterPanicState(ABaseMonster* AttackingMonster);

	// Bound directly to Space (BindKey() — see SetupPlayerInputComponent(); shares the physical key with
	// Jump/Dismount, same "both bound, each self-gates" pattern as Interact()/MashTrapEscape() sharing E)
	// — no-op unless bIsPanicking OR if this beat's already been resolved (bBeatResolvedThisWindow — extra
	// presses after a hit/miss already landed this cycle do nothing). Judged immediately, not deferred to
	// a timer: within BreathToleranceWindow of NextBreathBeatTime -> RegisterNoseBreath(); otherwise
	// (pressed too early/late) -> RegisterMouthGasp(), exactly the same outcome as never pressing at all
	// and letting OnBreathWindowClosed() catch it.
	UFUNCTION(BlueprintCallable, Category = "Panic")
	void OnBreathPressed();

	// True if any living ABaseMonster is within Radius of this character — used by ForceEyesOpenFromPanic()'s
	// "is anyone still close enough to notice" kill/survive check once PanicMeter maxes out. NOT used by
	// individual missed beats anymore (RegisterMouthGasp() only raises PanicMeter) — a single mistimed
	// press should never be an instant kill by itself. Returns the actual nearest one found (or nullptr) so
	// callers can route the kill through a specific monster's own ResolveBlindEncounterKill() rather than
	// assuming it's always BlindEncounterMonster itself — the monster that ends up close enough to matter
	// isn't necessarily the one that started the encounter.
	UFUNCTION(BlueprintCallable, Category = "Panic")
	class ABaseMonster* FindNearbyMonster(float Radius) const;

	// True while sprinting (see ToggleSprint()/StartSprinting()/StopSprinting()) —
	// GetCharacterMovement()->MaxWalkSpeed is SprintSpeed instead of DefaultWalkSpeed while this is true.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	bool bIsSprinting = false;

	// GetCharacterMovement()->MaxWalkSpeed while NOT sprinting — captured in BeginPlay() from whatever the
	// CharacterMovementComponent's own MaxWalkSpeed already is (300 by default, set in the constructor,
	// unless a Blueprint subclass overrides it), so StopSprinting() always has an accurate speed to
	// restore rather than a value hardcoded here that could silently drift out of sync with the real
	// default. Same "capture, don't hardcode" reasoning the old RunSpeed field used.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Movement")
	float DefaultWalkSpeed = 300.0f;

	// GetCharacterMovement()->MaxWalkSpeed while bIsSprinting.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	float SprintSpeed = 600.0f;

	// Toggles sprinting: flips bIsSprinting and applies the matching speed in one call (StopSprinting()
	// if currently sprinting, StartSprinting() otherwise) — a single press flips state, rather than the
	// press/hold behavior StartSprinting()/StopSprinting() used before. Bound to Left Alt by
	// SetupPlayerInputComponent() (IE_Pressed only — no IE_Released binding anymore).
	UFUNCTION(BlueprintCallable, Category = "Movement")
	void ToggleSprint();

	// Starts sprinting: sets GetCharacterMovement()->MaxWalkSpeed to SprintSpeed. No-ops if already
	// sprinting or if IsCrouched() is true — sprinting while crouched is blocked outright rather than
	// silently un-crouching or queuing up for when the crouch ends.
	UFUNCTION(BlueprintCallable, Category = "Movement")
	void StartSprinting();

	// Stops sprinting: restores GetCharacterMovement()->MaxWalkSpeed to DefaultWalkSpeed. Called by
	// ToggleSprint() on the press that ends a sprint, and every Tick() if a crouch starts mid-sprint
	// (bIsSprinting is a continuous block on crouch, not just a check at the moment Left Alt was
	// toggled) — while actually crouched, GetCharacterMovement()->GetMaxSpeed() reports
	// MaxWalkSpeedCrouched (150, set in the constructor) regardless of MaxWalkSpeed, so the character's
	// real movement speed still ends up at 150 the instant the crouch takes effect even though this
	// restores MaxWalkSpeed to the un-crouched 300 baseline underneath it.
	UFUNCTION(BlueprintCallable, Category = "Movement")
	void StopSprinting();

	// Loudness passed to MakeNoise() while sprinting AND actually moving — see Tick()'s own comment on
	// why this is throttled to SprintNoiseInterval rather than firing every frame.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Noise")
	float SprintNoiseLoudness = 1.0f;

	// Radius (world units) MakeNoise() reports while sprinting — 3500 units is roughly 35m, meant to
	// attract nearby monsters/AI hearing perception across a wide radius while the player is sprinting.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Noise")
	float SprintNoiseRadius = 3500.0f;

	// Seconds between sprint noise pulses (see Tick()) — throttles MakeNoise() to a periodic pulse
	// instead of once per frame, since a stream of Loudness=1.0 events every tick would be both wasteful
	// and meaningless to anything listening for a single noise event.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Noise")
	float SprintNoiseInterval = 0.5f;

	// Toggles crouching: Crouch() if not already crouched, UnCrouch() otherwise — the standard engine
	// crouch subsystem (ACharacter::Crouch()/UnCrouch(), enabled via
	// GetCharacterMovement()->NavAgentProps.bCanCrouch = true in the constructor), which shrinks the
	// capsule and moves the mesh down to match automatically. Bound to 'C' by
	// SetupPlayerInputComponent(). No explicit sprint-block here — if bIsSprinting is true when this
	// fires, Tick()'s own bIsSprinting && IsCrouched() check cuts the sprint on the very next frame once
	// IsCrouched() actually flips true (crouching is processed during movement simulation, not
	// synchronously the instant Crouch() is called — see IsCrouched()'s own engine comment).
	// FollowCameraComponent's own view height transition is handled separately — see
	// CrouchCameraZOffset's own comment for why that needs its own smoothing on top of the capsule's own
	// (uninterpolated) resize.
	UFUNCTION(BlueprintCallable, Category = "Movement")
	void ToggleCrouch();

	// Extra Z offset (world units, added to CameraBoomComponent's SocketOffset — negative moves the
	// camera down) blended in additionally while IsCrouched() is true, via the SAME FInterpTo/VInterpTo
	// block in Tick() that already blends AimCameraSocketOffset/DefaultCameraSocketOffset for aiming.
	// This exists because the capsule/mesh's own crouch transition (driven entirely by
	// UCharacterMovementComponent, see ToggleCrouch()'s own comment) is an instant, one-frame resize with
	// no built-in interpolation of its own — left alone, the camera (attached to the capsule via
	// CameraBoomComponent) would inherit that same instant snap. Applying this as an ADDITIONAL offset on
	// top of whatever CameraAimInterpSpeed-driven blend is already running, rather than a separate
	// dedicated interpolant, means crouching mid-aim still gets one smooth, unified camera move instead
	// of two independent interpolations fighting each other.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Camera")
	float CrouchCameraZOffset = -40.0f;

	// See FOnHealthChanged's own comment.
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnHealthChanged OnHealthChanged;

	// See FOnHighlightedItemChanged's own comment.
	UPROPERTY(BlueprintAssignable, Category = "Holy Items")
	FOnHighlightedItemChanged OnHighlightedItemChanged;

	// The quick-slot inventory — see UInventoryComponent's own header comment. Created in the
	// constructor so every hero has one regardless of subclass (the HUD needs it either way).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	TObjectPtr<UInventoryComponent> InventoryComponent;

	// Универсальная механика "Прощупывание" — см. UProbeComponent's own header comment. Каждый герой
	// получает его независимо от подкласса, тот же прецедент, что и у InventoryComponent.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Probe")
	TObjectPtr<UProbeComponent> ProbeComponent;

	// Универсальная механика "Укрытия" — см. UHidingComponent's own header comment. Каждый герой получает
	// его независимо от подкласса, тот же прецедент, что и у InventoryComponent/ProbeComponent.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hiding")
	TObjectPtr<UHidingComponent> HidingComponent;

	// True while UHidingComponent::bIsHidden — same one-line "read a component's state without exposing
	// the component itself to unrelated code" pattern as IsEyesClosed(). Checked by
	// ABaseMonster::IsPlayerVisible()/IsPlayerLookingAtMonster() so a hidden player is never detected by
	// either channel.
	bool IsHiddenInSpot() const;

	// Универсальное сердцебиение/дыхание — см. UHeartbeatAudioComponent's own header comment. Каждый
	// герой получает его независимо от подкласса, тот же прецедент, что и у InventoryComponent/
	// ProbeComponent/HidingComponent.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<UHeartbeatAudioComponent> HeartbeatAudioComponent;

	// Item currently held in the right hand, or nullptr if that hand is empty.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Holy Items")
	AHolyItem* EquippedRightHand = nullptr;

	// Item currently held in the left hand, or nullptr if that hand is empty.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Holy Items")
	AHolyItem* EquippedLeftHand = nullptr;

	// Item the interact scan is currently aimed at, or nullptr if none — kept outlined via
	// AHolyItem::EnableHighlight()/DisableHighlight() while it holds this slot.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Holy Items")
	AHolyItem* HighlightedItem = nullptr;

	// True while the player is aiming (RMB held, see StartAiming()/StopAiming()) — exposed for the
	// Animation Blueprint/camera to react to (e.g. an aim-down-sights pose or FOV change). Throwing
	// itself does not require aiming first — see ThrowItem() — this is purely presentational state.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Holy Items|Throw")
	bool bIsAiming = false;

	// True while aiming, for switching states in the Animation Blueprint.
	UFUNCTION(BlueprintCallable, Category = "Holy Items|Throw")
	bool GetIsAiming() const { return bIsAiming; }

	// Initial launch speed (units/sec) imparted to a thrown item, toward the aim target point computed
	// by TraceAimTarget() (the same point the reticle is drawn at). See ThrowItem().
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Items|Throw")
	float ThrowSpeed = 1500.0f;

	// Max distance (in units) the aim trace (TraceAimTarget()) reaches while bIsAiming — the reticle's
	// range, and (via ThrowItem() launching toward that same point) effectively the max distance a
	// throw can be aimed at, independent of whether ThrowSpeed could physically cover it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Items|Throw")
	float AimTraceDistance = 3000.0f;

	// Buffer (in units) ADDED to GetMountedMonster()'s own GetScaledCapsuleRadius() when ThrowItem()
	// pushes the launch origin forward while IsMounted() — see ThrowItem(). Same "compute from the real
	// capsule, not a flat guess" reasoning as UMountRiderComponent's MountRiderHeightBuffer, since a
	// hand-socket launch point otherwise spawns physically inside the ridden monster's own capsule
	// (Frame 0 self-hit — see ThrowItem()). Stays here (not in the mounting system) since throwing is a
	// universal mechanic every hero shares — only ARiderCharacter can ever actually be mounted, but any
	// hero's ThrowItem() needs to know how to adapt if IsMounted() ever reports true.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Items|Throw")
	float MountedThrowClearanceBuffer = 60.0f;

	// Fallback forward-offset distance (in units) ThrowItem() uses instead, only if GetMountedMonster()'s
	// GetCapsuleComponent() is somehow invalid at throw time.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Items|Throw")
	float MountedThrowFallbackOffset = 150.0f;

	// Screen-space HUD crosshair dot — a UCrosshairWidget instance (see its own header comment), created
	// in BeginPlay() and added full-screen to this locally-controlled player's viewport, replacing the
	// old 3D world-space "flying ball" reticle (mesh components that followed the aim trace's hit point
	// through the world). Repositioning isn't needed anymore (it's a fixed screen-center dot); only its
	// DotColor and visibility are updated every Tick() while bIsAiming, by UpdateAimCrosshair().
	UPROPERTY()
	TObjectPtr<UCrosshairWidget> CrosshairWidgetInstance;

	// Widget class CrosshairWidgetInstance is created from. Defaults to UCrosshairWidget itself (fully
	// native, no Widget Blueprint asset required) — override with a Blueprint subclass here only if
	// visual customization beyond DotColor/DotRadius/OutlineThickness/OutlineColor is ever needed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Items|Throw")
	TSubclassOf<UCrosshairWidget> CrosshairWidgetClass;

	// The Horror HUD — health bar, quick-slot bar, mechanic/status readouts. A UPlayerHUDWidget instance
	// (see its own header comment), created in BeginPlay() and added full-screen to this
	// locally-controlled player's viewport, same pattern as CrosshairWidgetInstance above but at a lower
	// Z-order so the crosshair always draws on top of it.
	UPROPERTY()
	TObjectPtr<UPlayerHUDWidget> PlayerHUDWidgetInstance;

	// Widget class PlayerHUDWidgetInstance is created from. Defaults to UPlayerHUDWidget itself (fully
	// native, no Widget Blueprint asset required) — set a WBP_HUD Blueprint subclass here to skin the HUD
	// without touching C++; see UPlayerHUDWidget's own header comment on bUseNativeDrawing/its
	// BlueprintImplementableEvents for how that skinning hook works.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HUD")
	TSubclassOf<UPlayerHUDWidget> PlayerHUDWidgetClass;

	// On-screen controls list (top-right corner, H hides/shows it) — see UControlsHintWidget. Created in
	// InitializeLocalPlayerUI() next to PlayerHUDWidgetInstance.
	UPROPERTY()
	TObjectPtr<UControlsHintWidget> ControlsHintWidgetInstance;

	// Fills the on-screen controls list (UControlsHintWidget). This base version adds the keys every hero
	// shares; each hero class overrides it, adds its own section first, then calls Super. Keep in sync with
	// SetupPlayerInputComponent() of the same class — a new/changed key binding there needs its line here.
	virtual void GetControlHints(TArray<FControlHint>& OutHints) const;

	// Crosshair dot color while the aim trace hits neither a mountable monster nor a target/item — plain
	// world geometry, sky/no hit, or anything else not covered by the two colors below.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Items|Throw")
	FLinearColor CrosshairDefaultColor = FLinearColor(0.85f, 0.85f, 0.85f); // Light grey

	// Crosshair dot color while the aim trace hits an ABaseMonster (this project's mountable-creature
	// class) — see UpdateAimCrosshair().
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Items|Throw")
	FLinearColor CrosshairMountableColor = FLinearColor::Green;

	// Crosshair dot color while the aim trace hits a target/item — an AHolyItem lying in the world, or
	// any actor tagged "Target" (e.g. a target-dummy Blueprint with no dedicated C++ class) — see
	// UpdateAimCrosshair().
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Items|Throw")
	FLinearColor CrosshairTargetColor = FLinearColor::Red;

	// TargetArmLength the camera boom (see CameraBoomComponent) interpolates toward while bIsAiming —
	// closer than the default follow distance, Witcher-3-style over-the-shoulder aiming.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Items|Throw|Camera")
	float AimCameraArmLength = 120.0f;

	// SocketOffset the camera boom interpolates toward while bIsAiming — positive Y shifts the camera
	// right (over the right shoulder), positive Z shifts it up. Y=50 keeps the character's back/side
	// fully in frame; Z=65 clears the foreground floor and frames at eye/shoulder height instead of
	// looking down at it, in a tight over-the-shoulder shot at AimCameraArmLength=120. If the
	// character's shoulder width/height changes, this may need retuning.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Items|Throw|Camera")
	FVector AimCameraSocketOffset = FVector(0.0f, 50.0f, 65.0f);

	// FInterpTo/VInterpTo speed used to blend the camera boom's TargetArmLength/SocketOffset between
	// their default (cached at BeginPlay) and Aim* values above as bIsAiming toggles.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Items|Throw|Camera")
	float CameraAimInterpSpeed = 8.0f;

	// FOV (degrees) the camera interpolates toward while bIsAiming — an ADS-style zoom. Ignored (no FOV
	// change at all) if BeginPlay() can't find a UCameraComponent, same fallback as the SpringArm above.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Items|Throw|Camera")
	float AimCameraFOV = 70.0f;

	// "Eye Peek Risk": while bIsAiming (RMB held — squinting/peeking, as opposed to ToggleEyesClosed()'s
	// fully-closed stealth state), Tick() applies a dark vignette + light Depth of Field to
	// FollowCameraComponent's own PostProcessSettings, simulating looking out through nearly-shut
	// eyelids — see Tick()'s own comment for exactly where. Cleared (PostProcessBlendWeight -> 0)
	// instantly the moment bIsAiming goes false, same authoritative every-tick enforcement as the rest of
	// this block. [0, 1] — see FPostProcessSettings::VignetteIntensity.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Items|Throw|Camera|Eye Peek")
	float EyePeekVignetteIntensity = 1.0f;

	// Aperture (F-stop) applied to FollowCameraComponent's PostProcessSettings while bIsAiming — a LOW
	// value (wide aperture) is what actually produces a shallow, blurrier depth of field; see
	// FPostProcessSettings::DepthOfFieldFstop.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Items|Throw|Camera|Eye Peek")
	float EyePeekDepthOfFieldFstop = 1.2f;

	// Focal distance (world units, from the camera) the light DoF blur above is centered on while
	// bIsAiming — roughly where TraceAimTarget() usually lands for a typical throw, so the reticle's own
	// target stays reasonably sharp while the edges of the screen soften; see
	// FPostProcessSettings::DepthOfFieldFocalDistance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Items|Throw|Camera|Eye Peek")
	float EyePeekDepthOfFieldFocalDistance = 400.0f;

	// Empty-base hook — called from Tick() only on an actual bIsAiming transition (not every tick), after
	// the Eye Peek post-process has already been applied/cleared for the new state. Lets a subclass react
	// to the peek visual state itself (e.g. muting a hero-specific overlay while peeking) without needing
	// to know anything about FollowCameraComponent/PostProcessSettings. Same "virtual hook, empty base"
	// pattern as OnHeldItemsChanged()/IsImmuneToMonsterCatch() elsewhere in this class.
	UFUNCTION(BlueprintCallable, Category = "Holy Items|Throw|Camera|Eye Peek")
	virtual void OnEyePeekStateChanged(bool bIsPeeking) {}

	// Whether this character is currently riding a mounted ABaseMonster — false unconditionally on
	// ABaseCharacter itself (the mounting SYSTEM is isolated entirely in ARiderCharacter/
	// UMountRiderComponent now — see that class's own header comment); ARiderCharacter overrides this to
	// report its UMountRiderComponent's actual state. Shared code that must still behave sensibly
	// regardless of which hero it's running on (ThrowItem()'s mounted-throw-clearance push,
	// AddMovementInput()'s movement redirect, TakeDamage()'s knock-off reaction, Tick()'s camera-arm
	// interpolation) queries this — and GetMountedMonster() below — instead of owning mount state
	// directly, so those systems work correctly for a Rider without ABaseCharacter needing to know HOW
	// mounting works.
	UFUNCTION(BlueprintCallable, Category = "Mounting")
	virtual bool IsMounted() const { return false; }

	// The monster currently being ridden, or nullptr if IsMounted() is false. See IsMounted()'s comment
	// — ARiderCharacter overrides this to return its UMountRiderComponent's MountedMonster.
	UFUNCTION(BlueprintCallable, Category = "Mounting")
	virtual ABaseMonster* GetMountedMonster() const { return nullptr; }

	// No-op on ABaseCharacter itself (nothing to dismount from — see IsMounted()'s comment).
	// ARiderCharacter overrides this to delegate to its UMountRiderComponent. Public (not just internal)
	// so ABaseMonster::TakeDamage() can call Rider->Dismount() directly when the RIDDEN monster takes a
	// hit (see its comment), and so OnJumpOrDismountPressed() (Space, shared by every hero) can call it
	// unconditionally — harmless on a hero that was never mounted to begin with.
	UFUNCTION(BlueprintCallable, Category = "Mounting")
	virtual void Dismount() {}

	// Mounted-riding pose for the AnimGraph (a Sequence Player + Blend Poses by Bool gated on IsMounted()
	// in ABP_Unarmed) — read by the graph, NEVER applied via PlayAnimation()/SetAnimationMode(), so it
	// doesn't disable the rest of the AnimBP (e.g. the Censer swing arm pose) the way the old
	// PlayAnimation()-override approach did. nullptr on ABaseCharacter itself (nothing to ride — see
	// IsMounted()'s comment); ARiderCharacter overrides this to return its UMountRiderComponent's
	// MountedRidingAnimation.
	UFUNCTION(BlueprintCallable, Category = "Mounting")
	virtual class UAnimSequence* GetMountedRidingAnimation() const { return nullptr; }

	// Called by TakeDamage() whenever ANY damage is taken while IsMounted() is true — "getting knocked
	// off a mount" per the mount spec. No-op on ABaseCharacter itself; ARiderCharacter overrides this to
	// dismount and launch the rider away from DamageCauser via its UMountRiderComponent.
	virtual void OnKnockedOffMount(AActor* DamageCauser) {}

	// Called every time EquippedRightHand/EquippedLeftHand actually change — after PickUp(), ThrowItem(),
	// or either drop branch in Interact() (same call sites as the InventoryComponent->
	// RefreshLightSourceSlot() immediate-refresh calls those already make). No-op on ABaseCharacter
	// itself; APriestCharacter overrides this to crouch while holding a lit Candle and stand back up once
	// none remain — see its own header comment.
	virtual void OnHeldItemsChanged() {}

	// TargetArmLength the camera boom interpolates toward while IsMounted() (see Tick()) — takes
	// priority over AimCameraArmLength if IsMounted() and bIsAiming are somehow both true, since a wide
	// rodeo view matters more than an ADS zoom while riding. A much longer arm than the default follow
	// distance, so the camera can actually see the monster it's riding instead of clipping into it. Kept
	// here (not in the mounting system) since Tick()'s camera-interpolation code is shared by every
	// hero, even though only ARiderCharacter's IsMounted() can ever actually make this apply.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mounting")
	float MountCameraArmLength = 500.0f;

	// The camera boom actually driving this character's view — see CameraBoomComponent's own comment
	// for how/when it's found. Exposed read-only so UMountRiderComponent (not a subclass of this class,
	// so it can't reach the private field directly) can toggle bDoCollisionTest for the duration of a
	// mount, same reasoning as that property's own comment on why the probe needs disabling while riding.
	FORCEINLINE USpringArmComponent* GetCameraBoomComponent() const { return CameraBoomComponent; }

	// The actual view camera — see FollowCameraComponent's own comment. Exposed read-only for parity
	// with GetCameraBoomComponent() above.
	FORCEINLINE UCameraComponent* GetFollowCamera() const { return FollowCameraComponent; }

	// Center-screen ("standard third-person shooter") aim trace, shared by UpdateAimCrosshair() (purely
	// visual), ThrowItem() (which launches toward this same OutTargetPoint, so the thrown item's
	// trajectory always matches what the crosshair showed), and UMountRiderComponent::TryMount() (mounts
	// whatever this same trace is currently aimed at — see its own header comment on why detection is
	// unified with the crosshair rather than a separate sweep). Public so that component, which is not a
	// subclass of this class, can call it. Traces from the ACTIVE CAMERA itself
	// (APlayerCameraManager::GetCameraLocation()/GetCameraRotation() — deliberately not
	// GetActorEyesViewPoint(), which returns a mesh/capsule-based point that can diverge from where a
	// third-person camera actually is/looks) out to AimTraceDistance, ignoring this character and both
	// held items so the ray isn't blocked by starting right next to/inside the player's own capsule and
	// mesh. Returns true and sets OutHit if something was struck; OutTargetPoint is always set
	// (OutHit.ImpactPoint on a hit, otherwise the trace's unobstructed end point).
	UFUNCTION(BlueprintCallable, Category = "Holy Items|Throw")
	bool TraceAimTarget(FVector& OutTargetPoint, FHitResult& OutHit) const;

	// Same camera-forward trace as TraceAimTarget() (active camera, ignoring this character/mesh/capsule/
	// both held items, ECC_Visibility), but with its own caller-specified MaxDistance rather than the
	// fixed AimTraceDistance — used by UHunterFocusComponent's step-distance calculation, which wants an
	// independently tunable range from the aim/throw reticle's. A separate function (not TraceAimTarget()
	// with an extra parameter) so nothing about the aim reticle/throw trajectory changes.
	UFUNCTION(BlueprintCallable, Category = "Hunter's Focus")
	bool TraceHunterFocusTarget(float MaxDistance, FVector& OutTargetPoint, FHitResult& OutHit) const;

	// Builds the interaction-prompt text (e.g. "[E] Pick Up Candle") for the current HighlightedItem, or
	// an empty string if none. Only AHolyItem pickup is implemented today — this is the one place a
	// future generic interactable (a door/gate/etc.) would plug in additional prompt text. Public so
	// UPlayerHUDWidget (not a subclass of this class) can call it too, for a Blueprint-skinned HUD that
	// wants to render its own prompt rather than relying on UCrosshairWidget's native one.
	UFUNCTION(BlueprintCallable, Category = "Holy Items")
	FString BuildInteractionPromptText() const;

	// True while UHunterFocusComponent::bIsFocusActive — same "virtual hook, empty base" pattern as
	// IsImmuneToMonsterCatch()/OnHeldItemsChanged(), so ABaseCharacter/BuildInteractionPromptText() can
	// show the Focus step-count prompt with top priority without knowing anything about
	// UHunterFocusComponent (a Redneck-only component, not a subclass of this class). Base implementation
	// is never active; ARedneckCharacter overrides this to read its own HunterFocusComponent.
	UFUNCTION(BlueprintCallable, Category = "Hunter's Focus")
	virtual FString GetHunterFocusPromptText() const { return FString(); }

protected:
	// Enhanced Input Mapping Context applied automatically in BeginPlay() (see ApplyDefaultInputMappingContext())
	// so WASD movement/jump work the instant this pawn is possessed, with no Blueprint "Add Input Mapping
	// Context" node required. Defaults to /Game/Input/IMC_Default via ConstructorHelpers — override per
	// Blueprint subclass only if a hero ever needs a different control scheme.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	// Separate mapping context carrying only mouse-look (IA_MouseLook) — this project's IMC_Default
	// binds IA_Look to the gamepad stick only, so mouse-look needs this second context layered on top for
	// WASD+mouse to work immediately without a gamepad. Defaults to /Game/Input/IMC_MouseLook.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputMappingContext> MouseLookMappingContext;

	// IA_Move (Axis2D) — bound to Move(). Defaults to /Game/Input/Actions/IA_Move.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction;

	// IA_Look (Axis2D, gamepad stick) — bound to Look(). Defaults to /Game/Input/Actions/IA_Look.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> LookAction;

	// IA_MouseLook (Axis2D, mouse) — bound to the same Look() handler as LookAction above. Defaults to
	// /Game/Input/Actions/IA_MouseLook.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MouseLookAction;

	// IA_Jump (Digital) — Started/Completed bound to the existing OnJumpOrDismountPressed()/
	// OnJumpOrDismountReleased() handlers below, so Space still dismounts while mounted per that
	// function's own comment. Defaults to /Game/Input/Actions/IA_Jump. The legacy BindKey(SpaceBar, ...)
	// fallback in SetupPlayerInputComponent() is left in place alongside this (same belt-and-braces
	// reasoning as this project's other input fallbacks); both handlers are idempotent so firing twice
	// for one press is harmless.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction;

	// Adds DefaultMappingContext/MouseLookMappingContext to this player's Enhanced Input subsystem.
	// Called from InitializeLocalPlayerUI(); harmless no-op if GetController() isn't a local
	// APlayerController (e.g. a remote/AI-possessed instance) or either context failed to load.
	void ApplyDefaultInputMappingContext();

	// Creates CrosshairWidgetInstance/PlayerHUDWidgetInstance and applies the default Enhanced Input
	// mapping contexts — everything in this class that only makes sense for THIS player's own local
	// viewport/input. Idempotent (guarded by bLocalPlayerUIInitialized) and safe to call from both
	// BeginPlay() and PossessedBy(): for the GameMode's initial default pawn, BeginPlay() runs BEFORE
	// Possess() ever reaches this pawn (SpawnDefaultPawnFor() spawns it, which fires BeginPlay()
	// immediately, and RestartPlayer() only calls Possess() afterward), so GetController() is still null
	// the first time BeginPlay() runs — this used to mean the HUD/crosshair/input mapping context never
	// appeared at all for the player's starting hero. PossessedBy() is what's actually guaranteed to run
	// with a valid controller, so it's the call site that matters; BeginPlay() still calls this too, in
	// case a pawn is ever possessed before its BeginPlay() runs instead (e.g. SpawnActorDeferred).
	void InitializeLocalPlayerUI();

	// Bound to MoveAction — standard control-rotation-relative WASD movement. Issues one
	// AddMovementInput() call per axis with a pure direction vector (matching the Enhanced Input
	// template convention AddMovementInput()'s own override already assumes — see its comment), so it
	// keeps working correctly while IsMounted() redirects movement to the mounted monster.
	//
	// Two panic-specific detours rather than AddMovementInput() at all:
	//   - bIsPanicking && a FRESH direction press: struggling against being pinned down costs PanicMeter
	//     (PanicMovementPenalty) — same FMath::Min(PanicMeterMax, ...) pattern RegisterMouthGasp() already
	//     uses — and re-checks PanicMeter >= PanicMeterMax -> ForceEyesOpenFromPanic() immediately (the same
	//     check AdvanceToNextBeat() runs after every breath beat, duplicated here since this bypasses that
	//     beat cycle entirely). Movement itself is already blocked (MaxWalkSpeed=0 from EnterPanicState()),
	//     so this never calls AddMovementInput() at all while panicking. Edge-triggered per AXIS DIRECTION
	//     (bWasForwardHeldLastFrame/bWasBackHeldLastFrame/bWasRightHeldLastFrame/bWasLeftHeldLastFrame, one
	//     bool per direction rather than a single "any input" flag) so a fast W->A->S->D switch — where
	//     MovementVector never actually passes back through exactly zero between presses — still registers
	//     as separate fresh presses instead of one continuous "already active" input; at most one PanicMeter
	//     jump per Move() call either way, even if multiple directions newly become held on the same frame.
	//   - bIsAwaitingGetUp, edge-triggered off bWasGetUpInputActiveLastFrame (fires once on the frame the
	//     input newly appears, not every frame the key stays held):
	//   - bIsAwaitingGetUp && forward (MovementVector.Y > 0): calls RecoverFromKnockdown() and clears
	//     bIsAwaitingGetUp — the explicit "stand up" confirmation EndPanicSafely() no longer does
	//     automatically. A/S/D or no input while awaiting get-up does nothing (no penalty, no movement).
	//     The triggering frame itself doesn't also call AddMovementInput() — it's the first frame back on
	//     their feet, not a move.
	void Move(const FInputActionValue& Value);

	// Bound to both LookAction (gamepad) and MouseLookAction (mouse) — turns the controller via
	// AddControllerYawInput()/AddControllerPitchInput().
	void Look(const FInputActionValue& Value);

	virtual void BeginPlay() override;

public:
	virtual void Tick(float DeltaTime) override;

	// Forces a dismount (see Dismount()) and knocks the rider away from DamageCauser whenever ANY damage
	// is taken while IsMounted() — "getting knocked off a mount" per the mount spec, via
	// OnKnockedOffMount() above. There's currently no monster-attack path in this project that actually
	// calls TakeDamage() on the player (only ABaseMonster receives holy damage today), so this hook is
	// wired and ready but won't fire until such an attack exists; ABaseMonster::TakeDamage() has the
	// matching hook for the RIDDEN monster's side.
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	// See InitializeLocalPlayerUI()'s own comment on why this (not just BeginPlay()) is needed for the
	// HUD/crosshair/input mapping context to reliably appear for the player's initial pawn.
	virtual void PossessedBy(AController* NewController) override;

	// Overridden so that while IsMounted(), every movement-input call (regardless of where in the input
	// graph it originates — WASD calls this same virtual via Move()'s AddMovementInput() calls, so
	// overriding it here catches it without needing to special-case Move() itself) is redirected to
	// GetMountedMonster() instead of this character's own CharacterMovementComponent. See the .cpp for
	// why WorldDirection/ScaleValue are passed through unchanged.
	virtual void AddMovementInput(FVector WorldDirection, float ScaleValue = 1.0f, bool bForce = false) override;

	// Bound to Space. While IsMounted(), Space dismounts (Dismount()) instead of jumping — while not
	// mounted, it's an ordinary jump (ACharacter::Jump()), so this key keeps working normally the rest
	// of the time (and Dismount() itself is a harmless no-op on any hero that was never mounted to begin
	// with — see its comment). Paired with OnJumpOrDismountReleased() (Space release -> StopJumping())
	// for correct jump-hold behavior when not mounted; StopJumping() is harmless to call when not
	// currently jumping.
	UFUNCTION(BlueprintCallable, Category = "Mounting")
	void OnJumpOrDismountPressed();

	UFUNCTION(BlueprintCallable, Category = "Mounting")
	void OnJumpOrDismountReleased();

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stealth")
	bool bIsEyesClosed;

	// See SetEyesClosedDarknessSuppressed() above — backing field, not meant to be touched directly.
	bool bIsDarknessSuppressedByAbility = false;

	// See SetBlindedByAbility() above — backing field, not meant to be touched directly.
	bool bIsBlindedByAbility = false;

	// See SetEyesLockedOpen()/AreEyesLockedOpen() above — generic guard checked at the top of CloseEyes().
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stealth")
	bool bEyesLockedOpen = false;

	// See SetEyesLockedClosed()/AreEyesLockedClosed() above — generic guard checked at the top of OpenEyes().
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Stealth")
	bool bEyesLockedClosed = false;

	// Eyes-closed look (see EyesClosedPostProcessComp). Tune these in the hero Blueprint, category "Eyes Closed".
	// How many stops darker the picture gets while the eyes are closed. More negative = darker; lights stay
	// visible long after ordinary surfaces have gone black. -4 is dim, -8 is nearly black.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eyes Closed")
	float EyesClosedExposureBias = -6.0f;

	// Blur radius (pixels at 1920 wide) applied to the whole picture while the eyes are closed. 0 = no blur.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eyes Closed")
	float EyesClosedBlurRadius = 60.0f;

	// Aperture while the eyes are closed. LOWER = blurrier (see EyePeekDepthOfFieldFstop).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eyes Closed")
	float EyesClosedBlurFstop = 0.5f;

	// Dark edges while the eyes are closed (0 = none, 1 = full).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eyes Closed")
	float EyesClosedVignetteIntensity = 1.0f;

	// How fast exposure adapts when eyes close/open or when squinting. Higher = faster.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Eyes Closed")
	float EyesClosedAutoExposureSpeed = 100.0f;

	// Own post-process component for the eyes-closed look (exposure + blur + vignette), values driven
	// in Tick(). Deliberately NOT FollowCameraComponent — that one carries Eye Peek Risk's vignette/DoF.
	UPROPERTY()
	TObjectPtr<UPostProcessComponent> EyesClosedPostProcessComp;

	// See SetImmuneToMonsterAttacks()/IsImmuneToMonsterAttacks() above — generic guard checked at the top
	// of TakeDamage() and EnterPanicState().
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Health")
	bool bImmuneToMonsterAttacks = false;

	// Bound to the "Interact" action (E by default, see DefaultInput.ini). Item pickup/drop ONLY: picks
	// up HighlightedItem into a free hand, or if nothing is highlighted and a hand is occupied, drops
	// that item at the last interact-scan hit point. Mounting is a completely separate action, isolated
	// in ARiderCharacter/UMountRiderComponent (bound to F there) — this never touches mount state at all.
	UFUNCTION(BlueprintCallable, Category = "Holy Items")
	virtual void Interact();

	// Runs every 0.1s off InteractScanTimerHandle: traces from the eyes and updates HighlightedItem,
	// toggling AHolyItem::EnableHighlight()/DisableHighlight() as the looked-at item changes, and
	// broadcasting OnHighlightedItemChanged whenever it actually does.
	void ScanForInteractable();

	// Attaches Item to whichever hand socket is free and records it in EquippedRightHand/
	// EquippedLeftHand. No-ops if both hands are already full.
	void PickUp(AHolyItem* Item);

	// Bound to "Aim" (RMB) press/release — sets bIsAiming.
	UFUNCTION(BlueprintCallable, Category = "Holy Items|Throw")
	void StartAiming();

	UFUNCTION(BlueprintCallable, Category = "Holy Items|Throw")
	void StopAiming();

	// Empty-base hook checked at the very start of ThrowItem() (LMB press), before even the bIsAiming
	// check below — lets a subclass hijack the attack press into a completely different ability instead
	// of throwing a held item, under whatever subclass-specific condition it wants to check itself (e.g.
	// ARedneckCharacter overrides this for the RMB+LMB "Bulls Charge" combo — see its own comment).
	// Returning true consumes the press: ThrowItem() returns immediately without throwing anything or
	// running its own bIsAiming/duplicate-call checks. Base implementation never intercepts anything, so
	// every hero without an override keeps today's plain throw behavior unchanged.
	UFUNCTION(BlueprintCallable, Category = "Holy Items|Throw")
	virtual bool TryHijackAttackPress() { return false; }

	// Bound to "Throw" (LMB) press. Throws whichever hand holds an item — right hand preferred, same
	// preference order as PickUp() — launching it via AHolyItem::Throw() toward the same aim-target
	// point TraceAimTarget()/UpdateAimCrosshair() computes (a standard-third-person-shooter "trajectory
	// converges on the center-screen crosshair" throw, not a throw that just follows the camera's raw
	// forward vector), and clearing that hand's Equipped*Hand reference immediately (not deferred to
	// any callback), so ABaseMonster::IsLocationProtected() stops counting it as held light protection
	// the instant it leaves the hand rather than waiting for it to land/hit. No-op if both hands are
	// empty, or if bIsAiming is false — a throw is a deliberate, aimed action. See TryHijackAttackPress()
	// above for how a subclass can redirect this same LMB press elsewhere first.
	UFUNCTION(BlueprintCallable, Category = "Holy Items|Throw")
	void ThrowItem();

	// Bound to "MeleeSwing" (Q) press. Swings whichever hand currently holds a Censer — right hand
	// preferred, same preference order as PickUp()/ThrowItem() — knocking back nearby ABaseMonster
	// actors (see AHolyItem::PerformMeleeSwing()). No-op if neither equipped hand holds a Censer.
	UFUNCTION(BlueprintCallable, Category = "Holy Items|Melee")
	void SwingHeldCenser();

	// Called from Tick() every frame while bIsAiming — runs TraceAimTarget(), classifies what it hit
	// (ABaseMonster -> CrosshairMountableColor, an AHolyItem or anything tagged "Target" ->
	// CrosshairTargetColor, anything else -> CrosshairDefaultColor), and applies the result to
	// CrosshairWidgetInstance's DotColor, making it visible. Tick() hides it again when bIsAiming goes
	// false.
	void UpdateAimCrosshair() const;

	// VERIFICATION TOOL, bound to "9": spawns a real, persistent, world-placed AStaticMeshActor at
	// the CURRENT TraceAimTarget() result — not a transient DrawDebug* primitive at all. Select it in
	// the World Outliner afterward and read its Transform in the Details panel directly; this proves
	// where TargetPoint actually is in the world completely independent of any HUD/reticle rendering
	// path.
	UFUNCTION(BlueprintCallable, Category = "Holy Items|Throw|Debug")
	void SpawnAimTargetMarker();

	// How far (in units) the interact scan trace reaches from the eyes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Items")
	float InteractTraceDistance = 250.0f;

	// Radius (in units) of the sphere swept by the interact scan, so aiming doesn't need to land a
	// pixel-thin line trace exactly on the item.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Items")
	float InteractSweepRadius = 15.0f;

	// Hand socket names on the character's mesh that picked-up items get attached to. "hand_r"/"hand_l"
	// are SKELETON BONE names, not custom USkeletalMeshSocket sockets — but AttachToComponent() resolves
	// a bone name just as well as a real socket. This matters here specifically: every Blueprint hero in
	// this project (BP_Redneck/BP_Priest/BP_Rider/BP_Sibling) uses the "Simple" mannequin meshes
	// (SKM_Manny_Simple/SKM_Quinn_Simple), which define NO custom sockets at all — only the legacy,
	// unused SK_Mannequin has actual "Socket_Hand_R"/"Socket_Hand_L" sockets. Defaulting to that name here
	// meant AttachToComponent() was silently attaching every held item to a socket that doesn't exist on
	// any mesh actually in use, which — per AttachToComponent()'s own fallback behavior for an unresolved
	// socket name — snapped it to the mesh component's own origin (feet/capsule-base level, Z≈0) instead
	// of the hand. See AHolyItem::PickUp() for the runtime fallback chain that also now guards against
	// this if a future mesh is swapped in without either of these bones.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Items")
	FName RightHandSocket = TEXT("hand_r");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Holy Items")
	FName LeftHandSocket = TEXT("hand_l");

private:
	// Bound to Number keys 1-4 — thin wrappers so SetupPlayerInputComponent() can bind directly (BindKey
	// requires an exact void(void)/void(FKey) member signature, which InventoryComponent->SelectSlot(int32)
	// doesn't match).
	UFUNCTION()
	void OnSelectQuickSlot1();
	UFUNCTION()
	void OnSelectQuickSlot2();
	UFUNCTION()
	void OnSelectQuickSlot3();
	UFUNCTION()
	void OnSelectQuickSlot4();

	// Bound to the mouse wheel — same wrapper reasoning as OnSelectQuickSlot1() above.
	UFUNCTION()
	void OnScrollSelectNext();
	UFUNCTION()
	void OnScrollSelectPrevious();

	// Bound to G — same thin-wrapper reasoning as OnSelectQuickSlot1() above (ProbeComponent->Probe()
	// doesn't match BindKey()'s required void(void) member signature on its own).
	UFUNCTION()
	void OnProbePressed();

	// Bound to E — a THIRD independent listener on that key alongside Interact()/MashTrapEscape() (see
	// CLAUDE.md's "Общие клавиши ввода" rule): HidingComponent->TryToggle() itself no-ops unless the owner
	// is already hidden or standing next to an unoccupied AHidingSpot, so Interact()/MashTrapEscape() keep
	// working exactly as before everywhere else. Same thin-wrapper reasoning as OnProbePressed() above.
	UFUNCTION()
	void OnHidingTogglePressed();

	// Releases the trap (ABearTrap::OnPlayerEscaped() detaches + destroys it), restores
	// GetCharacterMovement()->MaxWalkSpeed to PreTrapMaxWalkSpeed, and clears bIsTrapped/
	// TrapEscapePresses/CurrentTrap. Called by MashTrapEscape() once TrapEscapePresses reaches
	// TrapEscapePressesRequired.
	void EscapeTrap();

	// The trap currently holding this character, set by CaughtInTrap()/cleared by EscapeTrap(). Weak
	// since this character doesn't own the trap's lifetime (ABearTrap::OnPlayerEscaped() destroys itself).
	TWeakObjectPtr<ABearTrap> CurrentTrap;

	// GetCharacterMovement()->MaxWalkSpeed as of just before CaughtInTrap() clamped it down — what
	// EscapeTrap() restores back to.
	float PreTrapMaxWalkSpeed = 0.0f;

	// GetWorld()->GetTimeSeconds() of the next target breath beat — OnBreathPressed() judges its timing
	// against this (within BreathToleranceWindow either side = a hit), and GetBreathBeatProximity() reads
	// it for the HUD's pulsing indicator. Set BreathRhythmInterval seconds out in EnterPanicState(),
	// advanced by AdvanceToNextBeat() each time a beat resolves.
	float NextBreathBeatTime = 0.0f;

	// True once EITHER RegisterNoseBreath() or RegisterMouthGasp() has resolved the CURRENT beat — guards
	// against a same-cycle extra Space press (after an early off-window miss already resolved it) from
	// doing anything further, and against OnBreathWindowClosed() double-processing a beat a press already
	// resolved (AdvanceToNextBeat() re-arms BreathBeatTimerHandle for the NEXT window on the same handle,
	// which cancels/replaces whatever was still pending for the old one — this flag is a defensive
	// belt-and-braces check on top of that, not the only thing preventing a double-count).
	bool bBeatResolvedThisWindow = false;

	// Edge-detection for Move()'s panic-movement penalty, one bool PER AXIS DIRECTION rather than a single
	// "any input" flag — true if the PREVIOUS Move() call (while bIsPanicking) saw that specific direction
	// held. Tracking all four separately (instead of collapsing MovementVector into one non-zero check) is
	// what lets a fast W->A->S->D switch register as separate fresh presses: MovementVector doesn't pass
	// back through exactly zero between those presses, so a single combined flag would see one continuous
	// "already active" input and only charge the penalty once instead of per press.
	bool bWasForwardHeldLastFrame = false;
	bool bWasBackHeldLastFrame = false;
	bool bWasRightHeldLastFrame = false;
	bool bWasLeftHeldLastFrame = false;

	// Same edge-detection role as the four bWas*HeldLastFrame fields above, for the "press forward to
	// get up" check (bIsAwaitingGetUp) instead — a held-down forward key shouldn't call
	// RecoverFromKnockdown() more than once, though RecoverFromKnockdown() itself no-ops harmlessly if
	// called again since bIsAwaitingGetUp is already cleared by the first edge.
	bool bWasGetUpInputActiveLastFrame = false;

	// Timer callback (BreathBeatTimerHandle) — armed for NextBreathBeatTime + BreathToleranceWindow (i.e.
	// exactly when the current beat's grace window closes). If nothing resolved it by then
	// (bBeatResolvedThisWindow still false), that's a miss by default — same RegisterMouthGasp() an
	// off-window press would trigger.
	void OnBreathWindowClosed();

	// A successful nose-breath, landed via OnBreathPressed() within BreathToleranceWindow of
	// NextBreathBeatTime: PanicMeter -= PanicReliefPerBreath, plays NoseBreathSound, records
	// bLastBreathWasHit/LastBreathResultTime for the HUD's flash, then AdvanceToNextBeat().
	void RegisterNoseBreath();

	// A mouth-gasp — either an off-window OnBreathPressed() or OnBreathWindowClosed() catching total
	// silence: PanicMeter += PanicPenaltyPerGasp, plays MouthGaspSound, records bLastBreathWasHit/
	// LastBreathResultTime for the HUD's flash, and BroadcastAlert()s from BlindEncounterMonster (if set) —
	// a mouth gasp is loud, so it alerts every nearby monster within AlertRadius, not just whichever one is
	// close enough to matter for a kill. Deliberately NOT an instant kill by itself anymore — a single
	// missed beat only raises the meter; only PanicMeter actually reaching PanicMeterMax (see
	// AdvanceToNextBeat() -> ForceEyesOpenFromPanic()) rolls the proximity kill/survive check. Always falls
	// through to AdvanceToNextBeat(), same as a successful breath.
	void RegisterMouthGasp();

	// Shared tail end of both RegisterNoseBreath()/RegisterMouthGasp() (once a kill hasn't already
	// happened): advances NextBreathBeatTime by BreathRhythmInterval, clears bBeatResolvedThisWindow, then
	// either resolves the panic episode (EndPanicSafely() if PanicMeter <= 0, ForceEyesOpenFromPanic() if
	// >= PanicMeterMax) or re-arms BreathBeatTimerHandle for the new window's close.
	void AdvanceToNextBeat();

	// Success path: PanicMeter reached 0 with a nose-breath. Clears bIsPanicking/BlindEncounterMonster
	// (notifying the instigating monster via ABaseMonster::OnBlindEncounterResolved() so it's free to
	// react to a fresh blind touch later) and stops BreathBeatTimerHandle, then hands off to
	// RecoverFromKnockdown() — MaxWalkSpeed is restored only once that finishes (plays GetUpAnimation
	// first, if set), not immediately here. Eyes stay closed throughout — this is a controlled calm-down,
	// not an eyes-open event.
	void EndPanicSafely();

	// Failure/Timeout path: PanicMeter reached PanicMeterMax. The character's eyes are forced open
	// involuntarily — deliberately NOT routed through OpenEyes()'s own unconditional instant-kill check
	// (that's for a deliberate, voluntary open), since this specific case gets its own proximity roll:
	// FindNearbyMonster(MonsterProximityKillRadius) found -> instant kill through THAT monster (not
	// necessarily BlindEncounterMonster itself — see FindNearbyMonster()'s own comment); none found ->
	// "lucky survival," bIsPanicking clears and the character is free to stand back up under normal
	// control.
	void ForceEyesOpenFromPanic();

	// Plays KnockdownFallAnimation (if set) once, non-looping — see that property's own comment for why
	// PlayAnimation() (AnimationSingleNode) is used instead of a slot-based dynamic montage. No-op if unset.
	void PlayKnockdownAnimation();

	// Snaps the mesh back to its normal AnimBP-driven locomotion (undoes PlayKnockdownAnimation()'s
	// AnimationSingleNode switch). Safe to call even if the mesh was never switched out of AnimationBlueprint
	// mode in the first place. Deliberately does NOT touch MaxWalkSpeed — callers that need movement
	// restored too do that themselves (see RecoverFromKnockdown() vs. ForceEyesOpenFromPanic()'s own
	// immediate restore).
	void RestoreAnimationBlueprintControl();

	// EndPanicSafely()'s recovery flow: plays GetUpAnimation once (if set) and defers restoring
	// MaxWalkSpeed until it finishes via GetUpTimerHandle, so the character visibly gets up BEFORE regaining
	// movement control — matches FinishRecoveryFromKnockdown() firing immediately instead if GetUpAnimation
	// is unset (nothing to wait for).
	void RecoverFromKnockdown();

	// Shared tail of RecoverFromKnockdown(): restores AnimBP control and MaxWalkSpeed. Called immediately
	// if GetUpAnimation is unset, or via GetUpTimerHandle once it finishes playing.
	void FinishRecoveryFromKnockdown();

	FTimerHandle GetUpTimerHandle;

	FTimerHandle BreathBeatTimerHandle;

	// GetCharacterMovement()->MaxWalkSpeed as of just before EnterPanicState() zeroed it — what
	// EndPanicSafely()/ForceEyesOpenFromPanic() restore back to.
	float PrePanicMaxWalkSpeed = 0.0f;

	FTimerHandle InteractScanTimerHandle;

	// Cached result of the most recent ScanForInteractable() trace; Interact() reuses its location
	// as the drop point so pressing E doesn't need to run a second trace.
	FHitResult LastScanHit;

	// Guards InitializeLocalPlayerUI() against running twice (it's called from both BeginPlay() and
	// PossessedBy() — see that function's own comment on why).
	bool bLocalPlayerUIInitialized = false;

	// GetWorld()->GetTimeSeconds() as of the last ThrowItem() call that actually threw something — lets
	// ThrowItem() detect and swallow a same-frame duplicate call. "Throw" is bound BOTH via the
	// action-mapping path AND a direct BindKey(LeftMouseButton) fallback (see SetupPlayerInputComponent()'s
	// own comment on why that fallback exists), so a single click fires ThrowItem() twice in a row — without
	// this guard, the first call throws the right-hand candle and the second (same click) immediately
	// throws the left-hand one too, instead of leaving it lit for a follow-up press.
	float LastThrowActionTime = -1.0f;

	// GetWorld()->GetTimeSeconds() as of the last sprint-noise MakeNoise() pulse — Tick() only fires
	// another one once SprintNoiseInterval has elapsed since this, same throttling pattern as
	// LastThrowActionTime above guarding ThrowItem()'s duplicate-call case.
	float LastSprintNoiseTime = -1.0f;

	// bIsAiming as of the END of the previous Tick() — compared against the current frame's value so
	// OnEyePeekStateChanged() fires only on an actual transition, not every tick the post-process itself
	// is (harmlessly) re-applied.
	bool bWasPeekingLastTick = false;

	// CameraBoomComponent's TargetArmLength/SocketOffset as configured in the constructor, captured once
	// in BeginPlay() before anything modifies them — Tick() interpolates back to these when not aiming,
	// so the non-aiming camera setup never needs to be duplicated/hardcoded here.
	float DefaultCameraArmLength = 0.0f;
	FVector DefaultCameraSocketOffset = FVector::ZeroVector;

	// FollowCameraComponent's FieldOfView as configured in the constructor, captured once in BeginPlay()
	// — Tick() interpolates back to this when not aiming, same pattern as DefaultCameraArmLength above.
	float DefaultCameraFOV = 90.0f;

protected:
	// The camera boom driving this character's view — a native subobject created in the constructor
	// (standard offsets: see the .cpp) so every Blueprint subclass gets a correctly-framed third-person
	// camera with no manual "Add Component" wiring required. VisibleAnywhere (not EditDefaultsOnly) so a
	// Blueprint subclass can still see/nudge it in Class Defaults without being able to remove/replace
	// the underlying component.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<USpringArmComponent> CameraBoomComponent;

	// The actual view camera — attached to CameraBoomComponent's socket in the constructor. See
	// CameraBoomComponent's own comment.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Camera")
	TObjectPtr<UCameraComponent> FollowCameraComponent;
};
