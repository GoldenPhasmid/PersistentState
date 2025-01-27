#include "PersistentStateObjectId.h"

#include "PersistentStateModule.h"
#include "PersistentStateStatics.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

/** Annotation associating objects with their guids **/
static FUObjectAnnotationSparseSearchable<FPersistentStateObjectId, true> GuidAnnotation;

void AddNewAnnotation(const UObject* Object, const FPersistentStateObjectId& Id)
{
	UObject* OtherObject = GuidAnnotation.Find(Id);
	// objects removed from the Annotation map only when they're fully cleaned up by FUObjectArray, which is very close
	// to their full destruction by AsyncPurge thread. However, MirroredGarbage objects still present in the annotation
	// and occupy the object ID. It is frequently caused by Level Streaming, when old object is already garbage collected
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
	// log to fail tests
	UE_LOG(LogPersistentState, Error, TEXT("GUID %s is already generated for object with name %s"), *OtherId.ToString(), *OtherId.GetObjectName());
	checkf(false, TEXT("GUID %s is already generated for object with name %s"), *OtherId.ToString(), *OtherId.GetObjectName());
#else
	check(GuidAnnotation.GetAnnotation(Object).IsDefault());
	check(GuidAnnotation.Find(Id) == nullptr);
#endif // WITH_EDITOR
}

void FPersistentStateObjectId::AssignSerializedObjectId(FPersistentStateObjectIdScope& Initializer, const UObject* Object, const FPersistentStateObjectId& Id)
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
		
		// create dynamic id if expected ID is not static or we failed to create a static ID
		if	(ExpectType != EExpectObjectType::Static && ObjectType == EExpectObjectType::None &&
			(ExpectType == EExpectObjectType::None || !UE::PersistentState::HasStableName(*Object)))
		{
			// ObjectType == None - either we skipped creation of a static id or failed to do it
			// ExpectType == None - we failed to create static id, otherwise we have to verify that name is not stable
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
	bool bWithObjectName = WITH_OBJECT_NAME;
	Ar.SerializeBits(&bWithObjectName, 1);

#if WITH_OBJECT_NAME
	// save object name. or load object name if save was performed in editor
	if (Ar.IsSaving() || bWithObjectName)
	{
		Ar << Value.ObjectName;
	}
#else
	// if load archive and save came from editor, create a local ObjectName, serialize and drop it
	// do nothing for save archive
	if (bWithObjectName && Ar.IsLoading())
	{
		FString ObjectName;
		Ar << ObjectName;
	}
#endif // WITH_OBJECT_NAME
#endif // WITH_EDITOR_COMPATIBILITY
	
	return Ar;
}

FPersistentStateObjectIdScope::FPersistentStateObjectIdScope(const FPersistentStateObjectId& InObjectID, const FName& InObjectName, UClass* InObjectClass)
	: ObjectID(InObjectID)
	, ObjectName(InObjectName)
	, ObjectClass(InObjectClass)
{
	GUObjectArray.AddUObjectCreateListener(this);
}

FPersistentStateObjectIdScope::~FPersistentStateObjectIdScope()
{
	GUObjectArray.RemoveUObjectCreateListener(this);
}

void FPersistentStateObjectIdScope::NotifyUObjectCreated(const class UObjectBase* Object, int32 Index)
{
	if (!bCompleted)
	{
		if (Object->GetFName() == ObjectName && Object->GetClass() == ObjectClass)
		{
			FPersistentStateObjectId::AssignSerializedObjectId(*this, static_cast<const UObject*>(Object), ObjectID);
			bCompleted = true;
		}
	}
}

void FPersistentStateObjectIdScope::OnUObjectArrayShutdown()
{
	checkNoEntry();
}

FPersistentStateObjectPathGenerator FPersistentStateObjectPathGenerator::Instance;

FString FPersistentStateObjectPathGenerator::GetStableWorldPackage(const UWorld* InWorld)
{
	if (!InWorld->bIsWorldInitialized)
	{
		return {};
	}
	
	CacheWorldPackage(InWorld);
	return WorldPackageMap.FindChecked(InWorld).ToString();
}

FString FPersistentStateObjectPathGenerator::RemapObjectPath(const UObject& Object, const FString& InPathName)
{
	// remap world owned objects to the original package name
	if (UWorld* OuterWorld = Object.GetTypedOuter<UWorld>())
	{
		UPackage* Package = CastChecked<UPackage>(OuterWorld->GetOuter());
		check(Package);
		
		const FString CurrentPackage = Package->GetName();
		FString SourcePackage = GetStableWorldPackage(OuterWorld);
		if (SourcePackage.IsEmpty())
		{
			// code path for WP level packages
			SourcePackage = CurrentPackage;
			// remove Memory package prefix for streaming WP levels
			SourcePackage.RemoveFromStart(TEXT("/Memory"));
			// remove PIE package prefix
			SourcePackage = UWorld::RemovePIEPrefix(SourcePackage);
		}
		
		if (SourcePackage != CurrentPackage && InPathName.Contains(CurrentPackage))
		{
			// package starts from the beginning
			const int32 PackageNameEndIndex = CurrentPackage.Len();
			return SourcePackage + InPathName.RightChop(PackageNameEndIndex);
		}
	}

	return InPathName;
}

FPersistentStateObjectPathGenerator::FPersistentStateObjectPathGenerator()
{
	WorldCleanupHandle = FWorldDelegates::OnWorldCleanup.AddRaw(this, &ThisClass::OnWorldCleanup);
}

FPersistentStateObjectPathGenerator::~FPersistentStateObjectPathGenerator()
{
	FWorldDelegates::OnWorldCleanup.Remove(WorldCleanupHandle);
}

void FPersistentStateObjectPathGenerator::OnWorldCleanup(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	WorldPackageMap.Remove(World);
}

void FPersistentStateObjectPathGenerator::CacheWorldPackage(const UWorld* InWorld)
{
	if (WorldPackageMap.Contains(InWorld))
	{
		return;
	}

#if WITH_EDITOR_COMPATIBILITY
	const FName WorldName = InWorld->GetFName();
	// Look up in the AssetRegistry
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	check(FPackageName::IsShortPackageName(WorldName));
	
	const FName SourcePackageName = AssetRegistry.GetFirstPackageByName(WorldName.ToString());
	if (SourcePackageName.IsNone())
	{
		// world is created on the fly and not in memory, use world name as a package name
		WorldPackageMap.Add(InWorld, WorldName);
	}
	else
	{
		// world exists in secondary storage. This is a case for PIE worlds, that exists on storage but loaded into a different package
		// In this case we remap current package name to an original package name stored on disk
		WorldPackageMap.Add(InWorld, SourcePackageName);
	}
#else
	WorldPackageMap.Add(InWorld, InWorld->GetPackage()->GetFName());
#endif
}

