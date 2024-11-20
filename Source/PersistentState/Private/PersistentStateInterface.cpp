#include "PersistentStateInterface.h"

#include "PersistentStateSubsystem.h"

void IPersistentStateObject::NotifyObjectInitialized(UObject& This)
{
	if (UPersistentStateSubsystem* Subsystem = UPersistentStateSubsystem::Get(&This))
	{
		Subsystem->NotifyObjectInitialized(This);
	}
}

bool IPersistentStateWorldSettings::ShouldStoreWorldState(AWorldSettings& WorldSettings)
{
	return !WorldSettings.Implements<UPersistentStateWorldSettings>() || CastChecked<IPersistentStateWorldSettings>(&WorldSettings)->ShouldStoreWorldState();
}
