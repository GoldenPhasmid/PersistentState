#pragma once

#include "CoreMinimal.h"
#include "InstancedStruct.h"
#include "PersistentStateObjectId.h"
#include "PersistentStateStatics.h"
#include "PersistentStateSlot.h"
#include "WorldPersistentStateManager.h"

#include "LevelPersistentStateManager.generated.h"

class ULevelPersistentStateManager;

USTRUCT()
struct FUniqueObjectId
{
	GENERATED_BODY()
	
	FUniqueObjectId() = default;
	FUniqueObjectId(const FUniqueObjectGuid& InId)
		: Id(InId)
	{}
	FUniqueObjectId(const FGuid& Guid)
		: Id(Guid)
	{}

	/** custom serialization for unique object id */
	bool Serialize(FArchive& Ar)
	{
		Ar << Id;
		return true;
	}
	
	bool operator==(const FUniqueObjectId& Other) const
	{
		return Id == Other.Id;
	}

	FUniqueObjectGuid GetUniqueID() const { return Id; }
	bool IsValid() const { return !Id.IsDefault(); }

private:
	FUniqueObjectGuid Id;
};

template <>
struct TIsPODType<FUniqueObjectId>
{
	enum { Value = true };
};

template <>
struct TStructOpsTypeTraits<FUniqueObjectId>: public TStructOpsTypeTraitsBase2<FUniqueObjectId>
{
	enum
	{
		WithSerializer = true,
		WithIdenticalViaEquality = true
	};
};

USTRUCT()
struct PERSISTENTSTATE_API FComponentPersistentState
{
	GENERATED_BODY()
public:
	FComponentPersistentState() = default;
	FComponentPersistentState(UActorComponent* Component, FPersistentStateObjectId ComponentId, bool bStatic);

	void InitWithStaticComponent(UActorComponent* Component, FPersistentStateObjectId ComponentId) const;
	void InitWithDynamicComponent(UActorComponent* Component, FPersistentStateObjectId ComponentId) const;
	UActorComponent* CreateDynamicComponent(AActor* OwnerActor) const;

	void LoadComponent(ULevelPersistentStateManager& StateManager);
	void SaveComponent(ULevelPersistentStateManager& StateManager);

	FPersistentStateObjectId GetComponentId() const;
	FORCEINLINE bool IsStatic() const { return bComponentStatic; }
	FORCEINLINE bool IsDynamic() const { return !bComponentStatic; }
	FORCEINLINE bool IsInitialized() const { return bStateInitialized; }
	FORCEINLINE bool IsSaved() const { return bComponentSaved; }
	FORCEINLINE bool IsOutdated() const { return bComponentSaved && !ComponentClass.IsNull() && ComponentClass.Get() == nullptr; }
#if WITH_COMPONENT_CUSTOM_SERIALIZE
	friend FArchive& operator<<(FArchive& Ar, const FComponentPersistentState& Value);
	bool Serialize(FArchive& Ar);
#endif
	
	/**
	 * guid created at runtime for a given component
	 * for static components guid is derived from stable package path
	 * for dynamic components (e.g. created at runtime), guid is created on a fly and kept between laods
	 */
	UPROPERTY(meta = (AlwaysLoaded))
	mutable FPersistentStateObjectId WeakComponent;
	
	/** component attachment data, always relevant for dynamic components. Can be stored for static components in case their attachment changed at runtime */
	// @todo: component class should be pre-loaded by some smart loading system
    UPROPERTY()
    TSoftClassPtr<UActorComponent> ComponentClass;
    	
	/** relative component transform, valid for scene components */
	UPROPERTY()
	FTransform ComponentTransform;

	/** */
	UPROPERTY()
	FPersistentStateObjectId AttachParentId;

	/** */
	UPROPERTY()
	FName AttachSocketName = NAME_None;

	/** serialized save game properties */
	UPROPERTY()
	TArray<uint8> SaveGameBunch;

	/** custom state provided by component via PersistentState interface */
	UPROPERTY()
	FInstancedStruct InstanceState;

private:
	
