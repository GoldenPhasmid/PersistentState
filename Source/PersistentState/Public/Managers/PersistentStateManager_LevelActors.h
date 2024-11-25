#pragma once

#include "CoreMinimal.h"
#include "InstancedStruct.h"
#include "PersistentStateArchive.h"
#include "PersistentStateManager.h"
#include "PersistentStateObjectId.h"
#include "Engine/StreamableManager.h"

#include "PersistentStateManager_LevelActors.generated.h"

struct FActorPersistentState;
struct FComponentPersistentState;
struct FPersistentStateDescFlags;
struct FPersistentStateObjectDesc;
class UPersistentStateManager_LevelActors;

struct FLevelLoadContext
{
	FLevelLoadContext(FPersistentStateObjectTracker& InTracker, bool bInFromLevelStreaming)
		: ObjectTracker(InTracker)
		, bFromLevelStreaming(bInFromLevelStreaming)
	{}

	void AddCreatedActor(const FActorPersistentState& ActorState);
	void AddCreatedComponent(const FComponentPersistentState& ComponentState);
	
	TArray<FPersistentStateObjectId> CreatedActors;
	TArray<FPersistentStateObjectId> CreatedComponents;
	FPersistentStateObjectTracker& ObjectTracker;
	bool bFromLevelStreaming = false;
};

struct FLevelSaveContext
{
	FLevelSaveContext(FPersistentStateObjectTracker& InTracker, bool bInFromLevelStreaming)
		: ObjectTracker(InTracker)
		, bFromLevelStreaming(bInFromLevelStreaming)
	{}

	void ProcessActorState(const FActorPersistentState& State);
	void ProcessComponentState(const FComponentPersistentState& State);
	
	FORCEINLINE void AddDestroyedObject(const FPersistentStateObjectId& InObjectID)
	{
		check(InObjectID.IsValid() && !DestroyedObjects.Contains(InObjectID));
		DestroyedObjects.Add(InObjectID);
	}

	FORCEINLINE void AddOutdatedObject(const FPersistentStateObjectId& InObjectID)
	{
		check(InObjectID.IsValid() && !OutdatedObjects.Contains(InObjectID));
		OutdatedObjects.Add(InObjectID);
	}
	
	FORCEINLINE bool IsLevelUnloading() const { return bFromLevelStreaming; }

	TSet<FSoftObjectPath, DefaultKeyFuncs<FSoftObjectPath>, TInlineSetAllocator<16>> DynamicClasses;
	TArray<FPersistentStateObjectId, TInlineAllocator<16>> DestroyedObjects;
	TArray<FPersistentStateObjectId, TInlineAllocator<16>> OutdatedObjects;
	FPersistentStateObjectTracker& ObjectTracker;
	bool bFromLevelStreaming = false;
};

USTRUCT()
struct FPersistentStateObjectDesc
{
	GENERATED_BODY()

	static FPersistentStateObjectDesc Create(AActor& Actor, FPersistentStateObjectTracker& ObjectTracker);
	static FPersistentStateObjectDesc Create(UActorComponent& Component, FPersistentStateObjectTracker& ObjectTracker);
	
	bool EqualSaveGame(const FPersistentStateObjectDesc& Other) const
	{
		const int32 Num = SaveGameBunch.Num();
		return Num == Other.SaveGameBunch.Num() && FMemory::Memcmp(SaveGameBunch.GetData(), Other.SaveGameBunch.GetData(), Num) == 0;
	}

	UPROPERTY()
	FName Name = NAME_None;

	UPROPERTY()
	TSoftClassPtr<UObject> Class;
	
	UPROPERTY()
	FPersistentStateObjectId OwnerID;
	
	UPROPERTY()
	bool bHasTransform = false;

	UPROPERTY()
	FTransform Transform;

	UPROPERTY()
	FPersistentStateObjectId AttachParentID;
	
	UPROPERTY()
	FName AttachSocketName = NAME_None;

	UPROPERTY()
	TArray<uint8> SaveGameBunch;
};

USTRUCT()
struct alignas(1) FPersistentStateDescFlags
{
	GENERATED_BODY()

	bool Serialize(FArchive& Ar);
	friend FArchive& operator<<(FArchive& Ar, FPersistentStateDescFlags& Value);


