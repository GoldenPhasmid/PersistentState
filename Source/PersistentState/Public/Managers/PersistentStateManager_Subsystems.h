#pragma once

#include "CoreMinimal.h"
#include "PersistentStateManager.h"
#include "PersistentStateObjectId.h"

#include "PersistentStateManager_Subsystems.generated.h"

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
class UPersistentStateManager_Subsystems: public UPersistentStateManager
{
	GENERATED_BODY()
public:
	UPersistentStateManager_Subsystems();
	
	virtual void SaveState() override;

protected:

	void LoadGameState(TConstArrayView<USubsystem*> Subsystems);

	UPROPERTY()
	TArray<FSubsystemPersistentState> SubsystemState;
};

/**
 * 
 */
UCLASS()
class UPersistentStateManager_WorldSubsystems: public UPersistentStateManager_Subsystems
{
	GENERATED_BODY()
public:
	UPersistentStateManager_WorldSubsystems();
	
	virtual void NotifyActorsInitialized() override;
};

/**
 * 
 */
UCLASS()
class UPersistentStateManager_GameInstanceSubsystems: public UPersistentStateManager_Subsystems
{
	GENERATED_BODY()
public:
	UPersistentStateManager_GameInstanceSubsystems();
	
	virtual void NotifyActorsInitialized() override;
};

/**
 * 
 */
UCLASS()
class UPersistentStateManager_PlayerSubsystems: public UPersistentStateManager_Subsystems
{
	GENERATED_BODY()
public:
	UPersistentStateManager_PlayerSubsystems();
	
	virtual void NotifyActorsInitialized() override;

protected:
	void HandleLocalPlayerAdded(ULocalPlayer* LocalPlayer);
	void LoadPrimaryPlayer(ULocalPlayer* LocalPlayer);
};



