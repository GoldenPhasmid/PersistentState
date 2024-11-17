#pragma once

#include "CoreMinimal.h"
#include "PersistentStateSlot.h"

#include "PersistentStateStorage.generated.h"

class UPersistentStateStorage;
class UPersistentStateSubsystem;
class UPersistentStateManager;

USTRUCT(BlueprintType)
struct FPersistentStateSlotHandle
{
	GENERATED_BODY()

	FPersistentStateSlotHandle() = default;
	FPersistentStateSlotHandle(const UPersistentStateStorage& InStorage, const FName& InSlotName)
		: SlotName(InSlotName)
		, WeakStorage(&InStorage)
	{
		check(SlotName != NAME_None);
	}

	FORCEINLINE bool IsValid() const { return SlotName != NAME_None; }
	FORCEINLINE FName GetSlotName() const { return SlotName; }
	FPersistentStateSlotSharedRef GetSlot() const;

	static FPersistentStateSlotHandle InvalidHandle;
private:
	FName SlotName = NAME_None;
	TWeakObjectPtr<const UPersistentStateStorage> WeakStorage;
};

FORCEINLINE bool operator==(const FPersistentStateSlotHandle& A, const FPersistentStateSlotHandle& B)
{
	return A.GetSlotName() == B.GetSlotName();
}

UCLASS(BlueprintType, Abstract)
class PERSISTENTSTATE_API UPersistentStateStorage: public UObject
{
	GENERATED_BODY()
public:

	void SaveWorldState(const FPersistentStateSlotHandle& SourceSlotHandle, const FPersistentStateSlotHandle& TargetSlotHandle, UWorld* CurrentWorld, TArrayView<UPersistentStateManager*> Managers);
	void LoadWorldState(const FPersistentStateSlotHandle& TargetSlotHandle, FName WorldToLoad, TArrayView<UPersistentStateManager*> Managers);

	/** */
	virtual void Init() {}

	/** */
	virtual void Tick(float DeltaTime) {}
	/** */
	virtual void Shutdown() {}
	/** */
	virtual void RefreshSlots() {}
	
	/** */
	virtual FPersistentStateSlotHandle CreateStateSlot(const FString& SlotName, const FText& Tile)
	PURE_VIRTUAL(UPersistentStateStorage::CreateStateSlot, return {};)

	/** */
	virtual void GetAvailableSlots(TArray<FPersistentStateSlotHandle>& OutStates)
	PURE_VIRTUAL(UPersistentStateStorage::GetAvailableSlots, );
	
	/** */
	virtual FPersistentStateSlotHandle GetStateSlotByName(FName SlotName) const
	PURE_VIRTUAL(UPersistentStateStorage::GetStateBySlotName, return {};)
	
	/** */
	virtual FPersistentStateSlotSharedRef GetStateSlot(const FPersistentStateSlotHandle& SlotHandle) const
	PURE_VIRTUAL(UPersistentStateStorage::GetStateSlot, return {};)

	/** */
	virtual FName GetWorldFromStateSlot(const FPersistentStateSlotHandle& SlotHandle) const
	PURE_VIRTUAL(UPersistentStateStorage::GetStateSlot, return {};)

	/** */
	virtual bool CanLoadFromStateSlot(const FPersistentStateSlotHandle& SlotHandle) const
	PURE_VIRTUAL(UPersistentStateStorage::RemoveStateSlot, return false;)

	/** */
	virtual bool CanSaveToStateSlot(const FPersistentStateSlotHandle& SlotHandle) const
	PURE_VIRTUAL(UPersistentStateStorage::RemoveStateSlot, return false;)

	/** */
	virtual void RemoveStateSlot(const FPersistentStateSlotHandle& SlotHandle)
	PURE_VIRTUAL(UPersistentStateStorage::RemoveStateSlot, );

protected:
	
	/** */
	virtual void SaveWorldState(const FWorldStateSharedRef& WorldState, const FPersistentStateSlotHandle& SourceSlotHandle, const FPersistentStateSlotHandle& TargetSlotHandle)
	PURE_VIRTUAL(UPersistentStateStorage::SaveWorldState, );

	virtual FWorldStateSharedRef LoadWorldState(const FPersistentStateSlotHandle& TargetSlotHandle, FName WorldName)
	PURE_VIRTUAL(UPersistentStateStorage::LoadWorldState, return {};)
};
