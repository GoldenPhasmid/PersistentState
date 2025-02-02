#pragma once

#include "CoreMinimal.h"
#include "PersistentStateSlot.h"
#include "PersistentStateSlotView.h"

#include "PersistentStateStorage.generated.h"

class UPersistentStateStorage;
class UPersistentStateSubsystem;
class UPersistentStateManager;
class UPersistentStateSlotDescriptor;

DECLARE_DELEGATE(FSaveCompletedDelegate);
DECLARE_DELEGATE_TwoParams(FLoadCompletedDelegate, FGameStateSharedRef, FWorldStateSharedRef);
DECLARE_DELEGATE_OneParam(FSlotUpdateCompletedDelegate, TArray<FPersistentStateSlotHandle>);
DECLARE_DELEGATE_OneParam(FLoadScreenshotCompletedDelegate, UTexture2DDynamic*);

UCLASS(BlueprintType, Abstract)
class PERSISTENTSTATE_API UPersistentStateStorage: public UObject
{
	GENERATED_BODY()
public:
	
	/** Initialize state storage */
	virtual void Init()
	PURE_VIRTUAL(UPersistentStateStorage::Init, );
	
	/** Shutdown state storage, finish all pending or in progress operations */
	virtual void Shutdown()
	PURE_VIRTUAL(UPersistentStateStorage::Shutdown, );

	/** @return total allocated size by the state storage */
	virtual uint32 GetAllocatedSize() const
	PURE_VIRTUAL(UPersistentStateStorage::GetAllocatedSize, return 0;)

	/** waits until all scheduled operations are complete */
	virtual void WaitUntilTasksComplete() const
	PURE_VIRTUAL(UPersistentStateStorage::GetAllocatedSize, );

	/**
	 * Save game and world state to @TargetSlotHandle, transfer any other relevant data (e.g. other worlds) from @SourceSlotHandle to @TargetSlotHandle.
	 * Save op is done asynchronously, with @CompletedDelegate notifying its completion on a game thread.
	 * Caller can wait until the op is finished by using event ref, returned by the function, or calling @WaitUntilTasksComplete
	 * @param GameState game state to save
	 * @param WorldState world state to save
	 * @param SourceSlotHandle reference slot that provides data for other worlds
	 * @param TargetSlotHandle target slot to save world data to
	 * @param CompletedDelegate triggered after operation is complete
	 * @return task handle, may be completed on return. Task can be forced to completion via its handle
	 */
	virtual FGraphEventRef SaveState(FGameStateSharedRef GameState, FWorldStateSharedRef WorldState, const FPersistentStateSlotHandle& SourceSlotHandle, const FPersistentStateSlotHandle& TargetSlotHandle, FSaveCompletedDelegate CompletedDelegate)
	PURE_VIRTUAL(UPersistentStateStorage::SaveWorldState, return {};)

	/**
	 * Load game and world state stored inside @TargetSlotHandle. Use world state defined by short world name @WorldName 
	 * Load op is done asynchronously, with @CompletedDelegate notifying its completion on a game thread.
	 * Caller can wait until the op is finished by using event ref, returned by the function, or calling @WaitUntilTasksComplete
	 * @param TargetSlotHandle target slot to get world data from
	 * @param WorldName world to load
	 * @param CompletedDelegate triggered after operation is complete
	 * @return task handle, may be completed on return. Task can be forced to completion via its handle
	 */
	virtual FGraphEventRef LoadState(const FPersistentStateSlotHandle& TargetSlotHandle, FName WorldName, FLoadCompletedDelegate CompletedDelegate)
	PURE_VIRTUAL(UPersistentStateStorage::LoadWorldState, return {};)

	/**
	 * Launch an update slots task that searches for valid state slot files, results in an up-to-date state about
	 * what state slots are available on background storage and their details
	 * @param CompletedDelegate triggered after operation is complete 
	 */
	virtual FGraphEventRef UpdateAvailableStateSlots(FSlotUpdateCompletedDelegate CompletedDelegate)
	PURE_VIRTUAL(UPersistentStateStorage::UpdateAvailableStateSlots, return {};)

	/** @return true if screenshot exists for a given state slot */
	virtual bool HasScreenshotForStateSlot(const FPersistentStateSlotHandle& TargetSlotHandle)
	PURE_VIRTUAL(UPersistentStateStorage::HasScreenshotForStateSlot, return false;)

	/** update state slot screenshot without saving any state */
	virtual void SaveStateSlotScreenshot(const FPersistentStateSlotHandle& TargetSlotHandle)
	PURE_VIRTUAL(UPersistentStateStorage::SaveStateSlotScreenshot, );

	/**
	 * load screenshot associated with @TargetSlotHandle as a dynamic 2D texture and execute @Callback
	 * @return false if there's no screenshot data present, or initial checks have failed. Result can still return NULL texture if failed
	 */
	virtual bool LoadStateSlotScreenshot(const FPersistentStateSlotHandle& TargetSlotHandle, FLoadScreenshotCompletedDelegate CompletedDelegate)
	PURE_VIRTUAL(UPersistentStateStorage::LoadStateSlotScreenshot, return false;)

	/** create a new state slot and @return the handle */
	virtual FPersistentStateSlotHandle CreateStateSlot(const FName& SlotName, const FText& Title, TSubclassOf<UPersistentStateSlotDescriptor> DescriptorClass)
	PURE_VIRTUAL(UPersistentStateStorage::CreateStateSlot, return {};)

	/** delete slot data from the device storage and remove state slot itself, unless it is a persistent slot */
	virtual void RemoveStateSlot(const FPersistentStateSlotHandle& SlotHandle)
	PURE_VIRTUAL(UPersistentStateStorage::RemoveStateSlot, );

	/** @return a list of available state slots */
	virtual void GetAvailableStateSlots(TArray<FPersistentStateSlotHandle>& OutStates, bool bOnDiskOnly)
	PURE_VIRTUAL(UPersistentStateStorage::GetAvailableSlots, );

	/** @return slot desc view from slot handle */
	virtual UPersistentStateSlotDescriptor* GetStateSlotDescriptor(const FPersistentStateSlotHandle& SlotHandle) const
	PURE_VIRTUAL(UPersistentStateStorage::GetSlotDesc, return {};)
	
	/** @return slot handle identified by a slot name */
	virtual FPersistentStateSlotHandle GetStateSlotByName(FName SlotName) const
	PURE_VIRTUAL(UPersistentStateStorage::GetStateBySlotName, return {};)

	/** @return true if world can be loaded from a given state slot @SlotHandle */
	virtual bool CanLoadFromStateSlot(const FPersistentStateSlotHandle& SlotHandle, FName World) const
	PURE_VIRTUAL(UPersistentStateStorage::RemoveStateSlot, return false;)

	/** @return true if world can be saved to a given state slot @SlotHandle */
	virtual bool CanSaveToStateSlot(const FPersistentStateSlotHandle& SlotHandle, FName World) const
	PURE_VIRTUAL(UPersistentStateStorage::RemoveStateSlot, return false;)
	
};
