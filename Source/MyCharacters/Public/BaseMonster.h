// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AIController.h"
#include "BaseMonster.generated.h"

class UParticleSystem;
class UNiagaraSystem;
class USoundBase;
class ADayNightManager;
class UAnimSequence;
class UNavigationQueryFilter;

// Current high-level behavior of the monster.
enum class EMonsterState : uint8
{
	Patrol,	// Moving between PatrolPoints.
	Stalk,	// Holding position just outside a detected player's holy light, focused on them instead
			// of giving up and returning to Patrol.
	Chase	// Moving directly toward a spotted player.
};

// Result of this monster's last CheckSunExposure() line trace toward the sun — SunlitBurning is what
// drives ApplySunDamage(); SafeInShadow means the trace hit blocking geometry (a roof, rock, wall, etc.)
// between this monster and the sun before reaching SunTraceDistance.
UENUM(BlueprintType)
enum class EMonsterSunState : uint8
{
	SafeInShadow,
	SunlitBurning
};

/**
 * Monster that patrols between PatrolPoints and becomes Agro (Chase/Stalk) when it detects the player,
 * via either of two independent channels: (1) it spots them — within SightDistance, inside
 * FieldOfViewAngle, unobstructed line of sight, eyes open, see IsPlayerVisible() — or (2) "eye
 * contact" — the PLAYER is within DetectionDistance and looking roughly at the monster (see
 * IsPlayerLookingAtMonster()), whether or not the monster's own sight cone covers them. Closing the
 * eyes or breaking range/FOV/line of sight (and no eye contact either) makes it lose the player and
 * resume patrolling. Catching the player ends the level.
 * Once it has a target, entering/nearing the target's holy light does NOT make it lose them — light
 * never cancels Agro. Instead it holds position at the light's edge (Stalk, see IsTargetInLight()),
 * stepping along the boundary ring a short arc at a time (MoveToLightBorder()) — the same mechanism
 * regardless of whether the item is omnidirectional (Censer, where it just orbits forever since no step
 * is ever unprotected) or directional (Candle, where some step eventually is). Nothing here needs to
 * search for or jump straight to that unprotected step: CheckForPlayer() already re-evaluates
 * IsTargetInLight() from this monster's actual current position every 0.1s (far more often than the
 * boundary-stepping rescan runs) and drops it straight into Chase — and a kill, if already in range —
 * the instant a step happens to land somewhere the light no longer covers.
 */
UCLASS()
class MYCHARACTERS_API ABaseMonster : public ACharacter
{
	GENERATED_BODY()

public:
	ABaseMonster();

	// Monster sound design — a fully standalone mechanic, see UMonsterAudioComponent's own header
	// comment. Only reads this class's already-public state (GetMesh(), IsScanningForPlayer()); no
	// other line in this file was changed to add it.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Audio")
	TObjectPtr<class UMonsterAudioComponent> MonsterAudioComponent;

	virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	// Current health. The monster dies when this reaches 0.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster Stats")
	float Health = 100.0f;

	// Health the monster is spawned/reset with.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Monster Stats")
	float MaxHealth = 100.0f;

	// Distance (in units) within which the monster can spot the player.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	float SightDistance = 1000.0f;

	// Full field-of-view angle (in degrees) the monster can see within.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	float FieldOfViewAngle = 90.0f;

	// Distance (in units) within which "eye contact" detection (see IsPlayerLookingAtMonster()) can
	// trigger Agro — independent of, and in addition to, this monster's own sight cone above.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	float DetectionDistance = 800.0f;

	// Total angle (in degrees, centered on the PLAYER's own forward vector) within which the player
	// counts as "looking at" this monster for eye-contact detection.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	float EyeContactAngle = 30.0f;

	// TEMPORARY diagnostic visualization for the Eye Peek Risk bug hunt (missing attack animation +
	// "telepathic" stun). Now drives ONLY Tick()'s floating head-marker text ("НАШЁЛ! ИДУ АТАКОВАТЬ" /
	// "УДАР!") — the earlier floor/around-monster shapes (CheckForPlayer()'s sight-cone DrawDebugCone()
	// and DoesEyePeekAttackConnect()'s contact-check DrawDebugSphere()) have both been commented out per
	// a follow-up "strip debug down to just the head text" request; search this file for "DISABLED per"
	// to find and re-enable them if a future bug hunt needs the shapes back. Defaults true specifically
	// so the head marker is visible with zero extra setup; flip off once no longer needed.
	// Switched OFF by default (the text was showing above monsters during normal play) — turn on per
	// monster in its Blueprint when a bug hunt needs it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	bool bShowEyePeekDebug = false;

	// TEMPORARY diagnostic — true for EyePeekAttackDebugDuration seconds after PlayAttackAnimation() is
	// called from ResolveEyePeekReaction()'s reaction matrix (either the Fatality or the "swing blind"
	// branch) — drives Tick()'s floating "УДАР!" status text above this monster's head (see
	// StartEyePeekAttackAnimDebug()). Deliberately NOT tied to the animation's real length or the
	// "Attack_Hit" AnimNotify — just a fixed visualization window, so this stays visible even for the
	// Fatality branch (which never fires OnAttackHitNotify() at all, since it never sets
	// PendingEyePeekAttackTarget).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Debug")
	bool bIsPlayingEyePeekAttackAnim = false;

	// Seconds bIsPlayingEyePeekAttackAnim (and the "УДАР!" head text it drives) stays true after
	// StartEyePeekAttackAnimDebug() — roughly Anim_Whisper_Attack1's own length, just long enough to read
	// on screen.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Debug")
	float EyePeekAttackDebugDuration = 1.2f;

	// Points the monster patrols between, in order, while it hasn't spotted the player. Ignored entirely
	// while wandering (bUseRandomWander true, or this array left empty — see MoveToNextPatrolPoint()).
	UPROPERTY(EditAnywhere, Category = "AI")
	TArray<AActor*> PatrolPoints;

	// Time (in seconds) the monster waits at each patrol point before moving to the next. Only used for
	// FIXED PatrolPoints movement — random wander uses WanderWaitTimeMin/Max instead.
	UPROPERTY(EditAnywhere, Category = "AI")
	float PatrolWaitTime = 2.0f;

	// If true, this monster wanders randomly across the NavMesh (MoveToNextPatrolPoint() samples a random
	// reachable point within WanderRadius via UNavigationSystemV1::GetRandomReachablePointInRadius())
	// instead of moving between placed PatrolPoints actors. An EMPTY PatrolPoints array ALSO falls back to
	// random wander regardless of this flag being false — there's nothing else useful Patrol state could
	// do with zero patrol points to move between — so this really only matters as an override when BOTH
	// PatrolPoints are placed AND wandering is wanted instead of using them.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Patrol")
	bool bUseRandomWander = true;

	// Radius (in units), centered on this monster's CURRENT location (re-centered every time a new wander
	// point is picked, not anchored to a single origin), that a random reachable NavMesh point is sampled
	// from while wandering.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Patrol")
	float WanderRadius = 1200.0f;

	// Min/max seconds to wait at a reached wander point before picking a new random one — a fresh random
	// value in this range is rolled on every arrival (see Tick()'s Patrol handling), not a single fixed
	// duration like PatrolWaitTime's fixed-point sibling.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Patrol")
	float WanderWaitTimeMin = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Patrol")
	float WanderWaitTimeMax = 5.0f;

	// Movement speed while patrolling.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	float PatrolSpeed = 100.0f;

	// Movement speed while chasing/stalking the player — lowered from 400 to a slower, more threatening
	// pace so a larger group of monsters is actually testable/survivable in PIE at once.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	float ChaseSpeed = 150.0f;

	// How long (in seconds) ApplyStun() suppresses AI movement for.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	float StunDuration = 2.0f;

	// Extra distance (in units), beyond a held item's holy-aura radius, kept while Stalking — so the
	// monster holds position well clear of the light (and the player's own collision) instead of
	// clipping its edge, getting stunned, or landing flank/patrol points too close to path to reliably.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Stalk")
	float StalkSafetyBuffer = 200.0f;

	// Min/max seconds between MoveToLightBorder() re-evaluations while Stalking — re-armed with a fresh
	// random interval in this range every time, independent of whether the previous move was ever
	// reached. This is what lets the monster react to a newly opened gap (the player turning, dropping
	// an item, etc.) within about this long, instead of only after fully arriving somewhere and running
	// out a separate wait.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Stalk")
	float FlankRescanIntervalMin = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Stalk")
	float FlankRescanIntervalMax = 1.0f;

	// Degrees to advance per MoveToLightBorder() rescan, along BoundaryPatrolDirection — this is what
	// steps the monster around the light's boundary ring, one short arc at a time, whether or not any
	// step ever turns out to be unprotected (an omnidirectional Censer just orbits forever; a directional
	// Candle eventually steps somewhere IsTargetInLight() no longer covers, and CheckForPlayer() takes it
	// from there — see the class comment). Deliberately a large step (not a few degrees): tiny steps land
	// within/near StalkAcceptanceRadius of each other, which reads as jittering in place rather than a
	// visible patrol.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Stalk")
	float BoundaryPatrolStepDegrees = 30.0f;

	// Seconds after a Candle stun ends (see ResetStun()) during which a fresh stun is suppressed —
	// Health still takes the hit, but ApplyStun()/StopMovement() don't re-fire. Gives
	// MoveToLightBorder() a clean window to actually get this monster moving again right after
	// recovering from the shield contact that triggered Stalk in the first place, instead of a second
	// near-immediate re-trigger cancelling its movement again mid-recovery.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Stalk")
	float ShieldBounceCooldown = 1.5f;

	// Cascade particle effect spawned at death (e.g. burning to ashes). Optional.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	UParticleSystem* BurnEffect = nullptr;

	// Niagara effect spawned at death (e.g. burning to ashes). Optional.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	UNiagaraSystem* BurnNiagaraEffect = nullptr;

	// Niagara effect spawned at the monster's location on death. Optional.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	TObjectPtr<UNiagaraSystem> DeathEffect;

	// Sound played at death. Optional.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Effects")
	USoundBase* BurnSound = nullptr;

	// Checks whether the player is within sight range/FOV/line of sight and reacts based on ABaseCharacter::IsEyesClosed.
	UFUNCTION(BlueprintCallable, Category = "AI")
	void CheckForPlayer();

	// Current horizontal movement speed, for driving locomotion blend spaces in the Animation Blueprint.
	UFUNCTION(BlueprintCallable, Category = "AI|Animation")
	float GetMovementSpeed() const;

	// Animation played via PlayAttackAnimation() (through Slot AttackAnimSlotName, layered over whatever
	// the Animation Blueprint's locomotion state machine is doing) when this monster catches the player —
	// see the CatchDistance handling in CheckForPlayer()'s .cpp for the actual trigger point. Defaults to
	// /Game/Whisper/Animations/Anim_Whisper_Attack1 via ConstructorHelpers; set to None to fall back to
	// the original instant-kill behavior with no attack animation at all (PlayAttackAnimation() itself
	// no-ops, and the kill fires immediately rather than waiting out an animation that isn't playing).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Animation")
	TObjectPtr<UAnimSequence> AttackAnimation;

	// Slot name PlayAttackAnimation() plays AttackAnimation through — must match a Slot node actually
	// present in the Animation Blueprint's AnimGraph (fed from the locomotion state machine's output, in
	// turn feeding the final Output Pose) for the attack to actually blend in; otherwise
	// PlaySlotAnimationAsDynamicMontage() still "succeeds" (no error) but nothing visibly changes since no
	// node in the graph is listening on that slot name.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Animation")
	FName AttackAnimSlotName = TEXT("DefaultSlot");

	// Seconds OnKillPlayer() waits, after ragdolling the player (see ABaseCharacter::HandleDeath()) and
	// starting the attack animation, before actually reloading the level — long enough for both to
	// visibly register instead of the level cutting away the same instant the kill triggers, per this
	// project's "no instant disappearance" death handling.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Animation")
	float DeathReloadDelay = 2.5f;