	/**
	 * Indicates whether component static or dynamic by construction
	 * Static - component either is a default subobject or created by construction script at runtime
	 * Dynamic - component is created at runtime (via NewObject)
	 */
	UPROPERTY(meta = (AlwaysLoaded))
	uint8 bComponentStatic: 1 = true;
	
	/** 
	 * Indicates whether component state should be saved. If false, state does nothing when saving/loading
	 * Can be false if component has not been saved to its state yet or component doesn't want to be saved by overriding ShouldSave()
	 */
	UPROPERTY(meta = (AlwaysLoaded))
	uint8 bComponentSaved: 1 = false;
	
	/** Indicates whether component state has a stored transform */
	UPROPERTY(meta = (AlwaysLoaded))
	uint8 bHasTransform: 1 = false;
	
	/** */
	mutable uint8 bStateInitialized: 1 = false;
};

#if WITH_COMPONENT_CUSTOM_SERIALIZE
template <>
struct TStructOpsTypeTraits<FComponentPersistentState> : public TStructOpsTypeTraitsBase2<FComponentPersistentState>
{
	enum
	{
		WithSerializer = true
	};
};
#endif

USTRUCT()
struct FDynamicActorSpawnData
{
	GENERATED_BODY()

	FDynamicActorSpawnData() = default;
	explicit FDynamicActorSpawnData(AActor* InActor);

	FORCEINLINE bool IsValid() const { return !ActorClass.IsNull(); }
	FORCEINLINE bool HasOwner() const { return ActorOwnerId.IsValid(); }
	FORCEINLINE bool HasInstigator() const { return ActorInstigatorId.IsValid(); }

	/** actor class stored to recreate dynamic actor at runtime */
	// @todo: actor class should be pre-loaded by some smart loading system
	UPROPERTY()
	TSoftClassPtr<AActor> ActorClass;

	/** actor name */
	UPROPERTY()
	FName ActorName = NAME_None;
	
	/** actor owner */
	UPROPERTY()
	FPersistentStateObjectId ActorOwnerId;

	/** actor instigator */
	UPROPERTY()
	FPersistentStateObjectId ActorInstigatorId;
};

USTRUCT()
struct PERSISTENTSTATE_API FActorPersistentState
{
	GENERATED_BODY()
public:
	FActorPersistentState() = default;
	FActorPersistentState(AActor* InActor, FPersistentStateObjectId InActorId, bool bStatic);

	/** initialize actor state with actor stored in map */
	void InitWithStaticActor(AActor* Actor, FPersistentStateObjectId ActorId) const;
	/** initialize actor state with actor created at runtime */
	void InitWithDynamicActor(AActor* Actor, FPersistentStateObjectId ActorId) const;
	/** initialize actor state by re-creating dynamic actor */
	AActor* CreateDynamicActor(UWorld* World, FActorSpawnParameters& SpawnParams) const;

	void LoadActor(ULevelPersistentStateManager& StateManager);
	void SaveActor(ULevelPersistentStateManager& StateManager);
	FPersistentStateObjectId GetActorId() const;

	/** @return component state */
	const FComponentPersistentState* GetComponentState(const FPersistentStateObjectId& ComponentId) const;
	FComponentPersistentState* GetComponentState(const FPersistentStateObjectId& ComponentId);
	FComponentPersistentState* CreateComponentState(UActorComponent* Component, const FPersistentStateObjectId& ComponentId, bool bStatic);

	FORCEINLINE bool IsStatic() const { return bActorStatic; }
	FORCEINLINE bool IsDynamic() const { return !bActorStatic; }
	FORCEINLINE bool IsInitialized() const { return bStateInitialized; }
	FORCEINLINE bool IsSaved() const { return bActorSaved; }
	FORCEINLINE bool IsOutdated() const { return bActorSaved && !SpawnData.ActorClass.IsNull() && SpawnData.ActorClass.Get() == nullptr; }
#if WITH_ACTOR_CUSTOM_SERIALIZE
	friend FArchive& operator<<(FArchive& Ar, const FActorPersistentState& Value);
	bool Serialize(FArchive& Ar);
#endif
	
