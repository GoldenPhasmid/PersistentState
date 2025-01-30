#pragma once

#include "CoreMinimal.h"
#include "InstancedStruct.h"

#include "PersistentStateManager.generated.h"

class UPersistentStateSubsystem;

/**
 * Defines how, when and where persistent manager is saved and loaded
 * Profile - manager state is loaded once per game launch. Stored per user game profile, separately from any game/world save data.
 * Persistent managers should store globally available player data like meta progression, achievements, options and more.
 * 
 * Game - manager is saved to a global GAME state independently from a world state. Game state can be reloaded if user
 * switches save slot, so underlying game functionality should be aware of that and react accordingly. Game managers
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
	None = 0,
	Profile = 1,
	Game = 2,
	World = 4,
	/** any manager type */
	All = 255,
};
/** Defined to use ALL storage type when iterating managers */
ENUM_CLASS_FLAGS(EManagerStorageType);

/**
 * Base struct that represents object state struct
 */
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
 * Struct that represents a serialized property bunch for a single object
 */
USTRUCT()
struct FPersistentStatePropertyBunch
{
	GENERATED_BODY()
public:

#if WITH_STRUCTURED_SERIALIZATION
	bool Serialize(FStructuredArchive::FSlot Slot);
#endif
	FORCEINLINE bool IsEmpty() const { return Value.IsEmpty(); }
	FORCEINLINE typename TArray<uint8>::SizeType Num() const { return Value.Num(); }
	FORCEINLINE SIZE_T GetAllocatedSize() const { return Value.GetAllocatedSize(); }
	FORCEINLINE friend bool operator==(const FPersistentStatePropertyBunch& A, const FPersistentStatePropertyBunch& B)
	{
		return A.Value.Num() == B.Value.Num() && FMemory::Memcmp(A.Value.GetData(), B.Value.GetData(), A.Value.Num()) == 0;
	}
	
	UPROPERTY()
	TArray<uint8> Value;
};

#if WITH_STRUCTURED_SERIALIZATION
template <>
struct TStructOpsTypeTraits<FPersistentStatePropertyBunch> : public TStructOpsTypeTraitsBase2<FPersistentStatePropertyBunch>
{
	enum
	{
		WithStructuredSerializer = true,
	};
};
#endif

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

	UGameInstance* GetGameInstance() const { return GetTypedOuter<UGameInstance>(); }
	EManagerStorageType GetManagerType() const { return ManagerType; }
	
	virtual UWorld* GetWorld() const override;
	/** Called on CDO to check that manager has to be created for a given state system */
	virtual bool ShouldCreateManager(UPersistentStateSubsystem& InSubsystem) const;
	/** Called on manager instance right after creation */
	virtual void Init(UPersistentStateSubsystem& InSubsystem);
	/** Called on manager instance right before destruction */
	virtual void Cleanup(UPersistentStateSubsystem& InSubsystem);
	/** Save manager instance state for further serialization */
	virtual void SaveState();
	/** called before state manager data is loaded */
	virtual void PreLoadState();
	/** called after state manager data is loaded */
	virtual void PostLoadState();
	/** @return size of dynamically allocated memory stored in the manager state */
	virtual uint32 GetAllocatedSize() const;
	/** update stats */
	virtual void UpdateStats() const;

	// Manage callbacks for world-related events
	
	/** Notify that @Object has been initialized by the game code and is ready to save/load its state */
	virtual void NotifyObjectInitialized(UObject& Object);
	/** Notify that world has been initialized */
	virtual void NotifyWorldInitialized();
	/** Notify that @RouteActorInitialize has been called on always-loaded levels and world is ready to begin play */
	virtual void NotifyActorsInitialized();
	/** Notify world is being destroyed */
	virtual void NotifyWorldCleanup();
protected:
	
	/** @return owning subsystem */
	UPersistentStateSubsystem* GetStateSubsystem() const;

	/** manager type */
	EManagerStorageType ManagerType = EManagerStorageType::World;
};
