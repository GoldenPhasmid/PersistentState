#pragma once

#include "CoreMinimal.h"
#include "PersistentStateStorage.h"

#include "PersistentStateSlotStorage.generated.h"

UCLASS()
class PERSISTENTSTATE_API UPersistentStateSlotStorage final: public UPersistentStateStorage
{
	GENERATED_BODY()
public:
	UPersistentStateSlotStorage(const FObjectInitializer& Initializer);
	UPersistentStateSlotStorage(FVTableHelper& Helper);
	
	//~Begin PersistentStateStorage interface
	virtual void Init() override;
	virtual void Shutdown() override;
	virtual uint32 GetAllocatedSize() const override;

	virtual UE::Tasks::FTask SaveState(FGameStateSharedRef GameState, FWorldStateSharedRef WorldState, const FPersistentStateSlotHandle& SourceSlotHandle, const FPersistentStateSlotHandle& TargetSlotHandle, FSaveCompletedDelegate CompletedDelegate) override;
	virtual UE::Tasks::FTask LoadState(const FPersistentStateSlotHandle& TargetSlotHandle, FName WorldToLoad, FLoadCompletedDelegate CompletedDelegate) override;
	virtual FPersistentStateSlotHandle CreateStateSlot(const FName& SlotName, const FText& Title) override;
	virtual void UpdateAvailableStateSlots() override;
	virtual void GetAvailableStateSlots(TArray<FPersistentStateSlotHandle>& OutStates, bool bOnDiskOnly) override;
	virtual FPersistentStateSlotDesc GetStateSlotDesc(const FPersistentStateSlotHandle& SlotHandle) const override;
	virtual FPersistentStateSlotHandle GetStateSlotByName(FName SlotName) const override;
	virtual FName GetWorldFromStateSlot(const FPersistentStateSlotHandle& SlotHandle) const override;
	virtual bool CanLoadFromStateSlot(const FPersistentStateSlotHandle& SlotHandle) const override;
	virtual bool CanSaveToStateSlot(const FPersistentStateSlotHandle& SlotHandle) const override;
	virtual void RemoveStateSlot(const FPersistentStateSlotHandle& SlotHandle) override;
	//~End PersistentStorage interface
protected:

	void SaveState(FGameStateSharedRef GameState, FWorldStateSharedRef WorldState, FPersistentStateSlotSharedRef SourceSlot, FPersistentStateSlotSharedRef TargetSlot);
	TPair<FGameStateSharedRef, FWorldStateSharedRef> LoadState(FPersistentStateSlotSharedRef TargetSlot, FName WorldToLoad);

	FPersistentStateSlotSharedRef FindSlot(const FPersistentStateSlotHandle& SlotHandle) const;
	FPersistentStateSlotSharedRef FindSlot(FName SlotName) const;

	bool HasSaveGameFile(const FPersistentStateSlotSharedRef& Slot) const;
	void CreateSaveGameFile(const FPersistentStateSlotSharedRef& Slot) const;
	TUniquePtr<FArchive> CreateSaveGameReader(const FString& FilePath) const;
	TUniquePtr<FArchive> CreateSaveGameWriter(const FString& FilePath) const;

	/** @return available save game names */
	TArray<FString> GetSaveGameNames() const;
	void RemoveSaveGameFile(const FString& FilePath);

	void HandleScreenshotCapture(int32 Width, int32 Height, const TArray<FColor>& Bitmap);

	/** ensure all running tasks are completed */
	void EnsureTaskCompletion() const;

	/** A list of logical state slots, possibly linked to the physical slots */
	TArray<FPersistentStateSlotSharedRef> StateSlots;

	/** pre-loaded slot handle */
	FPersistentStateSlotWeakRef CurrentSlot;
	FWorldStateSharedRef CurrentWorldState;
	FGameStateSharedRef CurrentGameState;

	/** task pipe for running async operations in sequence */
	UE::Tasks::FPipe TaskPipe;
	
	FDelegateHandle CaptureScreenshotHandle;
	TArray<FPersistentStateSlotHandle> SlotsForScreenshotCapture;
};
