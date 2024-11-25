#pragma once

#include "CoreMinimal.h"
#include "InstancedStruct.h"

#include "PersistentStateManager.generated.h"

class UPersistentStateSubsystem;

UENUM()
enum class EPersistentStateManagerType: uint8
{
	/**
	 * 
	 */
	Persistent = 1,
	/**
	 * 
	 */
	Game = 2,
	/**
	 * 
	 */
	World = 4,
	/** any manager type */
	All = 255,
};
ENUM_CLASS_FLAGS(EPersistentStateManagerType);

USTRUCT()
struct PERSISTENTSTATE_API FPersistentStateBase
{
	GENERATED_BODY()
public:

	/** custom state provided via UPersistentStateObject interface */
	UPROPERTY()
	FInstancedStruct InstanceState;
};

/**
 * Base class for State Manager classes - objects that encapsulate both state and logic for a specific game feature
 * Game Managers are controlled by Persistent State subsystem and are bound to its lifetime
 * World Managers are instantiated for every new loaded world
 */
UCLASS(Abstract)
class PERSISTENTSTATE_API UPersistentStateManager: public UObject
{
	GENERATED_BODY()
public:

	EPersistentStateManagerType GetManagerType() const { return ManagerType; }

	virtual UWorld* GetWorld() const override;
	virtual bool ShouldCreateManager(UPersistentStateSubsystem& InSubsystem) const;
	virtual void Init(UPersistentStateSubsystem& InSubsystem);
	virtual void Cleanup(UPersistentStateSubsystem& InSubsystem);
	virtual void NotifyObjectInitialized(UObject& Object);
	/** */
	virtual void SaveState();

	/** manager type */
	EPersistentStateManagerType ManagerType = EPersistentStateManagerType::World;
};
