#pragma once

#include "CoreMinimal.h"
#include "ModularGameMode.h"
#include "PersistentStateInterface.h"

#include "PersistentStateGameMode.generated.h"

UCLASS()
class PERSISTENTSTATEGAMEFRAMEWORK_API APersistentStateGameModeBase: public AModularGameModeBase, public IPersistentStateObject
{
	GENERATED_BODY()
public:
	APersistentStateGameModeBase(const FObjectInitializer& Initializer);

	virtual void PostInitializeComponents() override;
	virtual FName GetStableName() const override { return GetClass()->GetFName(); }
};

UCLASS()
class PERSISTENTSTATEGAMEFRAMEWORK_API APersistentStateGameMode: public AModularGameMode, public IPersistentStateObject
{
	GENERATED_BODY()
public:
	APersistentStateGameMode(const FObjectInitializer& Initializer);

	virtual void PostInitializeComponents() override;
	virtual FName GetStableName() const override { return GetClass()->GetFName(); }
};