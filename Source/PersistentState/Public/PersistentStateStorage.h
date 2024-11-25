#pragma once

#include "CoreMinimal.h"
#include "PersistentStateSlot.h"

#include "PersistentStateStorage.generated.h"

class UPersistentStateStorage;
class UPersistentStateSubsystem;
class UPersistentStateManager;

USTRUCT(BlueprintType)
struct PERSISTENTSTATE_API FPersistentStateSlotHandle
{
	GENERATED_BODY()

	FPersistentStateSlotHandle() = default;
	FPersistentStateSlotHandle(const UPersistentStateStorage& InStorage, const FName& InSlotName)
		: SlotName(InSlotName)
		, WeakStorage(&InStorage)
	{
		check(SlotName != NAME_None);
	}

	bool IsValid() const;
	FORCEINLINE FName GetSlotName() const { return SlotName; }

	static FPersistentStateSlotHandle InvalidHandle;
private:
	FName SlotName = NAME_None;
	TWeakObjectPtr<const UPersistentStateStorage> WeakStorage;
};

FORCEINLINE bool operator==(const FPersistentStateSlotHandle& A, const FPersistentStateSlotHandle& B)
{
	return A.GetSlotName() == B.GetSlotName();
}

FORCEINLINE bool operator!=(const FPersistentStateSlotHandle& A, const FPersistentStateSlotHandle& B)
{
	return A.GetSlotName() != B.GetSlotName();
}

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

	/** save world state to @TargetSlotHandle, transfer any other data from @SourceSlotHandle to @TargetSlotHandle */
	virtual void SaveWorldState(const FWorldStateSharedRef& WorldState, const FPersistentStateSlotHandle& SourceSlotHandle, const FPersistentStateSlotHandle& TargetSlotHandle)
	PURE_VIRTUAL(UPersistentStateStorage::SaveWorldState, );

	/** load world state defined by @WorldName from a @TargetSlotHandle */
	virtual FWorldStateSharedRef LoadWorldState(const FPersistentStateSlotHandle& TargetSlotHandle, FName WorldName)
	PURE_VIRTUAL(UPersistentStateStorage::LoadWorldState, return {};)

	/** create a new state slot and @return the handle */
	virtual FPersistentStateSlotHandle CreateStateSlot(const FName& SlotName, const FText& Tile)
	PURE_VIRTUAL(UPersistentStateStorage::CreateStateSlot, return {};)

	/** delete slot data from the device storage and remove state slot itself, unless it is a persistent slot */
	virtual void RemoveStateSlot(const FPersistentStateSlotHandle& SlotHandle)
	PURE_VIRTUAL(UPersistentStateStorage::RemoveStateSlot, );

	/** */
	virtual void UpdateAvailableStateSlots()
	PURE_VIRTUAL(UPersistentStateStorage::UpdateAvailableStateSlots, );

	/** */
	virtual void GetAvailableStateSlots(TArray<FPersistentStateSlotHandle>& OutStates, bool bOnDiskOnly)
	PURE_VIRTUAL(UPersistentStateStorage::GetAvailableSlots, );
	
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