	// Plays AttackAnimation through AttackAnimSlotName via PlaySlotAnimationAsDynamicMontage() — no
	// pre-authored Montage asset needed, works with any AnimSequence directly. No-op if AttackAnimation is
	// unset or this monster has no valid AnimInstance (e.g. no Animation Blueprint assigned yet).
	UFUNCTION(BlueprintCallable, Category = "AI|Animation")
	void PlayAttackAnimation();

	// True while chasing the player, for switching states in the Animation Blueprint.
	UFUNCTION(BlueprintCallable, Category = "AI|Animation")
	bool GetIsChasing() const { return State == EMonsterState::Chase; }

	// True while Stalking (holding position at a detected player's light boundary), for switching
	// states in the Animation Blueprint.
	UFUNCTION(BlueprintCallable, Category = "AI|Animation")
	bool GetIsStalking() const { return State == EMonsterState::Stalk; }

	// Stuns this monster for Duration seconds — pauses AI movement (bIsStunned suppresses Tick()'s
	// Patrol handling, MoveToLightBorder(), and CheckForPlayer()'s Chase/attack logic alike, same as
	// the existing contact-stun path), then automatically resumes normal behavior via ResetStun().
	// Public and duration-parameterized (unlike the private contact-stun ApplyStun(), which always
	// uses this monster's own StunDuration) so a caller like a thrown Candle's impact
	// (AHolyItem::Throw()) can specify a duration independent of StunDuration. No-ops if already dead.
	UFUNCTION(BlueprintCallable, Category = "AI")
	void Stun(float Duration);

	// Kills this monster immediately — same death sequence as the Censer's instant-death aura
	// (HandleDeath(): stops AI permanently, disables capsule collision, plays burn effects, and
	// destroys the actor after a short delay). Callable directly (e.g. a thrown Censer's impact)
	// without needing to route through TakeDamage()/UCenserDamageType. No-ops if already dead.
	UFUNCTION(BlueprintCallable, Category = "AI")
	void Die();

	// Universal physical knockback: launches this monster along ImpactDirection (flattened horizontal —
	// see GetSafeNormal2D() in the .cpp — so a caller passing a direction with a large/zero Z component
	// still produces a sensible horizontal shove) scaled by PushForce, plus a fixed UpwardForce, via
	// LaunchCharacter(). Also staggers this monster (bIsStaggered) for StaggerDuration seconds, which
	// suppresses Tick()'s Patrol handling and CheckForPlayer()'s Chase/Stalk decision logic the same way
	// bIsStunned already does — without this, a Chase-state monster's own pathing would immediately
	// start correcting back toward the player the very same tick, masking the launch entirely. No-op if
	// already dead. Callable from anywhere (held-Censer aura, a melee swing, etc.) with its own
	// PushForce/UpwardForce, independent of any other caller's values.
	UFUNCTION(BlueprintCallable, Category = "AI|Knockback")
	void ApplyKnockback(FVector ImpactDirection, float PushForce = 800.0f, float UpwardForce = 150.0f);

	// True while a recent ApplyKnockback() is still suppressing AI movement/pathing decisions (see its
	// comment) — exposed for the Animation Blueprint to react to (e.g. a stagger pose), same pattern as
	// bIsAiming on ABaseCharacter.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Knockback")
	bool bIsStaggered = false;

	// Seconds ApplyKnockback()'s stagger (bIsStaggered) lasts — long enough for the physical launch to
	// actually carry the monster somewhere visible before AI movement resumes correcting toward its
	// target again.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Knockback")
	float StaggerDuration = 0.5f;

	// "Overhaul Monster Collision & Panic Sequence in Dark": physically bumping into this monster while
	// ABaseCharacter::IsEyesClosed() is true is deliberately NEVER an instant kill by itself (see
	// OnCapsuleHit(), bound to GetCapsuleComponent()->OnComponentHit in BeginPlay()) — it broadcasts an
	// alert to nearby monsters (see BroadcastAlert()/ReceiveAlert()) and immediately enters Scanning
	// (EnterScanningState() below, now unconditional on contact — the monster searches the area either
	// way), then a short beat later ResolveBlindEncounterReaction() decides whether the player ALSO drops
	// into Scenario B on top of that: stopped (Scanning alone continues to its own resolution) or kept
	// pushing forward into it (ABaseCharacter::EnterPanicState(), which suppresses Scanning's own timeout
	// for as long as the panic mini-game runs — see ResolveBlindEncounterReaction()'s own comment). Also
	// deliberately skipped entirely (no encounter starts at all, from any angle) if the player is mid-
	// Moonshine Rage Dash (ABaseCharacter::IsImmuneToMonsterCatch()) — a Ram is never a blind touch, it's
	// UMoonshineComponent::OnOwnerCapsuleHit()'s own knockback+stun instead. Either way, the ONLY way this
	// specific encounter actually kills the player is opening their eyes mid-Scan or mid-Panic — see
	// ABaseCharacter::OpenEyes()'s own check against BlindEncounterMonster, which calls
	// ResolveBlindEncounterKill() below — or PanicMeter maxing out with a monster still close by (see
	// ABaseCharacter::ForceEyesOpenFromPanic()); an individual missed breath beat no longer kills by itself.

	// Radius (world units) BroadcastAlert() searches for other ABaseMonster instances to notify. Also what
	// ABaseCharacter::RegisterMouthGasp() reuses (via this monster's own BroadcastAlert(), now public) to
	// alert nearby monsters every time the player gasps through their mouth during the Panic State — not
	// just whichever single monster happens to be close enough for ForceEyesOpenFromPanic()'s proximity
	// kill check. Raised from an earlier 2000 — a knockdown/mouth-gasp is meant to summon the whole nearby
	// pack (~35m), not just whoever happens to already be close.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Blind Encounter")
	float AlertRadius = 3500.0f;

	// Poll interval (seconds) for ResolveEyePeekReaction() while this monster is still closing in on a
	// caught-peeking player — TryTriggerEyePeekEncounter() arms EyePeekReactionTimerHandle as a REPEATING
	// timer at this interval (not a single one-shot delay) so the swing only actually launches once the
	// monster is genuinely within EyePeekAttackRange — see ResolveEyePeekReaction()'s own comment for why
	// a fixed delay used to let the swing fire from wherever the chase happened to be, including from
	// well outside melee range. Short (unlike the old one-shot 0.6s "windup"), matching
	// CheckForPlayer()'s own ~0.1s sight-cone cadence, so closing the last few units into range is
	// noticed promptly rather than up to a whole poll interval late.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Eye Peek Risk")
	float EyePeekReactionDelay = 0.15f;

	// Seconds ResolveEyePeekReaction() keeps polling/closing in on a caught-peeking player before giving
	// up entirely if it never actually gets within EyePeekAttackRange — measured from
	// EyePeekChaseStartTime. Prevents an indefinite chase when the player keeps enough distance (or
	// breaks line of sight/pathing) for the whole encounter.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Eye Peek Risk")
	float EyePeekMaxChaseDuration = 3.0f;

	// Seconds after ResolveEyePeekReaction()'s "swing blind" branch starts the animation before
	// OnAttackHitNotify() fires as a FALLBACK, even if the "Attack_Hit" AnimNotify on Anim_Whisper_Attack1
	// never actually fires (e.g. the Slot node isn't wired into the AnimGraph's Output Pose, or the
	// notify track never got saved correctly). 0.4s — roughly the swing's own windup — so the
	// crouch/proximity connect check (DoesEyePeekAttackConnect(), gating
	// Player->EnterPanicState()/BroadcastAlert()) still lands at approximately the right moment even with
	// no AnimNotify at all. OnAttackHitNotify() itself is idempotent — whichever of the real notify or
	// this fallback timer fires FIRST clears PendingEyePeekAttackTarget and does the real check; the
	// other one, arriving after, finds it already null and no-ops — so this is safe to have armed
	// unconditionally alongside the real notify, not just as an either/or choice.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Eye Peek Risk")
	float EyePeekAttackHitFallbackDelay = 0.4f;

	// Same idempotent-fallback role as EyePeekAttackHitFallbackDelay above, for TryStartChaseAttack()'s
	// own swing (PendingChaseAttackTarget) instead — a separate tuning value (not reused) so the two
	// attack sources' timings can be tuned independently later even though they currently share the same
	// 0.4s default.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Open-Eyes Chase")
	float ChaseAttackHitFallbackDelay = 0.4f;

	// Health damage applied (via UGameplayStatics::ApplyDamage(), same ABaseCharacter::TakeDamage()
	// pipeline every other damage source in this project already goes through) every time a monster's
	// swing actually connects and routes into ABaseCharacter::EnterPanicState() — in OnAttackHitNotify(),
	// for BOTH this rule's own hit branch (PendingChaseAttackTarget) AND Eye Peek Risk's existing
	// hit-while-eyes-closed branch (PendingEyePeekAttackTarget) — see that function's own comment. Purely
	// additive: EnterPanicState() and the Nose-Breathing panic-meter mini-game it starts are completely
	// untouched by this, so a hit still costs nothing extra if the mini-game is won — this is what makes a
	// long enough series of hits lethal on its own even when every single one of them is survived, rather
	// than the panic meter being the only thing at stake. Does NOT apply to Eye Peek Risk's separate
	// Fatality branch, ForceEyesOpenFromPanic()'s proximity-kill branch, or the Blind Encounter
	// eyes-reopened-while-scanning kill — all three already end the encounter outright via
	// ResolveBlindEncounterKill(); there is no Health left to spend by then. Default is a starting point
	// for PIE tuning (~5 hits to kill at default MaxHealth=100), not a final balance number.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Open-Eyes Chase")
	float ChaseAttackDamage = 20.0f;

	// True from TryTriggerEyePeekEncounter() until ResolveEyePeekReaction() actually resolves (in range,
	// or gives up after EyePeekMaxChaseDuration) — guards against re-triggering every ~0.1s
	// CheckForPlayer() tick while the player stays peeking inside the sight cone for the whole chase,
	// however long it ends up polling for. Also what drives Tick()'s "НАШЁЛ! ИДУ АТАКОВАТЬ" head marker
	// for the same duration.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Eye Peek Risk")
	bool bHasActiveEyePeekEncounter = false;

	// Radius (world units) of the overlap sphere DoesEyePeekAttackConnect() checks Player against when
	// the "Attack_Hit" AnimNotify fires (Content/Whisper/Animations/Anim_Whisper_Attack1, ~0.5s in — see
	// UAnimNotify_MonsterAttackHit's own comment). Deliberately generous (this monster's own attack
	// reach, not a pixel-perfect hitbox) since it's a blind swing, not an aimed one.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Eye Peek Risk")
	float EyePeekAttackRange = 200.0f;

	// How far (world units, along this monster's own forward vector) DoesEyePeekAttackConnect() centers
	// its overlap sphere in FRONT of this monster's own capsule origin — "перед лапой/грудью", not a
	// sphere that already contains the monster's own feet from the start.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Eye Peek Risk")
	float EyePeekAttackForwardOffset = 100.0f;

	// Seconds a monster spends turning toward + "scanning" a player who stopped moving right after
	// blindly touching it AND has no ongoing Scenario B panic to keep it suppressed — see
	// EnterScanningState()/EndScanningState() and ResolveBlindEncounterReaction()'s own comment.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Blind Encounter")
	float ScanDuration = 3.0f;

	// True while this monster is turning toward + scanning a player that stopped right after blindly
	// touching it — suppresses Tick()'s Patrol handling and CheckForPlayer()'s Chase/Stalk decision
	// logic, the same mechanism bIsStunned/bIsStaggered/bIsBeingRidden already use.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Blind Encounter")
	bool bIsScanning = false;

	// Seconds after OnCapsuleHit() before ResolveBlindEncounterReaction() checks the player's velocity to
	// decide Scenario A vs B — a hit event by definition means the player WAS moving into this monster,
	// so distinguishing "stopped right after" from "kept pushing" needs a follow-up sample, not the
	// collision instant itself.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Blind Encounter")
	float BlindTouchReactionDelay = 0.3f;

	// Player speed (units/sec) at or above which ResolveBlindEncounterReaction() treats them as still
	// pushing forward (Scenario B) rather than stopped (Scenario A).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Blind Encounter")
	float BlindTouchPushSpeedThreshold = 50.0f;

