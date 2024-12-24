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
	
	UFUNCTION(BlueprintCallable, Category = "Persistent State", DisplayName = "Reset Object ID")
	static void ObjectId_Reset(FPersistentStateObjectId& ObjectId);

	UFUNCTION(BlueprintPure, Category = "Persistent State", DisplayName = "Is Valid (Object ID)")
	static bool ObjectId_IsValid(const FPersistentStateObjectId& ObjectId);
	
	UFUNCTION(BlueprintPure, Category = "Persistent State", DisplayName = "Is Object Static")
	static bool ObjectId_IsStatic(const FPersistentStateObjectId& ObjectId);

	UFUNCTION(BlueprintPure, Category = "Persistent State", DisplayName = "Is Object Dynamic")
	static bool ObjectId_IsDynamic(const FPersistentStateObjectId& ObjectId);

	UFUNCTION(BlueprintPure, Category = "Persistent State", DisplayName = "Get Object ID")
	static FPersistentStateObjectId ObjectId_FindObjectId(const UObject* Object);
	
	UFUNCTION(BlueprintPure, Category = "Persistent State", DisplayName = "Create Object ID")
	static FPersistentStateObjectId ObjectId_CreateObjectId(const UObject* Object);

	UFUNCTION(BlueprintPure, Category = "Persistent State", DisplayName = "Resolve Object")
	static UObject* ObjectId_ResolveObject(const FPersistentStateObjectId& ObjectId);
	
	UFUNCTION(BlueprintPure, Category = "Persistent State", DisplayName = "Resolve Object (Class)", meta = (DeterminesOutputType = "ObjectClass"))
	static UObject* ObjectId_ResolveObjectWithClass(const FPersistentStateObjectId& ObjectId, TSubclassOf<UObject> ObjectClass);
};
