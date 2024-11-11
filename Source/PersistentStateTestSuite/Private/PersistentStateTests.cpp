
#include "PersistentStateTestClasses.h"
#include "AutomationCommon.h"
#include "AutomationWorld.h"
#include "PersistentStateSettings.h"
#include "PersistentStateStatics.h"
#include "PersistentStateSubsystem.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"

using namespace UE::PersistentState;

constexpr int32 AutomationFlags = EAutomationTestFlags::CriticalPriority | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::ApplicationContextMask;

class FPersistentStateAutomationTest: public FAutomationTestBase
{
public:
	FPersistentStateAutomationTest(const FString& InName, const bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{}
	
	virtual bool RunTest(const FString& Parameters) override
	{
		return true;
	}

	virtual void Initialize(const FString& WorldPackage, const TArray<FString>& SlotNames, TFunction<void(UWorld*)> InitWorldCallback = {})
	{
		UPersistentStateSettings* Settings = UPersistentStateSettings::GetMutable();
		SettingsCopy = DuplicateObject<UPersistentStateSettings>(Settings, nullptr);
		SettingsCopy->AddToRoot();

		// enable subsystem
		Settings->bEnabled = true;
		// override default slots with test slot names
		TArray<FPersistentSlotEntry> DefaultSlots;
		for (const FString& SlotName: SlotNames)
		{
			DefaultSlots.Add(FPersistentSlotEntry{SlotName, FText::FromString(SlotName)});
		}
		Settings->DefaultSlots = DefaultSlots;
		// override storage class
		Settings->StateStorageClass = UPersistentStateMockStorage::StaticClass();
	
		WorldPath = UE::Automation::FindWorldAssetByName(WorldPackage);
		EWorldInitFlags Flags = EWorldInitFlags::WithGameInstance;
		if (WorldPackage.Contains(TEXT("WP")))
		{
			Flags |= EWorldInitFlags::InitWorldPartition;
		}
		
		ScopedWorld = FAutomationWorldInitParams{EWorldType::Game, Flags}
		.SetInitWorld(InitWorldCallback).SetWorldPackage(WorldPath)
		.SetGameMode<APersistentStateTestGameMode>().EnableSubsystem<UPersistentStateSubsystem>().Create();

		StateSubsystem = ScopedWorld->GetSubsystem<UPersistentStateSubsystem>();
	}

	virtual void Cleanup()
	{
		WorldPath.Reset();
		ScopedWorld.Reset();
		StateSubsystem = nullptr;

		UPersistentStateSettings* Settings = UPersistentStateSettings::GetMutable();
		Settings->bEnabled = SettingsCopy->bEnabled;
		Settings->DefaultSlots = SettingsCopy->DefaultSlots;
		Settings->StateStorageClass = SettingsCopy->StateStorageClass;
		
		SettingsCopy->RemoveFromRoot();
		SettingsCopy->MarkAsGarbage();
		SettingsCopy = nullptr;
	}

	FSoftObjectPath WorldPath;
	FAutomationWorldPtr ScopedWorld;
	UPersistentStateSettings* SettingsCopy = nullptr;
	UPersistentStateSubsystem* StateSubsystem = nullptr;
};

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(FPersistentStateTest_PersistentStateSubsystem, FPersistentStateAutomationTest,
                                         "PersistentState.StateSubsystem", AutomationFlags)

void FPersistentStateTest_PersistentStateSubsystem::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("Default"));
	OutBeautifiedNames.Add(TEXT("World Partition"));
	OutTestCommands.Add(TEXT("/PersistentState/PersistentStateTestMap_Default"));
	OutTestCommands.Add(TEXT("/PersistentState/PersistentStateTestMap_WP"));
}

