#pragma once

#include "CoreMinimal.h"
#include "PersistentStateSlot.h"
#include "PersistentStateSlotView.h"

#include "PersistentStateStorage.generated.h"

struct FPersistentStateSlotDesc;
class UPersistentStateStorage;
class UPersistentStateSubsystem;
class UPersistentStateManager;

using FSaveCompletedDelegate = TDelegate<void(), FDefaultTSDelegateUserPolicy>;
using FLoadCompletedDelegate = TDelegate<void(FGameStateSharedRef, FWorldStateSharedRef), FDefaultTSDelegateUserPolicy>;
using FSlotUpdateCompletedDelegate = TDelegate<void(TArray<FPersistentStateSlotHandle>), FDefaultTSDelegateUserPolicy>;

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

	virtual uint32 GetAllocatedSize() const
	PURE_VIRTUAL(UPersistentStateStorage::GetAllocatedSize, return 0;)

	/**
	 * Save world state to @TargetSlotHandle, transfer any other data from @SourceSlotHandle to @TargetSlotHandle. By default, save operation is done asynchronously.
	 * @param GameState game state to save
	 * @param WorldState world state to save
	 * @param SourceSlotHandle reference slot that provides data for other worlds
	 * @param TargetSlotHandle target slot to save world data to
	 * @param CompletedDelegate triggered after operation is complete
	 * @return task handle, may be completed on return. Task can be forced to completion via its handle
	 */
	virtual UE::Tasks::FTask SaveState(FGameStateSharedRef GameState, FWorldStateSharedRef WorldState, const FPersistentStateSlotHandle& SourceSlotHandle, const FPersistentStateSlotHandle& TargetSlotHandle, FSaveCompletedDelegate CompletedDelegate)
	PURE_VIRTUAL(UPersistentStateStorage::SaveWorldState, return {};)

	/**
	 * Load world state defined by @WorldName from a @TargetSlotHandle. By default, load operation is done asynchronously.
	 * @param TargetSlotHandle target slot to get world data from
	 * @param WorldName world to load
	 * @param CompletedDelegate triggered after operation is complete
	 * @return task handle, may be completed on return. Task can be forced to completion via its handle
	 */
	virtual UE::Tasks::FTask LoadState(const FPersistentStateSlotHandle& TargetSlotHandle, FName WorldName, FLoadCompletedDelegate CompletedDelegate)
	PURE_VIRTUAL(UPersistentStateStorage::LoadWorldState, return {};)

	/** create a new state slot and @return the handle */
	virtual FPersistentStateSlotHandle CreateStateSlot(const FName& SlotName, const FText& Tile)
	PURE_VIRTUAL(UPersistentStateStorage::CreateStateSlot, return {};)

	/** delete slot data from the device storage and remove state slot itself, unless it is a persistent slot */
	virtual void RemoveStateSlot(const FPersistentStateSlotHandle& SlotHandle)
	PURE_VIRTUAL(UPersistentStateStorage::RemoveStateSlot, );
	
	/** */
	virtual void UpdateAvailableStateSlots(FSlotUpdateCompletedDelegate CompletedDelegate)
	PURE_VIRTUAL(UPersistentStateStorage::UpdateAvailableStateSlots, );

	/** */
	virtual void GetAvailableStateSlots(TArray<FPersistentStateSlotHandle>& OutStates, bool bOnDiskOnly)
	PURE_VIRTUAL(UPersistentStateStorage::GetAvailableSlots, );

	/** @return slot description */
	virtual FPersistentStateSlotDesc GetStateSlotDesc(const FPersistentStateSlotHandle& SlotHandle) const
	PURE_VIRTUAL(UPersistentStateStorage::GetSlotDesc, return {};)
	
	/** */
	virtual FPersistentStateSlotHandle GetStateSlotByName(FName SlotName) const
	PURE_VIRTUAL(UPersistentStateStorage::GetStateBySlotName, return {};)

	/** */
	virtual FName GetWorldFromStateSlot(const FPersistentStateSlotHandle& SlotHandle) const
	PURE_VIRTUAL(UPersistentStateStorage::GetStateSlot, return {};)

	/** */
	virtual bool CanLoadFromStateSlot(const FPersistentStateSlotHandle& SlotHandle) const
	PURE_VIRTUAL(UPersistentStateStorage::RemoveStateSlot, return false;)

	/** */
	virtual bool CanSaveToStateSlot(const FPersistentStateSlotHandle& SlotHandle) const
	PURE_VIRTUAL(UPersistentStateStorage::RemoveStateSlot, return false;)
	
};
