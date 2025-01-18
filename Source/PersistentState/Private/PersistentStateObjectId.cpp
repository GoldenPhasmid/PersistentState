#include "PersistentStateObjectId.h"

#include "PersistentStateStatics.h"

/** Annotation associating objects with their guids **/
static FUObjectAnnotationSparseSearchable<FPersistentStateObjectId, true> GuidAnnotation;

void AddNewAnnotation(const UObject* Object, const FPersistentStateObjectId& Id)
{
	UObject* OtherObject = GuidAnnotation.Find(Id);
	// objects removed from the Annotation map only when they're fully cleaned up by FUObjectArray, which is very close
	// to their full destruction by AsyncPurge thread. However, MirroredGarbage objects still present in the annotation
	// and occupy the object ID. It is frequenly caused by Level Streaming, when old object is already garbage collected
	// and new one is streamed in, thus causing ID collision.
	// We politely ignore such cases, as there's no good way to track "only live" objects
	if (!IsValid(OtherObject))
	{
		GuidAnnotation.AddAnnotation(Object, Id);
#if WITH_UNIQUE_OBJECT_ID_ANNOTATION
		FUniqueObjectGuid::AssignIDForObject(Object, Id.GetObjectID());
#endif
	
		return;
	}

	// If OtherObject is valid then it is a real ID collision and something is wrong with our game code.
#if WITH_EDITOR
	FPersistentStateObjectId OtherId = GuidAnnotation.GetAnnotation(OtherObject);
	checkf(false, TEXT("GUID %s is already generated for object with name %s"), *OtherId.ToString(), *OtherId.GetObjectName());
#else
	check(GuidAnnotation.GetAnnotation(Object).IsDefault());
	check(GuidAnnotation.Find(Id) == nullptr);
#endif // WITH_EDITOR
}

void FPersistentStateObjectId::AssignSerializedObjectId(const UObject* Object, const FPersistentStateObjectId& Id)
{
	check(Object && Id.IsValid());
	
	Id.WeakObject = Object;
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
	
	*this = GuidAnnotation.GetAnnotation(Object);
	WeakObject = Object;
	
	if (bCreateNew && !IsValid())
	{
		// create static id if expected ID is not dynamic
		if (ExpectType != EExpectObjectType::Dynamic)
		{
			FString StableName = UE::PersistentState::GetStableName(*Object);
            if (!StableName.IsEmpty())
            {
            	ObjectID = FGuid::NewDeterministicGuid(StableName, UE::PersistentState::GetGuidSeed());
            	ObjectType = EExpectObjectType::Static;
#if WITH_OBJECT_NAME
            	ObjectName = StableName;
#endif // WITH_OBJECT_NAME
            }
		}
		// create dynamic id if expected ID is not static
		else if (ExpectType == EExpectObjectType::None || (ExpectType == EExpectObjectType::Dynamic && UE::PersistentState::HasStableName(*Object) == false))
		{
			ObjectID = FGuid::NewGuid();
			ObjectType = EExpectObjectType::Dynamic;
#if WITH_OBJECT_NAME
			ObjectName = Object->GetName();
#endif // WITH_OBJECT_NAME
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

#if WITH_STRUCTURED_SERIALIZATION
bool FPersistentStateObjectId::Serialize(FStructuredArchive::FSlot Slot)
{
	Slot << *this;
	return true;
}

void operator<<(FStructuredArchive::FSlot Slot, FPersistentStateObjectId& Value)
{
	FStructuredArchive::FRecord Record = Slot.EnterRecord();
	FArchive& Ar = Record.GetUnderlyingArchive();
	
	Record << SA_VALUE(TEXT("ObjectID"), Value.ObjectID);
#if WITH_OBJECT_NAME
	Record << SA_VALUE(TEXT("ObjectName"), Value.ObjectName);
#endif // WITH_OBJECT_NAME

	// serialize object type as String
	FString ObjectTypeStr;
	if (Ar.IsSaving())
	{
		ObjectTypeStr = [](FPersistentStateObjectId::EExpectObjectType ObjectType)
		{
			switch (ObjectType)
			{
			case FPersistentStateObjectId::EExpectObjectType::None:
				return TEXT("None");
			case FPersistentStateObjectId::EExpectObjectType::Static:
				return TEXT("Static");
			case FPersistentStateObjectId::EExpectObjectType::Dynamic:
				return TEXT("Dynamic");
			default:
				checkNoEntry();
			}
			return TEXT("None");
		}(Value.ObjectType);
	}
	
	Record << SA_VALUE(TEXT("ObjectType"), ObjectTypeStr);
	
	if (Ar.IsLoading())
	{
		Value.ObjectType = [](const FString& Str)
		{
			return Str == TEXT("Static")
				? FPersistentStateObjectId::EExpectObjectType::Static
				: (Str == TEXT("Dynamic")
					? FPersistentStateObjectId::EExpectObjectType::Dynamic
					: FPersistentStateObjectId::EExpectObjectType::None);	
		}(ObjectTypeStr);
	}
}
#endif // WITH_STRUCTURED_SERIALIZATION 

bool FPersistentStateObjectId::Serialize(FArchive& Ar)
{
	Ar << *this;
	return true;
}

FArchive& operator<<(FArchive& Ar, FPersistentStateObjectId& Value)
{
	bool bValid = Value.ObjectID.IsValid();
	Ar.SerializeBits(&bValid, 1);

	if (bValid == false)
	{
		return Ar;
	}
	
	Ar << Value.ObjectID;

	check(Ar.IsLoading() || Value.ObjectType != FPersistentStateObjectId::EExpectObjectType::None);
	Ar.SerializeBits(&Value.ObjectType, 1);
	check(Ar.IsSaving() || Value.ObjectType != FPersistentStateObjectId::EExpectObjectType::None);
	
#if WITH_EDITOR_COMPATIBILITY
	bool bWithEditor = WITH_EDITOR;
	Ar.SerializeBits(&bWithEditor, 1);

#if WITH_OBJECT_NAME
	// save object name. or load object name if save was performed in editor
	if (Ar.IsSaving() || bWithEditor)
	{
		Ar << Value.ObjectName;
	}
#else
	// if load archive and save came from editor, create a local ObjectName, serialize and drop it
	// do nothing for save archive
	if (bWithEditor && Ar.IsLoading())
	{
		FString ObjectName;
		Ar << ObjectName;
	}
#endif // WITH_OBJECT_NAME
#endif // WITH_EDITOR_COMPATIBILITY
	
	return Ar;
}

FUObjectIDInitializer::FUObjectIDInitializer(const FPersistentStateObjectId& InObjectID, const FName& InObjectName, UClass* InObjectClass)
	: ObjectID(InObjectID)
	, ObjectName(InObjectName)
	, ObjectClass(InObjectClass)
{
	GUObjectArray.AddUObjectCreateListener(this);
}

FUObjectIDInitializer::~FUObjectIDInitializer()
{
	GUObjectArray.RemoveUObjectCreateListener(this);
}

void FUObjectIDInitializer::NotifyUObjectCreated(const class UObjectBase* Object, int32 Index)
{
	if (Object->GetFName() == ObjectName && Object->GetClass() == ObjectClass)
	{
		FPersistentStateObjectId::AssignSerializedObjectId(static_cast<const UObject*>(Object), ObjectID);
	}
}

void FUObjectIDInitializer::OnUObjectArrayShutdown()
{
	checkNoEntry();
}

