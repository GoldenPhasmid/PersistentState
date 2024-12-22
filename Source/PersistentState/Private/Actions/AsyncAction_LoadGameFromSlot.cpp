#include "Actions/AsyncAction_LoadGameFromSlot.h"

#include "PersistentStateSubsystem.h"

UAsyncAction_LoadGameFromSlot* UAsyncAction_LoadGameFromSlot::LoadGame(const UObject* WorldContextObject, const TSoftObjectPtr<UWorld>& World, const FString& TravelOptions)
{
	UPersistentStateSubsystem* Subsystem = UPersistentStateSubsystem::Get(WorldContextObject);
	if (Subsystem == nullptr)
	{
		return nullptr;
	}

	UAsyncAction_LoadGameFromSlot* Action = NewObject<UAsyncAction_LoadGameFromSlot>();
	Action->WeakSubsystem = Subsystem;
	Action->TargetWorld = World;
	Action->TravelOptions = TravelOptions;
	Action->RegisterWithGameInstance(Subsystem->GetGameInstance());

	return Action;
}

UAsyncAction_LoadGameFromSlot* UAsyncAction_LoadGameFromSlot::LoadGameFromSlot(const UObject* WorldContextObject, const FPersistentStateSlotHandle& TargetSlot, const TSoftObjectPtr<UWorld>& World, const FString& TravelOptions)
{
	UPersistentStateSubsystem* Subsystem = UPersistentStateSubsystem::Get(WorldContextObject);
	if (Subsystem == nullptr)
	{
		return nullptr;
	}

	UAsyncAction_LoadGameFromSlot* Action = NewObject<UAsyncAction_LoadGameFromSlot>();
	Action->WeakSubsystem = Subsystem;
	Action->TargetSlot = TargetSlot;
	Action->TargetWorld = World;
	Action->TravelOptions = TravelOptions;
	Action->RegisterWithGameInstance(Subsystem->GetGameInstance());

	return Action;
}

void UAsyncAction_LoadGameFromSlot::Activate()
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

	StartedHandle = Subsystem->OnLoadStateStarted.AddUObject(this, &ThisClass::OnLoadStateStarted);
	CompletedHandle = Subsystem->OnLoadStateFinished.AddUObject(this, &ThisClass::OnLoadStateFinished);
	
	const bool bResult = Subsystem->LoadGameWorldFromSlot(TargetSlot, TargetWorld, TravelOptions);
	if (bResult == false)
	{
		Failed.Broadcast();
		SetReadyToDestroy();
		return;
	}
}

void UAsyncAction_LoadGameFromSlot::SetReadyToDestroy()
{
	if (UPersistentStateSubsystem* Subsystem = WeakSubsystem.Get())
	{
		Subsystem->OnLoadStateStarted.Remove(StartedHandle);
		Subsystem->OnLoadStateFinished.Remove(CompletedHandle);
	}
	
	Super::SetReadyToDestroy();
}

void UAsyncAction_LoadGameFromSlot::OnLoadStateStarted(const FPersistentStateSlotHandle& InSlot)
{
	if (TargetSlot == InSlot)
	{
		Started.Broadcast();
	}
}

void UAsyncAction_LoadGameFromSlot::OnLoadStateFinished(const FPersistentStateSlotHandle& InSlot)
{
	if (TargetSlot == InSlot)
	{
		Completed.Broadcast();
		SetReadyToDestroy();
	}
}
