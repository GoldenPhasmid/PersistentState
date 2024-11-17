#pragma once

#include "CoreMinimal.h"

#include "PersistentStateSettings.generated.h"

class UPersistentStateStorage;

USTRUCT(BlueprintType)
struct PERSISTENTSTATE_API FPersistentSlotEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, meta = (Validate))
	FName SlotName;

	UPROPERTY(EditAnywhere, meta = (Validate))
	FText Title;
};

UCLASS(Config = Game, DefaultConfig)
class PERSISTENTSTATE_API UPersistentStateSettings: public UDeveloperSettings
{
	GENERATED_BODY()
public:
	UPersistentStateSettings(const FObjectInitializer& Initializer);

	static UPersistentStateSettings* GetMutable()
	{
		return GetMutableDefault<UPersistentStateSettings>();
	}
	
	static const UPersistentStateSettings* Get()
	{
		return GetDefault<UPersistentStateSettings>();
	}

	/** If false, fully disables persistent state subsystem */
	UPROPERTY(EditAnywhere, Config)
	bool bEnabled = true;

	/** state storage implementation used by state subsystem */
	UPROPERTY(EditAnywhere, Config, meta = (Validate))
	TSubclassOf<UPersistentStateStorage> StateStorageClass;

	/** a list of default slots that "should" be created at the start of the game by storage implementation */
	UPROPERTY(EditAnywhere, Config, meta = (Validate))
	TArray<FPersistentSlotEntry> DefaultSlots;

	UPROPERTY(EditAnywhere, Config)
	FName StartupSlotName = NAME_None;
};