	/** serialize object state to archive based on underlying state flags */
	void SerializeObjectState(FArchive& Ar, FPersistentStateObjectDesc& State);

	/**
	 * @return object state flags calculate for a static object as a different between @Default state and @Current state.
	 * Use @SourceFlags to copy flags unrelated to object state
	 */
	FPersistentStateDescFlags GetFlagsForStaticObject(FPersistentStateDescFlags SourceFlags, const FPersistentStateObjectDesc& Default, const FPersistentStateObjectDesc& Current) const;

	/**
	 * @return object state flags calculate for a static object as a different between @Default state and @Current state.
	 * Use @SourceFlags to copy flags unrelated to object state
	 */
	FPersistentStateDescFlags GetFlagsForDynamicObject(FPersistentStateDescFlags SourceFlags, const FPersistentStateObjectDesc& Current) const;
	
	/** */
	mutable uint8 bStateLinked: 1 = false;
	
	/** 
	 * Indicates whether component state should be saved. If false, state does nothing when saving/loading
	 * Can be false if component has not been saved to its state yet or component doesn't want to be saved by overriding ShouldSave()
	 */
	UPROPERTY(meta = (AlwaysLoaded))
	uint8 bStateSaved: 1 = false;

	/** flag for name serialization. Always true for dynamic objects */
	UPROPERTY(meta = (AlwaysLoaded))
	uint8 bHasInstanceName: 1 = false;

	/** flag for object class serialization. Always true for dynamic objects */
	UPROPERTY(meta = (AlwaysLoaded))
	uint8 bHasInstanceClass: 1 = false;
	
	/** flag for object owner serialization */
	UPROPERTY(meta = (AlwaysLoaded))
	uint8 bHasInstanceOwner: 1 = false;
	
	/** flag for transform serialization */
	UPROPERTY(meta = (AlwaysLoaded))
	uint8 bHasInstanceTransform: 1 = false;

	/** flag for attachment serialization */
	UPROPERTY(meta = (AlwaysLoaded))
	uint8 bHasInstanceAttachment: 1 = false;

	/** flag for save game serialization */
	UPROPERTY(meta = (AlwaysLoaded))
	uint8 bHasInstanceSaveGameBunch: 1 = false;
};

template <>
struct TStructOpsTypeTraits<FPersistentStateDescFlags> : public TStructOpsTypeTraitsBase2<FPersistentStateDescFlags>
{
	enum
	{
		WithSerializer = true
	};
};

USTRUCT()
struct PERSISTENTSTATE_API FComponentPersistentState: public FPersistentStateBase
{
	GENERATED_BODY()
public:
	FComponentPersistentState() = default;
	FComponentPersistentState(UActorComponent* Component, const FPersistentStateObjectId& InComponentHandle);

	void LinkComponentHandle(UActorComponent* Component, const FPersistentStateObjectId& InComponentHandle) const;
	
	UActorComponent* CreateDynamicComponent(AActor* OwnerActor) const;

	void LoadComponent(FLevelLoadContext& Context);
	void SaveComponent(FLevelSaveContext& Context);

	FORCEINLINE FPersistentStateObjectId GetHandle() const { return ComponentHandle; }
	FORCEINLINE const TSoftClassPtr<UObject>& GetClass() const { return SavedComponentState.Class; }
	FORCEINLINE bool IsStatic() const { return ComponentHandle.IsStatic(); }
	FORCEINLINE bool IsDynamic() const { return ComponentHandle.IsDynamic(); }
	FORCEINLINE bool IsLinked() const { return StateFlags.bStateLinked; }
	FORCEINLINE bool IsSaved() const { return StateFlags.bStateSaved; }
#if WITH_COMPONENT_CUSTOM_SERIALIZE
	friend FArchive& operator<<(FArchive& Ar, FComponentPersistentState& Value);
	bool Serialize(FArchive& Ar);
#endif

	FORCEINLINE FString ToString() const { return ComponentHandle.ToString(); }

private:
	
	FPersistentStateObjectDesc DefaultComponentState;

	/** serialized object state */
	UPROPERTY(meta = (AlwaysLoaded))
	FPersistentStateObjectDesc SavedComponentState;

