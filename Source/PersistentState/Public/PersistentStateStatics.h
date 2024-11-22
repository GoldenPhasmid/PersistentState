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

    /** */
	void MarkActorStatic(AActor& Actor);
	void MarkActorDynamic(AActor& Actor);
	void MarkComponentStatic(UActorComponent& Component);
	void MarkComponentDynamic(UActorComponent& Component);
	
	PERSISTENTSTATE_API bool IsStaticActor(const AActor& Actor);
	PERSISTENTSTATE_API bool IsDynamicActor(const AActor& Actor);
	PERSISTENTSTATE_API bool IsStaticComponent(const UActorComponent& Component);
	PERSISTENTSTATE_API bool IsDynamicComponent(const UActorComponent& Component);

	/** */
    bool HasStableName(const UObject& Object);
	FString GetStableName(const UObject& Object);
	
	/** */
	UPersistentStateManager* FindManagerByClass(TConstArrayView<UPersistentStateManager*> Managers, TSubclassOf<UPersistentStateManager> ManagerClass);

	/** */
	void LoadWorldState(TArrayView<UPersistentStateManager*> Managers, const FWorldStateSharedRef& WorldState);
	/** */
	FWorldStateSharedRef SaveWorldState(FName WorldName, FName WorldPackageName, TArrayView<UPersistentStateManager*> Managers);

	/** load object SaveGame property values */
	void LoadObjectSaveGameProperties(UObject& Object, const TArray<uint8>& SaveGameBunch);
	/** save object SaveGame property values */
	void SaveObjectSaveGameProperties(UObject& Object, TArray<uint8>& SaveGameBunch);
    /** load object SaveGame property values */
    void LoadObjectSaveGameProperties(UObject& Object, const TArray<uint8>& SaveGameBunch, FPersistentStateObjectTracker& ObjectTracker);
    /** save object SaveGame property values */
    void SaveObjectSaveGameProperties(UObject& Object, TArray<uint8>& SaveGameBunch, FPersistentStateObjectTracker& ObjectTracker);
}