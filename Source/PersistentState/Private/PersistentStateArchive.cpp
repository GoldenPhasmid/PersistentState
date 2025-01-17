#include "PersistentStateArchive.h"

#include "Serialization/VarInt.h"

uint64 FPersistentStateObjectTracker::SaveValue(const FSoftObjectPath& Value)
{
	if (int32* Index = ValueMap.Find(Value))
	{
		check(Values.Contains(Value));
		return *Index;
	}
		
	check(!Values.Contains(Value));
		
	int32 Index = Values.Add(Value);
	ValueMap.Add(Value, Index + 1);
		
	return Index + 1;
}

FSoftObjectPath FPersistentStateObjectTracker::LoadValue(uint64 Index)
{
	check(Values.IsValidIndex(Index - 1));
	return Values[Index - 1];
}

template <bool bLoading>
uint64 FPersistentStateStringTracker<bLoading>::SaveValue(const FString& Value) requires !bLoading
{
	if (int32* Index = ValueMap.Find(Value))
	{
		check(Values.Contains(Value));
		return *Index;
	}
		
	check(!Values.Contains(Value));
		
	int32 Index = Values.Add(Value);
	ValueMap.Add(Value, Index + 1);
		
	return Index + 1;
}

template <bool bLoading>
FString FPersistentStateStringTracker<bLoading>::LoadValue(uint64 Index) requires bLoading
{
	check(Values.IsValidIndex(Index - 1));
	return Values[Index - 1];
}

template struct FPersistentStateStringTracker<true>;
template struct FPersistentStateStringTracker<false>;

template <bool bLoading>
uint32 FPersistentStateStringTrackerProxy<bLoading>::WriteToArchive(FArchive& Ar)
{
	const uint32 StartPosition = Ar.Tell();
	Ar << StringTracker;

	return Ar.Tell() - StartPosition;
}

template <bool bLoading>
void FPersistentStateStringTrackerProxy<bLoading>::ReadFromArchive(FArchive& Ar, int32 StartPosition)
{
	const int32 CurrentPosition = Ar.Tell();
	Ar.Seek(StartPosition);
	
	Ar << StringTracker;
	
	Ar.Seek(CurrentPosition);
}

template <bool bLoading>
FArchive& FPersistentStateStringTrackerProxy<bLoading>::operator<<(FName& Name)
{
	if constexpr (bLoading)
	{
		const uint64 Index = ReadVarUIntFromArchive(InnerArchive);
		check(Index != 0);

		FString Str = StringTracker.LoadValue(Index);
		Name = FName{Str};
	}
	else
	{
		const uint64 Index = StringTracker.SaveValue(Name.ToString());
		check(Index != 0);
		
		WriteVarUIntToArchive(InnerArchive, Index);
	}

	return *this;
}

template struct FPersistentStateStringTrackerProxy<true>;
template struct FPersistentStateStringTrackerProxy<false>;

template <bool bLoading, ESerializeObjectDependency DependencyMode>
uint32 FPersistentStateObjectTrackerProxy<bLoading, DependencyMode>::WriteToArchive(FArchive& Ar) const
{
	if constexpr (!bLoading)
	{
		const uint32 StartPosition = Ar.Tell();

		int32 Num = ObjectTracker.NumValues();
		Ar << Num;

		// soft object paths are serialized as string, so they can be caught by a string tracker
		for (FSoftObjectPath& Obj: ObjectTracker.GetValues())
		{
			Obj.SerializePath(Ar);
		}
	
		return Ar.Tell() - StartPosition;
	}
	else
	{
		return 0;
	}
}

template <bool bLoading, ESerializeObjectDependency DependencyMode>
void FPersistentStateObjectTrackerProxy<bLoading, DependencyMode>::ReadFromArchive(FArchive& Ar, int32 StartPosition) const
{
	if constexpr (bLoading)
	{
		const int32 CurrentPosition = Ar.Tell();
		Ar.Seek(StartPosition);
	
		int32 Num{};
		Ar << Num;

		ObjectTracker.Values.SetNum(Num);
		for (FSoftObjectPath& Obj: ObjectTracker.Values)
		{
			Obj.SerializePath(Ar);
		}

		Ar.Seek(CurrentPosition);
	}
}

