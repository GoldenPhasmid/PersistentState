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
		: DependencyTracker(InTracker)
		, bFromLevelStreaming(bInFromLevelStreaming)
	{}

	void AddCreatedActor(const FActorPersistentState& ActorState);
	void AddCreatedComponent(const FComponentPersistentState& ComponentState);
	
	TArray<FPersistentStateObjectId> CreatedActors;
	TArray<FPersistentStateObjectId> CreatedComponents;
	FPersistentStateObjectTracker& DependencyTracker;
	bool bFromLevelStreaming = false;
};

struct FLevelSaveContext
{
	FLevelSaveContext(FPersistentStateObjectTracker& InTracker, bool bInFromLevelStreaming)
		: DependencyTracker(InTracker)
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
	
	TArray<FPersistentStateObjectId, TInlineAllocator<16>> DestroyedObjects;
	TArray<FPersistentStateObjectId, TInlineAllocator<16>> OutdatedObjects;
	FPersistentStateObjectTracker& DependencyTracker;
	bool bFromLevelStreaming = false;
};

USTRUCT()
struct FPersistentStateSaveGameBunch
{
	GENERATED_BODY()
public:

#if WITH_STRUCTURED_SERIALIZATION && 0
	bool Serialize(FStructuredArchive::FSlot Slot);
#endif

	FORCEINLINE typename TArray<uint8>::SizeType Num() const { return Value.Num(); }
	
	UPROPERTY()
	TArray<uint8> Value;
};

#if WITH_STRUCTURED_SERIALIZATION && 0
template <>
struct TStructOpsTypeTraits<FPersistentStateSaveGameBunch> : public TStructOpsTypeTraitsBase2<FPersistentStateSaveGameBunch>
{
	enum
	{
		WithStructuredSerializer = true,
	};
};
#endif

USTRUCT()
struct FPersistentStateObjectDesc
{
	GENERATED_BODY()

	static FPersistentStateObjectDesc Create(AActor& Actor, FPersistentStateObjectTracker& DependencyTracker);
	static FPersistentStateObjectDesc Create(UActorComponent& Component, FPersistentStateObjectTracker& DependencyTracker);
	
	bool EqualSaveGame(const FPersistentStateObjectDesc& Other) const;
	uint32 GetAllocatedSize() const;

	UPROPERTY()
	FTransform Transform;
	
	UPROPERTY()
	FSoftClassPath Class;

	UPROPERTY()
	FPersistentStateObjectId OwnerID;

	UPROPERTY()
	FPersistentStateObjectId AttachParentID;

	UPROPERTY()
	FName Name = NAME_None;
	
	UPROPERTY()
	FName AttachSocketName = NAME_None;

	UPROPERTY()
	FPersistentStateSaveGameBunch SaveGameBunch;
	
	UPROPERTY()
	bool bHasTransform = false;
};

/**
 * Actor/Component flags that describe the state, aligned to 1 byte
 * If you add any new flags to the struct make sure it is aligned properly
 */
USTRUCT()
struct alignas(1) FPersistentStateDescFlags
{
	GENERATED_BODY()

#if WITH_COMPACT_SERIALIZATION
	bool Serialize(FArchive& Ar);
	friend FArchive& operator<<(FArchive& Ar, FPersistentStateDescFlags& Value);
	/** serialize object state to archive based on underlying state flags */
	void SerializeObjectState(FArchive& Ar, FPersistentStateObjectDesc& State, const FPersistentStateObjectId& ObjectHandle);
#endif

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
	
	/** Transient Linked state flag */
	mutable uint8 bStateLinked: 1 = false;
	/** Transient Initialized state flag */
	mutable uint8 bStateInitialized: 1 = false;
	
	/** 
	 * Indicates whether component state should be saved. If false, state does nothing when saving/loading
	 * Can be false if component has not been saved to its state yet or component doesn't want to be saved by overriding ShouldSave()
	 */
	UPROPERTY(meta = (AlwaysLoaded))
	uint8 bStateSaved: 1 = false;
	
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

#if WITH_COMPACT_SERIALIZATION
template <>
struct TStructOpsTypeTraits<FPersistentStateDescFlags> : public TStructOpsTypeTraitsBase2<FPersistentStateDescFlags>
{
	enum
	{
		WithSerializer = true,
	};
};
#endif

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
	FORCEINLINE FSoftClassPath GetClass() const { return SavedComponentState.Class; }
	FORCEINLINE bool IsStatic() const { return ComponentHandle.IsStatic(); }
	FORCEINLINE bool IsDynamic() const { return ComponentHandle.IsDynamic(); }
	FORCEINLINE bool IsLinked() const { return StateFlags.bStateLinked; }
	FORCEINLINE bool IsSaved() const { return StateFlags.bStateSaved; }
#if WITH_COMPACT_SERIALIZATION
	bool Serialize(FArchive& Ar);
	friend FArchive& operator<<(FArchive& Ar, FComponentPersistentState& Value);
#endif // WITH_COMPACT_SERIALIZATION
	
