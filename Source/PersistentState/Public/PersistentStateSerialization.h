#pragma once

#include "CoreMinimal.h"

/** Persistent State Archive Formatter */
template <bool bWithTextSupport>
struct TPersistentStateFormatter
{
public:
	static bool IsBinary();
	static bool IsTextBased();
	static FString GetExtension();

	static TUniquePtr<FArchiveFormatterType> CreateLoadFormatter(FArchive& Ar);
	static TUniquePtr<FArchiveFormatterType> CreateSaveFormatter(FArchive& Ar);
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
	FPersistentStateSaveGameArchive(FArchive& InArchive, UObject& InSourceObject)
		: FPersistentStateProxyArchive(InArchive)
		, SourceObject(InSourceObject)
	{
	}

	virtual FArchive& operator<<(FName& Name) override;
	virtual FArchive& operator<<(UObject*& Obj) override;
	virtual FArchive& operator<<(FLazyObjectPtr& Obj) override;
	virtual FArchive& operator<<(FWeakObjectPtr& Obj) override;
	virtual FArchive& operator<<(FSoftObjectPtr& Value) override;
	virtual FArchive& operator<<(FSoftObjectPath& Value) override;

	UObject& SourceObject;
};

/** Memory reader */
class FPersistentStateMemoryReader: public FMemoryReader
{
public:
	FPersistentStateMemoryReader(const TArray<uint8>& InBytes, bool bIsPersistent = false)
		: FMemoryReader(InBytes, bIsPersistent)
	{}
};

/** Memory writer */
class FPersistentStateMemoryWriter: public FMemoryWriter
{
public:
	using FMemoryWriter::FMemoryWriter;
};

