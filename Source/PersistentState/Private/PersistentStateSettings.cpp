#include "PersistentStateSettings.h"

#include "PersistentStateSlotStorage.h"
#include "PersistentStateStorage.h"

UPersistentStateSettings::UPersistentStateSettings(const FObjectInitializer& Initializer): Super(Initializer)
{
	StateStorageClass = UPersistentStateSlotStorage::StaticClass();
}