template <bool bLoading, ESerializeObjectDependency DependencyMode>
FArchive& FPersistentStateObjectTrackerProxy<bLoading, DependencyMode>::operator<<(UObject*& Obj)
{
	if constexpr (DependencyMode & ESerializeObjectDependency::Hard)
	{
		if constexpr (bLoading)
		{
			// if this is 0, then it wasn't a class
			if (const uint64 Index = ReadVarUIntFromArchive(InnerArchive); Index != 0)
			{
				FSoftObjectPath ObjectPath = ObjectTracker.LoadValue(Index);
				check(ObjectPath.IsValid());
			
				UObject* Object = ObjectPath.ResolveObject();
				// @todo: this check will fail if we're trying to load deleted/outdated object
				check(Object);

				Obj = Object;
			}
			else
			{
				InnerArchive << Obj;
			}
		}
		else
		{
			if (UObject* Object = Obj; Object && FAssetData::IsTopLevelAsset(Object))
			{
				uint64 ObjectIndex = ObjectTracker.SaveValue(FSoftObjectPath{Object});
				check(ObjectIndex != 0);
			
				WriteVarUIntToArchive(InnerArchive, ObjectIndex);
			}
			else
			{
				// write
				WriteVarUIntToArchive(InnerArchive, 0ULL);
				InnerArchive << Obj;
			}
		}
	}
	else
	{
		InnerArchive << Obj;
	}

	return *this;
}

template <bool bLoading, ESerializeObjectDependency DependencyMode>
FArchive& FPersistentStateObjectTrackerProxy<bLoading, DependencyMode>::operator<<(FObjectPtr& Obj)
{
	// route serialization to UObject*&
	return FArchiveUObject::SerializeObjectPtr(*this, Obj);
}

template <bool bLoading, ESerializeObjectDependency DependencyMode>
FArchive& FPersistentStateObjectTrackerProxy<bLoading, DependencyMode>::operator<<(FSoftObjectPtr& Value)
{
	if constexpr (DependencyMode & ESerializeObjectDependency::Soft)
	{
		if constexpr (bLoading)
		{
			const uint64 Index = ReadVarUIntFromArchive(InnerArchive);
			check(Index != 0);

			Value = ObjectTracker.LoadValue(Index);
		}
		else
		{
			uint64 ObjectIndex = ObjectTracker.SaveValue(Value.GetUniqueID());
			check(ObjectIndex != 0);

			WriteVarUIntToArchive(InnerArchive, ObjectIndex);
		}
	}
	else
	{
		InnerArchive << Value;
	}

	return *this;
}

template <bool bLoading, ESerializeObjectDependency DependencyMode>
FArchive& FPersistentStateObjectTrackerProxy<bLoading, DependencyMode>::operator<<(FSoftObjectPath& Value)
{
	if constexpr (DependencyMode & ESerializeObjectDependency::Soft)
	{
		if constexpr (bLoading)
		{
			const uint64 Index = ReadVarUIntFromArchive(InnerArchive);
			check(Index != 0);
			
			Value = ObjectTracker.LoadValue(Index);
		}
		else
		{
			uint64 ObjectIndex = ObjectTracker.SaveValue(Value);
			check(ObjectIndex != 0);

			WriteVarUIntToArchive(InnerArchive, ObjectIndex);
		}
	}
	else
	{
		InnerArchive << Value;
	}

	return *this;
}

template struct FPersistentStateObjectTrackerProxy<true, ESerializeObjectDependency::Soft>;
template struct FPersistentStateObjectTrackerProxy<true, ESerializeObjectDependency::Hard>;
template struct FPersistentStateObjectTrackerProxy<true, ESerializeObjectDependency::All>;
template struct FPersistentStateObjectTrackerProxy<false, ESerializeObjectDependency::Soft>;
template struct FPersistentStateObjectTrackerProxy<false, ESerializeObjectDependency::Hard>;
template struct FPersistentStateObjectTrackerProxy<false, ESerializeObjectDependency::All>;
