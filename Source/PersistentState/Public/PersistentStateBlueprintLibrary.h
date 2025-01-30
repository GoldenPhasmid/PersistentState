#pragma once

#include "CoreMinimal.h"
#include "PersistentStateObjectId.h"
#include "Kismet/BlueprintFunctionLibrary.h"

#include "PersistentStateBlueprintLibrary.generated.h"

struct FPersistentStateSlotHandle;
struct FPersistentStateObjectId;
class UPersistentStateSlotDescriptor;

/**
 * Blueprint library that exposes core persistent state functionality to Blueprints
 * @see UPersistentStateSubsystem for all available functionality. Some common functions are duplicated for easy access
 */
UCLASS()
class PERSISTENTSTATE_API UPersistentStateBlueprintLibrary: public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:

	/** @return active save game slot that is currently used by the game, or invalid handle if no slot is currently used by the game  */
	UFUNCTION(BlueprintCallable, Category = "Persistent State", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
	static FPersistentStateSlotHandle GetActiveSaveGameSlot(const UObject* WorldContextObject);
	
	/**
	 * @return save game slot descriptor for the associated slot handle, or nullptr if game has not been saved to a given slot
	 * @see UPersistentStateSlotDescriptor for more information.
	 */
	UFUNCTION(BlueprintCallable, Category = "Persistent State", meta = (WorldContext = "WorldContextObject", DefaultToSelf = "WorldContextObject"))
	static UPersistentStateSlotDescriptor* GetSaveGameSlotDescriptor(const UObject* WorldContextObject, const FPersistentStateSlotHandle& SaveGameSlot);

	/** capture game screenshot to the provided save game slot */
	UFUNCTION(BlueprintCallable, Category = "Persistent State", meta = (WorldContext = "WorldContextObject"))
	void CaptureScreenshot(const UObject* WorldContextObject, const FPersistentStateSlotHandle& SaveGameSlot);
	
	/** @return true if persistent state does screenshots in current configuration */
	UFUNCTION(BlueprintPure, Category = "Persistent State")
	static bool HasScreenshotSupport();

protected:
	
	/**
	 * @return true if slot handle points to an existing state slot
	 */
	UFUNCTION(BlueprintPure, Category = "Persistent State", DisplayName = "Is Valid (Slot Handle)")
	static bool SlotHandle_IsValid(const FPersistentStateSlotHandle& SlotHandle);

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
