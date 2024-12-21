#pragma once

#include "CoreMinimal.h"
#include "ModularActor.h"
#include "PersistentStateInterface.h"
#include "GameFramework/Actor.h"

#include "PersistentStateActorBase.generated.h"

UCLASS()
class PERSISTENTSTATEGAMEFRAMEWORK_API APersistentStateActorBase: public AModularActor, public IPersistentStateObject
{
	GENERATED_BODY()
public:
	virtual void PostInitializeComponents() override;
};
