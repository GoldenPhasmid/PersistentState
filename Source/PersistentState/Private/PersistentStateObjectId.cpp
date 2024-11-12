#include "PersistentStateObjectId.h"

#include "PersistentStateStatics.h"

#define WITH_EDITOR_COMPATIBILITY !UE_BUILD_SHIPPING

/** Annotation associating objects with their guids **/
static FUObjectAnnotationSparseSearchable<FPersistentStateObjectId, true> GuidAnnotation;

void AddNewAnnotation(const UObject* Object, const FPersistentStateObjectId& Id)
{
	check(GuidAnnotation.GetAnnotation(Object).IsDefault());
	check(GuidAnnotation.Find(Id) == nullptr);
	
	GuidAnnotation.AddAnnotation(Object, Id);
	FUniqueObjectGuid::AssignIDForObject(Object, Id.GetObjectID());
}

void FPersistentStateObjectId::AssignSerializedObjectId(UObject* Object, const FGuid& Guid)
{
	check(Object && Guid.IsValid());
	
	FPersistentStateObjectId Id{Guid};
	AddNewAnnotation(Object, Id);
}

FPersistentStateObjectId::FPersistentStateObjectId(const FGuid& Id)
{
	ObjectID = Id;
}

FPersistentStateObjectId::FPersistentStateObjectId(const UObject* Object, bool bCreateNew, EExpectObjectType ExpectType)
{
	check(Object);
	check(IsInGameThread());

	WeakObject = Object;
	*this = GuidAnnotation.GetAnnotation(Object);
	
	if (bCreateNew && !IsValid())
	{
		// create static id if expected ID is not dynamic
		if (ExpectType != EExpectObjectType::Dynamic)
		{
			FString StableName = UE::PersistentState::GetStableName(*Object);
            if (!StableName.IsEmpty())
            {
            	ObjectID = FGuid::NewDeterministicGuid(StableName, UE::PersistentState::GetGuidSeed());
#if WITH_EDITOR
            	ObjectName = StableName;
#endif
            }
		}
		// create dynamic id if expected ID is not static
		else if (ExpectType == EExpectObjectType::None || (ExpectType == EExpectObjectType::Dynamic && UE::PersistentState::HasStableName(*Object) == false))
		{
			ObjectID = FGuid::NewGuid();
		}

		if (IsValid())
		{
			AddNewAnnotation(Object, *this);
		}
	}
}

UObject* FPersistentStateObjectId::ResolveObject() const
{
	if (!IsValid())
	{
		return nullptr;
	}
	
	constexpr bool bEvenIfGarbage = false;
	if (UObject* Object = WeakObject.Get(bEvenIfGarbage))
	{
		return Object;
	}

	WeakObject = GuidAnnotation.Find(*this);
	return WeakObject.Get(bEvenIfGarbage);
}

FPersistentStateObjectId FPersistentStateObjectId::CreateStaticObjectId(const UObject* Object)
{
	check(Object);
	return FPersistentStateObjectId{Object, true, EExpectObjectType::Static};
}

FPersistentStateObjectId FPersistentStateObjectId::CreateDynamicObjectId(const UObject* Object)
{
	check(Object);
	return FPersistentStateObjectId{Object, true, EExpectObjectType::Dynamic};
}

FPersistentStateObjectId FPersistentStateObjectId::CreateObjectId(const UObject* Object)
{
	check(Object);
	return FPersistentStateObjectId{Object, true, EExpectObjectType::None};
}

FPersistentStateObjectId FPersistentStateObjectId::FindObjectId(const UObject* Object)
{
	check(Object);
	return FPersistentStateObjectId{Object, false};
}

bool FPersistentStateObjectId::Serialize(FArchive& Ar)
{
	Ar << *this;
	return true;
}

bool FPersistentStateObjectId::Serialize(FStructuredArchive::FSlot Slot)
{
	Slot << *this;
	return true;
}

FArchive& operator<<(FArchive& Ar, FPersistentStateObjectId& Value)
{
	Ar << Value.ObjectID;
#if WITH_EDITOR_COMPATIBILITY
	bool bWithEditor = WITH_EDITOR;
	Ar.SerializeBits(&bWithEditor, 1);

#if WITH_EDITOR
	Ar << Value.ObjectName;
#else
	if (bWithEditor && Ar.IsLoading())
	{
		FString ObjectName;
		Ar << ObjectName;
	}
#endif // WITH_EDITOR
#endif // WITH_EDITOR_COMPATIBILITY
	
	return Ar;
}
