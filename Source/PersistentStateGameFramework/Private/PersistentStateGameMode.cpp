#include "PersistentStateGameMode.h"

#include "PersistentStateGameState.h"

APersistentStateGameModeBase::APersistentStateGameModeBase(const FObjectInitializer& Initializer): Super(Initializer)
{
	GameStateClass = APersistentStateGameStateBase::StaticClass();
}

void APersistentStateGameModeBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();

	IPersistentStateObject::NotifyObjectInitialized(*this);
}

APersistentStateGameMode::APersistentStateGameMode(const FObjectInitializer& Initializer): Super(Initializer)
{
	GameStateClass = APersistentStateGameState::StaticClass();
}

void APersistentStateGameMode::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	
	IPersistentStateObject::NotifyObjectInitialized(*this);
}