	// Seconds a Patrol-state monster that received an alert (see ReceiveAlert()) spends moving to/holding
	// at the alert location before resuming normal Patrol — suppresses Tick()'s Patrol handling (same
	// mechanism as bIsScanning etc.) but deliberately NOT CheckForPlayer()'s detection logic, so an
	// investigating monster can still spot and chase the player normally if it happens to see them.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Blind Encounter")
	float InvestigateDuration = 4.0f;

	// "Pack Formation on Arrival": the radius of the ring ReceiveAlert() spreads investigating monsters
	// onto around the alert point — NOT the alert location itself (300, ~3m, keeps them off the player's
	// exact position instead of walking onto it and physically bumping into them while they're trying to
	// stay still and quiet). Each monster's ring TARGET point is computed by BroadcastAlert()/
	// ReceiveAlert() (see their own comments) rather than every monster just pathing to AlertLocation with
	// this as a loose blob-shaped acceptance tolerance, so multiple arrivals actually surround the player
	// instead of clumping on whichever side happens to be nearest.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Blind Encounter")
	float InvestigateAcceptanceRadius = 300.0f;

	// How close ReceiveAlert()'s MoveToLocation() must get to its assigned ring point (see
	// InvestigateAcceptanceRadius) to consider itself "arrived" there — deliberately tight compared to
	// InvestigateAcceptanceRadius itself, since that property now defines the ring's radius, not the
	// tolerance for reaching a point on it.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Blind Encounter")
	float PackFormationTolerance = 50.0f;

	// Poll interval (seconds) CheckInvestigateArrival() re-checks distance to CurrentInvestigateRingTarget
	// at, once ReceiveAlert() has issued the move — same "short repeating timer" style
	// ResolveEyePeekReaction()/EyePeekReactionDelay already uses, so arrival is noticed promptly rather
	// than up to a whole poll interval late.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Blind Encounter")
	float InvestigateArrivalPollInterval = 0.4f;

	// "Piling on" — how long (seconds, from ABaseCharacter::LastPanicEpisodeStartTime) another monster is
	// still allowed to arrive at an already-panicking player and take its one attempt via
	// TryStartPanicStrike() — see CheckForPlayer()'s own standalone panic-strike check. Generous relative
	// to InvestigateDuration/PatrolSpeed: covers the real travel time across a level-sized distance to
	// reach the ring point AND still get one swing in once there, not just the hold-position window itself.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Blind Encounter")
	float PilingOnWindowDuration = 25.0f;

	// Scan duration (seconds) EnterScanningState() uses instead of ScanDuration specifically after a
	// "piling on" TryStartPanicStrike() swing is DODGED (see OnAttackHitNotify()'s own
	// PendingPanicStrikeTarget branch) — longer than the ordinary ScanDuration since a monster that just
	// closed the whole distance to reach an already-panicking player and still missed should search the
	// immediate area harder before giving up, rather than the shorter reaction-window scan a fresh Eye
	// Peek/Chase-attack dodge gets.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Blind Encounter")
	float PanicStrikeScanDuration = 5.0f;

	// Delay (seconds) between a "piling on" swing CONNECTING and this same monster arming its next
	// TryStartPanicStrike() attempt on the same still-panicking player (see OnAttackHitNotify()'s own
	// PendingPanicStrikeTarget hit branch) — without this, the next swing's PlayAttackAnimation() call fired
	// in the same instant as the hit, cutting the just-started attack animation off before it could ever
	// play (confirmed in PIE: read as a machine-gun stutter with no visible animation at all). Tune this in
	// the editor to roughly cover the attack animation's own length plus whatever "calling in reinforcements"
	// beat is wanted before the next swing visibly winds up.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Blind Encounter")
	float PanicStrikeRearmDelay = 1.2f;

	// True while this monster is investigating another monster's BroadcastAlert() — see
	// InvestigateDuration's own comment.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Blind Encounter")
	bool bIsInvestigatingAlert = false;

	// Called by ABaseCharacter::OpenEyes() when Player opens their eyes while BlindEncounterMonster ==
	// this — the actual, sole kill trigger for a blind encounter (Scenario A/B alike). Clears this
	// monster's own Scan state/focus and routes through the normal PlayAttackAnimation()+OnKillPlayer()
	// sequence for consistency with every other kill path.
	UFUNCTION(BlueprintCallable, Category = "AI|Blind Encounter")
	void ResolveBlindEncounterKill(class ABaseCharacter* Player);

	// Called by ABaseCharacter::EndPanicSafely()/ForceEyesOpenFromPanic()'s survival branch once Scenario
	// B's panic mini-game resolves without a kill — this monster has been sitting in Scanning
	// (bIsScanning) the whole time (ResolveBlindEncounterReaction() suppresses ScanTimerHandle's own
	// timeout for exactly this reason), so resolving the encounter now needs the SAME full cleanup
	// EndScanningState() already does (clear bIsScanning/focus/CurrentScanTarget/bHasActiveBlindEncounter),
	// not just the bHasActiveBlindEncounter flag alone. Scenario A's own timeout resolution calls
	// EndScanningState() directly instead of through this, since that path is entirely monster-side.
	UFUNCTION(BlueprintCallable, Category = "AI|Blind Encounter")
	void OnBlindEncounterResolved() { EndScanningState(); }

	// True while a player is riding this monster (see OnMounted()/OnDismounted()) — suppresses Tick()'s
	// Patrol handling and CheckForPlayer()'s Chase/Stalk decision logic, the same mechanism
	// bIsStunned/bIsStaggered already use, so this monster's own AI stops fighting the rider's
	// ABaseCharacter::AddMovementInput() passthrough with its own MoveTo requests.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mounting")
	bool bIsBeingRidden = false;

	// Whether this monster is allowed to be mounted at all — checked by
	// UMountRiderComponent::CanMountMonster() alongside Health/bIsBeingRidden/mesh validity. Defaults to true
	// and is NEVER written to false by anything in the Shadow AI (HandleDawnStarted(), CheckSunExposure(),
	// the shadow-restricted wander/combat logic, etc.) — mounting stays available through every Day/Night
	// phase and every AI state (Patrol, Stalk, Chase, fleeing-to-shadow, idle-in-shadow) unconditionally.
	// The only things that should ever actually prevent a mount are already-being-ridden or already-dead,
	// both checked separately; this exists purely as an explicit, independently toggleable designer/
	// Blueprint override (e.g. a boss or a scripted-sequence monster that should never be rideable) —
	// nothing in this class sets it itself.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mounting")
	bool bCanBeMounted = true;

	// Socket name on GetMesh() a rider attaches to, tried first by GetMountSocketName() — falls back to
	// "Spine_03" then "head" if this skeleton doesn't have it, and to NAME_None (caller uses a manual
	// offset instead) if none of the three exist.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mounting")
	FName MountSocketName = TEXT("MountSocket");

	// The player currently riding this monster, set by OnMounted()/cleared by OnDismounted(). Weak since
	// this monster doesn't own the rider's lifetime.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mounting")
	TWeakObjectPtr<ACharacter> CurrentRider;

	// GetCharacterMovement()'s rotation-mode properties as of just before OnMounted() overrode them (see
	// its comment on why AI's normal bUseControllerDesiredRotation must be replaced with movement-facing
	// rotation while ridden) — restored exactly by OnDismounted() rather than assuming this monster's own
	// AI defaults.
	bool bPreMountUseControllerDesiredRotation = false;
	bool bPreMountOrientRotationToMovement = false;
	FRotator PreMountRotationRate = FRotator::ZeroRotator;

	// Called once a rider's attachment/collision setup is already done (see
	// UMountRiderComponent::TryMount()) — sets bIsBeingRidden and stops any AI move already in
	// progress, so this monster goes physically still under the rider immediately rather than finishing
	// out its last MoveTo first.
	UFUNCTION(BlueprintCallable, Category = "Mounting")
	void OnMounted(ACharacter* Rider);

	// Called once a rider has already detached (see UMountRiderComponent::Dismount()) — clears bIsBeingRidden
	// and resumes this monster's own behavior: Patrol re-issues its current patrol move explicitly (it
	// has no periodic self-re-check the way Chase/Stalk do); Chase/Stalk simply pick back up on their
	// own via the next CheckForPlayer() tick / already-armed FlankRescanTimerHandle.
	UFUNCTION(BlueprintCallable, Category = "Mounting")
	void OnDismounted();

	// Best socket on GetMesh() for a rider to attach to: MountSocketName if this skeleton has it, else
	// "Spine_03", else "head", else NAME_None if none exist (caller should fall back to a manual
	// relative-location offset instead of attaching to a specific socket).
	UFUNCTION(BlueprintCallable, Category = "Mounting")
	FName GetMountSocketName() const;

	// Rotation turn speed (degrees/sec, yaw only) OnMounted() gives this monster's CharacterMovement so
	// it visibly turns to face the rider's movement direction instead of sliding around without turning.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mounting")
	float MountRotationRateYaw = 540.0f;

	// The level's day/night driver, found once in BeginPlay() via UGameplayStatics::GetActorOfClass() —
	// there's expected to be exactly one in the level. Null (and every sun-exposure feature below a no-op)
	// in a level that doesn't place one, e.g. an interior-only test level.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sun Exposure")
	TObjectPtr<ADayNightManager> DayNightManager;

	// Channel CheckSunExposure()'s line trace runs on. ECC_Visibility by default (matches every other
	// line-of-sight trace already in this class — see IsPlayerVisible()); point this at a dedicated custom
	// channel instead if roofs/foliage need to block sun differently than they block sight.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun Exposure")
	TEnumAsByte<ECollisionChannel> SunTraceChannel = ECC_Visibility;

	// How often (in seconds) CheckSunExposure() re-traces toward the sun while active — only runs at all
	// between OnDayStarted and OnNightStarted (see HandleDayStarted()/HandleNightStarted()), not every
	// Tick, since a line trace toward the sun every frame for every daylit monster in the level is far
	// more than this needs.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun Exposure")
	float SunCheckInterval = 0.25f;

	// Length (in units) of CheckSunExposure()'s trace toward the sun — needs to comfortably clear the
	// tallest roof/terrain in the level; reaching this without a hit counts as unobstructed sun exposure.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun Exposure")
	float SunTraceDistance = 10000.0f;

	// Radius (in units), centered on this monster's current location, FindShadowLocation() samples random
	// reachable NavMesh points from.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun Exposure")
	float ShadowSearchRadius = 1500.0f;

	// How many random NavMesh points FindShadowLocation() tests (each a separate sun-trace) before giving
	// up and falling back to the closest one tested — see that function's own comment.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun Exposure")
	int32 MaxShadowSearchAttempts = 12;

	// Acceptance radius (units) used for shadow-fleeing MoveToLocation() calls (HandleDawnStarted()'s
	// initial move and CheckSunExposure()'s retry-while-still-burning move) — generous relative to the
	// default per-agent radius MoveToLocation() would otherwise fall back to, so a monster fleeing at
	// FleeMaxWalkSpeed reports arrival cleanly near the shadow point's edge instead of overshooting and
	// endlessly correcting right on top of it, which reads as frozen/jittering rather than a completed
	// flee.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun Exposure")
	float ShadowAcceptanceRadius = 120.0f;

	// Holy-fire damage per second applied while SunState is SunlitBurning (scaled by SunCheckInterval per
	// actual tick — see ApplySunDamage()). Set high enough relative to Health/SunCheckInterval for an
	// effectively instant kill on direct sun exposure, or lower for a survivable scorch — this one property
	// covers both ends of "SunDamagePerSecond or instant death" rather than needing a separate toggle.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun Exposure")
	float SunDamagePerSecond = 25.0f;

	// GetCharacterMovement()->MaxWalkSpeed HandleDawnStarted() boosts to for the duration of a flee —
	// deliberately independent of PatrolSpeed/ChaseSpeed, since fleeing burning sunlight should read as
	// more urgent than either. Restored to whatever MaxWalkSpeed actually was beforehand (not assumed to
	// be PatrolSpeed — this monster might have been mid-Chase, at ChaseSpeed, the instant Dawn struck) by
	// RestoreNormalMaxWalkSpeed(), called once this monster reaches cover (CheckSunExposure() reports
	// SafeInShadow while still fleeing) or by HandleNightStarted(), whichever happens first.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun Exposure")
	float FleeMaxWalkSpeed = 1100.0f;

