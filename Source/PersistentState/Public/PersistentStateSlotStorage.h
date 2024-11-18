#pragma once

#include "CoreMinimal.h"
#include "PersistentStateStorage.h"

#include "PersistentStateSlotStorage.generated.h"

namespace UE::PersistentState
{
	PERSISTENTSTATE_API extern FString GCurrentWorldPackage;	
}


UCLASS()
class PERSISTENTSTATE_API UPersistentStateSlotStorage: public UPersistentStateStorage
{
	GENERATED_BODY()
public:

	//~Begin PersistentStateStorage interface
	virtual void Init() override;
	virtual void UpdateAvailableStateSlots() override;
	virtual void Shutdown() override;
	
	virtual FPersistentStateSlotHandle CreateStateSlot(const FString& SlotName, const FText& Title) override;
	virtual void GetAvailableStateSlots(TArray<FPersistentStateSlotHandle>& OutStates) override;
	virtual FPersistentStateSlotHandle GetStateSlotByName(FName SlotName) const override;
	virtual FPersistentStateSlotSharedRef GetStateSlot(const FPersistentStateSlotHandle& SlotHandle) const override;
	virtual FName GetWorldFromStateSlot(const FPersistentStateSlotHandle& SlotHandle) const override;
	virtual bool CanLoadFromStateSlot(const FPersistentStateSlotHandle& SlotHandle) const override;
	virtual bool CanSaveToStateSlot(const FPersistentStateSlotHandle& SlotHandle) const override;
	virtual void RemoveStateSlot(const FPersistentStateSlotHandle& SlotHandle) override;
protected:
	virtual void SaveWorldState(const FWorldStateSharedRef& WorldState, const FPersistentStateSlotHandle& SourceSlotHandle, const FPersistentStateSlotHandle& TargetSlotHandle) override;
	virtual FWorldStateSharedRef LoadWorldState(const FPersistentStateSlotHandle& TargetSlotHandle, FName WorldToLoad) override;
	//~End PersistentStorage interface

	FPersistentStateSlotSharedRef FindSlot(FName SlotName) const;

	bool HasSaveGameFile(const FPersistentStateSlotSharedRef& Slot) const;
	void CreateSaveGameFile(const FPersistentStateSlotSharedRef& Slot) const;
	TUniquePtr<FArchive> CreateSaveGameReader(const FString& FilePath) const;
	TUniquePtr<FArchive> CreateSaveGameWriter(const FString& FilePath) const;

	TArray<FString> GetSaveGameFiles() const;
	void RemoveSaveGameFile(const FString& FilePath);
	
	TArray<FPersistentStateSlotSharedRef> StateSlots;

	/** pre-loaded slot handle */
	FPersistentStateSlotHandle CurrentSlotHandle;
	FWorldStateSharedRef CurrentWorldState;
};
