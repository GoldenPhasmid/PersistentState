#pragma once

#include "CoreMinimal.h"
#include "PersistentStateInterface.h"
#include "PersistentStateManager.h"

#include "WorldPersistentStateManager.generated.h"

UCLASS()
class PERSISTENTSTATE_API UWorldPersistentStateManager: public UPersistentStateManager
{
	GENERATED_BODY()
public:
	virtual void Init(UWorld* InWorld);
	virtual void Cleanup(UWorld* InWorld);

protected:

	UPROPERTY(Transient)
	UWorld* CurrentWorld = nullptr;
};
