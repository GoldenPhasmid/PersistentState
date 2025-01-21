#include "Managers/PersistentStateManager.h"

#include "PersistentStateSerialization.h"
#include "PersistentStateSubsystem.h"

#if WITH_STRUCTURED_SERIALIZATION
bool FPersistentStatePropertyBunch::Serialize(FStructuredArchive::FSlot Slot)
{
	FStructuredArchive::FRecord Record = Slot.EnterRecord();
	FArchive& Ar = Slot.GetUnderlyingArchive();

	bool bIsTextBased = FPersistentStateFormatter::IsTextBased();
	Record << SA_VALUE(TEXT("IsTextBased"), bIsTextBased);
	if (!bIsTextBased)
	{
		Record << SA_VALUE(TEXT("Value"), Value);
	}
	else
	{
		// read for a text-based save game bunch is not supported
		check(Ar.IsSaving());
		if (!Value.IsEmpty())
		{
			const ANSICHAR* Str = reinterpret_cast<ANSICHAR*>(Value.GetData());
			FString ValueStr = ANSI_TO_TCHAR(Str);
		
			Record << SA_VALUE(TEXT("Value"), Value);
		}
	}
	
	return true;
}
#endif

UWorld* UPersistentStateManager::GetWorld() const
{
	return GetTypedOuter<UPersistentStateSubsystem>()->GetWorld();
}

bool UPersistentStateManager::ShouldCreateManager(UPersistentStateSubsystem& InSubsystem) const
{
	// override in derived classes
	return true;
}

void UPersistentStateManager::Init(UPersistentStateSubsystem& InSubsystem)
{
	// override in derived classes
}

void UPersistentStateManager::Cleanup(UPersistentStateSubsystem& InSubsystem)
{
	// override in derived classes
}

void UPersistentStateManager::NotifyObjectInitialized(UObject& Object)
{
	// override in derived classes
}

void UPersistentStateManager::NotifyWorldInitialized()
{
	// override in derived classes
}

void UPersistentStateManager::NotifyActorsInitialized()
{
	// override in derived classes
}

void UPersistentStateManager::NotifyWorldCleanup()
{
	// override in derived classes
}

void UPersistentStateManager::SaveState()
{
	// override in derived classes
}

void UPersistentStateManager::PreLoadState()
{
	// override in derived classes
}

void UPersistentStateManager::PostLoadState()
{
	// override in derived classes
}

uint32 UPersistentStateManager::GetAllocatedSize() const
{
	// override in derived classes
	return GetClass()->GetStructureSize();
}

void UPersistentStateManager::UpdateStats() const
{
	// override in derived classes
}

UPersistentStateSubsystem* UPersistentStateManager::GetStateSubsystem() const
{
	return GetTypedOuter<UPersistentStateSubsystem>();
}
