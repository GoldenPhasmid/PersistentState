#include "PersistentStateStorage.h"

#include "PersistentStateStatics.h"
#include "Managers/PersistentStateManager.h"

FPersistentStateSlotHandle FPersistentStateSlotHandle::InvalidHandle;

TSharedPtr<FPersistentStateSlot> FPersistentStateSlotHandle::GetSlot() const
{
	if (const UPersistentStateStorage* Storage = WeakStorage.Get())
	{
		return Storage->GetStateSlot(*this);
	}

	return nullptr;
}

void UPersistentStateStorage::SaveWorldState(const FPersistentStateSlotHandle& SourceSlotHandle, const FPersistentStateSlotHandle& TargetSlotHandle, UWorld* CurrentWorld, TArrayView<UPersistentStateManager*> Managers)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UPersistentStateStorage_SaveWorldState, PersistentStateChannel);
	
	TSharedPtr<FPersistentStateSlot> TargetSlot = GetStateSlot(TargetSlotHandle);
	FWorldStateSharedRef WorldState = UE::PersistentState::SaveWorldState(CurrentWorld, Managers);

	SaveWorldState(WorldState, SourceSlotHandle, TargetSlotHandle);
}

void UPersistentStateStorage::LoadWorldState(const FPersistentStateSlotHandle& TargetSlotHandle, FName WorldToLoad, TArrayView<UPersistentStateManager*> Managers)
{
	TRACE_CPUPROFILER_EVENT_SCOPE_ON_CHANNEL(UPersistentStateStorage_LoadWorldState, PersistentStateChannel);
	
	FWorldStateSharedRef WorldState = LoadWorldState(TargetSlotHandle, WorldToLoad);
	if (WorldState.IsValid())
	{
		UE::PersistentState::LoadWorldState(Managers, WorldState);
	}
}