	/**
	 * guid created at runtime for a given component
	 * for static components guid is derived from stable package path
	 * for dynamic components (e.g. created at runtime), guid is created on a fly and kept between laods
	 */
	UPROPERTY(meta = (AlwaysLoaded))
	mutable FPersistentStateObjectId ComponentHandle;
	
	UPROPERTY(meta = (AlwaysLoaded))
	FPersistentStateDescFlags StateFlags;
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
struct PERSISTENTSTATE_API FActorPersistentState: public FPersistentStateBase
{
	GENERATED_BODY()
public:
	FActorPersistentState() = default;
	FActorPersistentState(AActor* InActor, const FPersistentStateObjectId& InActorHandle);

	/** initialize actor state with actor handle */
	void LinkActorHandle(AActor* Actor, const FPersistentStateObjectId& InActorHandle) const;
	/** initialize actor state by re-creating dynamic actor */
	AActor* CreateDynamicActor(UWorld* World, FActorSpawnParameters& SpawnParams) const;

	void LoadActor(FLevelLoadContext& Context);
	void SaveActor(FLevelSaveContext& Context);

	/** @return component state */
	const FComponentPersistentState* GetComponentState(const FPersistentStateObjectId& ComponentHandle) const;
	FComponentPersistentState* GetComponentState(const FPersistentStateObjectId& ComponentHandle);
	FComponentPersistentState* CreateComponentState(UActorComponent* Component, const FPersistentStateObjectId& ComponentHandle);

	FORCEINLINE FPersistentStateObjectId GetHandle() const { return ActorHandle; }
	FORCEINLINE const TSoftClassPtr<UObject>& GetClass() const { return SavedActorState.Class; }
	FORCEINLINE bool IsStatic() const { return ActorHandle.IsStatic(); }
	FORCEINLINE bool IsDynamic() const { return ActorHandle.IsDynamic(); }
	FORCEINLINE bool IsLinked() const { return StateFlags.bStateLinked; }
	FORCEINLINE bool IsSaved() const { return StateFlags.bStateSaved; }
#if WITH_ACTOR_CUSTOM_SERIALIZE
	friend FArchive& operator<<(FArchive& Ar, FActorPersistentState& Value);
	bool Serialize(FArchive& Ar);
#endif
	FORCEINLINE FString ToString() const { return ActorHandle.ToString(); }
	
	/** A list of actor components */
	UPROPERTY()
	TArray<FComponentPersistentState> Components;

private:

	void UpdateActorComponents(FLevelSaveContext& Context, const AActor& Actor);

	/**
	 * guid created at runtime for a given actor
	 * for static actors guid is derived from stable package path
	 * for dynamic actors (e.g. created at runtime), guid is created on a fly and kept stable between laods
	 */
	UPROPERTY()
	mutable FPersistentStateObjectId ActorHandle;

	/** serialized object state */
	UPROPERTY()
	FPersistentStateObjectDesc SavedActorState;

	/** serialize state flags */
	UPROPERTY()
	FPersistentStateDescFlags StateFlags;
	
	FPersistentStateObjectDesc DefaultActorState;
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
	explicit FLevelPersistentState(const FPersistentStateObjectId& InLevelHandle)
		: LevelHandle(InLevelHandle)
	{}
	
	/** @return true if level state contains an actor */
	bool HasActor(const FPersistentStateObjectId& ActorId) const;
	/** @return true if level state contains a component */
	bool HasComponent(const FPersistentStateObjectId& ActorId, const FPersistentStateObjectId& ComponentId) const;

	/** @return actor state referenced by actor id */
	const FActorPersistentState* GetActorState(const FPersistentStateObjectId& ActorHandle) const;
	FActorPersistentState* GetActorState(const FPersistentStateObjectId& ActorHandle);
	FActorPersistentState* CreateActorState(AActor* Actor, const FPersistentStateObjectId& ActorHandle);

	/** create load context */
	FLevelLoadContext CreateLoadContext() const;

	void PreLoadAssets(FStreamableDelegate LoadCompletedDelegate);
	void FinishLoadAssets();
	void ReleaseLevelAssets();
	
	UPROPERTY()
	FPersistentStateObjectId LevelHandle;
	
	UPROPERTY()
	TArray<FSoftObjectPath> HardDependencies;
	
	UPROPERTY()
	TMap<FPersistentStateObjectId, FActorPersistentState> Actors;

