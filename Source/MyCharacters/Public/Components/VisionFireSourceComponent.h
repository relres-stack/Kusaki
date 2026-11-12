// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SWidget.h"
#include "VisionFireSourceComponent.generated.h"

class AVisionFireSource;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPostProcessComponent;
class UTexture2D;
class APlayerController;

// "Воспалённое зрение" (Inflamed Vision): while active, a AVisionFireSource actor (light + fire VFX — see
// its own header comment) sits wherever APriestCharacter's camera is actually pointing — up to
// MaxFireDistance away — hugging whatever surface it lands on instead of clipping through it. This is a
// deliberate "flashlight" design: the fire stays visually fixed on screen (always dead ahead of the
// camera) while its WORLD position slides along the view ray as the player looks around — down at their
// own feet, or far ahead at a distant point — capped at MaxFireDistance so it can never be aimed clear
// across the level. Priest-only UActorComponent — APriestCharacter's own precedent
// (UFingerFlamePeekComponent), not universal like UProbeComponent/UInventoryComponent. Spawns and owns its
// own AVisionFireSource instance in BeginPlay() rather than requiring one pre-placed per level (same
// "working default, no designer setup required" reasoning as AHolyItem being spawned by
// APriestCharacter::StartingCandleClass). Positioning logic only — the actual light/VFX toggle lives on
// AVisionFireSource::Activate()/Deactivate() itself; this component just decides WHEN and WHERE.
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYCHARACTERS_API UVisionFireSourceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVisionFireSourceComponent();

	// Class spawned in BeginPlay() to produce FireSourceActor — override on a BP subclass only if a
	// different AVisionFireSource variant is ever needed. Defaults to AVisionFireSource itself in the
	// constructor so this works with no editor setup at all.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Fire Source")
	TSubclassOf<AVisionFireSource> FireSourceClass;

	// The actual actor, spawned once in BeginPlay() — see FireSourceClass above.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vision Fire Source")
	TObjectPtr<AVisionFireSource> FireSourceActor;

	// MAXIMUM distance (world units) the fire can be from the camera, measured straight along the camera's
	// own view direction (pitch included — see UpdateFirePlacement()'s own comment). This is a hard cap, not
	// a fixed placement distance: looking at something closer (your own feet, a nearby wall) places the
	// fire there instead; looking at open space with nothing within range places it exactly MaxFireDistance
	// out along the view ray, same as a flashlight beam that just fades out past its reach. 800 reads as "a
	// stone's throw", not "across the room" — tune freely, this is the one knob that decides how far the
	// player can reach with this ability.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Fire Source")
	float FireDistance = 800.0f;

	// Pulled back this far (world units, back along the view direction) from whatever the trace hits — a
	// wall, the floor, a ceiling — so the fire never renders inside geometry. Works the same regardless of
	// how steep the view angle is, since it pulls back along the actual 3D hit direction, not just
	// horizontally. No lower clamp on the resulting distance — see UpdateFirePlacement()'s own comment.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Fire Source")
	float FireSafetyBuffer = 30.0f;

	// ---- Screen look of the ability ("Воспалённое зрение") ----
	// Rebuilt from scratch (the old M_InflamedVisionReal post-process material is no longer used by this
	// component). Target look: a dark red-brown, almost monochrome picture; everything soft and blurred;
	// bright things (the flame) glow; a smoky dark frame around the edges. The picture itself is a plain
	// UPostProcessComponent driven from code (same approach as ABaseCharacter's eyes-closed look); the smoky
	// frame is a procedurally generated texture shown as a full-screen Slate overlay. Everything fades in/out
	// smoothly. All of these can be tuned in the Priest Blueprint, category "Vision Look" — most of them take
	// effect live in PIE, the frame_* ones only on the next start of PIE (the frame texture is built in BeginPlay()).

	// How fast the look fades in/out (bigger = faster).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look")
	float VisionLookBlendSpeed = 3.0f;

	// Colour multiplier of the whole picture — the main "red-orange" tint. Lower G/B = redder.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look")
	FLinearColor VisionColorGain = FLinearColor(1.0f, 1.0f, 1.0f);

	// Remaining colour (0 = fully monochrome, 1 = original colours).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VisionSaturation = 1.0f;

	// Colour the blacks are lifted to — makes shadows dark BROWN-RED instead of pure black (as on the reference).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look")
	FLinearColor VisionShadowLift = FLinearColor(0.0f, 0.0f, 0.0f);

	// Exposure in stops. Negative = darker, moodier. Lights still glow.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look")
	float VisionExposureBias = 0.0f;

	// Glow around bright things (the flame). 0.675 is the engine default.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look")
	float VisionBloomIntensity = 2.2f;

	// ---- Blur: NEAR things stay fairly sharp, FAR things dissolve ----
	// Two layers: a real depth-of-field (sharp at VisionFocalDistance, blurrier the further from it) plus a
	// distance blur that grows with depth. To make far things blurrier: raise VisionBlurRadius or lower
	// VisionBlurFstop. To make near things sharper: raise VisionDepthBlurDistanceKm (the blur starts later).

	// Distance (cm) that is perfectly sharp.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look")
	float VisionFocalDistance = 150.0f;

	// Aperture of the depth-of-field. LOWER = blurrier outside the focus.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look")
	float VisionBlurFstop = 1.4f;

	// Maximum extra distance blur, in pixels (at 1920 wide) — what the far background gets.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look")
	float VisionBlurRadius = 18.0f;

	// Distance (in KILOMETRES: 0.008 = 8 m) at which the distance blur reaches half of VisionBlurRadius.
	// Bigger = near and middle things stay sharp for longer.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look")
	float VisionDepthBlurDistanceKm = 0.008f;

	// Dark edges from the engine's own vignette (the smoky frame below is on top of it). 0 = none.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look")
	float VisionVignetteIntensity = 0.5f;

	// Film grain (0 = none).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look")
	float VisionGrainIntensity = 0.5f;

	// ---- Smoky frame ----
	// Overall opacity of the frame (0 = off, 1 = fully dark at the edges).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Frame", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FrameOpacity = 1.0f;

	// How much the frame's brightness "breathes" over time (0 = static).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Frame", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float FrameBreathing = 0.05f;

	// The smoke at the edges ROLLS like fog: every layer slowly turns and breathes (zooms in and out) in its own
	// rhythm, on top of the cross-fade between shapes below. Swirl = how far it turns, in degrees (0 = no turning);
	// speed = how fast; overscan = how much bigger than the screen the haze is drawn (must stay above about
	// 1.15 + 0.03 per degree of swirl, otherwise gaps appear at the screen corners).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Frame", meta = (ClampMin = "0.0", ClampMax = "15.0"))
	float FrameSwirlAmount = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Frame", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float FrameSwirlSpeed = 0.65f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Frame", meta = (ClampMin = "1.0", ClampMax = "2.0"))
	float FrameOverscan = 1.25f;

	// How fast the smoke at the edges changes shape (full shape changes per second). 0 = frozen.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Frame", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float FrameMorphSpeed = 0.22f;

	// Distance from the screen centre (0 = centre, 1 = middle of the screen edge) where the haze starts. [needs PIE restart]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Frame", meta = (ClampMin = "0.0", ClampMax = "1.5"))
	float FrameInnerRadius = 0.48f;

	// Distance where the haze is fully dark. [needs PIE restart]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Frame", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float FrameOuterRadius = 0.95f;

	// How ragged/smoky the inner edge is (0 = clean oval). [needs PIE restart]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Frame", meta = (ClampMin = "0.0", ClampMax = "1.5"))
	float FrameNoiseAmount = 0.55f;

	// Colour of the haze right at its inner edge (dark red) and at the very corners (almost black). [needs PIE restart]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Frame")
	FLinearColor FrameInnerColor = FLinearColor(0.10f, 0.015f, 0.005f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Frame")
	FLinearColor FrameOuterColor = FLinearColor(0.003f, 0.001f, 0.0f);

	// ---- Blood vessels (veins) under the frame ----
	// A thin dark-red network of branching vessels growing from the screen edges and corners inwards. It is drawn
	// UNDER the smoky frame, so the dark part of the frame swallows the outer ends of the veins. The picture is
	// generated once at the start of the game (no asset needed). Properties marked [needs PIE restart] change the
	// generated picture; the others work live.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Veins")
	bool bShowVeins = true;

	// Overall opacity of the veins (0 = invisible, 1 = fully visible).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Veins", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VeinsOpacity = 1.0f;

	// Colour of the veins (very dark red-brown, close to the dark corners of the frame). Applied as a screen tint, so it can be tuned live in PIE.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Veins")
	FLinearColor VeinsColor = FLinearColor(0.030f, 0.007f, 0.005f);

	// How many main vessels start from the screen corners (shared evenly between the four corners). [needs PIE restart]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Veins", meta = (ClampMin = "0", ClampMax = "60"))
	int32 VeinsRootCount = 8;

	// How far the veins grow inwards, as a fraction of half the screen width. [needs PIE restart]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Veins", meta = (ClampMin = "0.05", ClampMax = "1.0"))
	float VeinsReach = 1.0f;

	// Veins are visible only in a ring of the screen: they fade in from this distance from the screen centre
	// (0 = centre, 1 = middle of the screen edge) and fade out again towards the outer one. Use them to place
	// the veins where the haze of the frame is thin enough to show them. [needs PIE restart]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Veins", meta = (ClampMin = "0.0", ClampMax = "1.5"))
	float VeinsInnerRadius = 0.30f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Veins", meta = (ClampMin = "0.2", ClampMax = "2.0"))
	float VeinsOuterRadius = 1.0f;

	// True = veins are drawn ON TOP of the dark frame (visible even where the frame is nearly black, as in the
	// reference). False = under the frame (its dark part swallows them).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Veins")
	bool bVeinsAboveFrame = true;

	// Thickness multiplier (1 = thin capillaries). [needs PIE restart]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Veins", meta = (ClampMin = "0.3", ClampMax = "4.0"))
	float VeinsThickness = 1.25f;

	// How much a vessel wanders off its straight course (0 = ruler-straight, 1 = gently wavy, 3+ = tangled).
	// [needs PIE restart]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Veins", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float VeinsWiggle = 1.3f;

	// How often a vessel splits into thinner ones (1 = normal, 0 = no splitting). [needs PIE restart]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Veins", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float VeinsBranching = 0.6f;

	// Different number = different pattern of veins. [needs PIE restart]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Veins")
	int32 VeinsSeed = 7;

	// Slow pulse of the veins' brightness: pulses per second and how deep it goes (0 = steady).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Veins", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float VeinsPulseSpeed = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Veins", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VeinsPulseDepth = 0.35f;

	// With every pulse the veins swell a little (thicker at the peak of the pulse): how much thicker, as a fraction
	// of their thickness (0.3 = up to 30% thicker, 0 = no swelling). [needs PIE restart]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Veins", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float VeinsPulseSwell = 0.25f;

	// Light trembling of the whole network (it stays in place, it just quivers): strength (roughly how many
	// degrees it shakes; 0.1 is a faint quiver, 0 = perfectly still) and speed (1 = normal, 2 = twice as fast).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Veins", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float VeinsTrembleAmount = 0.12f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Veins", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float VeinsTrembleSpeed = 1.0f;

	// ---- Fire strength ----
	// The fire (AVisionFireSource) can feel too strong and blow out walls/floor. These two scale it down while
	// the vision is on (they are applied each time the vision starts). 1 = unchanged.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fire", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float FireLightIntensityScale = 0.4f;

	// Size of the flame itself (the Niagara fire).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fire", meta = (ClampMin = "0.01", ClampMax = "2.0"))
	float FireVisualScale = 0.55f;

	// ---- Fire attached to the fingers ----
	// The fire (and its light) is placed along the view ray that passes through the fingertips on screen, instead
	// of straight ahead of the camera. On screen it therefore always sits exactly on the pinched fingers, as if
	// the hand held it; in the world it still lands on whatever that ray hits (or at FireDistance), so the lit
	// scene stays the same. If the flame is not quite on the fingertips, adjust FireScreenOffset (a fraction of
	// the screen: Y positive = lower) or FingersPinchPoint.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fire")
	bool bAttachFireToFingers = true;

	// Where on the fingers picture the pinched tips are (0,0 = its top-left corner, 1,1 = bottom-right).
	// Right for the supplied hand picture; change only if you swap the picture.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fire")
	FVector2D FingersPinchPoint = FVector2D(0.23, 0.045);

	// Fine adjustment of where the fire sits relative to the fingertips, in fractions of the screen.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fire")
	FVector2D FireScreenOffset = FVector2D(0.0, 0.02);

	// The fire may be hundreds of cm away (it sits where the ray lands), which would make it tiny next to the hand.
	// With this on, the flame is scaled up with distance so it always looks about the same size on screen:
	// its normal size is shown at FireReferenceDistance.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fire")
	bool bKeepFlameApparentSize = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fire", meta = (ClampMin = "50.0"))
	float FireReferenceDistance = 300.0f;

	// ---- The vision as ONE post-process material ----
	// Fog that drifts and thickens with distance, and the flat dark world (walls, objects and monsters alike: black
	// near, dark brown far) are drawn by one post-process material. If nothing is assigned, the asset
	// /Game/M_InflamedVision is used automatically when it exists. Its look is tuned in the material's own parameters.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Material")
	TObjectPtr<UMaterialInterface> VisionMaterial;

	// ---- Fingers with the flame (a flat picture drawn on top of the screen, bottom centre) ----
	// The "hand with a candle flame" from the reference: dark fingers, red-orange rim light, a small glowing
	// flame above them. It is a 2D screen overlay (the ability always uses the first-person camera, so the
	// fingers always sit in the same place on screen). The REAL fire/light still sits wherever the player
	// looks (AVisionFireSource) — this is only the picture of the hand.
	// Until you assign FingersTexture, a generated placeholder is used so the layout can be tuned right away.
	// Positions are fractions of the screen (0,0 = top-left, 1,1 = bottom-right) and apply live in PIE.

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers")
	bool bShowFingers = true;

	// 3D hand: instead of the flat picture, the Priest's own mannequin arm is shown. While the vision is on,
	// APriestCharacter::bVisionArmRaised is true (the AnimBP raises the right arm with it), the body is not
	// hidden from the head camera (only the head bone is), the flat fingers picture is not drawn and the fire is
	// aimed through the fingertip (APriestCharacter::VisionFingertipSocket). OFF by default: turn it on only
	// after the AnimBP raises the arm, otherwise there is no hand on screen at all. Takes effect on the next V.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers")
	bool bUse3DHand = false;

	// With the 3D hand: put the fire (flame + its light) right ON the fingertip (APriestCharacter::VisionFingertipSocket
	// of the arms mesh) instead of out along the view ray in the world. The flame then sits on the fingers and
	// lights the hand; its light only reaches as far as its own radius. OFF = the old "flashlight along the ray" aim.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers")
	bool bFireOnFingertip = true;

	// Where exactly the flame sits relative to the fingertip, in the camera's axes (cm): X away from the eye,
	// Y right, Z up. A small lift (Z) puts the flame just above the pinch. Applies live in PIE.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers")
	FVector FingertipFireOffset = FVector(0.0f, 0.0f, 0.0f);

	// Size of the flame while it sits on the fingertip. The fire's Niagara system (NS_Fire) does not follow the component's
	// scale, so this is written to its own user parameter FlameScaleParameter ("Flame Scale", multiplies the sprite size)
	// every frame. Bigger = bigger flame. Applies live in PIE.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers", meta = (ClampMin = "0.001", ClampMax = "5.0"))
	float FingertipFireScale = 0.2f;

	// Height of the flame on the fingertip: written to the user parameter LifetimeScaleParameter ("Lifetime Scale"):
	// particles live shorter, so the flame rises less. Lower = a lower, calmer flame. Applies live in PIE.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers", meta = (ClampMin = "0.01", ClampMax = "5.0"))
	float FingertipFlameLifetimeScale = 0.4f;

	// Strength of the fire's light while it sits on the fingertip, as a fraction of its normal intensity AND radius
	// (the hand is close to the eye, so the normal light is far too bright there).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float FingertipLightScale = 0.3f;

	// The flame on the fingertip is drawn as the flat painted flame (the same picture as bDrawFlameGlow) at the
	// fingertip's position on the screen, with the real light on the hand. Unlike the Niagara fire it always sits exactly on
	// the fingertip and its size is fully under control. ON by default.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers")
	bool bFingertipFlameGlow = true;

	// Half-size of that flame on the screen (fractions of screen width/height). Applies live in PIE.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers")
	FVector2D FingertipFlameHalfExtent = FVector2D(0.03, 0.055);

	// Also show the Niagara fire (NS_Fire) at the fingertip. OFF by default: its embers/smoke do not follow the
	// component's scale and spread all over the screen.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers")
	bool bFingertipNiagaraFlame = false;

	// The flame is glued to the camera (like the hand) so it never jumps on the screen when you move or turn; instead
	// it LEANS away from the direction of movement and straightens up when you stop. Max lean in degrees (0 = no
	// lean), how fast it follows (higher = snappier), and the speed (cm/s) that gives the full lean.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers", meta = (ClampMin = "0.0", ClampMax = "80.0"))
	float FingertipFlameMaxLean = 25.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers", meta = (ClampMin = "0.5", ClampMax = "30.0"))
	float FingertipFlameLeanResponse = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers", meta = (ClampMin = "50.0"))
	float FingertipFlameLeanSpeedRef = 400.0f;

	// Names of the Niagara user parameters above (as in NS_Fire's User Parameters). Change only if the fire system changes.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers")
	FName FlameScaleParameter = TEXT("User.Flame Scale");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers")
	FName LifetimeScaleParameter = TEXT("User.Lifetime Scale");

	// Your own picture of the fingers (PNG with transparent background, already dark and rim-lit). Empty = the
	// imported asset /Game/T_VisionFingers is used automatically if it exists, otherwise a generated placeholder.
	// If your picture already contains the flame, switch bDrawFlameGlow off. [needs PIE restart]
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers")
	TObjectPtr<UTexture2D> FingersTexture;

	// Top-left / bottom-right corner of the fingers picture. The picture keeps its own proportions inside this box.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers")
	FVector2D FingersScreenMin = FVector2D(0.4486, 0.551);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers")
	FVector2D FingersScreenMax = FVector2D(0.6726, 1.071);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FingersOpacity = 1.0f;

	// How far the hand drifts, as a fraction of the screen (0 = perfectly still), and how fast. A slow, small
	// drift makes the hand feel alive (breathing / unsteady hold).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers", meta = (ClampMin = "0.0", ClampMax = "0.05"))
	float FingersSwayAmount = 0.004f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float FingersSwaySpeed = 1.0f;

	// How much the edge light on the hand flickers together with the flame (0 = steady, 1 = strong).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FingersFlicker = 0.3f;

	// Draw a small painted flame above the fingertips. OFF by default: the real fire (AVisionFireSource) is used.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers")
	bool bDrawFlameGlow = false;

	// Centre of the flame on screen and its half-size (fractions of screen width/height).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers")
	FVector2D FlameScreenCenter = FVector2D(0.49, 0.485);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers")
	FVector2D FlameScreenHalfExtent = FVector2D(0.055, 0.098);

	// Brightness of the flame picture (0..1) and how strongly it flickers (0 = steady).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FlameGlowOpacity = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Look|Fingers", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float FlameFlicker = 0.5f;

	// Runtime objects of the look — built in BeginPlay().
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vision Look")
	TObjectPtr<UPostProcessComponent> LookPostProcess;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> VisionMID;

	// Several differently-shaped copies of the smoky frame; the screen slowly cross-fades between them.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vision Look")
	TArray<TObjectPtr<UTexture2D>> FrameTextures;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vision Look")
	TObjectPtr<UTexture2D> FingersPlaceholderTexture;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vision Look")
	TObjectPtr<UTexture2D> FlameGlowTexture;

	// True between ToggleVision() calls that turn the ability on/off — TickComponent() only recomputes/
	// repositions FireSourceActor while this is true.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vision Fire Source")
	bool bIsVisionActive = false;

	// Seconds the ability stays active after ToggleVision() turns it on, before automatically turning itself
	// back off — same "temporary, not a manual forever-toggle" pattern the old
	// UFingerFlamePeekComponent::PeekDuration used (that component is gone now, this replaces it for the
	// Priest). Starting value, tune freely in PIE.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Fire Source")
	float VisionDuration = 10.0f;

	// Seconds after the ability ends (naturally or cancelled early by a second ToggleVision() press) before
	// it can be activated again — same "can't spam it back-to-back" pattern as
	// UHunterFocusComponent::FocusCooldown. Starting value, tune freely in PIE.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Vision Fire Source")
	float VisionCooldown = 15.0f;

	// True once VisionCooldown has elapsed since the ability last ended — ToggleVision() refuses to activate
	// while this is false.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Vision Fire Source")
	bool bCanUseVision = true;

	// Flips bIsVisionActive on (refuses while on cooldown — see bCanUseVision), or ends it early if already
	// active (delegates to EndVisionEffect() — same result as the VisionDuration timeout, just early).
	// Turning on switches the owning APriestCharacter to its first-person camera
	// (Cast<APriestCharacter>(GetOwner())->SetVisionFirstPersonActive(), see that function's own comment) and
	// suppresses ABaseCharacter's own eyes-closed screen darkening
	// (SetEyesClosedDarknessSuppressed(true) — see that function's own comment for why: this ability is meant
	// to be used with the eyes closed, and the darkness would otherwise crush its own post-process material)
	// so the very next line already reads the RIGHT camera, then repositions FireSourceActor (so its very
	// first visible frame is already correct, not the spawn origin for one tick) then calls
	// AVisionFireSource::Activate() AND starts fading the screen look in (see UpdateLook()), and arms the
	// VisionDuration timer. No-ops (for the fire/camera part) if FireSourceActor is somehow invalid, and
	// silently skips the camera switch if the owner isn't actually an APriestCharacter (defensive only — this
	// component is Priest-only by design, see this class's own header comment). Bound to V by
	// APriestCharacter::SetupPlayerInputComponent().
	UFUNCTION(BlueprintCallable, Category = "Vision Fire Source")
	void ToggleVision();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	FTimerHandle VisionDurationTimerHandle;
	FTimerHandle VisionCooldownTimerHandle;

	// Shared teardown for both the natural VisionDuration timeout AND a manual mid-ability ToggleVision()
	// press — turns off camera/darkness-suppression/fire/post-process strength, then starts VisionCooldown.
	// Idempotent (no-ops if already inactive).
	void EndVisionEffect();

	void EndVisionCooldown();

	// A single trace from the camera's own location, along its FULL 3D rotation (pitch included — this is
	// the "flashlight" design; see the .cpp for the history of the yaw-only/capsule-anchored versions this
	// replaced and why they don't fit this requirement), out to FireDistance, by OBJECT TYPE (WorldStatic/
	// WorldDynamic) rather than by trace channel (see the .cpp for why a channel trace would wrongly hit
	// monsters here) — skipping any purely-Overlap trigger volumes it happens to graze (see the .cpp's
	// TraceIgnoringOverlapOnlyHits for why that matters). Moves FireSourceActor to whatever it hits, pulled
	// back FireSafetyBuffer, or to the full FireDistance if the view ray hits nothing. No-op (leaves
	// FireSourceActor where it was) if the owner has no valid camera this frame.
	void UpdateFirePlacement();

	// Screen look (see "Vision Look" properties above).
	void BuildLookPostProcess();
	void ApplyLookSettings();
	void BuildFrameTexture(int32 Seed);
	void ShowFrameOverlay();
	void HideFrameOverlay();
	UTexture2D* BuildVeinsTexture(float ThicknessMultiplier);
	void ShowVeinsOverlay();
	void HideVeinsOverlay();
	// Slow drift of the hand picture (screen fractions), shared by the picture and the fire attached to it.
	FVector2D ComputeFingersSway() const;

	// Where the fingertips are on screen, as a fraction of the screen, including the drift and FireScreenOffset.
	FVector2D ComputeFireScreenFraction(int32 ViewportWidth, int32 ViewportHeight) const;

	void BuildFingersTextures();
	void ShowFingersOverlay();
	void HideFingersOverlay();

	// Called every tick: eases LookWeight toward 1 (active) / 0 (inactive), drives the post-process blend
	// weight, and shows/hides the frame overlay accordingly.
	void UpdateLook(float DeltaTime);

	// Sends the camera (position, direction, field of view) and the floor height to the vision material every
	// frame, so it can work out where in the WORLD each screen pixel is (fog that hangs in the air and is thicker
	// near the floor). Material parameters C1, C2, C3 — see the material's own code comment.
	void UpdateCameraParams();

	float LookWeight = 0.0f;
	static constexpr int32 FrameStateCount = 3;
	float FrameCurrentAlpha = 0.0f;
	float FrameLayerAlpha[FrameStateCount] = { 0.0f, 0.0f, 0.0f };
	FSlateBrush FrameBrushes[FrameStateCount];
	TSharedPtr<SWidget> FrameWidget;

	// The vein network picture and its screen overlay (drawn just under the frame).
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> VeinsTexture;
	float VeinsCurrentAlpha = 0.0f;
	FSlateBrush VeinsBrush;

	// Same veins drawn thicker; faded in at the peak of each pulse so the veins seem to swell.
	UPROPERTY(Transient)
	TObjectPtr<UTexture2D> VeinsSwellTexture;
	float VeinsSwellAlpha = 0.0f;
	// True when the veins come from the /Game/T_VisionVeins texture asset instead of the procedural network.
	bool bVeinsFromAsset = false;
	FSlateBrush VeinsSwellBrush;
	TSharedPtr<SWidget> VeinsWidget;

	// The fire light's own intensity, read once in BeginPlay() so FireLightIntensityScale can scale it.
	float BaseFireLightIntensity = -1.0f;
	float BaseFireLightRadius = -1.0f;

	// Where the fingertip is on the screen (fractions), updated every frame by UpdateFirePlacement() in 3D-hand mode.
	FVector2D FingertipScreenCenter = FVector2D(0.5, 0.8);

	// Smoothed lean of the fingertip flame, in the camera's horizontal axes (X forward, Y right), -1..1.
	FVector2D FingertipLeanCurrent = FVector2D::ZeroVector;

	float FingersCurrentAlpha = 0.0f;
	float FingersBrightness = 1.0f;
	float FlameCurrentAlpha = 0.0f;
	FSlateBrush FingersBrush;
	FSlateBrush FlameBrush;
	TSharedPtr<SWidget> FingersWidget;
};
