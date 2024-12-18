#pragma once

#include "CoreMinimal.h"
#include "InstancedStruct.h"

#include "PersistentStateManager.generated.h"

class UPersistentStateSubsystem;

/**
 * Defines how, when and where persistent manager is saved and loaded
 * Persistent - manager state is loaded once per game launch. Only one persistent save slot exists per game profile.
 * Persistent managers should store globally available player data like meta progression, achievements, options and more.
 * 
 * Game - manager is saved to a global GAME state independently from a world state. Game state can be reloaded if user
 * switches states, so underlying game functionality should be aware of that and react accordingly. Game managers
 * are recreated if user has decided to load a different save slot.
 * Game managers should store game data that has to be available from multiple game worlds.
 * 
 * World - manager is saved to a world state. Save slot may contain data for multiple worlds. World managers are recreated
 * every time active world changes - LoadMap calls, LoadGame call, etc.
 * World managers should store state of the world - actors, streamed in levels, loaded data layers, world subsystems and more.
 *
 */
UENUM()
enum class EManagerStorageType: uint8
{
	Persistent = 1,
	Game = 2,
	World = 4,
	/** any manager type */
	All = 255,
};
/** Defined to indicate ALL storage type when iterating managers */
ENUM_CLASS_FLAGS(EManagerStorageType);

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

	EManagerStorageType GetManagerType() const { return ManagerType; }

	virtual UWorld* GetWorld() const override;

	/** */
	virtual bool ShouldCreateManager(UPersistentStateSubsystem& InSubsystem) const;
	/** */
	virtual void Init(UPersistentStateSubsystem& InSubsystem);
	/** */
	virtual void Cleanup(UPersistentStateSubsystem& InSubsystem);
	/** */
	virtual void NotifyObjectInitialized(UObject& Object);
	/** */
	virtual void SaveState();

protected:
	
	/** @return owning subsystem */
	UPersistentStateSubsystem* GetStateSubsystem() const;

	/** manager type */
	EManagerStorageType ManagerType = EManagerStorageType::World;
};
