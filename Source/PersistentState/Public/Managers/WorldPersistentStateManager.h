#pragma once

#include "CoreMinimal.h"
#include "PersistentStateManager.h"

#include "WorldPersistentStateManager.generated.h"

UCLASS()
class PERSISTENTSTATE_API UWorldPersistentStateManager: public UPersistentStateManager
{
	GENERATED_BODY()
public:
	
	virtual bool ShouldCreateManager(UWorld* InWorld) const;
	virtual void Init(UWorld* InWorld);
	virtual void Cleanup(UWorld* InWorld);

protected:

	UPROPERTY(Transient)
	UWorld* CurrentWorld = nullptr;
};