	/** streamable handle that keeps hard dependencies alive required by level state */
	TSharedPtr<FStreamableHandle> AssetHandle;

	uint8 bLevelInitialized: 1 = false;
	uint8 bLevelAdded: 1 = false;
	uint8 bStreamingLevel: 1 = false;
};

UCLASS()
class PERSISTENTSTATE_API UPersistentStateManager_LevelActors: public UPersistentStateManager
{
	GENERATED_BODY()
public:
	UPersistentStateManager_LevelActors();
	
	virtual bool ShouldCreateManager(UPersistentStateSubsystem& Subsystem) const override;
	virtual void Init(UPersistentStateSubsystem& Subsystem) override;
	virtual void Cleanup(UPersistentStateSubsystem& Subsystem) override;
	virtual void NotifyObjectInitialized(UObject& Object) override;
	
	virtual void SaveState() override;

	void AddDestroyedObject(const FPersistentStateObjectId& ObjectId);

protected:

	void LoadGameState();
	
	/** save level state */
	void SaveLevel(FLevelPersistentState& LevelState, bool bFromLevelStreaming);
	/** restore level state */
	void InitializeLevel(ULevel* Level, bool bFromLevelStreaming);

	const FLevelPersistentState* GetLevelState(ULevel* Level) const;
	FLevelPersistentState* GetLevelState(ULevel* Level);
	const FLevelPersistentState& GetLevelStateChecked(ULevel* Level) const;
	FLevelPersistentState& GetLevelStateChecked(ULevel* Level);
	
	FLevelPersistentState& GetOrCreateLevelState(ULevel* Level);
	
private:
	/** initial actor initialization callback */
	void OnWorldInitializedActors(const FActorsInitializedParams& InitParams);
	/** level fully loaded callback */
	void OnLevelAddedToWorld(ULevel* LoadedLevel, UWorld* World);
	/** level streaming becomes visible callback (transition to LoadedVisible state) */	
	void OnLevelBecomeVisible(UWorld* World, const ULevelStreaming* LevelStreaming, ULevel* LoadedLevel);
	/** level streaming becomes invisible callback (transition to LoadedNotVisible state) */	
	void OnLevelBecomeInvisible(UWorld* World, const ULevelStreaming* LevelStreaming, ULevel* LoadedLevel);
	/** actor callback after all components has been registered but before BeginPlay */
	void OnActorInitialized(AActor* Actor);

	FActorPersistentState* InitializeActor(AActor* Actor, FLevelPersistentState& LevelState, FLevelLoadContext& RestoreContext);
	/** callback for actor explicitly destroyed (not removed from the world) */
	void OnActorDestroyed(AActor* Actor);

	/** create dynamic actors that has to be restored by state system */
	void CreateDynamicActors(ULevel* Level);
	void InitializeActorComponents(AActor& Actor, FActorPersistentState& ActorState, FLevelLoadContext& Context);

	FORCEINLINE bool IsDestroyedObject(const FPersistentStateObjectId& ObjectId) const { return DestroyedObjects.Contains(ObjectId); }
	FORCEINLINE bool CanInitializeState() const { return !bInitializingActors && !bLoadingActors && !bCreatingDynamicActors; }

	UPROPERTY()
	TMap<FPersistentStateObjectId, FLevelPersistentState> Levels;

	UPROPERTY()
	TSet<FPersistentStateObjectId> DestroyedObjects;

	UPROPERTY()
	TSet<FPersistentStateObjectId> OutdatedObjects;
	
	UPROPERTY(Transient)
	AActor* CurrentlyProcessedActor = nullptr;

	UPROPERTY(Transient)
	UWorld* CurrentWorld = nullptr;
	
	FDelegateHandle LevelAddedHandle;
	FDelegateHandle LevelVisibleHandle;
	FDelegateHandle LevelInvisibleHandle;
	FDelegateHandle ActorsInitializedHandle;
	FDelegateHandle ActorDestroyedHandle;
	/** */
	uint8 bWorldInitializedActors: 1 = false;
	/** */
	uint8 bCreatingDynamicActors: 1 = false;
	/** */
	uint8 bInitializingActors: 1 = false;
	/** */
	uint8 bLoadingActors: 1 = false;
};
