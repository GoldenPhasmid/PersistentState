#include "PersistentStateActorComponent.h"

#include "PersistentStateInterface.h"

UPersistentStateActorComponent::UPersistentStateActorComponent(const FObjectInitializer& Initializer): Super(Initializer)
{
	bWantsInitializeComponent = true;
}

void UPersistentStateActorComponent::InitializeComponent()
{
	Super::InitializeComponent();

	IPersistentStateObject::NotifyObjectInitialized(*this);
}
