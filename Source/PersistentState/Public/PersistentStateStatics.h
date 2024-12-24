#pragma once

#include "CoreMinimal.h"
#include "PersistentStateArchive.h"
#include "PersistentStateSlot.h"

class UPersistentStateManager;
class AActor;
class UObject;
class UActorComponent;

namespace UE::PersistentState
{
	FORCEINLINE static uint32 GetGuidSeed() { return 0; }

	/** async utilities */
	void ScheduleAsyncComplete(TFunction<void()> Callback);
    void WaitForTask(UE::Tasks::FTask Task);

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

	/** */
    bool HasStableName(const UObject& Object);
	FString GetStableName(const UObject& Object);

	/** */
	void LoadWorldState(TConstArrayView<UPersistentStateManager*> Managers, const FWorldStateSharedRef& WorldState);
	/** */
	FWorldStateSharedRef CreateWorldState(const UWorld& World, TConstArrayView<UPersistentStateManager*> Managers);
	/** */
	void LoadGameState(TConstArrayView<UPersistentStateManager*> Managers, const FGameStateSharedRef& GameState);
	/** */
	FGameStateSharedRef CreateGameState(TConstArrayView<UPersistentStateManager*> Managers);
	
	void LoadManagerState(FArchive& Ar, TConstArrayView<UPersistentStateManager*> Managers, uint32 ChunkCount, uint32 ObjectTablePosition, uint32 StringTablePosition);
	void SaveManagerState(FArchive& Ar, TConstArrayView<UPersistentStateManager*> Managers, uint32& OutObjectTablePosition, uint32& OutStringTablePosition);
	
	/** load object SaveGame property values */
	void LoadObjectSaveGameProperties(UObject& Object, const TArray<uint8>& SaveGameBunch);
	/** save object SaveGame property values */
	void SaveObjectSaveGameProperties(UObject& Object, TArray<uint8>& SaveGameBunch);
    /** load object SaveGame property values */
    void LoadObjectSaveGameProperties(UObject& Object, const TArray<uint8>& SaveGameBunch, FPersistentStateObjectTracker& ObjectTracker);
    /** save object SaveGame property values */
    void SaveObjectSaveGameProperties(UObject& Object, TArray<uint8>& SaveGameBunch, FPersistentStateObjectTracker& ObjectTracker);
}