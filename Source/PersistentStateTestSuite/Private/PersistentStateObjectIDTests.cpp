#include "PersistentStateAutomationTest.h"

#include "AutomationCommon.h"
#include "AutomationWorld.h"
#include "PersistentStateObjectId.h"
#include "PersistentStateSerialization.h"
#include "PersistentStateStatics.h"
#include "PersistentStateTestClasses.h"
#include "Serialization/Formatters/BinaryArchiveFormatter.h"
#include "Serialization/Formatters/JsonArchiveInputFormatter.h"
#include "Serialization/Formatters/JsonArchiveOutputFormatter.h"

using namespace UE::PersistentState;

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPersistentStateTest_ObjectID_Persistence, "PersistentState.ObjectID.Persistence", AutomationFlags
)
bool FPersistentStateTest_ObjectID_Persistence::RunTest(const FString& Parameters)
{
	FSoftObjectPath WorldPath = UE::Automation::FindWorldAssetByName(TEXT("/PersistentState/PersistentStateTestMap_Default"));
	FWorldInitParams InitParams = FWorldInitParams{EWorldType::Game, EWorldInitFlags::WithGameInstance}.SetWorldPackage(WorldPath);
	FAutomationWorldPtr ScopedWorld;

	FPersistentStateObjectId StaticId, DefaultId, DynamicId;
	
	UTEST_TRUE(TEXT("Default created ID is not valid and default"), StaticId.IsDefault() && !StaticId.IsValid());

	auto SetupObjects = [this, &ScopedWorld, &StaticId, &DefaultId, &DynamicId](bool bWithSubsystem) -> bool
	{
		{
			AActor* StaticActor = ScopedWorld->FindActorByTag(TEXT("EmptyActor"));
			UTEST_TRUE(TEXT("Static actor has stable name"), UE::PersistentState::HasStableName(*StaticActor));

			if (!bWithSubsystem)
			{
				UTEST_TRUE(TEXT("ObjectID is not created"), !FPersistentStateObjectId::FindObjectId(StaticActor).IsValid());
				UTEST_TRUE(TEXT("ObjectID is not created"), !FPersistentStateObjectId::CreateDynamicObjectId(StaticActor).IsValid());
			}
			
			StaticId = FPersistentStateObjectId::CreateObjectId(StaticActor);
			UTEST_TRUE(TEXT("ObjectID is valid"), StaticId.IsValid());
			UTEST_TRUE(TEXT("ObjectID resolves to a valid object"), StaticId.ResolveObject() == StaticActor);
			UTEST_TRUE(TEXT("ObjectID is static"), StaticId.IsStatic() && !StaticId.IsDynamic() && !StaticId.IsDefault());
			UTEST_TRUE(TEXT("ObjectID debug name contains actor name"), StaticId.GetObjectName().Contains(StaticActor->GetName()));
		}

		
		{
			// ObjectID behaves the same way for objects that don't implement PersistentState interface
			AActor* DefaultActor = ScopedWorld->FindActorByTag(TEXT("DefaultActor"));
			check(!DefaultActor->Implements<UPersistentStateObject>());
			UTEST_TRUE(TEXT("Default actor has stable name"), UE::PersistentState::HasStableName(*DefaultActor));
			
			if (!bWithSubsystem)
			{
				UTEST_TRUE(TEXT("ObjectID is not created"), !FPersistentStateObjectId::FindObjectId(DefaultActor).IsValid());
				UTEST_TRUE(TEXT("ObjectID is not created"), !FPersistentStateObjectId::CreateDynamicObjectId(DefaultActor).IsValid());
			}
		
			DefaultId = FPersistentStateObjectId::CreateObjectId(DefaultActor);
			UTEST_TRUE(TEXT("ObjectID is valid"), DefaultId.IsValid());
			UTEST_TRUE(TEXT("ObjectID resolves to a valid object"), DefaultId.ResolveObject() == DefaultActor);
			UTEST_TRUE(TEXT("ObjectID is static"), DefaultId.IsStatic() && !DefaultId.IsDynamic() && !DefaultId.IsDefault());
			UTEST_TRUE(TEXT("ObjectID debug name contains actor name"), DefaultId.GetObjectName().Contains(DefaultActor->GetName()));
		}
		
		{
			AActor* DynamicActor = ScopedWorld->SpawnActor<APersistentStateEmptyTestActor>();
			UTEST_TRUE(TEXT("Dynamic actor has stable name"), !UE::PersistentState::HasStableName(*DynamicActor));

			if (!bWithSubsystem)
			{
				UTEST_TRUE(TEXT("ObjectID is not created"), !FPersistentStateObjectId::FindObjectId(DynamicActor).IsValid());
				UTEST_TRUE(TEXT("ObjectID is not created"), !FPersistentStateObjectId::CreateStaticObjectId(DynamicActor).IsValid());
			}

			DynamicId = FPersistentStateObjectId::CreateObjectId(DynamicActor);
			UTEST_TRUE(TEXT("ObjectID is valid"), DynamicId.IsValid());
			UTEST_TRUE(TEXT("ObjectID resolves to a valid object"), DynamicId.ResolveObject() == DynamicActor);
			UTEST_TRUE(TEXT("ObjectID is dynamic"), DynamicId.IsDynamic() && !DynamicId.IsStatic() && !DynamicId.IsDefault());
			UTEST_TRUE(TEXT("ObjectID debug name contains actor name"), DynamicId.GetObjectName().Contains(DynamicActor->GetName()));
		}

		return !HasAnyErrors();
	};

	// create test world without PersistentState system
	ScopedWorld = InitParams.Create();
	
	UTEST_TRUE("Setup is valid", SetupObjects(false));

	ScopedWorld.Reset();
	// Collect garbage is required to remove old world objects from the engine entirely.
	// Otherwise, there's going to be stable ID collision between new and previous world objects, because persistent state system does package remapping
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	
	UTEST_TRUE("ObjectID is valid even if associated object is destroyed", StaticId.IsValid() && DefaultId.IsValid() && DynamicId.IsValid());
	UTEST_TRUE("ObjectID cannot resolved to invalid object", StaticId.ResolveObject() == nullptr && DynamicId.ResolveObject() == nullptr && DefaultId.ResolveObject() == nullptr);
	
	// create test world without PersistentState system
	ScopedWorld = InitParams.Create();

	// PersistentState system should be enabled to restore/assign IDs to static objects
	{
		AActor* StaticActor = ScopedWorld->FindActorByTag(TEXT("EmptyActor"));
		UTEST_TRUE("ObjectID is not linked to an actor instance", StaticId.ResolveObject() == nullptr);
		FPersistentStateObjectId OtherId = FPersistentStateObjectId::CreateObjectId(StaticActor);
		UTEST_TRUE("IDs are equal", StaticId == OtherId);
		UTEST_TRUE("After new ID is created from a static actor, all IDs are resolved to the object", StaticId.ResolveObject() == StaticActor);
	}

	{
		AActor* DefaultActor = ScopedWorld->FindActorByTag(TEXT("DefaultActor"));
		UTEST_TRUE("ObjectID is not linked to an actor instance", DefaultId.ResolveObject() == nullptr);
		FPersistentStateObjectId OtherId = FPersistentStateObjectId::CreateObjectId(DefaultActor);
		UTEST_TRUE("IDs are equal", DefaultId == OtherId);
		UTEST_TRUE("After new ID is created from a default actor, all IDs are resolved to the object", DefaultId.ResolveObject() == DefaultActor);
	}

	{
		AActor* DynamicActor = ScopedWorld->SpawnActor<APersistentStateEmptyTestActor>();
		FPersistentStateObjectId OtherId = FPersistentStateObjectId::CreateObjectId(DynamicActor);
		UTEST_TRUE("ObjectID is not restored to a dynamic actor", OtherId != DynamicId);
		UTEST_TRUE("Old ObjectID is not resolved to a new dynamic actor", DynamicId.ResolveObject() == nullptr);
	}
	
	ScopedWorld.Reset();
	// Collect garbage is required to remove old world objects from the engine entirely.
	// Otherwise, there's going to be stable ID collision between new and previous world objects, because persistent state system does package remapping
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	// create test world with PersistentState system
	ScopedWorld = InitParams.EnableSubsystem<UPersistentStateSubsystem>().Create();
	
	UTEST_TRUE("Setup is valid", SetupObjects(true));

	ScopedWorld.Reset();
	// Collect garbage is required to remove old world objects from the engine entirely.
	// Otherwise, there's going to be stable ID collision between new and previous world objects, because persistent state system does package remapping
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	
	// create test world with PersistentState system
	ScopedWorld = InitParams.EnableSubsystem<UPersistentStateSubsystem>().Create();

	{
		// PersistentState system is responsible to re-initialize IDs for static objects and restore IDs for re-created dynamic objects
		// @note: current behavior is to assign IDs to all identified static objects - with or without PersistentState interface
		AActor* StaticActor = ScopedWorld->FindActorByTag(TEXT("EmptyActor"));
		AActor* DefaultActor = ScopedWorld->FindActorByTag(TEXT("DefaultActor"));
		UTEST_TRUE("ObjectID is resolved to an original object", StaticId.ResolveObject() == StaticActor);
		UTEST_TRUE("ObjectID is resolved to an original object", DefaultId.ResolveObject() == DefaultActor);
		// dynamic object ID is not resolved to anything as world state wasn't saved in any way
		UTEST_TRUE("Dynamic ObjectID is not resolved", DynamicId.ResolveObject() == nullptr);
	}
	
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPersistentStateTest_ObjectID_Serialization, "PersistentState.ObjectID.Serialization", AutomationFlags
)