	// While true, CheckSunExposure() draws its trace as a DrawDebugLine (green = hit shadow-blocking
	// geometry/safe, red = reached SunTraceDistance unobstructed/burning) and both it and ApplySunDamage()
	// post their on-screen readouts (current SunState, damage this tick, remaining Health). Kept ON by
	// default: DrawDebugLine()/AddOnScreenDebugMessage() are no-ops in Shipping builds and cost nothing
	// meaningful in Development, so there's no real cost to leaving this visible while tuning
	// SunDamagePerSecond/SunCheckInterval/SunTraceDistance against an actual level. UE_LOG output for the
	// same events is NOT gated by this — it's always on, independent of on-screen display settings.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sun Exposure|Debug")
	bool bDebugSunTrace = true;

	// Result of the last CheckSunExposure() trace — see EMonsterSunState's own comment. VisibleAnywhere/
	// BlueprintReadOnly like SunState's sibling read-only state flags elsewhere in this class (bIsStaggered
	// etc.) — driven entirely by CheckSunExposure(), not meant to be hand-authored.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sun Exposure")
	EMonsterSunState SunState = EMonsterSunState::SafeInShadow;

	// True from OnDawnStarted until the next OnNightStarted (see HandleDawnStarted()/HandleNightStarted())
	// — suppresses Tick()'s Patrol handling and CheckForPlayer()'s Chase/Stalk decision logic, the same
	// mechanism bIsStunned/bIsStaggered/bIsBeingRidden already use, so normal Patrol/Chase/Stalk
	// decision-making doesn't fight HandleDawnStarted()'s own native FindShadowLocation()+MoveToLocation()
	// call. This is what actually keeps the monster "in cover" once it arrives — nothing re-engages
	// Patrol/Chase while this stays true, so an arrived (or already-safe) monster simply stops instead of
	// wandering back off, right up until HandleNightStarted() clears it.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sun Exposure")
	bool bIsFleeingToShadow = false;

	// True while bIsFleeingToShadow, for switching states in the Animation Blueprint — same pattern as
	// GetIsChasing()/GetIsStalking() above.
	UFUNCTION(BlueprintCallable, Category = "Sun Exposure|Animation")
	bool GetIsFleeingToShadow() const { return bIsFleeingToShadow; }

	// True while this monster is SAFELY STOPPED in shadow (bIsFleeingToShadow stays true the whole time,
	// covering both "still actively running toward cover" and "arrived, standing still in cover" — this
	// flag distinguishes the two for the Animation Blueprint, and is what CheckSunExposure() sets once
	// the path follower genuinely finishes reaching CurrentShadowTarget while SunState reads
	// SafeInShadow, rather than the instant the trace merely reports safe (see its own comment on why
	// that distinction matters — cutting movement immediately on trace result alone is what used to read
	// as an abrupt mid-stride freeze). Cleared the moment SunState flips back to SunlitBurning (the sun
	// moved) or HandleNightStarted() ends the whole flee episode.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Sun Exposure")
	bool bIsIdleInShadow = false;

	// True while bIsIdleInShadow, for switching states in the Animation Blueprint — same pattern as
	// GetIsFleeingToShadow() above.
	UFUNCTION(BlueprintCallable, Category = "Sun Exposure|Animation")
	bool GetIsIdleInShadow() const { return bIsIdleInShadow; }

	// Fired once when HandleDawnStarted() sets bIsFleeingToShadow, AFTER it has already issued the native
	// FindShadowLocation()+MoveToLocation() call — movement itself no longer depends on this being bound to
	// anything. Purely a supplementary hook now (a fear animation, a sound cue, a Blueprint-side visual
	// reaction), safe to leave unbound.
	UFUNCTION(BlueprintImplementableEvent, Category = "Sun Exposure")
	void OnStartFleeingToShadow();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	// Crash hardening: explicitly clears every FTimerHandle this class owns the moment this actor is torn
	// down (Destroy(), level reload/reopen, PIE end, etc.) — reported crash was an assertion failure inside
	// FUObjectArray::IndexToObject() (Index >= 0) reached from MoveToRandomWanderPoint(), meaning a retry
	// timer (WaitTimerHandle) fired against this monster's `this` AFTER its underlying memory had already
	// been freed by a level reload, not merely marked pending-kill. IsValid()-style checks (this class uses
	// several already, see MoveToRandomWanderPoint()/MoveToNextPatrolPoint()) can only protect against a
	// pending-kill-but-still-allocated object — they still dereference `this`, which is itself undefined
	// behavior once the memory is genuinely gone. FTimerManager::SetTimer()'s UObject-bound overload (used
	// everywhere in this class) already wraps callbacks in a weak reference that normally no-ops once GC has
	// run, but a level reload's own teardown ordering can still race that safety net; explicitly clearing
	// every handle here removes the dependency on that race entirely rather than trying to out-guess it from
	// inside the callback.
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// Called when the monster catches the player with their eyes open. Ragdolls Player immediately (see
	// ABaseCharacter::HandleDeath()) and delays the actual level reload (see DeathReloadDelay below) so
	// the ragdoll — and PlayAttackAnimation()'s attack animation, already playing by the time this is
	// called from CheckForPlayer() — both have time to actually be seen instead of being cut off the
	// instant the level reloads.
	UFUNCTION(BlueprintCallable, Category = "AI")
	void OnKillPlayer(class ABaseCharacter* Player);

public:
	// "Eye Peek Risk": a THIRD, independent detection channel alongside IsPlayerVisible()/
	// IsPlayerLookingAtMonster() (both of which unconditionally reject a player with
	// ABaseCharacter::IsEyesClosed() true). A player who is nominally eyes-closed but currently PEEKING
	// (ABaseCharacter::GetIsAiming(), RMB held — see that class's own EyePeekVignetteIntensity comment)
	// is still catchable if inside this monster's ordinary sight cone (IsPlayerWithinSightCone()) —
	// checked every ~0.1s from CheckForPlayer(), which calls this the instant that's true. Starts the
	// chase (AIController::MoveToActor(), records EyePeekChaseStartTime) and arms
	// EyePeekReactionTimerHandle as a REPEATING poll (EyePeekReactionDelay interval) — see
	// ResolveEyePeekReaction() for the actual "close enough to swing yet?" gating and the
	// Fatality/Panic+Alert/Dodge decision once it is. ALSO calls BroadcastAlert(Player->GetActorLocation())
	// — "Group Aggro": every other living ABaseMonster within AlertRadius gets ReceiveAlert()'d and
	// converges on the spot the player was spotted at (same Call Pack mechanism the physical blind-touch
	// encounter and a mouth-gasp already use), not just this one monster giving chase alone. A recruited
	// monster that ends up close enough afterward will naturally re-detect the still-peeking player
	// through its OWN CheckForPlayer() sight-cone check and run its own independent
	// TryTriggerEyePeekEncounter()/chase/swing — no separate "recruited monsters attack too" system
	// needed on top of the existing pack-alert machinery. No-ops (does nothing, including no alert) if
	// bHasActiveEyePeekEncounter is already true (an encounter is already pending) or Player is invalid.
	// Public (unlike most of this class's other reaction functions — same "promoted for a specific
	// external caller" reasoning as BroadcastAlert()) so CheckForPlayer() above can call it and
	// FEyePeekRiskReactionTest can drive/verify it directly without waiting out
	// EyePeekReactionTimerHandle's real polling.
	void TryTriggerEyePeekEncounter(class ABaseCharacter* Player);

	// Background "open eyes in sight = attack" rule — formalizes what IsPlayerVisible()'s existing
	// IsEyesClosed()==false gate already implies: for ANY monster in its ordinary Patrol/Stalk/Chase
	// state machine (NOT a substitute for, and never running alongside, Eye Peek Risk or Blind Encounter/
	// Scanning — those keep their own separate entry conditions/timers untouched), catching the player
	// open-eyed within CatchDistance now starts a swing instead of an instant kill. Called from
	// CheckForPlayer()'s CatchDistance branch (after light-protection/Moonshine i-frames are ruled out
	// there) — see this function's own .cpp comment for the swing sequence, and PendingChaseAttackTarget's
	// comment for how the actual dodge-vs-hit decision resolves later, at the swing's real point of
	// contact, via the SAME OnAttackHitNotify()/DoesEyePeekAttackConnect() infrastructure Eye Peek Risk
	// uses. Public so CheckForPlayer() can call it and automation coverage can drive it directly, same
	// "promoted for a specific external caller" reasoning as TryTriggerEyePeekEncounter() above.
	void TryStartChaseAttack(class ABaseCharacter* Player);

	// Timer callback (EyePeekReactionTimerHandle, armed by TryTriggerEyePeekEncounter() as a REPEATING
	// timer) — polls every EyePeekReactionDelay seconds while this monster closes in on a caught-peeking
	// player, and only actually resolves the reaction matrix once Player is within EyePeekAttackRange —
	// see its own distance check for why: the swing must never launch from a distance it could never
	// have connected from in the first place (the earlier bug this fixes: a fixed windup timer let the
	// swing fire regardless of how far away the chase still was). While still out of range, this
	// function just returns and lets the SAME repeating timer poll again next interval — unless
	// EyePeekMaxChaseDuration has elapsed since EyePeekChaseStartTime, in which case it gives up
	// entirely (clears bHasActiveEyePeekEncounter, stops the timer and this monster's movement, no swing
	// ever plays). Once actually in range, clears bHasActiveEyePeekEncounter/the timer and resolves:
	//   - Player is STILL peeking (GetIsAiming() true — "eyes open" at the moment the blind swing
	//     lands) -> Fatality: the same PlayAttackAnimation() + OnKillPlayer() path CheckForPlayer()'s own
	//     catch-range kill uses.
	//   - Player let go of RMB (GetIsAiming() false, eyes closed again), whether standing OR crouched
	//     right now -> stops navigating and plays the swing (PlayAttackAnimation()) — but applies NO
	//     connect/miss decision of its own here: no crouch check, no
	//     Player->EnterPanicState()/BroadcastAlert() call. The ENTIRE connect/miss decision (crouch AND
	//     a SECOND, later proximity check at the swing's actual point of contact) happens in
	//     OnAttackHitNotify()/DoesEyePeekAttackConnect() — see PendingEyePeekAttackTarget, set here for it
	//     to pick up. Also arms EyePeekAttackHitFallbackTimerHandle to call OnAttackHitNotify() itself
	//     after EyePeekAttackHitFallbackDelay seconds regardless of whether the "Attack_Hit" AnimNotify on
	//     the animation ever actually fires — see that property's own comment on why relying solely on
	//     the AnimNotify firing turned out not to reliably apply the stun.
	// Takes a TWeakObjectPtr (not a raw pointer), same dangling-reference safety reasoning as
	// ResolveBlindEncounterReaction()'s own parameter — the player could be destroyed while this is still
	// polling. No-ops (clearing the encounter/timer) if the player went invalid or is already dead by
	// the time this fires. Public — same reasoning as TryTriggerEyePeekEncounter() above.
	void ResolveEyePeekReaction(TWeakObjectPtr<class ABaseCharacter> WeakPlayer);

