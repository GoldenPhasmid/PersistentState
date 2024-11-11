#pragma once

#include "CoreMinimal.h"
#include "PersistentStateInterface.h"
#include "PersistentStateManager.h"

#include "GamePersistentStateManager.generated.h"

struct FInstancedStruct;

UCLASS(Abstract)
class PERSISTENTSTATE_API UGamePersistentStateManager: public UPersistentStateManager
{
	GENERATED_BODY()
public:

	virtual void Init(UGameInstance* InGameInstance);
	virtual void Cleanup(UGameInstance* InGameInstance);
	
protected:
	
	UPROPERTY(Transient)
	UGameInstance* GameInstance = nullptr;
};
