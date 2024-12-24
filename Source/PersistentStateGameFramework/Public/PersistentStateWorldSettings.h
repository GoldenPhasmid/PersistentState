#pragma once

#include "CoreMinimal.h"
#include "PersistentStateInterface.h"
#include "GameFramework/WorldSettings.h"

#include "PersistentStateWorldSettings.generated.h"

UCLASS()
class APersistentStateWorldSettings: public AWorldSettings, public IPersistentStateWorldSettings
{
	GENERATED_BODY()
public:
	
	virtual bool ShouldStoreWorldState() const override { return bShouldStoreWorldState; }

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bShouldStoreWorldState = true;
};
