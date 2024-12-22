#include "Actions/AsyncAction_SaveGameToSlot.h"

#include "PersistentStateSubsystem.h"

UAsyncAction_SaveGameToSlot* UAsyncAction_SaveGameToSlot::SaveGame(const UObject* WorldContextObject)
{
	UPersistentStateSubsystem* Subsystem = UPersistentStateSubsystem::Get(WorldContextObject);
	if (Subsystem == nullptr)
	{
		return nullptr;
	}

	UAsyncAction_SaveGameToSlot* Action = NewObject<UAsyncAction_SaveGameToSlot>();
	Action->WeakSubsystem = Subsystem;
	Action->RegisterWithGameInstance(Subsystem->GetGameInstance());

	return Action;
}

UAsyncAction_SaveGameToSlot* UAsyncAction_SaveGameToSlot::SaveGameToSlot(const UObject* WorldContextObject, const FPersistentStateSlotHandle& TargetSlot)
{
	UPersistentStateSubsystem* Subsystem = UPersistentStateSubsystem::Get(WorldContextObject);
	if (Subsystem == nullptr)
	{
		return nullptr;
	}

	UAsyncAction_SaveGameToSlot* Action = NewObject<UAsyncAction_SaveGameToSlot>();
	Action->WeakSubsystem = Subsystem;
	Action->TargetSlot = TargetSlot;
	Action->RegisterWithGameInstance(Subsystem->GetGameInstance());

	return Action;
}

void UAsyncAction_SaveGameToSlot::Activate()
{
	Super::Activate();

	UPersistentStateSubsystem* Subsystem = WeakSubsystem.Get();
	if (Subsystem == nullptr)
	{
		Failed.Broadcast();
		SetReadyToDestroy();
		return;
	}
	
	if (!TargetSlot.IsValid())
	{
		TargetSlot = Subsystem->GetActiveSaveGameSlot();
		if (!TargetSlot.IsValid())
		{
			Failed.Broadcast();
			SetReadyToDestroy();
			return;
		}
	}
	
	StartedHandle = Subsystem->OnSaveStateStarted.AddUObject(this, &ThisClass::OnSaveStateStarted);
	CompletedHandle = Subsystem->OnSaveStateFinished.AddUObject(this, &ThisClass::OnSaveStateCompleted);
	
	const bool bResult = Subsystem->SaveGameToSlot(TargetSlot);
	if (bResult == false)
	{
		Failed.Broadcast();
		SetReadyToDestroy();
		return;
	}
}

void UAsyncAction_SaveGameToSlot::OnSaveStateStarted(const FPersistentStateSlotHandle& InSlot)
{
	if (TargetSlot == InSlot)
	{
		Started.Broadcast();
	}
}

void UAsyncAction_SaveGameToSlot::OnSaveStateCompleted(const FPersistentStateSlotHandle& InSlot)
{
	if (TargetSlot == InSlot)
	{
		Completed.Broadcast();
		SetReadyToDestroy();
	}
}

void UAsyncAction_SaveGameToSlot::SetReadyToDestroy()
{
	if (UPersistentStateSubsystem* Subsystem = WeakSubsystem.Get())
	{
		Subsystem->OnSaveStateStarted.Remove(StartedHandle);
		Subsystem->OnSaveStateFinished.Remove(CompletedHandle);
	}
	
	Super::SetReadyToDestroy();
}

