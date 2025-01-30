#pragma once

#include "CoreMinimal.h"
#include "PersistentStateSlotView.h"

#include "PersistentStateSlotDescriptor.generated.h"

/**
 * Save Game Slot Descriptor
 * Contains persistent information about the state slot as well as user-defined information, that can be retrieved without
 * loading any game or world data.
 * Users are encouraged to create a derived descriptor class and define custom save-specific information: 
 * "Character Class" that save uses, amount of money the player has, some progression info, etc.
 * @note descriptor data is loaded when state slot is created. Try to keep store data that is only necessary to
 * describe save game file to the player in UI.
 * 
 * State system is not required to manager descriptor's lifetime, so avoid holding weak references to it.
 * @see @GetSaveGameSlotDescriptor
 */
UCLASS(Blueprintable, BlueprintType)
class PERSISTENTSTATE_API UPersistentStateSlotDescriptor: public UObject
{
	GENERATED_BODY()
public:
	
	/** execute save descriptor callback */
	void SaveDescriptor(UWorld* World, const FPersistentStateSlotHandle& InHandle);
	/** execute load descriptor callback, after descriptor has been serialized with persistent state slot data */
	void LoadDescriptor(UWorld* World, const FPersistentStateSlotHandle& InHandle, const FPersistentStateSlotDesc& InDesc);

	/** @return world that should be loaded from a state slot */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Persistent State")
	FName GetWorldToLoad() const;
	
	/** @return text description for a state slot */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "Persistent State")
	FString DescribeStateSlot() const;
	
	FORCEINLINE FName GetSlotName() const { return SlotDescription.SlotName; }
	FORCEINLINE const FPersistentStateSlotHandle&	GetSlotHandle() const { return SlotHandle; }
	FORCEINLINE const FPersistentStateSlotDesc&		GetSlotDescription() const { return SlotDescription; }
protected:
	
	/**
	 * Calculate and save any 
	 */
	virtual void OnSaveDescriptor(UWorld* World);

	/**
	 * 
	 */
	virtual void OnLoadDescriptor(UWorld* World);

	UFUNCTION(BlueprintImplementableEvent, Category = "Persistent State", DisplayName = "Receive Save Descriptor")
	void K2_OnSaveDescriptor(UWorld* World);

	UFUNCTION(BlueprintImplementableEvent, Category = "Persistent State", DisplayName = "Receive Load Descriptor")
	void K2_OnLoadDescriptor(UWorld* World);
	
	/** slot description, filled when slot descriptor is created for load */
	UPROPERTY(BlueprintReadOnly, Transient)
	FPersistentStateSlotDesc SlotDescription;
	
	UPROPERTY(BlueprintReadOnly, Transient)
	FPersistentStateSlotHandle SlotHandle;
};
