#include "PersistentStateInterface.h"

#include "PersistentStateSubsystem.h"

void IPersistentStateObject::NotifyInitialized(UObject& This)
{
	if (UPersistentStateSubsystem* Subsystem = UPersistentStateSubsystem::Get(&This))
	{
		Subsystem->NotifyInitialized(This);
	}
}
