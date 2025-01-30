#pragma once

#include "CoreMinimal.h"

/** Persistent State Archive Formatter */
template <bool bWithTextSupport>
struct TPersistentStateFormatter
{
public:
	PERSISTENTSTATE_API static bool IsReleaseFormatter();
	PERSISTENTSTATE_API static bool IsDebugFormatter();
	PERSISTENTSTATE_API static FString GetExtension();

	PERSISTENTSTATE_API static TUniquePtr<FArchiveFormatterType> CreateLoadFormatter(FArchive& Ar);
	PERSISTENTSTATE_API static TUniquePtr<FArchiveFormatterType> CreateSaveFormatter(FArchive& Ar);
};

using FPersistentStateFormatter = TPersistentStateFormatter<WITH_TEXT_ARCHIVE_SUPPORT && WITH_STRUCTURED_SERIALIZATION>;

/** Persistent State Proxy archive */
struct PERSISTENTSTATE_API FPersistentStateProxyArchive: public FArchiveProxy
{
	FPersistentStateProxyArchive(FArchive& InArchive)
		: FArchiveProxy(InArchive)
	{
	}

	virtual FArchive& operator<<(FName& Name) override;
	virtual FArchive& operator<<(UObject*& Obj) override;
	virtual FArchive& operator<<(FObjectPtr& Obj) override;
	virtual FArchive& operator<<(FLazyObjectPtr& Obj) override;
	virtual FArchive& operator<<(FWeakObjectPtr& Obj) override;
	virtual FArchive& operator<<(FSoftObjectPtr& Value) override;
	virtual FArchive& operator<<(FSoftObjectPath& Value) override;
};

/** Save Game archive */
struct PERSISTENTSTATE_API FPersistentStateSaveGameArchive: public FPersistentStateProxyArchive
{
	explicit FPersistentStateSaveGameArchive(FArchive& InArchive)
		: FPersistentStateProxyArchive(InArchive)
	{}
	
	FPersistentStateSaveGameArchive(FArchive& InArchive, UObject& InOwningObject)
		: FPersistentStateProxyArchive(InArchive)
		, OwningObject(&InOwningObject)
	{
	}

	virtual FArchive& operator<<(FName& Name) override;
	virtual FArchive& operator<<(UObject*& Obj) override;
	virtual FArchive& operator<<(FLazyObjectPtr& Obj) override;
	virtual FArchive& operator<<(FWeakObjectPtr& Obj) override;
	virtual FArchive& operator<<(FSoftObjectPtr& Value) override;
	virtual FArchive& operator<<(FSoftObjectPath& Value) override;

	/** object that is being serialized to the archive, can be null */
	UObject* OwningObject = nullptr;
};

/** Memory reader */
class PERSISTENTSTATE_API FPersistentStateMemoryReader: public FMemoryReader
{
public:
	FPersistentStateMemoryReader(const TArray<uint8>& InBytes, bool bIsPersistent = false)
		: FMemoryReader(InBytes, bIsPersistent)
	{}
};

/** Memory writer */
class PERSISTENTSTATE_API FPersistentStateMemoryWriter: public FMemoryWriter
{
public:
	using FMemoryWriter::FMemoryWriter;
};

