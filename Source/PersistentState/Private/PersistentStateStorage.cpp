#include "PersistentStateStorage.h"

#include "PersistentStateModule.h"
#include "PersistentStateStatics.h"
#include "Managers/PersistentStateManager.h"

FPersistentStateSlotHandle FPersistentStateSlotHandle::InvalidHandle;

bool FPersistentStateSlotHandle::IsValid() const
{
	if (SlotName == NAME_None)
	{
		return false;
	}
	
	if (const UPersistentStateStorage* StateStorage = WeakStorage.Get())
	{
		return *this == StateStorage->GetStateSlotByName(SlotName);
	}

	return false;
}
