#pragma once

#include "CoreMinimal.h"
#include "InstancedStruct.h"

#include "PersistentStateManager.generated.h"

USTRUCT()
struct PERSISTENTSTATE_API FPersistentStateBase
{
	GENERATED_BODY()

	/** serialized save game properties */
	UPROPERTY()
	TArray<uint8> SaveGameBunch;

	/** custom state provided via UPersistentStateObject interface */
	UPROPERTY()
	FInstancedStruct InstanceState;
};

/**
 * Base class for State Manager classes - objects that encapsulate both state and logic for a specific game feature
 * Game Managers are controlled by Persistent State subsystem and are bound to its lifetime
 * World Managers are instantiated for every new loaded world
 */
UCLASS(Abstract)
class PERSISTENTSTATE_API UPersistentStateManager: public UObject
{
	GENERATED_BODY()
public:

	virtual void NotifyObjectInitialized(UObject& Object);

	/** */
	virtual void SaveGameState();
};
