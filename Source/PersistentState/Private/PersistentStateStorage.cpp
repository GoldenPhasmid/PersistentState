#include "PersistentStateStorage.h"

#include "PersistentStateDefines.h"
#include "PersistentStateStatics.h"
#include "Managers/PersistentStateManager.h"

FPersistentStateSlotHandle FPersistentStateSlotHandle::InvalidHandle;

FPersistentStateSlotSharedRef FPersistentStateSlotHandle::GetSlot() const
{
	if (const UPersistentStateStorage* Storage = WeakStorage.Get())
	{
		return Storage->GetStateSlot(*this);
	}

	return nullptr;
}
