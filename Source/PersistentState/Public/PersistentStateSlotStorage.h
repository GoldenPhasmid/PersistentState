#pragma once

#include "CoreMinimal.h"
#include "PersistentStateStorage.h"

#include "PersistentStateSlotStorage.generated.h"

UCLASS()
class PERSISTENTSTATE_API UPersistentStateSlotStorage: public UPersistentStateStorage
{
	GENERATED_BODY()
	
	friend class FUpdateAvailableSlotsAsyncTask;
	friend class FLoadStateAsyncTask;
public:
	UPersistentStateSlotStorage(const FObjectInitializer& Initializer);
	UPersistentStateSlotStorage(FVTableHelper& Helper);

	template <typename TDescriptor>
	TDescriptor* GetStateSlotDescriptor(const FPersistentStateSlotHandle& SlotHandle) const
	{
		return CastChecked<TDescriptor>(GetStateSlotDescriptor(SlotHandle), ECastCheckedType::NullAllowed);
	}
	
	//~Begin PersistentStateStorage interface
	virtual void Init() override;
	virtual void Shutdown() override;
	virtual uint32 GetAllocatedSize() const override;
	virtual void WaitUntilTasksComplete() const override;
	virtual FGraphEventRef SaveState(FGameStateSharedRef GameState, FWorldStateSharedRef WorldState, const FPersistentStateSlotHandle& SourceSlotHandle, const FPersistentStateSlotHandle& TargetSlotHandle, FSaveCompletedDelegate CompletedDelegate) override;
	virtual FGraphEventRef LoadState(const FPersistentStateSlotHandle& TargetSlotHandle, FName WorldToLoad, FLoadCompletedDelegate CompletedDelegate) override;
	virtual FGraphEventRef UpdateAvailableStateSlots(FSlotUpdateCompletedDelegate CompletedDelegate) override;
	virtual void SaveStateSlotScreenshot(const FPersistentStateSlotHandle& TargetSlotHandle) override;
	virtual bool LoadStateSlotScreenshot(const FPersistentStateSlotHandle& TargetSlotHandle, FLoadScreenshotCompletedDelegate CompletedDelegate) override;
	virtual bool HasScreenshotForStateSlot(const FPersistentStateSlotHandle& TargetSlotHandle) override;
	virtual FPersistentStateSlotHandle CreateStateSlot(const FName& SlotName, const FText& Title, TSubclassOf<UPersistentStateSlotDescriptor> DescriptorClass) override;
	virtual void GetAvailableStateSlots(TArray<FPersistentStateSlotHandle>& OutStates, bool bOnDiskOnly) override;
	virtual UPersistentStateSlotDescriptor* GetStateSlotDescriptor(const FPersistentStateSlotHandle& SlotHandle) const override;
	virtual FPersistentStateSlotHandle GetStateSlotByName(FName SlotName) const override;
	virtual bool CanLoadFromStateSlot(const FPersistentStateSlotHandle& SlotHandle, FName World) const override;
	virtual bool CanSaveToStateSlot(const FPersistentStateSlotHandle& SlotHandle, FName World) const override;
	virtual void RemoveStateSlot(const FPersistentStateSlotHandle& SlotHandle) override;
	//~End PersistentStorage interface
protected:
	void CompleteLoadState_GameThread(FPersistentStateSlotSharedRef TargetSlot, FGameStateSharedRef LoadedGameState, FWorldStateSharedRef LoadedWorldState, FLoadCompletedDelegate CompletedDelegate);
	void CompleteSlotUpdate_GameThread(const FUpdateAvailableSlotsAsyncTask& Task, FSlotUpdateCompletedDelegate CompletedDelegate);

	FPersistentStateSlotSharedRef FindSlot(const FPersistentStateSlotHandle& SlotHandle, bool* OutNamedSlot = nullptr) const;
	FPersistentStateSlotSharedRef FindSlot(FName SlotName, bool* OutNamedSlot = nullptr) const;
	
	static void AsyncSaveState(
		const FPersistentStateSlotSaveRequest& Request,
		FPersistentStateSlotSharedRef SourceSlot,
		FPersistentStateSlotSharedRef TargetSlot,
		const FString& FilePath,
		TSubclassOf<UPersistentStateSlotDescriptor> DefaultDescriptor
	);

	static bool HasStateSlotScreenshotFile(const FPersistentStateSlotSharedRef& Slot);
	static bool HasStateSlotFile(const FPersistentStateSlotSharedRef& Slot);
	/**
	 * Create physical save game file and associates it with a state slot
	 * @param Slot
	 * @param FilePath
	 */
	static void CreateStateSlotFile(const FPersistentStateSlotSharedRef& Slot, const FString& FilePath);
	
	static TUniquePtr<FArchive> CreateStateSlotReader(const FString& FilePath);
	static TUniquePtr<FArchive> CreateStateSlotWriter(const FString& FilePath);

	/** @return available save game names */
	static void RemoveStateSlotFile(const FString& FilePath);

	/** called on a game thread after screenshot was captured by the game viewport */
	void QueueScreenshotCapture(const FPersistentStateSlotHandle& Slot);
	void HandleScreenshotCapture(int32 Width, int32 Height, const TArray<FColor>& Bitmap);

	/** ensure all running tasks are completed */
	void EnsureTaskCompletion() const;
	FGraphEventArray GetPrerequisites() const;

	/** default descriptor */
	TSubclassOf<UPersistentStateSlotDescriptor> DefaultDescriptor;
	
	/**
	 * A list of named slots, user-defined in editor. Named slots are created during initialization and can be referenced
	 * throughout the state storage lifetime. Does not require a linked physical file
	 */
	TArray<FPersistentStateSlotSharedRef> NamedSlots;
	/** A list of runtime-created slots, linked to a physical files */
	TArray<FPersistentStateSlotSharedRef> RuntimeSlots;

	/** cached slot handle, supposedly used by the state subsystem */
	FPersistentStateSlotHandle CurrentSlot;
	/** cached world state, supposedly used by the state subsystem */
	FWorldStateSharedRef CurrentWorldState;
	/** cached game state, supposedly used by the state subsystem */
	FGameStateSharedRef CurrentGameState;
	
	/** last launched event, emulates a pipe behavior */
	FGraphEventRef LastQueuedEvent;

	/** OnViewportRendered delegate handle */
	FDelegateHandle CaptureScreenshotHandle;
	TArray<FPersistentStateSlotHandle> SlotsForScreenshotCapture;
};
