#pragma once

#include "CoreMinimal.h"
#include "PersistentStateInterface.h"
#include "Components/GameFrameworkComponent.h"

#include "PersistentStateActorComponent.generated.h"

UCLASS()
class PERSISTENTSTATEGAMEFRAMEWORK_API UPersistentStateActorComponent: public UGameFrameworkComponent, public IPersistentStateObject
{
	GENERATED_BODY()
public:
	UPersistentStateActorComponent(const FObjectInitializer& Initializer);
	
	virtual void InitializeComponent() override;
};
