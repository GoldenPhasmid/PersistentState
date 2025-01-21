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

	FORCEINLINE uint32 GetAllocatedSize() const { return SaveGameBunch.GetAllocatedSize(); }

	void Load();
	void Save();

	UPROPERTY(meta = (AlwaysLoaded))
	bool bStateSaved = false;
	
	UPROPERTY(meta = (AlwaysLoaded))
	FPersistentStateObjectId Handle;
	
	/** serialized save game properties */
	UPROPERTY()
	FPersistentStatePropertyBunch SaveGameBunch;
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

	//~Begin PersistentStateManager interface
	virtual void SaveState() override;
	virtual void UpdateStats() const override;
	virtual uint32 GetAllocatedSize() const override;
	//~End PersistentStateManager interface
	
protected:

	void LoadGameState(TConstArrayView<USubsystem*> SubsystemArray);

	UPROPERTY(meta = (AlwaysLoaded))
	TArray<FSubsystemPersistentState> Subsystems;
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