bool FPersistentStateTest_PersistentStateSubsystem::RunTest(const FString& Parameters)
{
	PrevWorldState = CurrentWorldState = nullptr;
	ExpectedSlot = {};
	
	const FString StateSlot1{TEXT("TestSlot1")};
	const FString StateSlot2{TEXT("TestSlot2")};
	
	FPersistentStateSubsystemCallbackListener Listener{};
	Initialize(Parameters, {StateSlot1, StateSlot2}, [&Listener](UWorld* World)
	{
		if (UPersistentStateSubsystem* StateSubsystem = World->GetGameInstance()->GetSubsystem<UPersistentStateSubsystem>())
		{
			Listener.SetSubsystem(*StateSubsystem);
		}
	});
	ON_SCOPE_EXIT { Cleanup(); };

	UTEST_TRUE("Listener is registered with state subsystem", Listener.Subsystem != nullptr);
	UTEST_TRUE("LoadGame has not happened without current slot", Listener.bLoadStarted == false);
	
	UTEST_TRUE("Current slot is empty", StateSubsystem->GetCurrentSlot().IsValid() == false);

	TArray<FPersistentStateSlotHandle> Slots;
	StateSubsystem->GetSaveGameSlots(Slots);
	UTEST_TRUE("State subsystem has two save slots", Slots.Num() == 2);

	FPersistentStateSlotHandle Slot1Handle = StateSubsystem->FindSaveGameSlotByName(FName{StateSlot1});
	UTEST_TRUE("State subsystem has slot1", Slot1Handle.IsValid());
	FPersistentStateSlotHandle Slot2Handle = StateSubsystem->FindSaveGameSlotByName(FName{StateSlot2});
	UTEST_TRUE("State subsystem has slot2", Slot2Handle.IsValid());
	
	StateSubsystem->SaveGame();
	UTEST_TRUE("SaveGame has failed without current slot", Listener.bSaveStarted == false);
	UTEST_TRUE("Current slot is empty", StateSubsystem->GetCurrentSlot().IsValid() == false);

	ExpectedSlot = Slot1Handle;
	StateSubsystem->SaveGameToSlot(Slot1Handle);
	
	UTEST_TRUE("TestSlot1 is a current slot", StateSubsystem->GetCurrentSlot() == Slot1Handle);
	UTEST_TRUE("TestSlot1 is fully saved", Listener.bSaveStarted && Listener.bSaveFinished && Listener.SaveSlot == Slot1Handle);
	UTEST_TRUE("TestSlot1 save created a world state", CurrentWorldState.IsValid());

	PrevWorldState = CurrentWorldState;
	Listener.Clear();
	
	StateSubsystem->LoadGameFromSlot(Slot1Handle);
	UTEST_TRUE("TestSlot1 is a current slot", StateSubsystem->GetCurrentSlot() == Slot1Handle);
	
	ScopedWorld->FinishWorldTravel();
	UTEST_TRUE("TestSlot1 is not saved before cleanup", Listener.bSaveStarted == false && CurrentWorldState == PrevWorldState);
	UTEST_TRUE("TestSlot1 is fully loaded for current slot", Listener.bLoadStarted && Listener.bLoadFinished && Listener.LoadSlot == Slot1Handle);

	PrevWorldState = CurrentWorldState;
    Listener.Clear();
	
	UGameplayStatics::OpenLevelBySoftObjectPtr(*ScopedWorld, TSoftObjectPtr<UWorld>{WorldPath}, true);
	ScopedWorld->FinishWorldTravel();
	UTEST_TRUE("TestSlot1 is saved before cleanup", Listener.bSaveStarted && Listener.bSaveFinished && Listener.SaveSlot == Slot1Handle && CurrentWorldState != PrevWorldState);
	UTEST_TRUE("TestSlot1 is fully loaded for current slot", Listener.bLoadStarted && Listener.bLoadFinished && Listener.LoadSlot == Slot1Handle);

	ExpectedSlot = Slot2Handle;
	PrevWorldState = CurrentWorldState;
	Listener.Clear();
    
	StateSubsystem->LoadGameWorldFromSlot(Slot2Handle, TSoftObjectPtr<UWorld>{WorldPath});
	UTEST_TRUE("TestSlot2 is a current slot", StateSubsystem->GetCurrentSlot() == Slot2Handle);

	ScopedWorld->FinishWorldTravel();
	UTEST_TRUE("World state wasn't updated nor saved to TestSlot1 before loading the new world", Listener.bSaveStarted == false && PrevWorldState == CurrentWorldState);
	UTEST_TRUE("TestSlot2 is a current slot", StateSubsystem->GetCurrentSlot() == Slot2Handle);
	UTEST_TRUE("TestSlot2 is fully loaded", Listener.bLoadStarted == true && Listener.bLoadFinished == true && Listener.LoadSlot == Slot2Handle);
	
	return !HasAnyErrors();
}

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(FPersistentStateTest_InterfaceAPI, FPersistentStateAutomationTest, "PersistentState.APICallbacks", AutomationFlags)

