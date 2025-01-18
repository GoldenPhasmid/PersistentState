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

	virtual FGraphEventRef SaveState(FGameStateSharedRef GameState, FWorldStateSharedRef WorldState, const FPersistentStateSlotHandle& SourceSlotHandle, const FPersistentStateSlotHandle& TargetSlotHandle, FSaveCompletedDelegate CompletedDelegate) override;
	virtual FGraphEventRef LoadState(const FPersistentStateSlotHandle& TargetSlotHandle, FName WorldToLoad, FLoadCompletedDelegate CompletedDelegate) override;
	virtual void SaveStateSlotScreenshot(const FPersistentStateSlotHandle& TargetSlotHandle) override;
	virtual bool LoadStateSlotScreenshot(const FPersistentStateSlotHandle& TargetSlotHandle, TFunction<void(UTexture2DDynamic*)> Callback) override;
	virtual bool HasScreenshotForStateSlot(const FPersistentStateSlotHandle& TargetSlotHandle) override;
	virtual FPersistentStateSlotHandle CreateStateSlot(const FName& SlotName, const FText& Title) override;
	virtual void UpdateAvailableStateSlots(FSlotUpdateCompletedDelegate CompletedDelegate) override;
	virtual void GetAvailableStateSlots(TArray<FPersistentStateSlotHandle>& OutStates, bool bOnDiskOnly) override;
	virtual FPersistentStateSlotDesc GetStateSlotDesc(const FPersistentStateSlotHandle& SlotHandle) const override;
	virtual FPersistentStateSlotHandle GetStateSlotByName(FName SlotName) const override;
	virtual bool CanLoadFromStateSlot(const FPersistentStateSlotHandle& SlotHandle, FName World) const override;
	virtual bool CanSaveToStateSlot(const FPersistentStateSlotHandle& SlotHandle, FName World) const override;
	virtual void RemoveStateSlot(const FPersistentStateSlotHandle& SlotHandle) override;
	//~End PersistentStorage interface
protected:

	void CompleteLoadState(FPersistentStateSlotSharedRef TargetSlot, FGameStateSharedRef LoadedGameState, FWorldStateSharedRef LoadedWorldState, FLoadCompletedDelegate CompletedDelegate);
	void CompleteSlotUpdate(const TArray<FPersistentStateSlotSharedRef>& NewNamedSlots, const TArray<FPersistentStateSlotSharedRef>& NewRuntimeSlots, FSlotUpdateCompletedDelegate CompletedDelegate);

	static void AsyncSaveState(FGameStateSharedRef GameState, FWorldStateSharedRef WorldState, FPersistentStateSlotSharedRef SourceSlot, FPersistentStateSlotSharedRef TargetSlot, const FString& FilePath);
	static TPair<FGameStateSharedRef, FWorldStateSharedRef> AsyncLoadState(FPersistentStateSlotSharedRef TargetSlot, FName WorldToLoad, FGameStateSharedRef CachedGameState, FWorldStateSharedRef CachedWorldState);

	FPersistentStateSlotSharedRef FindSlot(const FPersistentStateSlotHandle& SlotHandle, bool* OutNamedSlot = nullptr) const;
	FPersistentStateSlotSharedRef FindSlot(FName SlotName, bool* OutNamedSlot = nullptr) const;

	static bool HasSaveGameFile(const FPersistentStateSlotSharedRef& Slot);
	/**
	 * Create physical save game file and associates it with a state slot
	 * @param Slot
	 * @param FilePath
	 */
	static void CreateSaveGameFile(const FPersistentStateSlotSharedRef& Slot, const FString& FilePath);
	
	static TUniquePtr<FArchive> CreateSaveGameReader(const FString& FilePath);
	static TUniquePtr<FArchive> CreateSaveGameWriter(const FString& FilePath);

	/** @return available save game names */
	static void RemoveSaveGameFile(const FString& FilePath);

	/** called on a game thread after screenshot was captured by the game viewport */
	void QueueScreenshotCapture(const FPersistentStateSlotHandle& Slot);
	void HandleScreenshotCapture(int32 Width, int32 Height, const TArray<FColor>& Bitmap);

	/** ensure all running tasks are completed */
	void EnsureTaskCompletion() const;
	FGraphEventArray GetPrerequisites() const;

	/** named slots */
	TArray<FPersistentStateSlotSharedRef, TInlineAllocator<8>> NamedSlots;
	/** A list of logical state slots, possibly linked to the physical slots */
	TArray<FPersistentStateSlotSharedRef> RuntimeSlots;

	/** pre-loaded slot handle */
	FPersistentStateSlotHandle CurrentSlot;
	FWorldStateSharedRef CurrentWorldState;
	FGameStateSharedRef CurrentGameState;
	
	/** last launched event, emulates a pipe behavior */
	FGraphEventRef LastQueuedEvent;
	
	FDelegateHandle CaptureScreenshotHandle;
	TArray<FPersistentStateSlotHandle> SlotsForScreenshotCapture;
};