	/**
	 * guid created at runtime for a given actor
	 * for static actors guid is derived from stable package path
	 * for dynamic actors (e.g. created at runtime), guid is created on a fly and kept stable between laods
	 */
	UPROPERTY(meta = (AlwaysLoaded))
	mutable FPersistentStateObjectId WeakActor;

	/** actor spawn data, always relevant for dynamic actors, never stored for static actors */
	UPROPERTY()
    FDynamicActorSpawnData SpawnData;
	
	/** actor world transform, always relevant for dynamic actors */
	UPROPERTY()
	FTransform ActorTransform;
	
	/** actor attach parent */
	UPROPERTY()
	FPersistentStateObjectId AttachParentId;

	/** */
	UPROPERTY()
	FName AttachSocketName = NAME_None;

	/** serialized save game properties */
	UPROPERTY()
	TArray<uint8> SaveGameBunch;

	/** custom state provided by actor via PersistentState interface */
	UPROPERTY()
	FInstancedStruct InstanceState;

	/** A list of actor components */
	UPROPERTY()
	TArray<FComponentPersistentState> Components;

private:
	
	/**
	 * Indicates whether actor is static (e.g. stored in the map) or dynamic (created at runtime).
	 * Static actors have "PersistentState_Static" stored in Tags
	 * Dynamic actors have "PersistentState_Dynamic" stored in Tags
	 */
	UPROPERTY(meta = (AlwaysLoaded))
	uint8 bActorStatic: 1 = true;

	/** 
	 * Indicates whether actor state should be saved at all. If false, state does nothing when saving/loading
	 * Can be false if actor has not been saved to its state yet or actor doesn't want to be saved by overriding ShouldSave()
	 */
	UPROPERTY(meta = (AlwaysLoaded))
	uint8 bActorSaved: 1 = false;
	
	/** Indicates whether actor state has a stored transform */
	UPROPERTY(meta = (AlwaysLoaded))
	uint8 bHasTransform: 1 = false;

	/** initialized state bit */
	mutable uint8 bStateInitialized: 1 = false;
};

#if WITH_ACTOR_CUSTOM_SERIALIZE
template <>
struct TStructOpsTypeTraits<FActorPersistentState> : public TStructOpsTypeTraitsBase2<FActorPersistentState>
{
	enum
	{
		WithSerializer = true
	};
};
#endif

USTRUCT()
struct PERSISTENTSTATE_API FLevelPersistentState
{
	GENERATED_BODY()

	FLevelPersistentState() = default;
	explicit FLevelPersistentState(const ULevel* Level);
	explicit FLevelPersistentState(const FPersistentStateObjectId& InLevelId)
		: LevelId(InLevelId)
	{}
	
	/** @return true if level state contains an actor */
	bool HasActor(const FPersistentStateObjectId& ActorId) const;
	/** @return true if level state contains a component */
	bool HasComponent(const FPersistentStateObjectId& ActorId, const FPersistentStateObjectId& ComponentId) const;

	/** @return actor state referenced by actor id */
	const FActorPersistentState* GetActorState(const FPersistentStateObjectId& ActorId) const;
	FActorPersistentState* GetActorState(const FPersistentStateObjectId& ActorId);
	FActorPersistentState* CreateActorState(AActor* Actor, const FPersistentStateObjectId& ActorId, bool bStatic);
	
	UPROPERTY()
	FPersistentStateObjectId LevelId;
	
	UPROPERTY()
	TMap<FPersistentStateObjectId, FActorPersistentState> Actors;
};

UCLASS()
class PERSISTENTSTATE_API ULevelPersistentStateManager: public UWorldPersistentStateManager
{
	GENERATED_BODY()
public:
	virtual void Init(UWorld* World) override;
	virtual void Cleanup(UWorld* World) override;
	virtual void NotifyInitialized(UObject& Object) override;
	