	FORCEINLINE FString ToString() const { return ComponentHandle.ToString(); }
	/** @return size of dynamically allocated memory stored in the state */
	FORCEINLINE uint32 GetAllocatedSize() const { return DefaultComponentState.GetAllocatedSize() + SavedComponentState.GetAllocatedSize(); }

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

#if WITH_COMPACT_SERIALIZATION
template <>
struct TStructOpsTypeTraits<FComponentPersistentState> : public TStructOpsTypeTraitsBase2<FComponentPersistentState>
{
	enum
	{
		WithSerializer = true,
	};
};
#endif

/**
 * Actor State, linked to the owning actor via Persistent Object ID
 * Actor state is linked with actor during AddToWorld level streaming flow. If actor should be tracked but
 * doesn't have an associated state, state is created and linked in place.
 * State is "initialized" during actor initialization by calling LoadActor(). Based on the state properties
 * and object type (static or dynamic)
 */
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
	FORCEINLINE FSoftClassPath GetClass() const { return SavedActorState.Class; }
	FORCEINLINE bool IsStatic() const { return ActorHandle.IsStatic(); }
	FORCEINLINE bool IsDynamic() const { return ActorHandle.IsDynamic(); }
	FORCEINLINE bool IsLinked() const { return StateFlags.bStateLinked; }
	FORCEINLINE bool IsSaved() const { return StateFlags.bStateSaved; }
#if WITH_COMPACT_SERIALIZATION
	bool Serialize(FArchive& Ar);
	friend FArchive& operator<<(FArchive& Ar, FActorPersistentState& Value);
#endif // WITH_COMPACT_SERIALIZATION
	FORCEINLINE FString ToString() const { return ActorHandle.ToString(); }
	/** @return size of dynamically allocated memory stored in the state */
	uint32 GetAllocatedSize() const;

	/** A list of actor components */
	UPROPERTY()
	TArray<FComponentPersistentState> Components;

private:

	void UpdateActorComponents(FLevelSaveContext& Context, const AActor& Actor);

	FPersistentStateObjectDesc DefaultActorState;

	/** serialized object state */
	UPROPERTY(meta = (AlwaysLoaded))
	FPersistentStateObjectDesc SavedActorState;
	
	/**
	 * guid created at runtime for a given actor
	 * for static actors guid is derived from stable package path
	 * for dynamic actors (e.g. created at runtime), guid is created on a fly and kept stable between loads
	 */
	UPROPERTY(meta = (AlwaysLoaded))
	mutable FPersistentStateObjectId ActorHandle;
	
	/** serialize state flags */
	UPROPERTY(meta = (AlwaysLoaded))
	FPersistentStateDescFlags StateFlags;
};

#if WITH_COMPACT_SERIALIZATION
template <>
struct TStructOpsTypeTraits<FActorPersistentState> : public TStructOpsTypeTraitsBase2<FActorPersistentState>
{
	enum
	{
		WithSerializer = true,
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
	
	FLevelLoadContext CreateLoadContext();
	FLevelSaveContext CreateSaveContext(bool bFromLevelStreaming);
	
	/** @return size of dynamically allocated memory stored in the state */
	uint32 GetAllocatedSize() const;

	FORCEINLINE bool IsEmpty() const { return Actors.IsEmpty(); }

	void PreLoadAssets(FStreamableDelegate LoadCompletedDelegate);
	void FinishLoadAssets();
	void ReleaseLevelAssets();
	
	UPROPERTY()
	FPersistentStateObjectId LevelHandle;

	UPROPERTY()
	TMap<FPersistentStateObjectId, FActorPersistentState> Actors;
	
	UPROPERTY()
	FPersistentStateObjectTracker DependencyTracker;

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

	//~Begin PersistentStateManager interface
	virtual bool ShouldCreateManager(UPersistentStateSubsystem& Subsystem) const override;
	virtual void Init(UPersistentStateSubsystem& Subsystem) override;
	virtual void NotifyWorldInitialized() override;
	virtual void NotifyActorsInitialized() override;
	virtual void Cleanup(UPersistentStateSubsystem& Subsystem) override;
	virtual void NotifyObjectInitialized(UObject& Object) override;
	virtual void SaveState() override;
	virtual uint32 GetAllocatedSize() const override;
	virtual void UpdateStats() const override;
	//~End PersistentStateManager interface

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
	void OnPersistentLevelInitialized();
	/** level fully loaded callback */
	void OnLevelAddedToWorld(ULevel* LoadedLevel, UWorld* World);
	/** level streaming becomes visible callback (transition to LoadedVisible state) */	
	void OnLevelBecomeVisible(UWorld* World, const ULevelStreaming* LevelStreaming, ULevel* LoadedLevel);
	/** level streaming becomes invisible callback (transition to LoadedNotVisible state) */	
	void OnLevelBecomeInvisible(UWorld* World, const ULevelStreaming* LevelStreaming, ULevel* LoadedLevel);
	/** actor callback after all components has been registered but before BeginPlay */
	void OnActorInitialized(AActor* Actor);
	/** callback for actor explicitly destroyed (not removed from the world) */
	void OnActorDestroyed(AActor* Actor);
	
	FActorPersistentState* InitializeActor(AActor* Actor, FLevelPersistentState& LevelState, FLevelLoadContext& RestoreContext);
	
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