	// Called from TWO places — whichever fires first wins, the other is a safe no-op: (1)
	// UAnimNotify_MonsterAttackHit::Notify(), the real "Attack_Hit" AnimNotify placed on
	// Anim_Whisper_Attack1 at the moment the swing's paw/claw actually reaches forward, and (2)
	// EyePeekAttackHitFallbackTimerHandle, a fixed-delay fallback armed alongside PlayAttackAnimation()
	// so the stun still lands reliably even if the real AnimNotify never fires (e.g. the Slot node isn't
	// actually wired into the AnimGraph's Output Pose) — see EyePeekAttackHitFallbackDelay's own comment.
	// This is the ONLY place ResolveEyePeekReaction()'s swing actually applies
	// Player->EnterPanicState()/BroadcastAlert() now — gated on PendingEyePeekAttackTarget being set
	// (only true right after that branch ran; the ordinary CheckForPlayer() catch-kill and the Eye Peek
	// Fatality branch both resolve synchronously via OnKillPlayer() and never set it, so this safely
	// no-ops for them even though the notify fires during their playback of the same animation too) AND
	// DoesEyePeekAttackConnect() actually passing at THIS later moment (crouch state AND proximity both
	// re-checked here, not whatever the sight-cone check saw when the encounter first triggered). Clears
	// PendingEyePeekAttackTarget AND EyePeekAttackHitFallbackTimerHandle unconditionally either way, so
	// neither a stale target nor a redundant second call can leak into a later, unrelated swing.
	//
	// Also the same shared entry point for TryStartChaseAttack()'s swing (PendingChaseAttackTarget,
	// checked after PendingEyePeekAttackTarget so the two never resolve each other's swing) — a dodge
	// there routes into EnterScanningState() instead of missing silently, a hit into
	// ABaseCharacter::EnterPanicState() (no BroadcastAlert() on that hit specifically — TryStartChaseAttack()
	// already made its own second Group Aggro call right before the swing started).
	void OnAttackHitNotify();

	// Called by ABaseCharacter::EnterPanicState() on the AttackingMonster that just caused the FIRST hit of
	// a panic episode — arms PanicStrikeEligibleTime (GetWorld()->GetTimeSeconds() + PanicStrikeRearmDelay,
	// the SAME delay a "piling on" monster already waits between its own repeat swings) so this monster can
	// join the same "piling on" cycle everyone else uses, once its own opening hit's attack animation has
	// had time to finish — rather than needing five separate call sites (Chase-attack/Naskok/Eye Peek/Blind
	// Encounter) each individually taught to delay before re-swinging. Single entry point, since
	// EnterPanicState() is itself the one place every one of those five paths already funnels through.
	// Public so ABaseCharacter (not a subclass of this class) can call it.
	void ArmSelfPilingOnCooldown();

	// Публичная проверка "сканирует ли этот монстр именно ЗА ЭТИМ игроком" (одного bIsScanning
	// недостаточно — он не говорит, за кем именно) — нужна внешним вызывающим (UProbeComponent::Probe())
	// для правила "прощупать уже раздражённого монстра второй раз — провоцирует".
	UFUNCTION(BlueprintCallable, Category = "AI|Blind Encounter")
	bool IsScanningForPlayer(const class ABaseCharacter* Player) const { return bIsScanning && CurrentScanTarget.Get() == Player; }

	// Пока монстр ещё стоит развёрнутым от ReactToProbe() (ProbeStartleTimerHandle ещё тикает) И
	// повернулся именно к ЭТОМУ игроку — тот же дух, что и IsScanningForPlayer() выше, но для более
	// лёгкой реакции "насторожился" вместо полноценного Scanning. Взаимоисключающи по времени: пока
	// bIsScanning == true, ReactToProbe() вообще не вызывается (см. её собственный guard), так что оба
	// никогда не отвечают true одновременно.
	bool IsStartledByProbe(const class ABaseCharacter* Player) const
	{
		return GetWorldTimerManager().IsTimerActive(ProbeStartleTimerHandle) && ProbeStartleTarget.Get() == Player;
	}

	// То же наказание "второй раз без шанса увернуться", что уже есть у TryStartNaskokAttack()'s ветки
	// повторного ФИЗИЧЕСКОГО контакта во время сканирования (см. её собственный комментарий) — вызывается
	// отдельно (не рефакторинг того существующего блока — сознательно, чтобы не трогать уже протестированный
	// код) из трёх мест: UProbeComponent::Probe() (повтор прощупывания во время bIsScanning ИЛИ во время
	// ReactToProbe()'s "насторожился"-окна — см. IsScanningForPlayer()/IsStartledByProbe()) и
	// OnCapsuleHit() (повторное физическое касание тем же игроком во время активного Blind Encounter с
	// ним же). Название без "WhileScanning" сознательно — больше не единственный источник вызова.
	void PunishRepeatProvocation(class ABaseCharacter* Player);

	// Секунды, на которые монстр разворачивается к игроку после "безопасного" прощупывания (ещё НЕ
	// bIsScanning) — короткая реакция "насторожился", не полноценное состояние Scanning: не останавливает
	// движение, не блокирует Tick()/CheckForPlayer(), не делает повторное прощупывание опасным. Просто
	// AICon->SetFocus() на это время, потом ClearFocus().
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Probe")
	float ProbeStartleDuration = 1.5f;

	// Вызывается UProbeComponent::Probe() из "безопасной" ветки (монстр найден, но ещё не сканирует
	// именно за этим игроком) — разворачивает монстра к игроку на ProbeStartleDuration секунд, чисто
	// косметическая реакция. No-op, если монстр уже bIsScanning (та ветка обрабатывается отдельно,
	// PunishRepeatProvocation()) или оглушён/шатается/оседлан — тогда фокус менять не нужно/не к месту.
	void ReactToProbe(class ABaseCharacter* Player);

private:
	// The actual "did the blind swing connect" check, run from OnAttackHitNotify() at the swing's real
	// point of contact: false outright if Player->IsCrouched() (they ducked under it, however close they
	// are); otherwise a physics overlap sphere (radius EyePeekAttackRange) centered
	// EyePeekAttackForwardOffset units in FRONT of this monster — "перед лапой/грудью", not a sphere that
	// already contains this monster's own feet — checked for Player specifically among whatever it
	// overlaps. Also draws the TEMPORARY diagnostic sphere — see bShowEyePeekDebug's own comment.
	bool DoesEyePeekAttackConnect(const class ABaseCharacter* Player) const;

	// Open-eyes swing connect check — тот же range/proximity-check, что и DoesEyePeekAttackConnect() выше, но
	// ДРУГОЕ правило приседа: пока глаза открыты, присед — не слепое уклонение (игрок ВИДИТ замах в любом
	// случае), поэтому само по себе не спасает. Вместо этого замах "коммитится" к высоте в момент своего
	// НАЧАЛА (bTargetWasCrouchedAtSwingStart, записывается TryStartChaseAttack()/TryStartNaskokAttack() прямо
	// перед PlayAttackAnimation()) — смена стойки ПОСЛЕ этого коммита (присесть или встать во время замаха)
	// уводит именно ЭТОТ замах в молоко; следующий замах заново считает текущую стойку и бьёт как обычно, если
	// стойка снова не поменяется. Если к моменту фактической проверки глаза уже ЗАКРЫТЫ (закрыл во время
	// замаха) — откатывается на обычное слепое правило (присел сейчас = уклонился), как в
	// DoesEyePeekAttackConnect() выше.
	bool DoesOpenEyesAttackConnect(const class ABaseCharacter* Player, bool bTargetWasCrouchedAtSwingStart) const;

	// Общая для DoesEyePeekAttackConnect()/DoesOpenEyesAttackConnect() часть — дальность/overlap-проверка без
	// какой-либо логики приседа (та уже решена вызывающей стороной). Раньше жила прямо внутри
	// DoesEyePeekAttackConnect(); вынесена, чтобы не дублировать при добавлении DoesOpenEyesAttackConnect().
	bool IsPlayerWithinAttackContactRange(const class ABaseCharacter* Player) const;

	// Set by ResolveEyePeekReaction()'s Panic branch right before PlayAttackAnimation() — see
	// OnAttackHitNotify()'s own comment for why the actual stun/alert application waits for this instead
	// of firing synchronously there. TWeakObjectPtr, same dangling-reference safety reasoning as
	// ResolveEyePeekReaction()'s own WeakPlayer parameter.
	TWeakObjectPtr<class ABaseCharacter> PendingEyePeekAttackTarget;

	// GetWorld()->GetTimeSeconds() as of TryTriggerEyePeekEncounter() first arming the chase — what
	// ResolveEyePeekReaction() measures EyePeekMaxChaseDuration against to decide when to give up
	// polling for the player to come within EyePeekAttackRange.
	float EyePeekChaseStartTime = 0.0f;

	// TEMPORARY diagnostic — sets bIsPlayingEyePeekAttackAnim and (re)arms
	// EyePeekAttackDebugTimerHandle to clear it again after EyePeekAttackDebugDuration. Called from
	// ResolveEyePeekReaction() right before EITHER of its own PlayAttackAnimation() call sites (Fatality
	// and "swing blind" alike) — see bIsPlayingEyePeekAttackAnim's own comment.
	void StartEyePeekAttackAnimDebug();

	// Timer callback (EyePeekAttackDebugTimerHandle) — clears bIsPlayingEyePeekAttackAnim.
	void ClearEyePeekAttackAnimDebug();

	FTimerHandle EyePeekAttackDebugTimerHandle;

	// Timer handle for the FALLBACK OnAttackHitNotify() call — see EyePeekAttackHitFallbackDelay's own
	// comment. Armed alongside PlayAttackAnimation() in ResolveEyePeekReaction()'s "swing blind" branch;
	// cleared by OnAttackHitNotify() itself (whichever of the real notify or this fallback fires first).
	FTimerHandle EyePeekAttackHitFallbackTimerHandle;

	// Set by TryStartChaseAttack() right before PlayAttackAnimation() — the background "open eyes in
	// sight = attack" rule's own equivalent of PendingEyePeekAttackTarget above, kept as a SEPARATE field
	// (not reused) so the two attack sources never resolve or clear each other's swing in
	// OnAttackHitNotify(). Also doubles as CheckForPlayer()'s own "a swing from this rule is already in
	// flight" guard (checked at the top of that function) — see TryStartChaseAttack()'s own comment for
	// why that guard is needed. TWeakObjectPtr, same dangling-reference safety reasoning as
	// PendingEyePeekAttackTarget.
	TWeakObjectPtr<class ABaseCharacter> PendingChaseAttackTarget;

	// Player->IsCrouched() в момент, когда TryStartChaseAttack() закоммитил этот замах — см. собственный
	// комментарий DoesOpenEyesAttackConnect() про то, зачем высота цели фиксируется один раз в начале замаха,
	// а не перечитывается в момент удара.
	bool bChaseAttackTargetWasCrouchedAtSwingStart = false;

	// Timer handle for the FALLBACK OnAttackHitNotify() call, armed by TryStartChaseAttack() — same role
	// as EyePeekAttackHitFallbackTimerHandle, kept separate so the two attack sources never clear or
	// re-arm each other's fallback.
	FTimerHandle ChaseAttackHitFallbackTimerHandle;

	// Set by TryStartNaskokAttack() right before PlayAttackAnimation() — Naskok's (open-eyes physical
	// ram, OnCapsuleHit()) own equivalent of PendingChaseAttackTarget above, kept as a SEPARATE field so
	// none of the three attack sources (Eye Peek, background open-eyes Chase, Naskok) ever resolve or
	// clear each other's swing in OnAttackHitNotify(). Also doubles as TryStartNaskokAttack()'s own
	// "a swing from THIS rule is already in flight" re-entrancy guard — the capsule can keep generating
	// OnComponentHit events every physics substep while the two capsules stay overlapped mid-windup, and
	// OnCapsuleHit() has no bIsScanning-style guard of its own the way CheckForPlayer() does for the
	// other two sources. TWeakObjectPtr, same dangling-reference safety reasoning as
	// PendingEyePeekAttackTarget/PendingChaseAttackTarget.
	TWeakObjectPtr<class ABaseCharacter> PendingNaskokAttackTarget;

	// Та же роль, что у bChaseAttackTargetWasCrouchedAtSwingStart выше, но для TryStartNaskokAttack().
	bool bNaskokAttackTargetWasCrouchedAtSwingStart = false;

	// Timer handle for the FALLBACK OnAttackHitNotify() call, armed by TryStartNaskokAttack() — same role
	// as ChaseAttackHitFallbackTimerHandle/EyePeekAttackHitFallbackTimerHandle, kept separate so none of
	// the three attack sources ever clear or re-arm each other's fallback. Reuses ChaseAttackHitFallbackDelay
	// for the actual delay value (deliberately not a new tunable — see TryStartNaskokAttack()'s own comment).
	FTimerHandle NaskokAttackHitFallbackTimerHandle;

