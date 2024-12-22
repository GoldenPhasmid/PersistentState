#pragma once

#include "CoreMinimal.h"
#include "PersistentStateStorage.h"
#include "Engine/CancellableAsyncAction.h"

#include "AsyncAction_SaveGameToSlot.generated.h"

class UPersistentStateSubsystem;
struct FPersistentStateSlotHandle;

UCLASS()
class UAsyncAction_SaveGameToSlot: public UBlueprintAsyncActionBase
{
	GENERATED_BODY()
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSaveGameToSlotDelegate);
public:

	UFUNCTION(BlueprintCallable, Category = "Persistent State", meta = (BlueprintInternalUseOnly = "true"))
	static UAsyncAction_SaveGameToSlot* SaveGame(const UObject* WorldContextObject);
	
	UFUNCTION(BlueprintCallable, Category = "Persistent State", meta = (BlueprintInternalUseOnly = "true"))
	static UAsyncAction_SaveGameToSlot* SaveGameToSlot(const UObject* WorldContextObject, const FPersistentStateSlotHandle& TargetSlot);

	//~Begin CancellableAsyncAction interface
	virtual void Activate() override;
	virtual void SetReadyToDestroy() override;
	//~End CancellableAsyncAction interface

	UPROPERTY(BlueprintAssignable)
	FSaveGameToSlotDelegate Started;

	UPROPERTY(BlueprintAssignable)
	FSaveGameToSlotDelegate Completed;

	UPROPERTY(BlueprintAssignable)
	FSaveGameToSlotDelegate Failed;

private:

	void OnSaveStateStarted(const FPersistentStateSlotHandle& InSlot);
	void OnSaveStateCompleted(const FPersistentStateSlotHandle& InSlot);

	TWeakObjectPtr<UPersistentStateSubsystem> WeakSubsystem;
	FPersistentStateSlotHandle TargetSlot;
	FDelegateHandle StartedHandle;
	FDelegateHandle CompletedHandle;
};
