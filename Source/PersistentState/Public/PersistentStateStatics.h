#pragma once

#include "CoreMinimal.h"
#include "PersistentStateTypes.h"

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
    FGuid CreateUniqueObjectId(const UObject& Object);
    FGuid CreateUniqueIdFromStableName(const UObject& Object);
    FGuid AssignObjectId(const UObject& Object, const FGuid& ObjectId);
    
    /** */
    PERSISTENTSTATE_API FGuid FindUniqueIdFromObject(const UObject* Object);
    
    /** */
	PERSISTENTSTATE_API UObject* FindObjectByUniqueId(const FGuid& ObjectId);
    template <typename T = UObject>
	FORCEINLINE T* FindObjectByUniqueId(const FGuid& ObjectId)
    {
    	return CastChecked<T>(FindObjectByUniqueId(ObjectId), ECastCheckedType::NullAllowed);
    }

	/** */
	UPersistentStateManager* FindManagerByClass(TConstArrayView<UPersistentStateManager*> Managers, TSubclassOf<UPersistentStateManager> ManagerClass);

	/** */
	void LoadWorldState(TArrayView<UPersistentStateManager*> Managers, const FWorldStateSharedRef& WorldState);
	/** */
	FWorldStateSharedRef SaveWorldState(UWorld* World, TArrayView<UPersistentStateManager*> Managers);
    /** */
    void LoadObjectSaveGameProperties(UObject& Object, TArray<uint8>& SaveGameBunch);
    /** */
    void SaveObjectSaveGameProperties(UObject& Object, const TArray<uint8>& SaveGameBunch);
}