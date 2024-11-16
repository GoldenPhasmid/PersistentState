#pragma once

#include "CoreMinimal.h"

#include "PersistentStateManager.generated.h"

UCLASS(Abstract)
class PERSISTENTSTATE_API UPersistentStateManager: public UObject
{
	GENERATED_BODY()
public:

	virtual void NotifyObjectInitialized(UObject& Object);

	virtual void Tick(float DeltaTime);

	/** */
	virtual void SaveGameState();
};
