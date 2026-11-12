// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SilhouetteVisionComponent.generated.h"

class UPostProcessComponent;
class UMaterialInterface;
class UMeshComponent;
class UTexture2D;

// Shared "temporary silhouette vision with eyes closed" screen effect — reused as-is by two unrelated
// mechanics with only the color/desaturation differing: APriestCharacter's UFingerFlamePeekComponent
// (warm reddish-orange, see its own header comment) and the Rider's future ink-drops ability (Фаза 2,
// п.2 — b/w). First-iteration approximation only (desaturation + tint via FPostProcessSettings, no new
// material asset) — swappable later for real Custom Depth silhouettes without changing this interface.
//
// Creates its own UPostProcessComponent dynamically in BeginPlay() (a UActorComponent's constructor can't
// CreateDefaultSubobject a sibling component — only an owning AActor's constructor can — so unlike
// UFlashlightComponent::LightComponent this one doesn't need the owner to create and assign it; it makes
// its own via NewObject()).
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYCHARACTERS_API USilhouetteVisionComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USilhouetteVisionComponent();

	// Screen tint while active — white means no tint, just black-and-white. Set once per owner (e.g. warm
	// reddish-orange for the Priest's Finger Flame, matching APriestCharacter::FingerFlameLight's color).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Silhouette Vision")
	FLinearColor SilhouetteTintColor = FLinearColor::White;

	// 0 = full black-and-white, 1 = no desaturation at all. Feeds Settings.ColorSaturation directly.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Silhouette Vision")
	float DesaturationAmount = 0.0f;

	// Сила аддитивной подкраски (ColorOffset) — SilhouetteTintColor умножается на это перед подмешиванием.
	// SilhouetteTintColor обычно задан как насыщенный цвет (0..1 на канал, как обычный LinearColor), а
	// ColorOffset — это ДОБАВКА к цвету пикселя, так что применять его напрямую было бы огромным пересветом;
	// небольшое значение (0.1-0.2) даёт заметный, но не выжигающий оттенок.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Silhouette Vision")
	float TintIntensity = 0.15f;

	// Radius (world units), centered on the owner, ActivateVision() searches for ABaseMonster/AHolyItem
	// actors to turn into black silhouettes (see ApplySilhouettes()) — separate from PeekDuration/whatever
	// duration the caller uses, purely a spatial cutoff.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Silhouette Vision|Silhouettes")
	float SilhouetteSearchRadius = 1200.0f;

	// Material ApplySilhouettes() instances (via UMaterialInstanceDynamic::Create(), one per mesh slot,
	// "Color" param set to black) onto every found monster/item mesh. Defaults to the same translucent
	// unlit engine debug material UHunterFocusComponent::MentalTrailMaterial already uses
	// (/Engine/EngineDebugMaterials/M_SimpleUnlitTranslucent) — loaded once in the constructor via
	// ConstructorHelpers, same pattern — just opaque black here instead of translucent blue.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Silhouette Vision|Silhouettes")
	TObjectPtr<UMaterialInterface> SilhouetteMaterial;

	// Strong dark vignette while active — full-screen FPostProcessSettings parameter, no material needed.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Silhouette Vision|Background")
	float VignetteIntensity = 0.7f;

	// Film-grain strength [0,1] — feeds Settings.FilmGrainIntensity. NOTE: UE 5.8 replaced the old
	// GrainIntensity/GrainJitter post-process API entirely with a texture-based "Film Grain" system;
	// GrainJitter has no surviving equivalent, so it isn't reproduced here. FilmGrainIntensity is a SILENT
	// NO-OP unless a grain texture is actually bound — either this project's own default (Project Settings
	// > Rendering > Default Settings > Film Grain Texture) or FilmGrainTextureOverride below.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Silhouette Vision|Background")
	float GrainIntensity = 0.6f;

	// Optional override texture for the Film Grain effect — only needed if the project has no project-wide
	// default Film Grain Texture set; leave unset to fall back to that default.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Silhouette Vision|Background")
	TObjectPtr<UTexture2D> FilmGrainTextureOverride;

	// True between ActivateVision() and DeactivateVision(). Named bIsVisionActive, not bIsActive — the
	// latter already exists on UActorComponent itself (component enabled/disabled), a different concept.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Silhouette Vision")
	bool bIsVisionActive = false;

	// Turns the effect on. Duration <= 0 (default) leaves it on until an explicit DeactivateVision() call —
	// e.g. UFingerFlamePeekComponent already runs its own PeekDuration timer and just wants to be told when
	// to turn the screen back to normal. Duration > 0 arms this component's own timer instead, for callers
	// that don't want to track their own.
	UFUNCTION(BlueprintCallable, Category = "Silhouette Vision")
	void ActivateVision(float Duration = 0.0f);

	// Turns the effect off and clears any pending auto-deactivate timer. Idempotent (harmless if already
	// inactive).
	UFUNCTION(BlueprintCallable, Category = "Silhouette Vision")
	void DeactivateVision();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// bUnbound = true — the effect is global (not tied to a volume in the world), same reasoning as an
	// always-on flashlight/light component riding the owner rather than a placed world actor.
	UPROPERTY()
	TObjectPtr<UPostProcessComponent> PostProcessComp;

	FTimerHandle AutoDeactivateTimerHandle;

	// Finds every ABaseMonster/AHolyItem within SilhouetteSearchRadius of the owner and swaps each of
	// their mesh's materials (per slot) for a black UMaterialInstanceDynamic of SilhouetteMaterial — see
	// SilhouettedMeshes/OriginalMaterials below for the undo bookkeeping. No-op if SilhouetteMaterial
	// failed to load. Called from ActivateVision(), after the post-process side is already set up.
	void ApplySilhouettes();

	// Reverts every mesh ApplySilhouettes() touched back to its saved per-slot materials — skips (via
	// IsValid()) any mesh whose actor was destroyed/died while the vision was active, since there's
	// nothing left to restore. Called from DeactivateVision(), clears both arrays afterward.
	void RestoreSilhouettes();

	// Deliberately NOT UPROPERTY: OriginalMaterials is a nested TArray<TArray<...>>, which UHT can't
	// reflect at all, so neither array is reflected, for consistency (both are purely transient bookkeeping
	// that only lives between one ApplySilhouettes()/RestoreSilhouettes() pair, never serialized). Each
	// original material pointer stays safely alive regardless — it's still referenced by the owning mesh
	// ASSET's own default material list (StaticMesh/SkeletalMesh), independent of whatever a component
	// instance's material override slot currently points to.
	TArray<TWeakObjectPtr<UMeshComponent>> SilhouettedMeshes;
	TArray<TArray<UMaterialInterface*>> OriginalMaterials;
};