bool FPersistentStateTest_ObjectID_Serialization::RunTest(const FString& Parameters)
{
	FSoftObjectPath WorldPath = UE::Automation::FindWorldAssetByName(TEXT("/PersistentState/PersistentStateTestMap_Default"));
	FWorldInitParams InitParams = FWorldInitParams{EWorldType::Game, EWorldInitFlags::WithBeginPlay}.SetWorldPackage(WorldPath);
	FAutomationWorldPtr ScopedWorld = InitParams.Create();

	AActor* StaticActor = ScopedWorld->FindActorByTag(TEXT("EmptyActor"));
	AActor* DefaultActor = ScopedWorld->FindActorByTag(TEXT("DefaultActor"));
	AActor* DynamicActor = ScopedWorld->SpawnActor<APersistentStateEmptyTestActor>();

	FPersistentStateObjectId StaticId = FPersistentStateObjectId::CreateObjectId(StaticActor);
	FPersistentStateObjectId DefaultId = FPersistentStateObjectId::CreateObjectId(DefaultActor);
	FPersistentStateObjectId DynamicId = FPersistentStateObjectId::CreateObjectId(DynamicActor);

	// archive serialization
	{
		TArray<uint8> RawData;
		FPersistentStateObjectId OtherStaticId, OtherDefaultId, OtherDynamicId;
		{
			FPersistentStateMemoryWriter Archive{RawData, true};
			Archive << StaticId << DefaultId << DynamicId;
		}

		{
			FPersistentStateMemoryReader Archive{RawData, true};
			Archive << OtherStaticId << OtherDefaultId << OtherDynamicId;
		}

		UTEST_TRUE("StaticId is loaded", StaticId == OtherStaticId && StaticId.GetObjectName() == OtherStaticId.GetObjectName());
		UTEST_TRUE("DefaultId is loaded", DefaultId == OtherDefaultId && DefaultId.GetObjectName() == OtherDefaultId.GetObjectName());
		UTEST_TRUE("DynamicId is loaded", DynamicId == OtherDynamicId && DynamicId.GetObjectName() == OtherDynamicId.GetObjectName());
	}
	
	// binary structured serialization
	{
		TArray<uint8> RawData;
		FPersistentStateObjectId OtherStaticId, OtherDefaultId, OtherDynamicId;
		{
			FPersistentStateMemoryWriter Writer{RawData, true};
			FBinaryArchiveFormatter Formatter{Writer};
			FStructuredArchive StructuredArchive{Formatter};
		
			FStructuredArchive::FRecord Root = StructuredArchive.Open().EnterRecord();
			Root << SA_VALUE(TEXT("StaticId"), StaticId);
			Root << SA_VALUE(TEXT("DefaultId"), DefaultId);
			Root << SA_VALUE(TEXT("DynamicId"), DynamicId);
		}

		{
			FPersistentStateMemoryReader Reader{RawData, true};
			FBinaryArchiveFormatter Formatter{Reader};
			FStructuredArchive StructuredArchive{Formatter};
			FStructuredArchive::FRecord Root = StructuredArchive.Open().EnterRecord();

			Root << SA_VALUE(TEXT("StaticId"), OtherStaticId);
			Root << SA_VALUE(TEXT("DefaultId"), OtherDefaultId);
			Root << SA_VALUE(TEXT("DynamicId"), OtherDynamicId);
		}

		UTEST_TRUE("StaticId is loaded", StaticId == OtherStaticId && StaticId.GetObjectName() == OtherStaticId.GetObjectName());
		UTEST_TRUE("DefaultId is loaded", DefaultId == OtherDefaultId && DefaultId.GetObjectName() == OtherDefaultId.GetObjectName());
		UTEST_TRUE("DynamicId is loaded", DynamicId == OtherDynamicId && DynamicId.GetObjectName() == OtherDynamicId.GetObjectName());
	}

	// json structured serialization
	{
		TArray<uint8> RawData;
		FPersistentStateObjectId OtherStaticId, OtherDefaultId, OtherDynamicId;
		{
			FPersistentStateMemoryWriter Writer{RawData, true};
			FJsonArchiveOutputFormatter Formatter{Writer};
			FStructuredArchive Archive{Formatter};
		
			FStructuredArchive::FRecord Root = Archive.Open().EnterRecord();
			Root << SA_VALUE(TEXT("StaticId"), StaticId);
			Root << SA_VALUE(TEXT("DefaultId"), DefaultId);
			Root << SA_VALUE(TEXT("DynamicId"), DynamicId);
		}

		{
			FPersistentStateMemoryReader Reader{RawData, true};
			FJsonArchiveInputFormatter Formatter{Reader};
			FStructuredArchive Archive{Formatter};
			FStructuredArchive::FRecord Root = Archive.Open().EnterRecord();

			Root << SA_VALUE(TEXT("StaticId"), OtherStaticId);
			Root << SA_VALUE(TEXT("DefaultId"), OtherDefaultId);
			Root << SA_VALUE(TEXT("DynamicId"), OtherDynamicId);
		}

		UTEST_TRUE("StaticId is loaded", StaticId == OtherStaticId && StaticId.GetObjectName() == OtherStaticId.GetObjectName());
		UTEST_TRUE("DefaultId is loaded", DefaultId == OtherDefaultId && DefaultId.GetObjectName() == OtherDefaultId.GetObjectName());
		UTEST_TRUE("DynamicId is loaded", DynamicId == OtherDynamicId && DynamicId.GetObjectName() == OtherDynamicId.GetObjectName());
	}

	
	return !HasAnyErrors();
}