	// Set by TryStartPanicStrike() right before PlayAttackAnimation() — the "piling on" rule's own
	// equivalent of PendingChaseAttackTarget/PendingNaskokAttackTarget above, kept as a SEPARATE field so
	// none of the four attack sources ever resolve or clear each other's swing in OnAttackHitNotify().
	// Also doubles as CheckForPlayer()'s own "a swing from this rule is already in flight" re-entrancy
	// guard (checked at the top of that function, same as the other two). TWeakObjectPtr, same
	// dangling-reference safety reasoning as the others.
	TWeakObjectPtr<class ABaseCharacter> PendingPanicStrikeTarget;

	// Timer handle for the FALLBACK OnAttackHitNotify() call, armed by TryStartPanicStrike() — same role
	// as the other three sources' own fallback timers, kept separate so none of them ever clear or re-arm
	// each other's fallback. Reuses ChaseAttackHitFallbackDelay for the delay value, same as
	// NaskokAttackHitFallbackTimerHandle already does — no new tunable needed.
	FTimerHandle PanicStrikeHitFallbackTimerHandle;

	// Armed by OnAttackHitNotify()'s own PendingPanicStrikeTarget HIT branch (bound to
	// TryStartPanicStrikeIfStillEligible()) — delays this monster's next TryStartPanicStrike() attempt by
	// PanicStrikeRearmDelay after a confirmed hit, so the just-played attack animation (and the alert's own
	// "reinforcements incoming" beat) has time to actually be seen instead of being cut off by an
	// immediately re-triggered swing. See PanicStrikeRearmDelay's own comment.
	FTimerHandle PanicStrikeRearmTimerHandle;

	// Pure geometry check — distance (SightDistance), angle (FieldOfViewAngle), and an unobstructed
	// line-of-sight trace — with no opinion of its own on whether the player's eyes are open, closed, or
	// peeking. Factored out of IsPlayerVisible() (which layers its own "eyes must be open" gate on top)
	// so TryTriggerEyePeekEncounter() can reuse the exact same cone/LOS math while layering a DIFFERENT
	// gate instead (ABaseCharacter::GetIsAiming(), peeking through nominally-closed eyes).
	bool IsPlayerWithinSightCone(const class ABaseCharacter* Player) const;

	// True if Player is within SightDistance/FieldOfViewAngle, has an unobstructed line of sight, and their eyes are open.
	bool IsPlayerVisible(const class ABaseCharacter* Player) const;

	// Alternate sight-detection path checked by IsPlayerVisible(): true if THIS monster can see (same
	// range/FOV/LOS checks as the direct player check, just re-targeted) any OTHER live ABaseMonster
	// that's currently being ridden BY Player. A ridden monster's own body is a much bigger, harder-to-
	// block sight target than the small rider capsule sitting on top of it, so this catches an obviously-
	// visible rider that a nearby monster's own body happens to block the direct line-of-sight trace to.
	// The actual chase TARGET stays Player throughout (not the ridden monster) — this only broadens how
	// a monster can SPOT that target, so every downstream light-protection/catch-distance/kill check
	// keeps working unchanged.
	bool IsRiddenMonsterOfPlayerVisible(const class ABaseCharacter* Player) const;

	// True if Player is within DetectionDistance of this monster, has an unobstructed line of sight to
	// it, and their own forward vector points roughly at this monster (within EyeContactAngle) — i.e.
	// the player has made "eye contact" with the monster. An independent detection channel from
	// IsPlayerVisible(): either one alone is enough to trigger Agro.
	bool IsPlayerLookingAtMonster(const class ABaseCharacter* Player) const;

	// True if Player is currently protected — by ANY held item — from this monster at its CURRENT
	// location (GetActorLocation()). An item protects if this monster is both within that item's
	// HolyAuraRadius + StalkSafetyBuffer AND within its protection cone (AHolyItem::
	// IsLocationInProtectionCone() — always true for an omnidirectional item like the Censer). Checked
	// per-item (not by combining a shared max-radius with a shared cone result) so an omnidirectional
	// Censer in one hand still protects even if a directional Candle in the other hand doesn't cover
	// this monster's approach angle.
	bool IsTargetInLight(const class ABaseCharacter* Player) const;

	// Single-item half of IsTargetInLight()/IsLocationProtected(): true if Item is valid, lit, and
	// MonsterLocation is within both its radius and its protection cone.
	bool IsProtectedByHeldItem(const class AHolyItem* Item, const class ABaseCharacter* Player, const FVector& MonsterLocation) const;

	// True if ANY of Player's held items protect Location — used by IsTargetInLight() to test this
	// monster's own current position.
	bool IsLocationProtected(const class ABaseCharacter* Player, const FVector& Location) const;

	// True if a navigable NavMesh point exists near Point AND this monster can actually path to it from
	// its current location; OutNavigablePoint is that point's actual projected location, which callers
	// should move to instead of Point itself. Point is purely geometric (computed from angles/distances
	// around the player) and may sit inside a wall/obstacle or entirely outside the level's NavMesh —
	// and even a point that IS near a valid NavMesh polygon (ProjectPointToNavigation succeeds) can still
	// be unreachable from here (a disconnected NavMesh island, cut off by an obstacle), which is why this
	// also runs a real TestPathSync() query rather than trusting proximity alone.
	bool IsPointNavigable(const FVector& Point, FVector& OutNavigablePoint) const;

	// Timer callback (AttackKillTimerHandle, armed by OnKillPlayer()): actually reloads the level, after
	// DeathReloadDelay seconds have given the player's ragdoll (and this monster's attack animation) time
	// to be seen.
	void ReloadLevelAfterDeath();

	// Enters Chase state.
	void StartChasing();

	// Enters Stalk state and issues the first MoveToLightBorder() call.
	void StartStalking();

	// Ensures State is Stalk (entering it via StartStalking() if not already there) and focuses the
	// controller on Player. Shared by CheckForPlayer()'s two bTargetInLight checks: target retention
	// (already engaged, light keeps it from falling back to Patrol) and the override on a freshly
	// acquired sighting (light stops the very same tick from falling through to the Chase/kill logic).
	void EnterStalkState(class ABaseCharacter* Player);

	// Recalculates the current light-boundary target every rescan — a point BoundaryPatrolStepDegrees
	// further along the boundary ring than the last one (see BoundaryPatrolDirection/CurrentPatrolAngle),
	// NavMesh-validated (see IsPointNavigable()) — but only actually issues a new MoveToLocation() while
	// the path follower is Idle/Waiting/Paused, never while it's still actively Moving toward the
	// previous step. Re-arms FlankRescanTimerHandle for another call FlankRescanIntervalMin-Max seconds
	// later as long as still Stalking. No-ops (and lets CheckForPlayer() re-route next tick) if no longer
	// Stalking or the player is no longer in light.
	void MoveToLightBorder();

	// Resets bIsWaiting and issues a MoveToActor toward PatrolPoints[CurrentPatrolIndex] — or, while
	// wandering (bUseRandomWander, or PatrolPoints left empty — see MoveToRandomWanderPoint()), a
	// MoveToLocation toward a fresh random NavMesh point instead. Does NOT advance CurrentPatrolIndex —
	// call this once per point, not every tick, so the AI controller isn't spammed with requests.
	void MoveToNextPatrolPoint();

	// Wander half of MoveToNextPatrolPoint(): samples one random reachable NavMesh point within
	// WanderRadius of this monster's current location and issues a MoveToLocation toward it. If the
	// NavMesh search itself fails to find a reachable point, retries after 1s (same fallback
	// AEnemyAIController::MoveToNewPatrolPoint() already uses for the identical failure case) rather than
	// leaving this monster stuck with no move ever issued.
	void MoveToRandomWanderPoint();

	// Called when Health drops to 0 or below from holy damage. Disables collision/AI, plays
	// burn effects, and destroys the actor after a short delay. No-ops if already dead.
	void HandleDeath(AActor* DamageCauser);

	// Stops AI movement for StunDuration seconds, then resumes normal logic via ResetStun.
	void ApplyStun();

	// Called when StunTimerHandle expires; clears bIsStunned and resumes patrolling.
	void ResetStun();

	// Bound to DayNightManager->OnDawnStarted in BeginPlay(). UFUNCTION() (unlike CheckSunExposure()/
	// ApplySunDamage() below) because AddDynamic() on a dynamic multicast delegate resolves its target by
	// reflection and requires it. Sets bIsFleeingToShadow, issues the initial FindShadowLocation()+
	// MoveToLocation(), and ALSO arms SunCheckTimerHandle (same as HandleDayStarted() does) so periodic
	// exposure checks/retry-toward-shadow keep running for the entire Dawn grace period — ApplySunDamage()
	// itself is what withholds actual damage until DayNightManager->SunPhase == Day (see its comment), so
	// running checks during Dawn is safe and lets the monster keep actively pathing to real cover instead
	// of freezing at its first (possibly wrong) destination until Day's damage starts. Also broadcasts
	// OnStartFleeingToShadow() — see its own comment for the behavior split between this class and
	// Blueprint.
	UFUNCTION()
	void HandleDawnStarted();

	// Bound to DayNightManager->OnDayStarted. (Re-)arms SunCheckTimerHandle (SunCheckInterval-repeating
	// CheckSunExposure()) with an immediate first check — HandleDawnStarted() above already starts this
	// same timer for the Dawn grace period in the normal case, but this is the ONLY place it starts for a
	// monster that spawns (or is seeded by BeginPlay()) straight into Day with no Dawn transition ever
	// observed, so it re-arms unconditionally rather than assuming it's already running.
	UFUNCTION()
	void HandleDayStarted();

	// Bound to DayNightManager->OnNightStarted. Stops SunCheckTimerHandle, resets SunState to
	// SafeInShadow, and clears bIsFleeingToShadow — "back to Normal/Hunting" per this feature's spec:
	// Patrol/Chase/Stalk simply resume on their own via the next Tick()/CheckForPlayer() cycle once the
	// suppression flag is clear, same as every other bIsStunned-style flag in this class already does.
	UFUNCTION()
	void HandleNightStarted();

	// Timer callback (SunCheckTimerHandle, armed by HandleDayStarted()): line-traces from this monster
	// toward the sun (the DIRECTION opposite DayNightManager->DirectionalLight's forward vector, since a
	// directional light's forward vector is the direction its RAYS travel, sun -> ground) for
	// SunTraceDistance on SunTraceChannel. A blocking hit means shadow (SafeInShadow); reaching full
	// distance unobstructed means SunlitBurning, which calls ApplySunDamage(). Also re-issues
	// FindShadowLocation()+MoveToLocation() while bIsFleeingToShadow if STILL SunlitBurning here — the sun
	// moves across the sky over the course of a Day, so a spot that was shaded at Dawn (or a
	// FindShadowLocation() fallback point that was never actually verified shaded) can stop being safe
	// later; this is what keeps a monster that isn't ACTUALLY in cover yet still trying, rather than
	// standing still and burning at its first (possibly wrong) destination for the rest of the day. No-ops
	// if DayNightManager or its DirectionalLight isn't assigned.
	void CheckSunExposure();

	// Applies SunDamagePerSecond * SunCheckInterval directly to Health (bypassing TakeDamage()/
	// UGameplayStatics::ApplyDamage() entirely — TakeDamage()'s own DamageCauser-directional "flee from
	// attacker" logic doesn't make sense for an ambient environmental source with no single origin point,
	// and there's no external DamageCauser actor to pass it anyway), then calls HandleDeath() if that drops
	// Health to 0 or below — same as every other lethal damage path in this class.
	void ApplySunDamage();

	// Samples up to MaxShadowSearchAttempts random reachable NavMesh points within ShadowSearchRadius of
	// this monster's current location and, for each, line-traces from +100 Z toward the sun (same
	// DirectionalLight-forward-vector convention as CheckSunExposure()) on SunTraceChannel for
	// SunTraceDistance. Returns the first point whose trace hits blocking geometry (i.e. is actually
	// shaded). If none of the sampled points are shaded, returns the CLOSEST one tested instead (a "best
	// effort" destination still worth moving to over standing still) — or FVector::ZeroVector if the
	// NavMesh search itself never produced even one reachable point (checked by callers via
	// FVector::IsZero()). Const: reads DayNightManager/NavSys and runs traces, never mutates this monster's
	// own state.
	FVector FindShadowLocation() const;

