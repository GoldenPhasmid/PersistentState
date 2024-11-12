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
	FPersistentStateSlotHandle(const UPersistentStateStorage& InStorage, const FPersistentStateSlot& Slot)
		: SlotName(Slot.Header.SlotName)
		, WeakStorage(&InStorage)
	{
		check(SlotName != NAME_None);
	}

	FORCEINLINE FName GetSlotName() const { return SlotName; }
	FORCEINLINE bool IsValid() const { return SlotName != NAME_None; }
	TSharedPtr<FPersistentStateSlot> GetSlot() const;

	static FPersistentStateSlotHandle InvalidHandle;
private:
	FName SlotName = NAME_None;
	TWeakObjectPtr<const UPersistentStateStorage> WeakStorage;
};

FORCEINLINE bool operator==(const FPersistentStateSlotHandle& A, const FPersistentStateSlotHandle& B)
{
	return A.GetSlotName() == B.GetSlotName();
}

UCLASS(BlueprintType)
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
	PURE_VIRTUAL(UPersistentStateManager::CreateStateSlot, return {};)

	/** */
	virtual void GetAvailableSlots(TArray<FPersistentStateSlotHandle>& OutStates)
	PURE_VIRTUAL(UPersistentStateManager::GetAvailableSlots, );
	
	/** */
	virtual FPersistentStateSlotHandle GetStateBySlotName(FName SlotName) const
	PURE_VIRTUAL(UPersistentStateManager::GetStateBySlotName, return {};)
	
	/** */
	virtual TSharedPtr<FPersistentStateSlot> GetStateSlot(const FPersistentStateSlotHandle& SlotHandle) const
	PURE_VIRTUAL(UPersistentStateManager::GetStateSlot, return {};)

protected:
	
	/** */
	virtual void SaveWorldState(const FWorldStateSharedRef& WorldState, const FPersistentStateSlotHandle& SourceSlotHandle, const FPersistentStateSlotHandle& TargetSlotHandle)
	PURE_VIRTUAL(UPersistentStateManager::SaveWorldState, );

	virtual FWorldStateSharedRef LoadWorldState(const FPersistentStateSlotHandle& TargetSlotHandle, FName WorldName)
	PURE_VIRTUAL(UPersistentStateManager::LoadWorldState, return {};)
};
