#pragma once

#include "CoreMinimal.h"
#include "PersistentStateStorage.h"

#include "PersistentStateSlotStorage.generated.h"

UCLASS()
class PERSISTENTSTATE_API UPersistentStateSlotStorage: public UPersistentStateStorage
{
	GENERATED_BODY()
public:

	//~Begin PersistentStateStorage interface
	virtual void Init() override;
	virtual void RefreshSlots() override;
	virtual void Shutdown() override;
	
	virtual FPersistentStateSlotHandle CreateStateSlot(const FString& SlotName, const FText& Title) override;
	virtual void GetAvailableSlots(TArray<FPersistentStateSlotHandle>& OutStates) override;
	virtual FPersistentStateSlotHandle GetStateBySlotName(FName SlotName) const override;
	virtual TSharedPtr<FPersistentStateSlot> GetStateSlot(const FPersistentStateSlotHandle& SlotHandle) const override;
protected:
	virtual void SaveWorldState(const FWorldStateSharedRef& WorldState, const FPersistentStateSlotHandle& SourceSlotHandle, const FPersistentStateSlotHandle& TargetSlotHandle) override;
	virtual FWorldStateSharedRef LoadWorldState(const FPersistentStateSlotHandle& TargetSlotHandle, FName WorldToLoad) override;
	//~End PersistentStorage interface

	TSharedPtr<FPersistentStateSlot> FindSlot(FName SlotName) const;
	
	TUniquePtr<FArchive> CreateReadArchive(const FString& FilePath) const;
	TUniquePtr<FArchive> CreateWriteArchive(const FString& FilePath) const;
	
	TArray<TSharedPtr<FPersistentStateSlot>> StateSlots;

	/** pre-loaded slot handle */
	FPersistentStateSlotHandle CurrentSlotHandle;
	FWorldStateSharedRef CurrentWorldState;
};
