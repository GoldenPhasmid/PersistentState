#pragma once

#include "CoreMinimal.h"
#include "PersistentStateSlotView.h"
#include "Engine/CancellableAsyncAction.h"

#include "AsyncAction_LoadSaveGameScreenshot.generated.h"

class UPersistentStateSubsystem;
struct FPersistentStateSlotHandle;

UCLASS()
class UAsyncAction_LoadSaveGameScreenshot: public UCancellableAsyncAction
{
	GENERATED_BODY()
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FLoadSaveGameScreenshotDelegate, UTexture2DDynamic*, Texture);
public:

	UFUNCTION(BlueprintCallable, Category = "Persistent State", meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
	static UAsyncAction_LoadSaveGameScreenshot* LoadSaveGameScreenshot(UWorld* WorldContextObject, const FPersistentStateSlotHandle& Slot);

	virtual void Activate() override;
	virtual void SetReadyToDestroy() override;

	void OnLoadCompleted(UTexture2DDynamic* Texture);

	UPROPERTY(BlueprintAssignable)
	FLoadSaveGameScreenshotDelegate OnLoaded;

	UPROPERTY(BlueprintAssignable)
	FLoadSaveGameScreenshotDelegate OnFailed;

	TWeakObjectPtr<UPersistentStateSubsystem> WeakSubsystem;
	FPersistentStateSlotHandle TargetSlot;
};