void FPersistentStateTest_InterfaceAPI::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("Default"));
	OutBeautifiedNames.Add(TEXT("World Partition"));
	OutTestCommands.Add(TEXT("/PersistentState/PersistentStateTestMap_Default"));
	OutTestCommands.Add(TEXT("/PersistentState/PersistentStateTestMap_WP"));
}

bool FPersistentStateTest_InterfaceAPI::RunTest(const FString& Parameters)
{
	FPersistentStateAutomationTest::RunTest(Parameters);

	const FString SlotName{TEXT("TestSlot")};
	Initialize(Parameters, {SlotName});
	ON_SCOPE_EXIT { Cleanup(); };

	TArray<FGuid, TInlineAllocator<16>> ObjectIds;

	{
		APersistentStateEmptyTestActor* StaticActor = ScopedWorld->FindActorByTag<APersistentStateEmptyTestActor>(TEXT("EmptyActor"));
		UTEST_TRUE("Found static actor", StaticActor != nullptr);
		UPersistentStateEmptyTestComponent* StaticComponent = StaticActor->Component;
		UPersistentStateEmptyTestComponent* DynamicComponent = UE::Automation::CreateActorComponent<UPersistentStateEmptyTestComponent>(*ScopedWorld, StaticActor);
	
		FGuid ActorId = UE::PersistentState::FindUniqueIdFromObject(StaticActor);
		FGuid StaticComponentId = UE::PersistentState::FindUniqueIdFromObject(StaticComponent);
		FGuid DynamicComponentId = UE::PersistentState::FindUniqueIdFromObject(DynamicComponent);
		UTEST_TRUE("Static objects located", ActorId.IsValid() && StaticComponentId.IsValid() && DynamicComponentId.IsValid());

		ObjectIds.Append({ActorId, StaticComponentId, DynamicComponentId});
	}

	{
		APersistentStateEmptyTestActor* DynamicActor = ScopedWorld->SpawnActor<APersistentStateEmptyTestActor>();
		UPersistentStateEmptyTestComponent* StaticComponent = DynamicActor->Component;
		UPersistentStateEmptyTestComponent* DynamicComponent = UE::Automation::CreateActorComponent<UPersistentStateEmptyTestComponent>(*ScopedWorld, DynamicActor);

		FGuid ActorId = UE::PersistentState::FindUniqueIdFromObject(DynamicActor);
		FGuid StaticComponentId = UE::PersistentState::FindUniqueIdFromObject(StaticComponent);
		FGuid DynamicComponentId = UE::PersistentState::FindUniqueIdFromObject(DynamicComponent);
		UTEST_TRUE("Dynamic objects located", ActorId.IsValid() && StaticComponentId.IsValid() && DynamicComponentId.IsValid());

		ObjectIds.Append({ActorId, StaticComponentId, DynamicComponentId});
	}
	
	ExpectedSlot = StateSubsystem->FindSaveGameSlotByName(FName{SlotName});

	{
		StateSubsystem->SaveGameToSlot(ExpectedSlot);

		for (const FGuid& ObjectId: ObjectIds)
		{
			UObject* Object = UE::PersistentState::FindObjectByUniqueId(ObjectId);
			UTEST_TRUE("Object located back after save", Object != nullptr);
		
			auto Listener = CastChecked<IPersistentStateCallbackListener>(Object);
			UTEST_TRUE("Save callbacks executed for explicit slot save", Listener->bPreSaveStateCalled && Listener->bPostSaveStateCalled && Listener->bCustomStateSaved);

			Listener->ResetCallbacks();
		}
	}
	
	{
		// add travel option to override game mode for the loaded map. Otherwise it will load default game mode which will not match the current one
		const FString TravelOptions = TEXT("GAME=") + FSoftClassPath{ScopedWorld->GetGameMode()->GetClass()}.ToString();
		StateSubsystem->LoadGameFromSlot(ExpectedSlot, TravelOptions);
		ScopedWorld->FinishWorldTravel();

		for (const FGuid& ObjectId: ObjectIds)
		{
			UObject* Object = UE::PersistentState::FindObjectByUniqueId(ObjectId);
			UTEST_TRUE("Object located back after full load", Object != nullptr);
		
			auto Listener = CastChecked<IPersistentStateCallbackListener>(Object);
			UTEST_TRUE("Load callbacks executed for explicit slot load", Listener->bPreLoadStateCalled && Listener->bPostLoadStateCalled && Listener->bCustomStateLoaded);

			Listener->ResetCallbacks();
		}
	}

	{
		bool bSaveCallbacks = true;
		FDelegateHandle AllSavedHandle = StateSubsystem->OnSaveStateFinished.AddLambda([&bSaveCallbacks, &ObjectIds, this](const FPersistentStateSlotHandle& Slot) -> void
		{
			for (const FGuid& ObjectId: ObjectIds)
			{
				UObject* Object = UE::PersistentState::FindObjectByUniqueId(ObjectId);
				bSaveCallbacks &= TestTrue("Object located back after save", Object != nullptr);
		
				auto Listener = CastChecked<IPersistentStateCallbackListener>(Object);
				bSaveCallbacks &= TestTrue("Save callbacks executed for explicit slot save", Listener->bPreSaveStateCalled && Listener->bPostSaveStateCalled && Listener->bCustomStateSaved);

				Listener->ResetCallbacks();
			}
		});
	
		ScopedWorld->AbsoluteWorldTravel(TSoftObjectPtr<UWorld>{WorldPath}, ScopedWorld->GetGameMode()->GetClass());
		StateSubsystem->OnSaveStateFinished.Remove(AllSavedHandle);
		UTEST_TRUE("Save callbacks executed", bSaveCallbacks);
	
		for (const FGuid& ObjectId: ObjectIds)
		{
			UObject* Object = UE::PersistentState::FindObjectByUniqueId(ObjectId);
			UTEST_TRUE("Object located back after full load", Object != nullptr);
		
			auto Listener = CastChecked<IPersistentStateCallbackListener>(Object);
			UTEST_TRUE("Load callbacks executed for world travel", Listener->bPreLoadStateCalled && Listener->bPostLoadStateCalled && Listener->bCustomStateLoaded);

			Listener->ResetCallbacks();
		}
	}

	{
		for (const FGuid& ObjectId: ObjectIds)
		{
			UObject* Object = UE::PersistentState::FindObjectByUniqueId(ObjectId);
			auto Listener = CastChecked<IPersistentStateCallbackListener>(Object);
			Listener->bShouldSave = false;
		}
		
		bool bSaveCallbacks = true;
		FDelegateHandle NoneSavedHandle = StateSubsystem->OnSaveStateFinished.AddLambda([&bSaveCallbacks, &ObjectIds, this](const FPersistentStateSlotHandle& Slot) -> void
		{
			for (const FGuid& ObjectId: ObjectIds)
			{
				UObject* Object = UE::PersistentState::FindObjectByUniqueId(ObjectId);
				bSaveCallbacks &= TestTrue("Object located back after save", Object != nullptr);
			
				auto Listener = CastChecked<IPersistentStateCallbackListener>(Object);
				bSaveCallbacks &= TestTrue("Save callbacks NOT executed for world travel", !(Listener->bPreSaveStateCalled || Listener->bPostSaveStateCalled || Listener->bCustomStateSaved));

				Listener->ResetCallbacks();
			}
		});
		
		ScopedWorld->AbsoluteWorldTravel(TSoftObjectPtr<UWorld>{WorldPath}, ScopedWorld->GetGameMode()->GetClass());
		StateSubsystem->OnSaveStateFinished.Remove(NoneSavedHandle);
		UTEST_TRUE("Save callbacks NOT executed", bSaveCallbacks);
	
		for (const FGuid& ObjectId: ObjectIds)
		{
			UObject* Object = UE::PersistentState::FindObjectByUniqueId(ObjectId);
			UTEST_TRUE("Object located back after full load", Object != nullptr);
		
			auto Listener = CastChecked<IPersistentStateCallbackListener>(Object);
			UTEST_TRUE("Load callbacks NOT executed for world travel", !(Listener->bPreLoadStateCalled || Listener->bPostLoadStateCalled || Listener->bCustomStateLoaded));

			Listener->ResetCallbacks();
		}
	}
	
	return !HasAnyErrors();
}

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(FPersistentStateTest_Attachment, FPersistentStateAutomationTest, "PersistentState.Attachment", AutomationFlags)

