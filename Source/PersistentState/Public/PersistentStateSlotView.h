#pragma once

#include "CoreMinimal.h"

#include "PersistentStateSlotView.generated.h"

class UPersistentStateStorage;
struct FPersistentStateSlot;

/**
 * State Slot Handle
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
struct PERSISTENTSTATE_API FPersistentStateSlotDesc
{
	GENERATED_BODY()
	
	FPersistentStateSlotDesc() = default;
	explicit FPersistentStateSlotDesc(const FPersistentStateSlot& Slot);

	/** @return string representation of a slot description, mainly for debug purposes */
	FString ToString() const;
	
	friend FORCEINLINE bool operator==(const FPersistentStateSlotDesc& A, const FPersistentStateSlotDesc& B)
	{
		return	A.SlotName == B.SlotName && A.SlotTitle.EqualTo(B.SlotTitle) &&
				A.FilePath == B.FilePath && A.LastSaveTimestamp == B.LastSaveTimestamp &&
				A.LastSavedWorld == B.LastSavedWorld && A.SavedWorlds == B.SavedWorlds;
	}

	FORCEINLINE bool HasGameState() const { return bHasGameState; }
	FORCEINLINE bool HasWorldState(FName World) const { return SavedWorlds.Contains(World); }

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bHasGameState = false;
	
};