#pragma once

#include "CoreMinimal.h"
#include "PersistentStateArchive.h"
#include "PersistentStateSlot.h"

struct FLevelSaveContext;
struct FLevelLoadContext;
class UPersistentStateManager;
class AActor;
class UObject;
class UActorComponent;

namespace UE::PersistentState
{
	FORCEINLINE static uint32 GetGuidSeed() { return 0; }

	/** async utilities */
	void ScheduleGameThreadCallback(FSimpleDelegateGraphTask::FDelegate&& Callback);
    void WaitForTask(UE::Tasks::FTask Task);
	void WaitForPipe(UE::Tasks::FPipe& Pipe);

    /** */
	void MarkActorStatic(AActor& Actor);
	void MarkActorDynamic(AActor& Actor);
	void MarkComponentStatic(UActorComponent& Component);
	void MarkComponentDynamic(UActorComponent& Component);
	
	PERSISTENTSTATE_API bool IsStaticActor(const AActor& Actor);
	PERSISTENTSTATE_API bool IsDynamicActor(const AActor& Actor);
	PERSISTENTSTATE_API bool IsStaticComponent(const UActorComponent& Component);
	PERSISTENTSTATE_API bool IsDynamicComponent(const UActorComponent& Component);

	/** delete all save games by a specified path */
	PERSISTENTSTATE_API void ResetSaveGames(const FString& Path, const FString& Extension);

	/** @return stable name created from @Object e.g. object can be identified by its name between launches  */
	FString GetStableName(const UObject& Object);
	/** @return true if @Object's name is stable e.g. object can be identified by its name between launches */
    bool HasStableName(const UObject& Object);

	/** sanitize object reference, editor only */
	void SanitizeReference(const UObject& SourceObject, const UObject* ReferenceObject);
	
	/** */
	FWorldStateSharedRef CreateWorldState(const FString& World, const FString& WorldPackage, TConstArrayView<UPersistentStateManager*> Managers);
	/** */
	FGameStateSharedRef CreateGameState(TConstArrayView<UPersistentStateManager*> Managers);
	/** */
	void LoadGameState(TConstArrayView<UPersistentStateManager*> Managers, const FGameStateSharedRef& GameState);
	/** */
	void LoadWorldState(TConstArrayView<UPersistentStateManager*> Managers, const FWorldStateSharedRef& WorldState);
	
	void LoadManagerState(FArchive& Ar, TConstArrayView<UPersistentStateManager*> Managers, uint32 ChunkCount, uint32 ObjectTablePosition, uint32 StringTablePosition);
	void SaveManagerState(FArchive& Ar, TConstArrayView<UPersistentStateManager*> Managers, uint32& OutObjectTablePosition, uint32& OutStringTablePosition);
	
	/** load object SaveGame property values */
	void LoadObjectSaveGameProperties(UObject& Object, const TArray<uint8>& SaveGameBunch);
	/** save object SaveGame property values */
	void SaveObjectSaveGameProperties(UObject& Object, TArray<uint8>& SaveGameBunch);
    /** load object SaveGame property values */
    void LoadObjectSaveGameProperties(UObject& Object, const TArray<uint8>& SaveGameBunch, FPersistentStateObjectTracker& DependencyTracker);
    /** save object SaveGame property values */
    void SaveObjectSaveGameProperties(UObject& Object, TArray<uint8>& SaveGameBunch, FPersistentStateObjectTracker& DependencyTracker);
}