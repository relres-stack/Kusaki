#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ResourceStockpileComponent.generated.h"

UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class MYCHARACTERS_API UResourceStockpileComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UResourceStockpileComponent();

	// Что именно копится (чисто для отладки/логов — "Candles", "CenserOil" и т.п.), не влияет на логику.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource Stockpile")
	FString ResourceName = TEXT("Resource");

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource Stockpile")
	int32 MaxAmount = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Resource Stockpile")
	int32 CurrentAmount = 10;

	// Пытается списать Amount единиц. Возвращает false и НИЧЕГО не меняет, если Amount больше текущего
	// остатка (нельзя уйти в минус) — вызывающий код сам решает, что делать при отказе.
	UFUNCTION(BlueprintCallable, Category = "Resource Stockpile")
	bool TrySpend(int32 Amount);

	// Пополняет запас, но не выше MaxAmount — "лишнее пропадает". Возвращает РЕАЛЬНО добавленное
	// количество (может быть меньше запрошенного, если упёрлись в потолок), чтобы вызывающий код мог,
	// например, решить не забирать подобранный предмет с земли, если пополнение того не стоило.
	UFUNCTION(BlueprintCallable, Category = "Resource Stockpile")
	int32 Add(int32 Amount);

	UFUNCTION(BlueprintPure, Category = "Resource Stockpile")
	bool IsEmpty() const { return CurrentAmount <= 0; }

	UFUNCTION(BlueprintPure, Category = "Resource Stockpile")
	int32 GetCurrentAmount() const { return CurrentAmount; }

	UFUNCTION(BlueprintPure, Category = "Resource Stockpile")
	int32 GetMaxAmount() const { return MaxAmount; }
};
