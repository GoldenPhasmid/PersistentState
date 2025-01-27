#include "PersistentStateAutomationTest.h"

#include "AutomationCommon.h"
#include "AutomationWorld.h"
#include "PersistentStateObjectId.h"
#include "PersistentStateStatics.h"
#include "PersistentStateTestClasses.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"

using namespace UE::PersistentState;

struct FPersistentStateTest_Streaming: public FPersistentStateAutoTest
{
	FPersistentStateTest_Streaming(const FString& InName, const bool bInComplexTask)
		: FPersistentStateAutoTest(InName, bInComplexTask)
	{}

	virtual void Cleanup() override
	{
		FPersistentStateAutoTest::Cleanup();

		LevelStreaming = nullptr;
	}
	
	virtual void InitializeImpl(const FString& Parameters) override
	{
		FPersistentStateAutoTest::InitializeImpl(Parameters);
		
		if (!Parameters.Contains(TEXT("WP")))
		{
			const FString Sublevel{TEXT("PersistentStateTestMap_Default_SubLevel")};
			LevelStreaming = FStreamLevelAction::FindAndCacheLevelStreamingObject(FName{Sublevel}, *ScopedWorld);
		}
	}
	
	void LoadStreamingLevel(const FString& Parameters) const;

	void UnloadStreamingLevel(const FString& Parameters) const;

	ULevelStreaming* LevelStreaming = nullptr;
};

void FPersistentStateTest_Streaming::LoadStreamingLevel(const FString& Parameters) const
{
	if (Parameters.Contains(TEXT("WP")))
	{
		ScopedWorld->GetWorld()->GetWorldPartition()->RuntimeHash->ForEachStreamingCells([this](const UWorldPartitionRuntimeCell* Cell)
		{
			if (!Cell->IsAlwaysLoaded())
			{
				Cell->Activate();
			}

			return true;
		});
	}
	else if (LevelStreaming)
	{
		LevelStreaming->SetShouldBeLoaded(true);
		LevelStreaming->SetShouldBeVisible(true);
	}
	GEngine->BlockTillLevelStreamingCompleted(*ScopedWorld);
}

void FPersistentStateTest_Streaming::UnloadStreamingLevel(const FString& Parameters) const
{
	if (Parameters.Contains(TEXT("WP")))
	{
		ScopedWorld->GetWorld()->GetWorldPartition()->RuntimeHash->ForEachStreamingCells([this](const UWorldPartitionRuntimeCell* Cell)
		{
			if (!Cell->IsAlwaysLoaded())
			{
				Cell->Unload();
			}

			return true;
		});
	}
	else if (LevelStreaming)
	{
		LevelStreaming->SetShouldBeLoaded(false);
		LevelStreaming->SetShouldBeVisible(false);
	}
	GEngine->BlockTillLevelStreamingCompleted(*ScopedWorld);
}

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(
	FPersistentStateTest_Streaming_Impl, FPersistentStateTest_Streaming,
	"PersistentState.LevelStreaming", AutomationFlags
)

void FPersistentStateTest_Streaming_Impl::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("Default"));
	OutBeautifiedNames.Add(TEXT("World Partition"));
	OutTestCommands.Add(TEXT("/PersistentState/PersistentStateTestMap_DefaultEmpty"));
	OutTestCommands.Add(TEXT("/PersistentState/PersistentStateTestMap_WPEmpty"));
}

