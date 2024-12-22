#pragma once

#include "CoreMinimal.h"
#include "PersistentStateStorage.h"
#include "Kismet/BlueprintAsyncActionBase.h"

#include "AsyncAction_LoadGameFromSlot.generated.h"

struct FPersistentStateSlotHandle;

UCLASS()
class UAsyncAction_LoadGameFromSlot: public UBlueprintAsyncActionBase
{
	GENERATED_BODY()
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FLoadGameFromSlotDelegate);
public:

	UFUNCTION(BlueprintCallable, Category = "Persistent State", meta = (BlueprintInternalUseOnly = "true"))
	static UAsyncAction_LoadGameFromSlot* LoadGame(const UObject* WorldContextObject, const TSoftObjectPtr<UWorld>& World, const FString& TravelOptions);
	
	UFUNCTION(BlueprintCallable, Category = "Persistent State", meta = (BlueprintInternalUseOnly = "true"))
	static UAsyncAction_LoadGameFromSlot* LoadGameFromSlot(const UObject* WorldContextObject, const FPersistentStateSlotHandle& TargetSlot, const TSoftObjectPtr<UWorld>& World, const FString& TravelOptions);

	//~Begin CancellableAsyncAction interface
	virtual void Activate() override;
	virtual void SetReadyToDestroy() override;
	//~End CancellableAsyncAction interface
	
	UPROPERTY(BlueprintAssignable)
	FLoadGameFromSlotDelegate Started;

	UPROPERTY(BlueprintAssignable)
	FLoadGameFromSlotDelegate Completed;

	UPROPERTY(BlueprintAssignable)
	FLoadGameFromSlotDelegate Failed;

private:

	void OnLoadStateStarted(const FPersistentStateSlotHandle& InSlot);
	void OnLoadStateFinished(const FPersistentStateSlotHandle& InSlot);

	TWeakObjectPtr<UPersistentStateSubsystem> WeakSubsystem;
	FPersistentStateSlotHandle TargetSlot;
	TSoftObjectPtr<UWorld> TargetWorld;
	FString TravelOptions;
	FDelegateHandle StartedHandle;
	FDelegateHandle CompletedHandle;
};
