#pragma once

#include "CoreMinimal.h"

#include "PersistentStateSlotView.generated.h"

class UPersistentStateStorage;
struct FPersistentStateSlot;

/**
 * Handle that references a particular slot by named
 */
USTRUCT(BlueprintType)
struct PERSISTENTSTATE_API FPersistentStateSlotHandle
{
	GENERATED_BODY()

	FPersistentStateSlotHandle() = default;
	FPersistentStateSlotHandle(const UPersistentStateStorage& InStorage, const FName& InSlotName);

	bool IsValid() const;
	FORCEINLINE FName GetSlotName() const { return SlotName; }
	FORCEINLINE FString ToString() const { return SlotName.ToString(); }

	static FPersistentStateSlotHandle InvalidHandle;
private:
	// @todo: store WeakPtr to FPersistentStateSlot instead of SlotName, because unwanted collisions may occur
	FName SlotName = NAME_None;
	TWeakObjectPtr<const UPersistentStateStorage> WeakStorage;
};

FORCEINLINE bool operator==(const FPersistentStateSlotHandle& A, const FPersistentStateSlotHandle& B)
{
	return A.GetSlotName() == B.GetSlotName();
}

FORCEINLINE bool operator!=(const FPersistentStateSlotHandle& A, const FPersistentStateSlotHandle& B)
{
	return A.GetSlotName() != B.GetSlotName();
}

/**
 * Blueprint view of slot information
 */
USTRUCT(BlueprintType)
struct FPersistentStateSlotDesc
{
	GENERATED_BODY()
	
	FPersistentStateSlotDesc() = default;
	explicit FPersistentStateSlotDesc(const FPersistentStateSlot& Slot);

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName SlotName = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FText SlotTitle = FText::GetEmpty();

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString FilePath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FDateTime LastSaveTimestamp;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FName LastSavedWorld = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FName> SavedWorlds;

	FString ToString() const;
};