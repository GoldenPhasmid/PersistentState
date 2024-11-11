#include "PersistentStateArchive.h"

#include "PersistentStateStatics.h"

FArchive& FPersistentStateProxyArchive::operator<<(class FName& Name)
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

FArchive& FPersistentStateProxyArchive::operator<<(UObject*& Obj)
{
	// try to serialize object property using unique object id, created by state system beforehand
	// if it fails, fallback to object-path-as-string serialization, so we can safely save references
	// to top level assets (data assets, data tables, etc.) or actors with stable names
	bool bObjectValid = IsSaving() ? Obj != nullptr : false;
	InnerArchive.SerializeBits(&bObjectValid, 1);

	if (!bObjectValid)
	{
		Obj = nullptr;
		return *this;
	}

	if (IsSaving())
	{
		FGuid ObjectId = UE::PersistentState::FindUniqueIdFromObject(Obj);
		bool bUseObjectId = ObjectId.IsValid();
		InnerArchive.SerializeBits(&bUseObjectId, 1);

		if (bUseObjectId)
		{
			InnerArchive << ObjectId;
		}
		else if (UE::PersistentState::HasStableName(*Obj))
		{
			FString PathName = Obj->GetPathName();
			InnerArchive << PathName;
		}
		else
		{
			
		}
	}
	else if (IsLoading())
	{
		bool bUseObjectId = false;
		InnerArchive.SerializeBits(&bUseObjectId, 1);

		if (bUseObjectId)
		{
			FGuid ObjectId{};
			InnerArchive << ObjectId;

			UObject* Value = UE::PersistentState::FindObjectByUniqueId(ObjectId);
#if WITH_EDITOR
			ensureAlwaysMsgf(Value != nullptr, TEXT("Failed to find object by unique id %s- persistent state assumption failed."), *ObjectId.ToString());
#endif
			Obj = Value;
		}
		else
		{
			FString PathName{};
			InnerArchive << PathName;

			Obj = FindObject<UObject>(nullptr, *PathName, false);
			if (Obj == nullptr)
			{
#if WITH_EDITOR
				ensureAlwaysMsgf(false, TEXT("Failed to resolve object by path %s - persistent state assumption failed."), *PathName);
#endif
			}
		}
	}
	
	return *this;
}

FArchive& FPersistentStateProxyArchive::operator<<(FObjectPtr& Obj)
{
	return FArchiveUObject::SerializeObjectPtr(*this, Obj);
}

FArchive& FPersistentStateProxyArchive::operator<<(FLazyObjectPtr& Obj)
{
	return FArchiveUObject::SerializeLazyObjectPtr(*this, Obj);
}

FArchive& FPersistentStateProxyArchive::operator<<(FWeakObjectPtr& Obj)
{
	return FArchiveUObject::SerializeWeakObjectPtr(*this, Obj);
}

FArchive& FPersistentStateProxyArchive::operator<<(FSoftObjectPtr& Value)
{
	return FArchiveUObject::SerializeSoftObjectPtr(*this, Value);
}

FArchive& FPersistentStateProxyArchive::operator<<(FSoftObjectPath& Value)
{
	Value.SerializePath(*this);
	return *this;
}
