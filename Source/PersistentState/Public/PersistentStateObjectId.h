#pragma once

#include "CoreMinimal.h"

#include "PersistentStateObjectId.generated.h"

struct FPersistentStateObjectId;

/**
 * Struct that associates loaded game objects with a deterministic object ID, that is persistent between game runs
 * Objects with RF_Loaded flag, stable name, known globals, default subobjects or subobjects which outer chain
 * has been mapped to an object ID are considered Static. Those object can deterministically restore their ID after load.
 * 
 * Other object types are considered Dynamic. In this case, ID is generated once when object is associated for the first
 * time, and then restored after each respawn. Currently Persistent State System is fully resposible for re-creating
 * known Dynamic Objects.
 * 
 * You can also use IPersistentStateObject::GetStableName to make your object known as Static and associate it
 * with a stable object ID. Core game classes that are spawned at runtime like Game Mode, Game State and Player Controllers
 * can use to become Static and still allow Persistent State System to properly identify and restore them.
 * 
 * It also requires only a few lines of code. Stable name is generated using full outer chain, so your Game Mode have
 * different stable name on different maps.
 * class AMyGameMode: public AGameMode, public IPersistentStateObject
 * {
 *		virtual FName GetStableName() const override { return GetClass()->GetFName(); }
 * };
 *
 * Underlying implementation is a GUID generated based on a full object name.
 */
USTRUCT()
struct PERSISTENTSTATE_API FPersistentStateObjectId
{
	GENERATED_BODY()
public:

	/**
	 * Tries to create an object ID from a loaded object or spawned object with a stable name.
	 * If object already has object ID, returns it. If object is Dynamic, returned ID is not valid
	 */
	static FPersistentStateObjectId CreateStaticObjectId(const UObject* Object);
	
	/**
	 * Tries to create an object ID from a dynamically spawned object without a stable name.
	 * If object already has object ID, returns it. If object is Dynamic, returned ID is not valid
	 */
	static FPersistentStateObjectId CreateDynamicObjectId(const UObject* Object);
	
	/** Creates an object ID for object, either Static or Dynamic. If object already has object ID, returns it */
	static FPersistentStateObjectId CreateObjectId(const UObject* Object);
	
	/** @return valid object ID associated with an object, or none */
	static FPersistentStateObjectId FindObjectId(const UObject* Object);
	
	FPersistentStateObjectId() = default;
	FPersistentStateObjectId(const FPersistentStateObjectId& Other) = default;
	FPersistentStateObjectId(FPersistentStateObjectId&& Other) = default;
	FPersistentStateObjectId& operator=(const FPersistentStateObjectId& Other) = default;
	FPersistentStateObjectId& operator=(FPersistentStateObjectId&& Other) = default;

	/** attempts to find loaded object associated with ID, nullptr if it is not currently in memory */
	UObject* ResolveObject() const;

	/** attempts to find loaded object associated with ID, nullptr if it is not currently in memory */
	template <typename T>
	T* ResolveObject() const
	{
		return CastChecked<T>(ResolveObject(), ECastCheckedType::NullAllowed);
	}

	/** @return object ID */
	FORCEINLINE FGuid GetObjectID() const
	{
		return ObjectID;
	}

	/** @return true if this object stores a valid object ID */
	FORCEINLINE bool IsValid() const
	{
		return ObjectID.IsValid();
	}

	/** @return true if ID is a default object */
	FORCEINLINE bool IsDefault() const
	{
		return !IsValid();
	}

	/** @return true if ID is */
	FORCEINLINE bool IsStatic() const
	{
		return ObjectType == EExpectObjectType::Static;
	}

	FORCEINLINE bool IsDynamic() const
	{
		return ObjectType == EExpectObjectType::Dynamic;
	}

	FORCEINLINE void Reset()
	{
		*this = FPersistentStateObjectId{};
	}

	FORCEINLINE bool operator==(const FPersistentStateObjectId& Other) const
	{
		return ObjectID == Other.ObjectID && ObjectType == Other.ObjectType;
	}

	FORCEINLINE bool operator!=(const FPersistentStateObjectId& Other) const
	{
		return ObjectID != Other.ObjectID;
	}

#if WITH_EDITOR
	/** @return object name that was used to generate ID. Used mainly for debugging purposes */
	FORCEINLINE FString GetObjectName() const
	{
		return ObjectName;
	}
#endif

	/** output object ID in a string format */
	FORCEINLINE FString ToString() const
	{
		return ObjectID.ToString();
	}

	static void AssignSerializedObjectId(const UObject* Object, const FPersistentStateObjectId& Id);

	// serialization
	bool Serialize(FArchive& Ar);
	friend FArchive& operator<<(FArchive& Ar, FPersistentStateObjectId& Value);
private:
	enum class EExpectObjectType { None = 255, Static = 0, Dynamic = 1 };
	explicit FPersistentStateObjectId(const UObject* Object, bool bCreateNew = true, EExpectObjectType ExpectType = EExpectObjectType::None);
	/** initializing constructor, used to associate Dynamic object with previously associated ID */
	explicit FPersistentStateObjectId(const FGuid& Id);
	
	/** object ID */
	FGuid ObjectID;
	/** object type, either Static or Dynamic */
	EExpectObjectType ObjectType = EExpectObjectType::None;
	/** weak object reference */
	mutable FWeakObjectPtr WeakObject;
#if WITH_EDITOR
	/** object name that was used to generate object ID */
	FString ObjectName;
#endif
};

FORCEINLINE FString LexToString(const FPersistentStateObjectId& Value)
{
	return Value.ToString();
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
	};
};

template<> struct TCanBulkSerialize<FPersistentStateObjectId> { enum { Value = true }; };

/**
 * Helper class to assign ObjectID to a newly created object
 * Should be created on a stack in a scope of NewObject call
 */
class FUObjectIDInitializer: public FUObjectArray::FUObjectCreateListener
{
public:
	FUObjectIDInitializer(const FPersistentStateObjectId& InObjectID, const FName& InObjectName, UClass* InObjectClass);
	virtual ~FUObjectIDInitializer() override;

private:

	virtual void NotifyUObjectCreated(const class UObjectBase* Object, int32 Index) override;
	virtual void OnUObjectArrayShutdown() override;

	FPersistentStateObjectId ObjectID;
	const FName& ObjectName;
	UClass* ObjectClass;
};
