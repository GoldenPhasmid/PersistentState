#pragma once

#include "CoreMinimal.h"
#include "InstancedStruct.h"
#include "PersistentStateObjectId.h"
#include "WorldPersistentStateManager.h"

#include "WorldPersistentStateManager_LevelActors.generated.h"

struct FPersistentStateDescFlags;
struct FPersistentStateObjectDesc;
class UWorldPersistentStateManager_LevelActors;


USTRUCT()
struct FPersistentStateObjectDesc
{
	GENERATED_BODY()

	static FPersistentStateObjectDesc Create(AActor& Actor);
	static FPersistentStateObjectDesc Create(UActorComponent& Component);
	
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

	void LoadComponent(UWorldPersistentStateManager_LevelActors& StateManager);
	void SaveComponent(UWorldPersistentStateManager_LevelActors& StateManager, bool bFromLevelStreaming);

	FORCEINLINE FPersistentStateObjectId GetHandle() const { return ComponentHandle; }
	FORCEINLINE bool IsStatic() const { return ComponentHandle.IsStatic(); }
	FORCEINLINE bool IsDynamic() const { return ComponentHandle.IsDynamic(); }
	FORCEINLINE bool IsLinked() const { return StateFlags.bStateLinked; }
	FORCEINLINE bool IsSaved() const { return StateFlags.bStateSaved; }
#if WITH_COMPONENT_CUSTOM_SERIALIZE
	friend FArchive& operator<<(FArchive& Ar, FComponentPersistentState& Value);
	bool Serialize(FArchive& Ar);
#endif

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

	void LoadActor(UWorldPersistentStateManager_LevelActors& StateManager);
	void SaveActor(UWorldPersistentStateManager_LevelActors& StateManager, bool bFromLevelStreaming);

	/** @return component state */
	const FComponentPersistentState* GetComponentState(const FPersistentStateObjectId& ComponentHandle) const;
	FComponentPersistentState* GetComponentState(const FPersistentStateObjectId& ComponentHandle);
	FComponentPersistentState* CreateComponentState(UActorComponent* Component, const FPersistentStateObjectId& ComponentHandle);

	FORCEINLINE FPersistentStateObjectId GetHandle() const { return ActorHandle; }
	FORCEINLINE bool IsStatic() const { return ActorHandle.IsStatic(); }
	FORCEINLINE bool IsDynamic() const { return ActorHandle.IsDynamic(); }
	FORCEINLINE bool IsLinked() const { return StateFlags.bStateLinked; }
	FORCEINLINE bool IsSaved() const { return StateFlags.bStateSaved; }
#if WITH_ACTOR_CUSTOM_SERIALIZE
	friend FArchive& operator<<(FArchive& Ar, FActorPersistentState& Value);
	bool Serialize(FArchive& Ar);
#endif

	/** A list of actor components */
	UPROPERTY()
	TArray<FComponentPersistentState> Components;

private:

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

	void UpdateActorComponents(UWorldPersistentStateManager_LevelActors& StateManager, const AActor& Actor);
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
	
	UPROPERTY()
	FPersistentStateObjectId LevelHandle;
	
	UPROPERTY()
	TMap<FPersistentStateObjectId, FActorPersistentState> Actors;
};

UCLASS()
class PERSISTENTSTATE_API UWorldPersistentStateManager_LevelActors: public UWorldPersistentStateManager
{
	GENERATED_BODY()
public:
	virtual void Init(UWorld* World) override;
	virtual void Cleanup(UWorld* World) override;
	virtual void NotifyObjectInitialized(UObject& Object) override;
	
	virtual void SaveGameState() override;

	void AddDestroyedObject(const FPersistentStateObjectId& ObjectId);

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
	void SaveLevel(FLevelPersistentState& LevelState, bool bFromLevelStreaming);
	/** restore level state */
	void InitializeLevel(ULevel* Level, FLevelRestoreContext& Context, bool bFromLevelStreaming);

	const FLevelPersistentState* GetLevelState(ULevel* Level) const;
	FLevelPersistentState* GetLevelState(ULevel* Level);
	FLevelPersistentState& GetOrCreateLevelState(ULevel* Level);
	
private:
	/** initial actor initialization callback */
	void OnWorldInitializedActors(const FActorsInitializedParams& InitParams);
	/** level fully loaded callback */
	void OnLevelLoaded(ULevel* LoadedLevel, UWorld* World);
	/** level streaming becomes visible callback (transition to LoadedVisible state) */	
	void OnLevelBecomeVisible(UWorld* World, const ULevelStreaming* LevelStreaming, ULevel* LoadedLevel);
	/** level streaming becomes invisible callback (transition to LoadedNotVisible state) */	
	void OnLevelBecomeInvisible(UWorld* World, const ULevelStreaming* LevelStreaming, ULevel* LoadedLevel);
	/** actor callback after all components has been registered but before BeginPlay */
	void OnActorInitialized(AActor* Actor);

	FActorPersistentState* InitializeActor(AActor* Actor, FLevelRestoreContext& RestoreContext);
	/** callback for actor explicitly destroyed (not removed from the world) */
	void OnActorDestroyed(AActor* Actor);
	
	void RestoreDynamicActors(ULevel* Level, FLevelPersistentState& LevelState, FLevelRestoreContext& Context);
	void RestoreActorComponents(AActor& Actor, FActorPersistentState& ActorState, FLevelRestoreContext& Context);

	FORCEINLINE bool IsDestroyedObject(const FPersistentStateObjectId& ObjectId) const { return DestroyedObjects.Contains(ObjectId); }
	FORCEINLINE bool CanInitializeState() const { return !bRegisteringActors && !bLoadingActors && !bRestoringDynamicActors; }

	UPROPERTY()
	TMap<FPersistentStateObjectId, FLevelPersistentState> Levels;

	UPROPERTY()
	TSet<FPersistentStateObjectId> DestroyedObjects;

	UPROPERTY()
	TSet<FPersistentStateObjectId> OutdatedObjects;
	
	UPROPERTY(Transient)
	TArray<FPersistentStateObjectId> LoadedLevels;
	
	UPROPERTY(Transient)
	AActor* CurrentlyProcessedActor = nullptr;
	
	FDelegateHandle LevelAddedHandle;
	FDelegateHandle LevelVisibleHandle;
	FDelegateHandle LevelInvisibleHandle;
	FDelegateHandle ActorsInitializedHandle;
	FDelegateHandle ActorDestroyedHandle;
	/** */
	uint8 bWorldInitializedActors: 1 = false;
	/** */
	uint8 bRestoringDynamicActors: 1 = false;
	/** */
	uint8 bRegisteringActors: 1 = false;
	/** */
	uint8 bLoadingActors: 1 = false;
};