	// True if Location is currently shaded from DayNightManager's sun (same +100 Z / DirectionToSun /
	// SunTraceChannel / SunTraceDistance line-trace convention as CheckSunExposure() and
	// FindShadowLocation()'s own per-candidate check, which this factors out for reuse there too). No
	// DayNightManager/DirectionalLight at all returns true (nothing meaningfully restricts movement in
	// that case, so don't block it) rather than false.
	bool IsLocationInShadow(const FVector& Location) const;

	// Shared move-issuing half of the flee-to-shadow path — called both by HandleDawnStarted()'s initial
	// reaction and CheckSunExposure()'s still-burning retry, so boosting MaxWalkSpeed and updating
	// CurrentShadowTarget/the debug readout happens exactly once, in exactly one place, regardless of
	// which caller actually triggers a move. Boosts MaxWalkSpeed to FleeMaxWalkSpeed (guarded by
	// bMaxWalkSpeedBoosted, same as before) if not already boosted, issues
	// AICon->MoveToLocation(TargetLocation, ShadowAcceptanceRadius), and records TargetLocation as
	// CurrentShadowTarget. No-ops if this monster has no AIController.
	void IssueShadowFleeMove(const FVector& TargetLocation);

	// Restores GetCharacterMovement()->MaxWalkSpeed to PreFleeMaxWalkSpeed and clears bMaxWalkSpeedBoosted
	// — a no-op if bMaxWalkSpeedBoosted is already false (HandleNightStarted() and CheckSunExposure()'s
	// "reached cover" path can both reach this for the same flee episode; only the first should actually
	// restore anything).
	void RestoreNormalMaxWalkSpeed();

	// Called when StaggerTimerHandle expires; clears bIsStaggered so ApplyKnockback()'s AI-suppression
	// window ends and Tick()/CheckForPlayer() resume normal decision-making.
	void ResetStagger();

	// Bound to GetCapsuleComponent()->OnComponentHit in BeginPlay() — the entry point for BOTH physical-
	// contact reactions this monster has: Blind Encounter (OtherActor's eyes closed — see AlertRadius's
	// own header comment for the full picture, logic UNCHANGED by the eyes-open split below) and Naskok
	// (OtherActor's eyes open — see TryStartNaskokAttack()). No-ops unless OtherActor is a valid, living
	// ABaseCharacter, or if this monster is already dead/mid-Blind-Encounter (bHasActiveBlindEncounter).
	// Moonshine Rage Dash i-frames (ABaseCharacter::IsImmuneToMonsterCatch()) are checked ONCE here,
	// shared by both branches below — a Ram is never either kind of encounter, from any angle of impact
	// (UMoonshineComponent::OnOwnerCapsuleHit(), bound to the PLAYER's own capsule, already handles it via
	// knockback+stun).
	UFUNCTION()
	void OnCapsuleHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse, const FHitResult& Hit);

	// Naskok — the eyes-OPEN counterpart to the Blind Encounter branch of OnCapsuleHit(), called from
	// there instead when OtherActor's eyes are open. Two outcomes:
	//   - This monster is ALREADY bIsScanning for THIS SAME Player (a repeat Ram while still riled up
	//     from an earlier Naskok miss) -> immediate hit, NO dodge chance: ChaseAttackDamage,
	//     Player->EnterPanicState(), BroadcastAlert(), all synchronous.
	//   - Otherwise (first contact) -> the usual "swing with a chance to dodge", resolved later at the
	//     animation's real point of contact via OnAttackHitNotify()/DoesEyePeekAttackConnect() — same
	//     infrastructure Eye Peek Risk and the background open-eyes Chase-attack (TryStartChaseAttack())
	//     both already use. Deliberately does NOT call BroadcastAlert() here the way TryStartChaseAttack()
	//     does on swing start — a Naskok only calls the pack in on a CONFIRMED hit (see
	//     OnAttackHitNotify()'s own PendingNaskokAttackTarget branch), never on a mere attempt. No-ops if
	//     a swing from this rule is already in flight (PendingNaskokAttackTarget.IsValid()) — see that
	//     field's own comment for why OnCapsuleHit() needs this guard that CheckForPlayer()'s other two
	//     attack sources get from bIsScanning/PendingChaseAttackTarget instead.
	void TryStartNaskokAttack(class ABaseCharacter* Player);

	// "Piling on" — mirrors TryStartChaseAttack(): stops navigating, sets PendingPanicStrikeTarget,
	// PlayAttackAnimation(), arms PanicStrikeHitFallbackTimerHandle (reusing ChaseAttackHitFallbackDelay,
	// same as TryStartNaskokAttack() already does). Deliberately does NOT BroadcastAlert() — same
	// reasoning as TryStartNaskokAttack()'s own swing-start: don't cascade a fresh alert from every
	// monster that piles on. Called from CheckForPlayer()'s own standalone panic-strike check (see that
	// check's own comment for why it's deliberately NOT inside the bHasDetectedPlayer block) once a
	// monster already investigating this alert (bIsInvestigatingAlert) or already Chasing this player
	// (State == Chase) reaches an already-panicking player at CatchDistance. Resolution (hit vs. the
	// player having closed their eyes AND dodged/moved out of range in time) happens later, at the
	// animation's real point of contact, in OnAttackHitNotify() — see its own PendingPanicStrikeTarget
	// branch.
	void TryStartPanicStrike(class ABaseCharacter* Player);

	// Timer callback (PanicStrikeRearmTimerHandle, armed by OnAttackHitNotify()'s own PendingPanicStrikeTarget
	// HIT branch via a bound FTimerDelegate — see PanicStrikeRearmDelay's own comment for why the re-swing
	// needed delaying in the first place) — re-checks WeakPlayer is still valid and alive (they could have
	// died or been destroyed during the delay) before calling TryStartPanicStrike() again. A thin wrapper
	// rather than teaching TryStartPanicStrike() itself about weak pointers/staleness, since every OTHER
	// caller of TryStartPanicStrike() already has a live, freshly-validated ABaseCharacter* in hand.
	void TryStartPanicStrikeIfStillEligible(TWeakObjectPtr<class ABaseCharacter> WeakPlayer);

	// Timer callback (BlindReactionTimerHandle, armed by OnCapsuleHit()) — samples WeakPlayer's CURRENT
	// velocity BlindTouchReactionDelay seconds after contact to decide Scenario A (stopped:
	// EnterScanningState()) vs Scenario B (still pushing: ABaseCharacter::EnterPanicState()). No-ops
	// (and clears bHasActiveBlindEncounter) if the player went invalid, died, or opened their eyes in the
	// meantime — that last case already resolved via OpenEyes()'s own instant-kill check, so there's
	// nothing left here to react to.
	void ResolveBlindEncounterReaction(TWeakObjectPtr<class ABaseCharacter> WeakPlayer);

	// Scenario A: stops this monster's own movement, focuses (AICon->SetFocus()) on Player so it visibly
	// turns toward them, sets Player->BlindEncounterMonster, and arms ScanTimerHandle for OverrideScanDuration
	// if positive, otherwise the ordinary ScanDuration — see EndScanningState() for what happens once that
	// elapses. OverrideScanDuration lets a specific dodge branch (currently only the "piling on" swing's
	// own PanicStrikeScanDuration, in OnAttackHitNotify()'s PendingPanicStrikeTarget branch) search longer
	// than every other dodge without a new dedicated function.
	void EnterScanningState(class ABaseCharacter* Player, float OverrideScanDuration = -1.0f);

	// Timer callback (ScanTimerHandle) — reaching this means eyes stayed closed for the entire ScanDuration
	// (an eyes-open event during scanning resolves via ResolveBlindEncounterKill() instead, which clears
	// bIsScanning itself and never lets this timer matter). Clears bIsScanning/focus and CurrentScanTarget's
	// own BlindEncounterMonster, and bHasActiveBlindEncounter, so the monster "loses interest" and both
	// sides are free for a fresh encounter later.
	void EndScanningState();

	// Timer callback (ProbeStartleTimerHandle) — clears the AICon->SetFocus() ReactToProbe() set, so the
	// monster stops facing the player and resumes whatever it was doing (Patrol/Chase/Stalk untouched the
	// whole time — ReactToProbe() never suppressed them).
	void EndProbeStartle();

public:
	// Finds every other living ABaseMonster within AlertRadius of AlertLocation and calls ReceiveAlert() on
	// each — see ReceiveAlert()'s own comment for what they do with it. Public (unlike the rest of this
	// blind-encounter block) so ABaseCharacter::RegisterMouthGasp() can reuse it directly — a mouth gasp
	// is loud enough that it alerts nearby monsters exactly like the original blind touch did, not just
	// whichever single monster is close enough for a proximity kill check.
	//
	// "Pack Formation on Arrival": recipients are sorted by their current bearing around AlertLocation
	// before being notified, then each gets ReceiveAlert(AlertLocation, SlotIndex, Recipients.Num()) —
	// an evenly-spaced angular slot around the alert point, roughly matching the direction it's already
	// approaching from (fewer crossed paths converging). Slot numbering is local to THIS broadcast call
	// only — a later broadcast during the same encounter (e.g. a follow-up mouth gasp) numbers its own
	// new recruits independently, without reserving against monsters already in position from an earlier
	// call; ordinary capsule collision between the monsters themselves is what keeps two independently-
	// numbered arrivals from literally standing on top of each other.
	//
	// Debug logging (UE_LOG + a 5s AddOnScreenDebugMessage per line, so a pack call is visibly traceable
	// without opening the log): "Total monsters on map", "Monsters in radius" (of AlertRadius), then one
	// line per in-radius monster with its name, distance in meters, and either "ACCEPTED" or "REJECTED
	// (reason)" — see ReceiveAlert()'s OutReason for what reasons can show up there.
	void BroadcastAlert(const FVector& AlertLocation);

	// SINGLE CHOKE POINT for every AIController::MoveToActor() request this class issues — every call
	// site in this file that used to call AICon->MoveToActor() directly now routes through this instead.
	// Refuses outright (returns Failed, never touches AICon at all) while bIsBeingRidden is true:
	// UMountRiderComponent owns 100% of this monster's movement while mounted (the rider's own input
	// drives it via ABaseCharacter::AddMovementInput()'s passthrough — see its own comment), and any AI
	// nav-move request issued underneath that is exactly the "two sources driving movement at once"
	// conflict that used to read as spinning/twitching. This makes that conflict structurally
	// IMPOSSIBLE to trigger — a future call site added anywhere in this class automatically inherits the
	// same guard just by calling through here, no separate bIsBeingRidden check to remember — rather than
	// merely suppressing its symptoms every Tick() the way the bIsBeingRidden branch at the top of Tick()
	// used to (that branch's own StopMovement()/ClearFocus() calls are a reactive patch for a request
	// that already got issued; this is the preventive fix upstream of it). Same parameter list/defaults
	// as AAIController::MoveToActor() itself, so every existing call site only needed
	// "AICon->MoveToActor(" replaced with "RequestMoveToActor(AICon, ".
	//
	// Public (like BroadcastAlert() above) so MountedMonsterMoveConflictTest.cpp can call it directly —
	// simulating "some call site forcibly issues a move request while ridden" without needing a NavMesh
	// or a fully ticking world to observe the result: the ridden-check runs before AICon is ever touched,
	// so the return value alone (Failed vs. whatever AICon->MoveToActor() itself would have returned)
	// proves whether the request reached the AIController at all.
	EPathFollowingRequestResult::Type RequestMoveToActor(AAIController* AICon, AActor* Goal, float AcceptanceRadius = -1, bool bStopOnOverlap = true,
		bool bUsePathfinding = true, bool bCanStrafe = true, TSubclassOf<UNavigationQueryFilter> FilterClass = nullptr, bool bAllowPartialPath = true) const;

	// Same single choke point as RequestMoveToActor() above, for this class's AIController::
	// MoveToLocation() requests — see that function's own comment for the full reasoning.
	EPathFollowingRequestResult::Type RequestMoveToLocation(AAIController* AICon, const FVector& Dest, float AcceptanceRadius = -1, bool bStopOnOverlap = true,
		bool bUsePathfinding = true, bool bProjectDestinationToNavigation = false, bool bCanStrafe = true,
		TSubclassOf<UNavigationQueryFilter> FilterClass = nullptr, bool bAllowPartialPath = true) const;