void FPersistentStateTest_Attachment::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("Default"));
	OutBeautifiedNames.Add(TEXT("World Partition"));
	OutTestCommands.Add(TEXT("/PersistentState/PersistentStateTestMap_Default"));
	OutTestCommands.Add(TEXT("/PersistentState/PersistentStateTestMap_WP"));
}

bool FPersistentStateTest_Attachment::RunTest(const FString& Parameters)
{
	ExpectedSlot = {};
	PrevWorldState = CurrentWorldState = nullptr;

	const FString SlotName{TEXT("TestSlot")};
	Initialize(Parameters, {SlotName});
	ON_SCOPE_EXIT { Cleanup(); };

	APersistentStateTestActor* Parent = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StaticActor1"));
	APersistentStateTestActor* Child = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StaticActor2"));
	UTEST_TRUE("Found static actors", Parent && Child);
	
	APersistentStateTestActor* DynamicParent = ScopedWorld->SpawnActor<APersistentStateTestActor>();
	FGuid DynamicParentId = UE::PersistentState::FindUniqueIdFromObject(DynamicParent);
	
	APersistentStateTestActor* DynamicChild = ScopedWorld->SpawnActor<APersistentStateTestActor>();
	FGuid DynamicChildId = UE::PersistentState::FindUniqueIdFromObject(DynamicChild);
	UTEST_TRUE("Created dynamic actors", DynamicParent && DynamicChild);
	UTEST_TRUE("Created dynamic actor ids", DynamicParentId.IsValid() && DynamicChildId.IsValid());
	
	ExpectedSlot = StateSubsystem->FindSaveGameSlotByName(FName{SlotName});
	UTEST_TRUE("Found slot", ExpectedSlot.IsValid());

	Child->AttachToActor(Parent, FAttachmentTransformRules::KeepWorldTransform);
	DynamicParent->AttachToActor(Child, FAttachmentTransformRules::KeepWorldTransform);
	DynamicChild->AttachToActor(DynamicParent, FAttachmentTransformRules::KeepWorldTransform);
	UTEST_TRUE("Attach parents are valid", Child->GetAttachParentActor() == Parent && DynamicParent->GetAttachParentActor() == Child && DynamicChild->GetAttachParentActor() == DynamicParent);

	StateSubsystem->SaveGameToSlot(ExpectedSlot);
	
	// add travel option to override game mode for the loaded map. Otherwise it will load default game mode which will not match the current one
	const FString TravelOptions = TEXT("GAME=") + FSoftClassPath{ScopedWorld->GetGameMode()->GetClass()}.ToString();
	StateSubsystem->LoadGameFromSlot(ExpectedSlot, TravelOptions);
	ScopedWorld->FinishWorldTravel();

	Parent = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StaticActor1"));
	Child = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StaticActor2"));
	UTEST_TRUE("Found static actors", Parent && Child);
	
	DynamicParent = UE::PersistentState::FindObjectByUniqueId<APersistentStateTestActor>(DynamicParentId);
	DynamicChild = UE::PersistentState::FindObjectByUniqueId<APersistentStateTestActor>(DynamicChildId);
	UTEST_TRUE("Created dynamic actors", DynamicParent && DynamicChild);
	
	UTEST_TRUE("Attachment parent is restored", Child->GetAttachParentActor() == Parent);
	UTEST_TRUE("Attachment parent is restored", DynamicParent->GetAttachParentActor() == Child);
	UTEST_TRUE("Attachment parent is restored", DynamicChild->GetAttachParentActor() == DynamicParent);
	
	return !HasAnyErrors();
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPersistentStateTest_Streaming_Default, FPersistentStateAutomationTest, "PersistentState.Streaming.Default", AutomationFlags)

