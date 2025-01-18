#pragma once

#include "CoreMinimal.h"
#include "ModularGameState.h"
#include "PersistentStateInterface.h"

#include "PersistentStateGameState.generated.h"

UCLASS()
class PERSISTENTSTATEGAMEFRAMEWORK_API APersistentStateGameStateBase: public AModularGameStateBase, public IPersistentStateObject
{
	GENERATED_BODY()
public:
	virtual void PostInitializeComponents() override;
	virtual FName GetStableName() const override { return GetClass()->GetFName(); }
};

UCLASS()
class PERSISTENTSTATEGAMEFRAMEWORK_API APersistentStateGameState: public AModularGameState, public IPersistentStateObject
{
	GENERATED_BODY()
public:
	virtual void PostInitializeComponents() override;
	virtual FName GetStableName() const override { return GetClass()->GetFName(); }
};