bool FPersistentStateTest_Streaming_Impl::RunTest(const FString& Parameters)
{
	FPersistentStateAutoTest::RunTest(Parameters);

	const FString Level{Parameters};
	const FString SlotName{TEXT("TestSlot")};
	
	Initialize(Parameters, TArray<FString>{SlotName}, AGameModeBase::StaticClass());
	ON_SCOPE_EXIT { Cleanup(); };

	LoadStreamingLevel(Parameters);
	
	APersistentStateTestActor* StaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StreamActor1"));
	APersistentStateTestActor* OtherStaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StreamActor2"));
	UTEST_TRUE("Found static actors", StaticActor != nullptr && OtherStaticActor != nullptr);

	FActorSpawnParameters Params{};
	// spawn dynamic actors in the same scoped as owner, so that have the same streamed level
	Params.Owner = StaticActor;
	APersistentStateTestActor* DynamicActor = ScopedWorld->SpawnActorSimple<APersistentStateTestActor>(Params);
	FPersistentStateObjectId DynamicActorId = FPersistentStateObjectId::FindObjectId(DynamicActor);
	UTEST_TRUE("Found dynamic actor", DynamicActorId.IsValid());
	
	APersistentStateTestActor* OtherDynamicActor = ScopedWorld->SpawnActorSimple<APersistentStateTestActor>(Params);
	FPersistentStateObjectId OtherDynamicActorId = FPersistentStateObjectId::FindObjectId(OtherDynamicActor);
	UTEST_TRUE("Found dynamic actor", OtherDynamicActorId.IsValid());
	UTEST_TRUE("Dynamic actors have different id", DynamicActorId != OtherDynamicActorId);

	auto InitActor = [this](APersistentStateTestActor* Target, APersistentStateTestActor* Static, APersistentStateTestActor* Dynamic, FName Name, int32 Index)
	{
		Target->StoredInt = Index;
		Target->StoredName = Name;
		Target->StoredString = Name.ToString();
		Target->CustomStateData.Name = Name;
		Target->StoredStaticActor = Static;
		Target->StoredDynamicActor = Dynamic;
		Target->StoredStaticComponent = Static->StaticComponent;
		Target->StoredDynamicComponent = Dynamic->DynamicComponent;
	};

	auto VerifyActor = [this](APersistentStateTestActor* Target, APersistentStateTestActor* Static, APersistentStateTestActor* Dynamic, FName Name, int32 Index)
	{
		UTEST_TRUE("Index matches", Target->StoredInt == Index);
		UTEST_TRUE("Name matches", Target->StoredName == Name && Target->StoredString == Name.ToString() && Target->CustomStateData.Name == Name);
		UTEST_TRUE("Actor references match", Target->StoredStaticActor == Static && Target->StoredDynamicActor == Dynamic);
		UTEST_TRUE("Component references match", Target->StoredStaticComponent == Static->StaticComponent && Target->StoredDynamicComponent == Dynamic->DynamicComponent);
		UTEST_TRUE("Has dynamic component reference", IsValid(Target->DynamicComponent) && Target->DynamicComponent->GetOwner() == Target);
		return true;
	};

	StaticActor->DynamicComponent = UE::Automation::CreateActorComponent<UPersistentStateTestComponent>(*ScopedWorld, StaticActor);
	OtherStaticActor->DynamicComponent = UE::Automation::CreateActorComponent<UPersistentStateTestComponent>(*ScopedWorld, OtherStaticActor);
	DynamicActor->DynamicComponent = UE::Automation::CreateActorComponent<UPersistentStateTestComponent>(*ScopedWorld, DynamicActor);
	OtherDynamicActor->DynamicComponent = UE::Automation::CreateActorComponent<UPersistentStateTestComponent>(*ScopedWorld, OtherDynamicActor);
	
	InitActor(StaticActor, OtherStaticActor, DynamicActor, TEXT("StreamActor"), 1);
	InitActor(OtherStaticActor, StaticActor, DynamicActor, TEXT("OtherStreamActor"), 2);
	InitActor(DynamicActor, StaticActor, OtherDynamicActor, TEXT("DynamicActor"), 3);
	InitActor(OtherDynamicActor, StaticActor, DynamicActor, TEXT("OtherDynamicActor"), 4);

	UnloadStreamingLevel(Parameters);
	// ensure that unloaded level is GC'd so we get a new level
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	
	StaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StreamActor1"));
	OtherStaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StreamActor2"));
	UTEST_TRUE("Unloaded static actors", StaticActor == nullptr && OtherStaticActor == nullptr);
	
	LoadStreamingLevel(Parameters);
	
	StaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StreamActor1"));
	OtherStaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StreamActor2"));
	UTEST_TRUE("Found static actors", StaticActor && OtherStaticActor);
	DynamicActor = DynamicActorId.ResolveObject<APersistentStateTestActor>();
	OtherDynamicActor = OtherDynamicActorId.ResolveObject<APersistentStateTestActor>();
	UTEST_TRUE("Found dynamic actors", DynamicActor && OtherDynamicActor);
	
	UTEST_TRUE("Restored references are correct", VerifyActor(StaticActor, OtherStaticActor, DynamicActor, TEXT("StreamActor"), 1));
	UTEST_TRUE("Restored references are correct", VerifyActor(OtherStaticActor, StaticActor, DynamicActor, TEXT("OtherStreamActor"), 2));
	UTEST_TRUE("Restored references are correct", VerifyActor(DynamicActor, StaticActor, OtherDynamicActor, TEXT("DynamicActor"), 3));
	UTEST_TRUE("Restored references are correct", VerifyActor(OtherDynamicActor, StaticActor, DynamicActor, TEXT("OtherDynamicActor"), 4));
	
	UnloadStreamingLevel(Parameters);

	StaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StreamActor1"));
	OtherStaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StreamActor2"));
	UTEST_TRUE("Unloaded static actors", StaticActor == nullptr && OtherStaticActor == nullptr);

	LoadStreamingLevel(Parameters);
	
	StaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StreamActor1"));
	OtherStaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StreamActor2"));
	UTEST_TRUE("Found static actors", StaticActor && OtherStaticActor);
	DynamicActor = DynamicActorId.ResolveObject<APersistentStateTestActor>();
	OtherDynamicActor = OtherDynamicActorId.ResolveObject<APersistentStateTestActor>();
	UTEST_TRUE("Found dynamic actors", DynamicActor && OtherDynamicActor);
	
	UTEST_TRUE("Restored references are correct", VerifyActor(StaticActor, OtherStaticActor, DynamicActor, TEXT("StreamActor"), 1));
	UTEST_TRUE("Restored references are correct", VerifyActor(OtherStaticActor, StaticActor, DynamicActor, TEXT("OtherStreamActor"), 2));
	UTEST_TRUE("Restored references are correct", VerifyActor(DynamicActor, StaticActor, OtherDynamicActor, TEXT("DynamicActor"), 3));
	UTEST_TRUE("Restored references are correct", VerifyActor(OtherDynamicActor, StaticActor, DynamicActor, TEXT("OtherDynamicActor"), 4));
	
	return !HasAnyErrors();
}