private:

	// Diverts a Patrol-state monster to investigate AlertLocation (see InvestigateDuration's own header
	// comment) — no-ops if this monster is doing anything more urgent already (stunned/staggered/ridden/
	// ACTIVELY fleeing to shadow (not merely having fled earlier — see bIsIdleInShadow's own comment)/
	// scanning/already investigating/already engaged with the player). SlotIndex/SlotCount (see
	// BroadcastAlert()'s own comment) place this monster at its own evenly-spaced point on the
	// InvestigateAcceptanceRadius ring around AlertLocation instead of pathing to AlertLocation itself.
	// Returns true if the alert was accepted (this monster is now investigating); false otherwise, with
	// OutReason set to a short human-readable cause (e.g. "Already chasing", "No NavMesh path") for
	// BroadcastAlert()'s debug readout.
	bool ReceiveAlert(const FVector& AlertLocation, int32 SlotIndex, int32 SlotCount, FString& OutReason);

	// Timer callback (InvestigateArrivalCheckTimerHandle, armed by ReceiveAlert() as a REPEATING timer at
	// InvestigateArrivalPollInterval) — polls whether this monster has actually reached
	// CurrentInvestigateRingTarget yet (within PackFormationTolerance). On arrival: stops the poll and
	// starts the ordinary "hold position, then give up" countdown (InvestigateTimerHandle, InvestigateDuration).
	// Still not there once InvestigateDuration itself has elapsed since InvestigateStartTime (an unreachable
	// ring point, a blocked path, etc.): gives up outright via EndInvestigateAlert() rather than polling
	// forever — see ReceiveAlert()'s own comment for why the travel time itself needed decoupling from the
	// hold-then-timeout countdown in the first place.
	void CheckInvestigateArrival();

	// Timer callback (InvestigateTimerHandle) — clears bIsInvestigatingAlert; normal Patrol/Chase/Stalk
	// decision-making resumes on its own on the next Tick()/CheckForPlayer() cycle, no explicit "resume
	// patrol" call needed here.
	void EndInvestigateAlert();

	// The ring point ReceiveAlert() last issued a MoveToLocation() for — CheckInvestigateArrival() measures
	// distance to this every poll to decide whether this monster has actually arrived.
	FVector CurrentInvestigateRingTarget = FVector::ZeroVector;

	// GetWorld()->GetTimeSeconds() as of the CURRENT ReceiveAlert() acceptance — CheckInvestigateArrival()
	// uses this to cap the TRAVEL attempt at InvestigateDuration, independent of the separate
	// InvestigateDuration countdown that starts fresh only once arrival is actually confirmed.
	float InvestigateStartTime = -1000.0f;

	EMonsterState State = EMonsterState::Patrol;
	int32 CurrentPatrolIndex = 0;

	// True while paused at a patrol point, waiting out PatrolWaitTime before moving to the next one.
	bool bIsWaiting = false;

	// Persistent sweep direction (+1/-1) for MoveToLightBorder()'s boundary-patrol stepping. Chosen once
	// per Stalk episode (StartStalking()) and NEVER inverted afterward for any reason — not on a failed
	// move, not on a rescan, not on anything short of a fresh StartStalking() call. Flipping this
	// mid-episode is what caused side-to-side jittering in an earlier version of this logic: a small
	// bearing shift between rescans could flip which side looked closer, snapping the target to the
	// opposite side of the player every cycle.
	float BoundaryPatrolDirection = 1.0f;

	// Persistent, ever-accumulating angle (degrees, offset from Player->GetActorForwardVector()) for
	// MoveToLightBorder()'s boundary-patrol stepping. Deliberately NOT derived from this monster's own
	// current position each cycle — anchoring to the player's forward vector (always available) and
	// unconditionally advancing this every rescan guarantees a genuinely new target each time even if
	// the previous MoveToLocation() stalled and this monster's actual position never changed; re-deriving
	// the bearing from a possibly-stuck position would otherwise keep recomputing the same stuck target.
	float CurrentPatrolAngle = 0.0f;

	// The destination MoveToLightBorder() last actually issued a MoveToLocation() for (as opposed to
	// merely calculated). Only used to seed StartStalking()'s initial tangent step comparison-free —
	// the actual "should I issue a new move" decision is purely the path follower's status (Idle/
	// Waiting/Paused vs. Moving), not how far the candidate is from this.
	FVector CurrentTargetLocation = FVector::ZeroVector;

	// Consecutive MoveToLocation() failures in MoveToLightBorder(), reset to 0 on any RequestSuccessful.
	// Purely diagnostic bookkeeping now — each Failed already triggers an immediate pathfinding-free
	// sidestep (see MoveToLightBorder()'s comment), so nothing currently branches on the count itself.
	int32 ConsecutiveMoveFailures = 0;

	// If GetWorld()->GetTimeSeconds() is before this, TakeDamage() suppresses a fresh Candle stun (see
	// ShieldBounceCooldown's comment). Set in ResetStun() when a stun ends.
	float ShieldBounceCooldownUntil = -1.0f;

	// True once HandleDeath has run, so it only runs once.
	bool bIsDead = false;

	// True while stunned by Candle holy damage; suppresses AI movement and fleeing.
	bool bIsStunned = false;

	// True from OnCapsuleHit() until the blind encounter it started fully resolves (EndScanningState() or
	// OnBlindEncounterResolved()/ResolveBlindEncounterKill()) — guards against re-triggering a fresh
	// encounter (or a duplicate alert broadcast) while continued capsule contact keeps generating more
	// OnComponentHit events during the same encounter.
	bool bHasActiveBlindEncounter = false;

	// The player this monster is currently Scenario-A scanning (see EnterScanningState()) — needed by
	// EndScanningState() to clear that SAME player's own BlindEncounterMonster reference, not just this
	// monster's own bIsScanning flag.
	TWeakObjectPtr<class ABaseCharacter> CurrentScanTarget;

	// "Piling on" gate — allows at most ONE ATTEMPT per panic episode at TryStartPanicStrike() from THIS
	// monster: set true only on a genuine DODGE (Player->IsEyesClosed() AND !DoesEyePeekAttackConnect(),
	// resolved in OnAttackHitNotify()'s own PendingPanicStrikeTarget branch) — a CONFIRMED HIT does NOT set
	// this, since per the design a hit re-arms TryStartPanicStrike() again immediately instead of retiring
	// this monster from the episode. Reset by comparing Player->PanicEpisodeId against
	// LastSeenPanicEpisodeId below (see CheckForPlayer()'s own standalone panic-strike check) rather than
	// polling !Player->bIsPanicking every tick — a monster that's still mid-Investigate/Chase when a brand
	// NEW episode starts (the player got hit again after recovering) needs a fresh attempt immediately, not
	// only once the old episode's bIsPanicking happens to clear first.
	bool bHasMissedPanicStrikeThisEpisode = false;

	// Player->PanicEpisodeId as of the last time this monster's standalone panic-strike check ran — the
	// moment these two disagree, a NEW episode has started since this monster last looked, and
	// bHasMissedPanicStrikeThisEpisode resets for it. -1 (never matches a real episode, which starts at 1)
	// so the very first episode this monster ever sees is always treated as new.
	int32 LastSeenPanicEpisodeId = -1;

	// GetWorld()->GetTimeSeconds() this monster becomes eligible to join the standalone "piling on" check
	// in CheckForPlayer() as the INITIATOR of the current panic episode — set by ArmSelfPilingOnCooldown()
	// (called from ABaseCharacter::EnterPanicState() on the monster that just caused the first hit) to
	// GetWorld()->GetTimeSeconds() + PanicStrikeRearmDelay, the same pause every other "piling on" monster
	// already waits between its own repeat swings. Defaults deep in the past so a monster that's never been
	// an initiator (the overwhelmingly common case — every OTHER monster reaching the standalone check) is
	// always immediately eligible, exactly as before this field existed.
	float PanicStrikeEligibleTime = -1000.0f;

	// GetCharacterMovement()->MaxWalkSpeed as of just before HandleDawnStarted() last boosted it to
	// FleeMaxWalkSpeed — what RestoreNormalMaxWalkSpeed() restores back to.
	float PreFleeMaxWalkSpeed = 0.0f;

	// True from the moment HandleDawnStarted() actually applies FleeMaxWalkSpeed until
	// RestoreNormalMaxWalkSpeed() restores it — guards against re-storing an already-boosted speed as
	// "original" (e.g. if CheckSunExposure()'s retry-while-still-burning path ran again before this
	// monster ever reached cover) and against RestoreNormalMaxWalkSpeed() restoring twice for the same
	// episode (HandleNightStarted() and the "reached cover" path in CheckSunExposure() can both reach it).
	bool bMaxWalkSpeedBoosted = false;

	// The shadow point the last actually-issued MoveToLocation() (HandleDawnStarted()'s initial move, or
	// CheckSunExposure()'s retry-while-still-burning move) was sent to for the CURRENT flee episode.
	// ZeroVector whenever no flee move has been issued yet this episode (reset at the top of
	// HandleDawnStarted()). CheckSunExposure()'s retry logic compares each freshly-sampled candidate
	// against this before ever calling MoveToLocation() again — see its own comment on why that guard
	// exists (path flapping).
	FVector CurrentShadowTarget = FVector::ZeroVector;

	// Armed by OnKillPlayer() — delays the actual level reload (ReloadLevelAfterDeath()) by
	// DeathReloadDelay seconds so the player's ragdoll (ABaseCharacter::HandleDeath(), called immediately)
	// and this monster's attack animation both have time to be seen instead of the level cutting away the
	// same instant the kill triggers.
	FTimerHandle AttackKillTimerHandle;

	// True once OnKillPlayer() has run for this monster. CheckForPlayer() re-polls every ~0.1s and a
	// ragdolled Player's GetActorLocation() (the capsule, frozen where they died — not the physically
	// falling mesh) never moves back outside CatchDistance, so without this guard the very next poll
	// would re-enter the catch branch and call OnKillPlayer() again, endlessly re-arming
	// AttackKillTimerHandle with a fresh DeathReloadDelay and never actually letting it fire.
	bool bHasCaughtPlayer = false;

	FTimerHandle SightCheckTimerHandle;
	FTimerHandle WaitTimerHandle;
	FTimerHandle FlankRescanTimerHandle;
	FTimerHandle DestroyTimerHandle;
	FTimerHandle StunTimerHandle;
	FTimerHandle StaggerTimerHandle;
	FTimerHandle SunCheckTimerHandle;
	FTimerHandle BlindReactionTimerHandle;
	FTimerHandle ScanTimerHandle;
	FTimerHandle ProbeStartleTimerHandle;

	UPROPERTY()
	TWeakObjectPtr<class ABaseCharacter> ProbeStartleTarget;
	FTimerHandle InvestigateTimerHandle;
	FTimerHandle InvestigateArrivalCheckTimerHandle;
	FTimerHandle EyePeekReactionTimerHandle;

	static constexpr float PatrolAcceptanceRadius = 120.0f;

	// Generous on purpose: a tight acceptance radius this close to the NavMesh's high-cost
	// UNavArea_Obstacle ring (see AHolyItem::NavAvoidanceRadius) can make pathing stall trying to
	// resolve an exact point right at/near that edge instead of just accepting "close enough".
	static constexpr float StalkAcceptanceRadius = 80.0f;

	// Kill/attack range: catching the player within this distance (while not light-protected) ends the
	// level. Must stay strictly smaller than any held item's AHolyItem::HolyAuraRadius (220 by default)
	// — otherwise there's a gap where a monster is outside the light's protection but still within kill
	// range at once, letting it reach the player despite them holding a lit item.
	static constexpr float CatchDistance = 150.0f;

	static constexpr float DeathDestroyDelay = 1.5f;
};
