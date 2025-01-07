#include "Actions/AsyncAction_UpdateSaveGameSlots.h"

#include "PersistentStateSubsystem.h"

UAsyncAction_UpdateSaveGameSlots* UAsyncAction_UpdateSaveGameSlots::UpdateSaveGameSlots(const UObject* WorldContextObject)
{
	UPersistentStateSubsystem* Subsystem = UPersistentStateSubsystem::Get(WorldContextObject);
	if (Subsystem == nullptr)
	{
		return nullptr;
	}

	UAsyncAction_UpdateSaveGameSlots* Action = NewObject<UAsyncAction_UpdateSaveGameSlots>();
	Action->WeakSubsystem = Subsystem;
	Action->RegisterWithGameInstance(Subsystem->GetGameInstance());

	return Action;
}

void UAsyncAction_UpdateSaveGameSlots::Activate()
{
	Super::Activate();

	UPersistentStateSubsystem* Subsystem = WeakSubsystem.Get();
	if (Subsystem == nullptr)
	{
		Failed.Broadcast({});
		SetReadyToDestroy();
		return;
	}

	Subsystem->UpdateSaveGameSlots(FSlotUpdateCompletedDelegate::CreateUObject(this, &ThisClass::OnSlotUpdateCompleted));
}

void UAsyncAction_UpdateSaveGameSlots::SetReadyToDestroy()
{
	Super::SetReadyToDestroy();
}

void UAsyncAction_UpdateSaveGameSlots::OnSlotUpdateCompleted(TArray<FPersistentStateSlotHandle> Slots)
{
	Updated.Broadcast(Slots);
	SetReadyToDestroy();
}
