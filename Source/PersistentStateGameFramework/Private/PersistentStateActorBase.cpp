#include "PersistentStateActorBase.h"

#include "PersistentStateInterface.h"

void APersistentStateActorBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	IPersistentStateObject::NotifyObjectInitialized(*this);
}
