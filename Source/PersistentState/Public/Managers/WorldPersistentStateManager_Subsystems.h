#pragma once

#include "CoreMinimal.h"
#include "PersistentStateManager.h"
#include "PersistentStateObjectId.h"

#include "WorldPersistentStateManager_Subsystems.generated.h"

struct FPersistentStateObjectId;

USTRUCT()
struct PERSISTENTSTATE_API FSubsystemPersistentState: public FPersistentStateBase
{
	GENERATED_BODY()
public:
	
	FSubsystemPersistentState() = default;
	explicit FSubsystemPersistentState(const USubsystem* Subsystem);
	explicit FSubsystemPersistentState(const FPersistentStateObjectId& InHandle)
		: Handle(InHandle)
	{}

	void Load();
	void Save();

	UPROPERTY(meta = (AlwaysLoaded))
	bool bStateSaved = false;
	
	UPROPERTY(meta = (AlwaysLoaded))
	FPersistentStateObjectId Handle;
	
	/** serialized save game properties */
	UPROPERTY()
	TArray<uint8> SaveGameBunch;
};

FORCEINLINE bool operator==(const FSubsystemPersistentState& State, const FPersistentStateObjectId& Handle)
{
	return State.Handle == Handle;
}

/**
 * 
 */
UCLASS(Abstract)
class UWorldPersistentStateManager_Subsystems: public UPersistentStateManager
{
	GENERATED_BODY()
public:
	UWorldPersistentStateManager_Subsystems();
	
	virtual void Init(UPersistentStateSubsystem& InSubsystem) override;
	virtual void SaveState() override;
	
	virtual TArrayView<USubsystem*> GetSubsystems(UPersistentStateSubsystem& InSubsystem) const
	{
		return {};
	}

protected:

	UPROPERTY()
	TArray<FSubsystemPersistentState> Subsystems;
};