bool FPersistentStateTest_Streaming_Default::RunTest(const FString& Parameters)
{
	PrevWorldState = CurrentWorldState = nullptr;
	ExpectedSlot = {};

	const FString Level{TEXT("/PersistentState/PersistentStateTestMap_Default")};
	const FString Sublevel{TEXT("PersistentStateTestMap_Default_SubLevel")};
	const FString SlotName{TEXT("TestSlot")};
	
	Initialize(Level, {SlotName});
	ON_SCOPE_EXIT { Cleanup(); };

	ULevelStreaming* LevelStreaming = FStreamLevelAction::FindAndCacheLevelStreamingObject(FName{Sublevel}, *ScopedWorld);
	UTEST_TRUE("Found level streaming", LevelStreaming != nullptr);
	
	LevelStreaming->SetShouldBeLoaded(true);
	LevelStreaming->SetShouldBeVisible(true);
	GEngine->BlockTillLevelStreamingCompleted(*ScopedWorld);
	
	APersistentStateTestActor* StaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StreamActor1"));
	APersistentStateTestActor* OtherStaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StreamActor2"));
	UTEST_TRUE("Found static actors", StaticActor != nullptr && OtherStaticActor != nullptr);

	FActorSpawnParameters Params{};
	// spawn dynamic actors in the same scoped as owner, so that have the same streamed level
	Params.Owner = StaticActor;
	APersistentStateTestActor* DynamicActor = ScopedWorld->SpawnActorSimple<APersistentStateTestActor>();
	FGuid DynamicActorId = UE::PersistentState::FindUniqueIdFromObject(DynamicActor);
	UTEST_TRUE("Found dynamic actor", DynamicActorId.IsValid());
	
	APersistentStateTestActor* OtherDynamicActor = ScopedWorld->SpawnActorSimple<APersistentStateTestActor>();
	FGuid OtherDynamicActorId = UE::PersistentState::FindUniqueIdFromObject(OtherDynamicActor);
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

	LevelStreaming->SetShouldBeLoaded(false);
	LevelStreaming->SetShouldBeVisible(false);
	GEngine->BlockTillLevelStreamingCompleted(*ScopedWorld);

	StaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StreamActor1"));
	OtherStaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StreamActor2"));
	UTEST_TRUE("Unloaded static actors", StaticActor == nullptr && OtherStaticActor == nullptr);
	
	LevelStreaming->SetShouldBeLoaded(true);
	LevelStreaming->SetShouldBeVisible(true);
	GEngine->BlockTillLevelStreamingCompleted(*ScopedWorld);
	
	StaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StreamActor1"));
	OtherStaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StreamActor2"));
	UTEST_TRUE("Found static actors", StaticActor && OtherStaticActor);
	DynamicActor = UE::PersistentState::FindObjectByUniqueId<APersistentStateTestActor>(DynamicActorId);
	OtherDynamicActor = UE::PersistentState::FindObjectByUniqueId<APersistentStateTestActor>(OtherDynamicActorId);
	UTEST_TRUE("Found dynamic actors", DynamicActor && OtherDynamicActor);
	
	UTEST_TRUE("Restored references are correct", VerifyActor(StaticActor, OtherStaticActor, DynamicActor, TEXT("StreamActor"), 1));
	UTEST_TRUE("Restored references are correct", VerifyActor(OtherStaticActor, StaticActor, DynamicActor, TEXT("OtherStreamActor"), 2));
	UTEST_TRUE("Restored references are correct", VerifyActor(DynamicActor, StaticActor, OtherDynamicActor, TEXT("DynamicActor"), 3));
	UTEST_TRUE("Restored references are correct", VerifyActor(OtherDynamicActor, StaticActor, DynamicActor, TEXT("OtherDynamicActor"), 4));
	
	return !HasAnyErrors();
}


IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(FPersistentStateTest_ObjectReferences, FPersistentStateAutomationTest, "PersistentState.ObjectReferences", AutomationFlags)

void FPersistentStateTest_ObjectReferences::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("Default"));
	OutBeautifiedNames.Add(TEXT("World Partition"));
	OutTestCommands.Add(TEXT("/PersistentState/PersistentStateTestMap_Default"));
	OutTestCommands.Add(TEXT("/PersistentState/PersistentStateTestMap_WP"));
}

bool FPersistentStateTest_ObjectReferences::RunTest(const FString& Parameters)
{
	ExpectedSlot = {};
	PrevWorldState = CurrentWorldState = nullptr;

	const FString SlotName{TEXT("TestSlot")};
	Initialize(Parameters, {SlotName});
	ON_SCOPE_EXIT { Cleanup(); };
	
	APersistentStateTestActor* StaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StaticActor1"));
	APersistentStateTestActor* OtherStaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StaticActor2"));
	UTEST_TRUE("Found static actors", StaticActor != nullptr && OtherStaticActor != nullptr);
	
	APersistentStateTestActor* DynamicActor = ScopedWorld->SpawnActor<APersistentStateTestActor>();
	FGuid DynamicActorId = UE::PersistentState::FindUniqueIdFromObject(DynamicActor);
	UTEST_TRUE("Found dynamic actor", DynamicActorId.IsValid());
	
	APersistentStateTestActor* OtherDynamicActor = ScopedWorld->SpawnActor<APersistentStateTestActor>();
	FGuid OtherDynamicActorId = UE::PersistentState::FindUniqueIdFromObject(OtherDynamicActor);
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
	
	InitActor(StaticActor, OtherStaticActor, DynamicActor, TEXT("StaticActor"), 1);
	InitActor(OtherStaticActor, StaticActor, DynamicActor, TEXT("OtherStaticActor"), 2);
	InitActor(DynamicActor, StaticActor, OtherDynamicActor, TEXT("DynamicActor"), 3);
	InitActor(OtherDynamicActor, StaticActor, DynamicActor, TEXT("OtherDynamicActor"), 4);
	
	ExpectedSlot = StateSubsystem->FindSaveGameSlotByName(FName{SlotName});
	UTEST_TRUE("Found slot", ExpectedSlot.IsValid());
	
	StateSubsystem->SaveGameToSlot(ExpectedSlot);
	
	// add travel option to override game mode for the loaded map. Otherwise it will load default game mode which will not match the current one
	const FString TravelOptions = TEXT("GAME=") + FSoftClassPath{ScopedWorld->GetGameMode()->GetClass()}.ToString();
	StateSubsystem->LoadGameFromSlot(ExpectedSlot, TravelOptions);
	ScopedWorld->FinishWorldTravel();

	StaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StaticActor1"));
	OtherStaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StaticActor2"));
	UTEST_TRUE("Found static actors", StaticActor && OtherStaticActor);
	DynamicActor = UE::PersistentState::FindObjectByUniqueId<APersistentStateTestActor>(DynamicActorId);
	OtherDynamicActor = UE::PersistentState::FindObjectByUniqueId<APersistentStateTestActor>(OtherDynamicActorId);
	UTEST_TRUE("Found dynamic actors", DynamicActor && OtherDynamicActor);

	UTEST_TRUE("Restored references are correct", VerifyActor(StaticActor, OtherStaticActor, DynamicActor, TEXT("StaticActor"), 1));
	UTEST_TRUE("Restored references are correct", VerifyActor(OtherStaticActor, StaticActor, DynamicActor, TEXT("OtherStaticActor"), 2));
	UTEST_TRUE("Restored references are correct", VerifyActor(DynamicActor, StaticActor, OtherDynamicActor, TEXT("DynamicActor"), 3));
	UTEST_TRUE("Restored references are correct", VerifyActor(OtherDynamicActor, StaticActor, DynamicActor, TEXT("OtherDynamicActor"), 4));
	
	return !HasAnyErrors();
}
