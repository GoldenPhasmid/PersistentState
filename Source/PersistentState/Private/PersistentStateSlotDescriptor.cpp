#include "PersistentStateSlotDescriptor.h"

void UPersistentStateSlotDescriptor::SaveDescriptor(UWorld* World, const FPersistentStateSlotHandle& InHandle)
{
	SlotHandle = InHandle;
	
	OnSaveDescriptor(World);
	K2_OnSaveDescriptor(World);
}

void UPersistentStateSlotDescriptor::LoadDescriptor(UWorld* World, const FPersistentStateSlotHandle& InHandle, const FPersistentStateSlotDesc& InDesc)
{
	SlotDescription = InDesc;
	
	OnLoadDescriptor(World);
	K2_OnLoadDescriptor(World);
}

void UPersistentStateSlotDescriptor::OnSaveDescriptor(UWorld* World)
{
	
}

void UPersistentStateSlotDescriptor::OnLoadDescriptor(UWorld* World)
{
	
}

FName UPersistentStateSlotDescriptor::GetWorldToLoad_Implementation() const
{
	return FName{SlotDescription.LastSavedWorld};
}

FString UPersistentStateSlotDescriptor::DescribeStateSlot_Implementation() const
{
	return SlotDescription.ToString();
}
