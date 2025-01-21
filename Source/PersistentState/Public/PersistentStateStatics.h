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
struct FPersistentStatePropertyBunch;

namespace UE::PersistentState
{
	FORCEINLINE static uint32 GetGuidSeed() { return 0; }

	/** */
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
	PERSISTENTSTATE_API bool LoadScreenshot(const FString& FilePath, FImage& Image);

	/** @return stable name created from @Object e.g. object can be identified by its name between launches  */
	PERSISTENTSTATE_API FString GetStableName(const UObject& Object);
	/** @return true if @Object's name is stable e.g. object can be identified by its name between launches */
    PERSISTENTSTATE_API bool HasStableName(const UObject& Object);

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
	
	/** load object SaveGame property values */
	PERSISTENTSTATE_API void LoadObject(UObject& Object, const FPersistentStatePropertyBunch& PropertyBunch, bool bIsSaveGame = true);
	/** load object SaveGame property values, convert indexes to top-level asset dependencies via @DependencyTracker */
	PERSISTENTSTATE_API void LoadObject(UObject& Object, const FPersistentStatePropertyBunch& PropertyBunch, FPersistentStateObjectTracker& DependencyTracker, bool bIsSaveGame = true);
	/** save object SaveGame property values */
	PERSISTENTSTATE_API void SaveObject(UObject& Object, FPersistentStatePropertyBunch& PropertyBunch, bool bIsSaveGame = true);
    /** save object SaveGame property values, converts top-level asset dependencies to indexes via @DependencyTracker */
    PERSISTENTSTATE_API void SaveObject(UObject& Object, FPersistentStatePropertyBunch& PropertyBunch, FPersistentStateObjectTracker& DependencyTracker, bool bIsSaveGame = true);

namespace Private
{
	void LoadManagerState(FArchive& Ar, TConstArrayView<UPersistentStateManager*> Managers, uint32 ChunkCount, uint32 ObjectTablePosition, uint32 StringTablePosition);
	void SaveManagerState(FArchive& Ar, TConstArrayView<UPersistentStateManager*> Managers, uint32& OutObjectTablePosition, uint32& OutStringTablePosition);
} // Private
} // UE::PersistentState