	virtual void SaveGameState() override;

protected:

	struct FLevelRestoreContext
	{
		TArray<FPersistentStateObjectId> CreatedActors;
		TArray<FPersistentStateObjectId> CreatedComponents;

		void AddCreatedActor(const FActorPersistentState& ActorState);
		void AddCreatedComponent(const FComponentPersistentState& ComponentState);
	};

	void LoadGameState();
	
	/** save level state */
	void SaveLevel(FLevelPersistentState& LevelState);
	/** restore level state */
	void RestoreLevel(ULevel* Level, FLevelRestoreContext& Context);
	/** */
	void ProcessPendingRegisterActors(FLevelRestoreContext& Context);

	const FLevelPersistentState* GetLevelState(ULevel* Level) const;
	FLevelPersistentState* GetLevelState(ULevel* Level);
	FLevelPersistentState& GetOrCreateLevelState(ULevel* Level);
	
private:
	/** initial actor initialization callback */
	void OnWorldActorsInitialized(const FActorsInitializedParams& InitParams);
	/** level streaming becomes visible callback (transition to LoadedVisible state) */	
	void OnLevelBecomeVisible(UWorld* World, const ULevelStreaming* LevelStreaming, ULevel* LoadedLevel);
	/** level streaming becomes invisible callback (transition to LoadedNotVisible state) */	
	void OnLevelBecomeInvisible(UWorld* World, const ULevelStreaming* LevelStreaming, ULevel* LoadedLevel);
	/** actor callback after all components has been registered but before BeginPlay */
	void OnActorRegistered(AActor* Actor);

	FActorPersistentState* RegisterActor(AActor* Actor, FLevelRestoreContext& RestoreContext);
	/** callback for actor explicitly destroyed (not removed from the world) */
	void OnActorDestroyed(AActor* Actor);
	
	void RestoreDynamicActors(ULevel* Level, FLevelPersistentState& LevelState, FLevelRestoreContext& Context);
	void RestoreActorComponents(AActor& Actor, FActorPersistentState& ActorState, FLevelRestoreContext& Context);
	void UpdateActorComponents(AActor& Actor, FActorPersistentState& ActorState);

	FORCEINLINE bool IsDestroyedObject(const FPersistentStateObjectId& ObjectId) const { return DestroyedObjects.Contains(ObjectId); }
	FORCEINLINE void AddDestroyedObject(const FPersistentStateObjectId& ObjectId) { DestroyedObjects.Add(ObjectId); }

	FORCEINLINE bool CanRestoreLevel() const { return !bRegisteringActors && !bLoadingActors && !bRestoringDynamicActors; }
	FORCEINLINE bool CanRegisterActors() const { return bWorldInitializedActors && !bRegisteringActors && !bLoadingActors && !bRestoringDynamicActors; }

	UPROPERTY()
	TMap<FPersistentStateObjectId, FLevelPersistentState> Levels;

	UPROPERTY()
	TSet<FPersistentStateObjectId> DestroyedObjects;

	UPROPERTY()
	TSet<FPersistentStateObjectId> OutdatedObjects;

	UPROPERTY(Transient)
	TArray<AActor*> PendingRegisterActors;

	UPROPERTY(Transient)
	TArray<FPersistentStateObjectId> LoadedLevels;
	
	UPROPERTY(Transient)
	AActor* CurrentlyProcessedActor = nullptr;
	
	FDelegateHandle LevelVisibleHandle;
	FDelegateHandle LevelInvisibleHandle;
	FDelegateHandle ActorsInitializedHandle;
	FDelegateHandle ActorRegisteredHandle;
	FDelegateHandle ActorDestroyedHandle;
	/** */
	uint8 bWorldInitializedActors: 1 = false;
	/** */
	uint8 bRestoringDynamicActors: 1 = false;
	/** */
	uint8 bRegisteringActors: 1 = false;
	/** */
	uint8 bLoadingActors: 1 = false;

	// @todo: remove
	friend struct FActorPersistentState;
};
