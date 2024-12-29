#pragma once

#include "CoreMinimal.h"
#include "PersistentStateObjectId.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "PersistentStateBlueprintLibrary.generated.h"

struct FPersistentStateObjectId;

UCLASS()
class PERSISTENTSTATE_API UPersistentStateBlueprintLibrary: public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
protected:

	/** reset object ID */
	UFUNCTION(BlueprintCallable, Category = "Persistent State", DisplayName = "Reset Object ID")
	static void ObjectId_Reset(FPersistentStateObjectId& ObjectId);

	/** @return true if Object ID is valid. Does not mean object is alive */
	UFUNCTION(BlueprintPure, Category = "Persistent State", DisplayName = "Is Valid (Object ID)")
	static bool ObjectId_IsValid(const FPersistentStateObjectId& ObjectId);

	/** @return true if Object ID does not point to a live UObject */
	UFUNCTION(BlueprintPure, Category = "Persistent State", DisplayName = "Is Stale (Object ID)")
	static bool ObjectId_IsStale(const FPersistentStateObjectId& ObjectId);

	/** @return true if Object ID points to a live UObject */
	UFUNCTION(BlueprintPure, Category = "Persistent State", DisplayName = "Is Stale (Object ID)")
	static bool ObjectId_IsAlive(const FPersistentStateObjectId& ObjectId);

	/** @return true if Object ID points to a static object - loaded from a package or assigned a stable name at runtime */
	UFUNCTION(BlueprintPure, Category = "Persistent State", DisplayName = "Is Object Static")
	static bool ObjectId_IsStatic(const FPersistentStateObjectId& ObjectId);

	/** @return true if Object ID points to a dynamic object - created at runtime and does not have a stable name */
	UFUNCTION(BlueprintPure, Category = "Persistent State", DisplayName = "Is Object Dynamic")
	static bool ObjectId_IsDynamic(const FPersistentStateObjectId& ObjectId);

	/** @return Object ID associated with given UObject created beforehand or default */
	UFUNCTION(BlueprintPure, Category = "Persistent State", DisplayName = "Get Object ID")
	static FPersistentStateObjectId ObjectId_FindObjectId(const UObject* Object);

	/** @return Object ID created from UObject */
	UFUNCTION(BlueprintPure, Category = "Persistent State", DisplayName = "Create Object ID")
	static FPersistentStateObjectId ObjectId_CreateObjectId(const UObject* Object);

	/** @return live UObject associated with the Object ID */
	UFUNCTION(BlueprintPure, Category = "Persistent State", DisplayName = "Resolve Object")
	static UObject* ObjectId_ResolveObject(const FPersistentStateObjectId& ObjectId);

	/** @return live UObject associated with the Object ID */
	UFUNCTION(BlueprintPure, Category = "Persistent State", DisplayName = "Resolve Object (Class)", meta = (DeterminesOutputType = "ObjectClass"))
	static UObject* ObjectId_ResolveObjectWithClass(const FPersistentStateObjectId& ObjectId, TSubclassOf<UObject> ObjectClass);
};
