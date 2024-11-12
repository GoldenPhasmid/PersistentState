#pragma once

#include "CoreMinimal.h"

#include "PersistentStateObjectId.generated.h"

USTRUCT()
struct PERSISTENTSTATE_API FPersistentStateObjectId
{
	GENERATED_BODY()
public:

	/** @return */
	static FPersistentStateObjectId CreateStaticObjectId(const UObject* Object);
	/** */
	static FPersistentStateObjectId CreateDynamicObjectId(const UObject* Object);
	/** */
	static FPersistentStateObjectId CreateObjectId(const UObject* Object);
	/** */
	static FPersistentStateObjectId FindObjectId(const UObject* Object);
	
	FPersistentStateObjectId() = default;

	FPersistentStateObjectId(const FPersistentStateObjectId& Other) = default;
	FPersistentStateObjectId(FPersistentStateObjectId&& Other) = default;
	FPersistentStateObjectId& operator=(const FPersistentStateObjectId& Other) = default;
	FPersistentStateObjectId& operator=(FPersistentStateObjectId&& Other) = default;

	UObject* ResolveObject() const;

	template <typename T>
	T* ResolveObject() const
	{
		return CastChecked<T>(ResolveObject(), ECastCheckedType::NullAllowed);
	}
	
	FORCEINLINE bool IsValid() const
	{
		return ObjectID.IsValid();
	}

	FORCEINLINE bool IsDefault() const
	{
		return !IsValid();
	}

	FORCEINLINE void Reset()
	{
		*this = FPersistentStateObjectId{};
	}

	FORCEINLINE bool operator==(const FPersistentStateObjectId& Other) const
	{
		return ObjectID == Other.ObjectID;
	}

	FORCEINLINE bool operator!=(const FPersistentStateObjectId& Other) const
	{
		return ObjectID != Other.ObjectID;
	}
	
	FORCEINLINE FGuid GetObjectID() const
	{
		return ObjectID;
	}

#if WITH_EDITOR
	FORCEINLINE FString GetObjectName() const
	{
		return ObjectName;
	}
#endif

	FORCEINLINE FString ToString() const
	{
		return ObjectID.ToString();
	}

	static void AssignSerializedObjectId(UObject* Object, const FGuid& Guid);

	// serialization
	bool Serialize(FArchive& Ar);
	bool Serialize(FStructuredArchive::FSlot Slot);
	friend FArchive& operator<<(FArchive& Ar, FPersistentStateObjectId& Value);
private:
	enum class EExpectObjectType { None, Static, Dynamic };
	explicit FPersistentStateObjectId(const UObject* Object, bool bCreateNew = true, EExpectObjectType ExpectType = EExpectObjectType::None);
	explicit FPersistentStateObjectId(const FGuid& Id);
	
	/** object ID */
	FGuid ObjectID;
	/** weak object reference */
	mutable FWeakObjectPtr WeakObject;
#if WITH_EDITOR
	/** object name that was used to generate object id */
	FString ObjectName;
#endif
};

FORCEINLINE FString LexToString(const FPersistentStateObjectId& Value)
{
	return Value.GetObjectID().ToString();
}

FORCEINLINE uint32 GetTypeHash(const FPersistentStateObjectId& Value)
{
	return GetTypeHash(Value.GetObjectID());
}

template <>
struct TStructOpsTypeTraits<FPersistentStateObjectId>: public TStructOpsTypeTraitsBase2<FPersistentStateObjectId>
{
	enum
	{
		WithSerializer = true,
		WithStructuredSerializer = true,
	};
};

template<> struct TCanBulkSerialize<FPersistentStateObjectId> { enum { Value = true }; };
