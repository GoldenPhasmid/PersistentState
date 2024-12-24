#include "PersistentStateInterface.h"

#include "PersistentStateSubsystem.h"

void IPersistentStateObject::NotifyObjectInitialized(UObject& This)
{
	if (UPersistentStateSubsystem* Subsystem = UPersistentStateSubsystem::Get(&This))
	{
		Subsystem->NotifyObjectInitialized(This);
	}
}

bool IPersistentStateWorldStateController::ShouldStoreWorldState(AWorldSettings& WorldSettings)
{
	return !WorldSettings.Implements<UPersistentStateWorldStateController>() || CastChecked<IPersistentStateWorldStateController>(&WorldSettings)->ShouldStoreWorldState();
}
