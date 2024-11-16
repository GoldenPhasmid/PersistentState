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
