#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"

#include "AsyncAction_UpdateSaveGameSlots.generated.h"

struct FPersistentStateSlotHandle;
class UPersistentStateSubsystem;

UCLASS()
class UAsyncAction_UpdateSaveGameSlots: public UBlueprintAsyncActionBase
{
	GENERATED_BODY()
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FUpdateSaveGameSlotsDelegate, const TArray<FPersistentStateSlotHandle>&, SlotHandles);
public:
	UFUNCTION(BlueprintCallable, Category = "Persistent State", meta = (BlueprintInternalUseOnly = "true"))
	static UAsyncAction_UpdateSaveGameSlots* UpdateSaveGameSlots(const UObject* WorldContextObject);

	//~Begin CancellableAsyncAction interface
	virtual void Activate() override;
	virtual void SetReadyToDestroy() override;
	//~End CancellableAsyncAction interface

	UPROPERTY(BlueprintAssignable)
	FUpdateSaveGameSlotsDelegate Updated;

	UPROPERTY(BlueprintAssignable)
	FUpdateSaveGameSlotsDelegate Failed;

private:
	void OnSlotUpdateCompleted(TArray<FPersistentStateSlotHandle> Slots);
	
	TWeakObjectPtr<UPersistentStateSubsystem> WeakSubsystem;
};
