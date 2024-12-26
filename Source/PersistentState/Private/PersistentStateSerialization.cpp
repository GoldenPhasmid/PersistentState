#include "PersistentStateSerialization.h"

#include "PersistentStateObjectId.h"

FArchive& FPersistentStateProxyArchive::operator<<(UObject*& Obj)
{
	// try to serialize object property using unique object id, created by state system beforehand
	// if it fails, fallback to object-path-as-string serialization, so we can safely save references
	// to top level assets (data assets, data tables, etc.)
	bool bObjectValid = IsSaving() ? IsValid(Obj) : false;
	InnerArchive.SerializeBits(&bObjectValid, 1);

	if (!bObjectValid)
	{
		Obj = nullptr;
		return *this;
	}

	if (IsSaving())
	{
		FPersistentStateObjectId ObjectId = FPersistentStateObjectId::FindObjectId(Obj);
		bool bUseObjectId = ObjectId.IsValid();
		InnerArchive.SerializeBits(&bUseObjectId, 1);

		if (bUseObjectId)
		{
			InnerArchive << ObjectId;
		}
		else
		{
			bool bTopLevelAsset = FAssetData::IsTopLevelAsset(Obj);
			InnerArchive.SerializeBits(&bTopLevelAsset, 1);

			ensureAlwaysMsgf(bTopLevelAsset, TEXT("Saving object %s that will not be loaded."), *Obj->GetPathName());
			if (bTopLevelAsset)
			{
				FString PathName = Obj->GetPathName();
				InnerArchive << PathName;
			}
		}
	}
	else if (IsLoading())
	{
		bool bUseObjectId = false;
		InnerArchive.SerializeBits(&bUseObjectId, 1);

		if (bUseObjectId)
		{
			FPersistentStateObjectId ObjectId{};
			InnerArchive << ObjectId;

			UObject* Value = ObjectId.ResolveObject();
			ensureAlwaysMsgf(Value != nullptr, TEXT("Failed to find object by unique id %s."), *ObjectId.ToString());

			Obj = Value;
		}
		else
		{
			bool bTopLevelAsset = false;
			InnerArchive.SerializeBits(&bTopLevelAsset, 1);

			if (bTopLevelAsset)
			{
				FString PathName{};
				InnerArchive << PathName;

				Obj = FindObject<UObject>(nullptr, *PathName, false);
				ensureAlwaysMsgf(Obj != nullptr, TEXT("Failed to resolve saved reference to top level asset %s."), *PathName);
			}

		}
	}
	
	return *this;
}


FArchive& FPersistentStateProxyArchive::operator<<(FObjectPtr& Obj)
{
	return FArchiveUObject::SerializeObjectPtr(*this, Obj);
}

FArchive& FPersistentStateProxyArchive::operator<<(class FName& Name)
{
	checkf(false, TEXT("Persistent state archive doesn't support name serialization. Use FPersistentStateNameTracker as a proxy to serialize names beforehand."));
	return *this;
}


FArchive& FPersistentStateProxyArchive::operator<<(FLazyObjectPtr& Obj)
{
	checkf(false, TEXT("Persistent state archive doesn't support lazy object references"));
	return *this;
}

FArchive& FPersistentStateProxyArchive::operator<<(FWeakObjectPtr& Obj)
{
	checkf(false, TEXT("Persistent state archive doesn't support weak object references"));
	return *this;
}

FArchive& FPersistentStateProxyArchive::operator<<(FSoftObjectPtr& Value)
{
	checkf(false, TEXT("Persistent state archive doesn't support soft object references. Use FPersistentStateSoftObjectTracker as a proxy to serialize soft object properties beforehand."));
	return *this;
}

FArchive& FPersistentStateProxyArchive::operator<<(FSoftObjectPath& Value)
{
	checkf(false, TEXT("Persistent state archive doesn't support soft object paths. Use FPersistentStateSoftObjectTracker as a proxy to serialize soft object properties beforehand."));
	return *this;
}

FArchive& FPersistentStateSaveGameArchive::operator<<(FName& Name)
{
	if (IsLoading())
	{
		FString LoadedString;
		InnerArchive << LoadedString;
		Name = FName(*LoadedString);

#if 0
		bool bEmptyName = false;
		InnerArchive.SerializeBits(&bEmptyName, 1);

		if (!bEmptyName)
		{
			FString LoadedString{};
			InnerArchive << LoadedString;
			Name = FName{*LoadedString};
		}
#endif
	}
	else if (IsSaving())
	{
		FString SavedString{Name.ToString()};
		InnerArchive << SavedString;

#if 0
		bool bEmptyName = Name == NAME_None;
		InnerArchive.SerializeBits(&bEmptyName, 1);

		if (!bEmptyName)
		{
			FString SavedString{Name.ToString()};
			InnerArchive << SavedString;
		}
#endif
	}

	return *this;
}

FArchive& FPersistentStateSaveGameArchive::operator<<(UObject*& Obj)
{
	// uses base implementation
	return FPersistentStateProxyArchive::operator<<(Obj);
}

FArchive& FPersistentStateSaveGameArchive::operator<<(FLazyObjectPtr& Obj)
{
	return FArchiveUObject::SerializeLazyObjectPtr(*this, Obj);
}

FArchive& FPersistentStateSaveGameArchive::operator<<(FWeakObjectPtr& Obj)
{
	return FArchiveUObject::SerializeWeakObjectPtr(*this, Obj);
}

FArchive& FPersistentStateSaveGameArchive::operator<<(FSoftObjectPtr& Value)
{
	return FArchiveUObject::SerializeSoftObjectPtr(*this, Value);
}

FArchive& FPersistentStateSaveGameArchive::operator<<(FSoftObjectPath& Value)
{
	Value.SerializePath(*this);
	return *this;
}
