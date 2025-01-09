#include "Actions/AsyncAction_LoadSaveGameScreenshot.h"

#include "PersistentStateSubsystem.h"

UAsyncAction_LoadSaveGameScreenshot* UAsyncAction_LoadSaveGameScreenshot::LoadSaveGameScreenshot(UWorld* WorldContextObject, const FPersistentStateSlotHandle& Slot)
{
	UPersistentStateSubsystem* Subsystem = UPersistentStateSubsystem::Get(WorldContextObject);
	if (Subsystem == nullptr)
	{
		return nullptr;
	}

	UAsyncAction_LoadSaveGameScreenshot* Action = NewObject<UAsyncAction_LoadSaveGameScreenshot>();
	Action->WeakSubsystem = Subsystem;
	Action->TargetSlot = Slot;
	Action->RegisterWithGameInstance(Subsystem->GetGameInstance());

	return Action;
}

void UAsyncAction_LoadSaveGameScreenshot::Activate()
{
	Super::Activate();

	UPersistentStateSubsystem* Subsystem = WeakSubsystem.Get();
	if (Subsystem == nullptr)
	{
		OnFailed.Broadcast(nullptr);
		SetReadyToDestroy();
		return;
	}

	if (!TargetSlot.IsValid())
	{
		OnFailed.Broadcast(nullptr);
		SetReadyToDestroy();
		return;
	}

	Subsystem->LoadScreenshotFromSlot(TargetSlot, [WeakThis=TWeakObjectPtr<ThisClass>{this}](UTexture2DDynamic* ScreenshotTexture)
	{
		if (ThisClass* This = WeakThis.Get())
		{
			This->OnLoadCompleted(ScreenshotTexture);
		}
	});
}

void UAsyncAction_LoadSaveGameScreenshot::SetReadyToDestroy()
{
	Super::SetReadyToDestroy();
}

void UAsyncAction_LoadSaveGameScreenshot::OnLoadCompleted(UTexture2DDynamic* Texture)
{
	if (Texture != nullptr)
	{
		OnLoaded.Broadcast(Texture);
	}
	else
	{
		OnFailed.Broadcast(Texture);
	}
	
	SetReadyToDestroy();
}
