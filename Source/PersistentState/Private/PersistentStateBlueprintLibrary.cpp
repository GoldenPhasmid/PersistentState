#include "PersistentStateBlueprintLibrary.h"

#include "PersistentStateObjectId.h"
#include "PersistentStateSettings.h"
#include "PersistentStateSlotView.h"
#include "PersistentStateSubsystem.h"

FPersistentStateSlotHandle UPersistentStateBlueprintLibrary::GetActiveSaveGameSlot(const UObject* WorldContextObject)
{
	if (UPersistentStateSubsystem* Subsystem = UPersistentStateSubsystem::Get(WorldContextObject))
	{
		return Subsystem->GetActiveSaveGameSlot();
	}

	return {};
}

UPersistentStateSlotDescriptor* UPersistentStateBlueprintLibrary::GetSaveGameSlotDescriptor(const UObject* WorldContextObject, const FPersistentStateSlotHandle& Slot)
{
	if (UPersistentStateSubsystem* Subsystem = UPersistentStateSubsystem::Get(WorldContextObject))
	{
		return Subsystem->GetSaveGameSlotDescriptor(Slot);
	}

	return nullptr;
}

void UPersistentStateBlueprintLibrary::CaptureScreenshot(const UObject* WorldContextObject, const FPersistentStateSlotHandle& SaveGameSlot)
{
	if (UPersistentStateSubsystem* Subsystem = UPersistentStateSubsystem::Get(WorldContextObject))
	{
		Subsystem->CaptureScreenshotForSlot(SaveGameSlot);
	}
}

bool UPersistentStateBlueprintLibrary::HasScreenshotSupport()
{
	return UPersistentStateSettings::Get()->bCaptureScreenshot;
}

bool UPersistentStateBlueprintLibrary::SlotHandle_IsValid(const FPersistentStateSlotHandle& SlotHandle)
{
	return SlotHandle.IsValid();
}

void UPersistentStateBlueprintLibrary::ObjectId_Reset(FPersistentStateObjectId& ObjectId)
{
	ObjectId.Reset();
}

bool UPersistentStateBlueprintLibrary::ObjectId_IsValid(const FPersistentStateObjectId& ObjectId)
{
	return ObjectId.IsValid();
}

bool UPersistentStateBlueprintLibrary::ObjectId_IsStale(const FPersistentStateObjectId& ObjectId)
{
	return ObjectId.ResolveObject() == nullptr;
}

bool UPersistentStateBlueprintLibrary::ObjectId_IsAlive(const FPersistentStateObjectId& ObjectId)
{
	return ObjectId.ResolveObject() != nullptr;
}

bool UPersistentStateBlueprintLibrary::ObjectId_IsStatic(const FPersistentStateObjectId& ObjectId)
{
	return ObjectId.IsStatic();
}

bool UPersistentStateBlueprintLibrary::ObjectId_IsDynamic(const FPersistentStateObjectId& ObjectId)
{
	return ObjectId.IsDynamic();
}

FPersistentStateObjectId UPersistentStateBlueprintLibrary::ObjectId_FindObjectId(const UObject* Object)
{
	return FPersistentStateObjectId::FindObjectId(Object);
}

FPersistentStateObjectId UPersistentStateBlueprintLibrary::ObjectId_CreateObjectId(const UObject* Object)
{
	return FPersistentStateObjectId::CreateObjectId(Object);
}

UObject* UPersistentStateBlueprintLibrary::ObjectId_ResolveObject(const FPersistentStateObjectId& ObjectId)
{
	return ObjectId.ResolveObject();
}

UObject* UPersistentStateBlueprintLibrary::ObjectId_ResolveObjectWithClass(const FPersistentStateObjectId& ObjectId, TSubclassOf<UObject> ObjectClass)
{
	return ObjectId.ResolveObject();
}
