
#include "PersistentStateTestClasses.h"
#include "AutomationCommon.h"
#include "AutomationWorld.h"
#include "PersistentStateObjectId.h"
#include "PersistentStateSettings.h"
#include "PersistentStateSubsystem.h"
#include "GameFramework/GameModeBase.h"
#include "Kismet/GameplayStatics.h"
#include "PersistentStateAutomationTest.h"

UE_DISABLE_OPTIMIZATION

using namespace UE::PersistentState;

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(
	FPersistentStateTest_ManagerCallbacks, FPersistentStateAutoTest,
	"PersistentState.ManagerCallbacks", AutomationFlags
)

bool FPersistentStateTest_ManagerCallbacks::RunTest(const FString& Parameters)
{
	FPersistentStateAutoTest::RunTest(Parameters);

	const FString StateSlot1{TEXT("TestSlot1")};
	const FString StateSlot2{TEXT("TestSlot2")};

	const FString WorldPackage{TEXT("/PersistentState/PersistentStateTestMap_Default")};
	Initialize(WorldPackage, TArray{StateSlot1, StateSlot2});
	ON_SCOPE_EXIT { Cleanup(); };
	
	FPersistentStateSlotHandle Slot1Handle = StateSubsystem->FindSaveGameSlotByName(FName{StateSlot1});
	UTEST_TRUE("State subsystem has slot1", Slot1Handle.IsValid());
	FPersistentStateSlotHandle Slot2Handle = StateSubsystem->FindSaveGameSlotByName(FName{StateSlot2});
	UTEST_TRUE("State subsystem has slot2", Slot2Handle.IsValid());
	
	UPersistentStateTestWorldManager* WorldManager = StateSubsystem->GetStateManager<UPersistentStateTestWorldManager>();
	UTEST_TRUE("Found world manager", WorldManager != nullptr);
	UTEST_TRUE("World manager is initialized, but not loaded", WorldManager->bInitCalled && !WorldManager->bPreLoadStateCalled && !WorldManager->bPostLoadStateCalled);
	
	UPersistentStateTestGameManager* GameManager = StateSubsystem->GetStateManager<UPersistentStateTestGameManager>();
	UTEST_TRUE("Found game manager", GameManager != nullptr);
	UTEST_TRUE("Game manager is initialized, but not loaded", GameManager->bInitCalled && !GameManager->bPreLoadStateCalled && !GameManager->bPostLoadStateCalled);
	
	ExpectedSlot = Slot1Handle;
	
	StateSubsystem->SaveGameToSlot(Slot1Handle);
	StateSubsystem->Tick(1.f);

	UTEST_TRUE("Current slot changed to Slot1", StateSubsystem->GetActiveSaveGameSlot() == Slot1Handle);
	UTEST_TRUE("SaveState callbacks executed", WorldManager->bSaveStateCalled && GameManager->bSaveStateCalled);

	const FString TravelOptions = TEXT("GAME=") + FSoftClassPath{ScopedWorld->GetGameMode()->GetClass()}.ToString();
	StateSubsystem->LoadGameFromSlot(Slot1Handle, TravelOptions);
	StateSubsystem->Tick(1.f);

	UTEST_TRUE("Cleanup callback executed for world manager, because the world has been reloaded", WorldManager->bCleanupCalled);
	UTEST_TRUE("Cleanup callback NOT executed for game manager, as slot hasn't changed", !GameManager->bCleanupCalled);
	
	ScopedWorld->FinishWorldTravel();

	auto PrevWorldManager = WorldManager;
	WorldManager = StateSubsystem->GetStateManager<UPersistentStateTestWorldManager>();
	UTEST_TRUE("New world manager is created and loaded", PrevWorldManager != WorldManager && WorldManager->bInitCalled && WorldManager->bPreLoadStateCalled && WorldManager->bPostLoadStateCalled);

	auto PrevGameManager = GameManager;
	GameManager = StateSubsystem->GetStateManager<UPersistentStateTestGameManager>();
	UTEST_TRUE("Old game manager is used, it is not initialized (as it wasn't cleaned up) and it wasn't loaded", PrevGameManager == GameManager && !GameManager->bPreLoadStateCalled && !GameManager->bPostLoadStateCalled);

	ExpectedSlot = Slot2Handle;
	StateSubsystem->SaveGameToSlot(Slot2Handle);
	StateSubsystem->Tick(1.f);
	UTEST_TRUE("Current slot changed to Slot2", StateSubsystem->GetActiveSaveGameSlot() == Slot2Handle);

	ExpectedSlot = Slot1Handle;
	StateSubsystem->LoadGameFromSlot(Slot1Handle);
	StateSubsystem->Tick(1.f);
	
	UTEST_TRUE("SaveState callbacks executed", WorldManager->bSaveStateCalled && GameManager->bSaveStateCalled);
	UTEST_TRUE("Cleanup callback executed for world manager, because the world has been reloaded", WorldManager->bCleanupCalled);
	UTEST_TRUE("Cleanup callback executed for game manager, because new slot is pending load", GameManager->bCleanupCalled);
	
	ScopedWorld->FinishWorldTravel();
	
	PrevWorldManager = WorldManager;
	WorldManager = StateSubsystem->GetStateManager<UPersistentStateTestWorldManager>();
	UTEST_TRUE("New world manager is created and loaded", PrevWorldManager != WorldManager && WorldManager->bInitCalled && WorldManager->bPreLoadStateCalled && WorldManager->bPostLoadStateCalled);

	PrevGameManager = GameManager;
	GameManager = StateSubsystem->GetStateManager<UPersistentStateTestGameManager>();
	UTEST_TRUE("New game manager is created and loaded", PrevGameManager != GameManager && GameManager->bInitCalled && GameManager->bPreLoadStateCalled && GameManager->bPostLoadStateCalled);
	
	return !HasAnyErrors();
}

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(
	FPersistentStateTest_SubsystemEvents, FPersistentStateAutoTest,
	"PersistentState.SubsystemEvents", AutomationFlags
)

void FPersistentStateTest_SubsystemEvents::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("Default"));
	OutBeautifiedNames.Add(TEXT("World Partition"));
	OutTestCommands.Add(TEXT("/PersistentState/PersistentStateTestMap_Default"));
	OutTestCommands.Add(TEXT("/PersistentState/PersistentStateTestMap_WP"));
}

bool FPersistentStateTest_SubsystemEvents::RunTest(const FString& Parameters)
{
	FPersistentStateAutoTest::RunTest(Parameters);
	 
	const FString StateSlot1{TEXT("TestSlot1")};
	const FString StateSlot2{TEXT("TestSlot2")};
	
	FPersistentStateSubsystemCallbackListener Listener{};
	Initialize(Parameters, TArray{StateSlot1, StateSlot2}, APersistentStateGameMode::StaticClass(), TEXT(""), [&Listener](UWorld* World)
	{
		if (UPersistentStateSubsystem* StateSubsystem = World->GetGameInstance()->GetSubsystem<UPersistentStateSubsystem>())
		{
			Listener.SetSubsystem(*StateSubsystem);
		}
	});
	ON_SCOPE_EXIT { Cleanup(); };

	UTEST_TRUE("Listener is registered with state subsystem", Listener.Subsystem != nullptr);
	UTEST_TRUE("LoadGame has not happened without current slot", Listener.bLoadStarted == false);
	
	UTEST_TRUE("Current slot is empty", StateSubsystem->GetActiveSaveGameSlot().IsValid() == false);

	TArray<FPersistentStateSlotHandle> Slots;
	StateSubsystem->GetSaveGameSlots(Slots);
	UTEST_TRUE("State subsystem has two save slots", Slots.Num() == 2);

	FPersistentStateSlotHandle Slot1Handle = StateSubsystem->FindSaveGameSlotByName(FName{StateSlot1});
	UTEST_TRUE("State subsystem has slot1", Slot1Handle.IsValid());
	FPersistentStateSlotHandle Slot2Handle = StateSubsystem->FindSaveGameSlotByName(FName{StateSlot2});
	UTEST_TRUE("State subsystem has slot2", Slot2Handle.IsValid());
	
	bool bSaveResult = StateSubsystem->SaveGame();
	UTEST_TRUE("SaveGame has failed without current slot", bSaveResult == false && Listener.bSaveStarted == false);
	UTEST_TRUE("Current slot is empty", StateSubsystem->GetActiveSaveGameSlot().IsValid() == false);

	ExpectedSlot = Slot1Handle;
	StateSubsystem->SaveGameToSlot(Slot1Handle);
	StateSubsystem->Tick(1.f);
	
	UTEST_TRUE("TestSlot1 is a current slot", StateSubsystem->GetActiveSaveGameSlot() == Slot1Handle);
	UTEST_TRUE("TestSlot1 is fully saved", Listener.bSaveStarted && Listener.bSaveFinished && Listener.SaveSlot == Slot1Handle);
	UTEST_TRUE("TestSlot1 save created a world state", CurrentWorldState.IsValid());
	UTEST_TRUE("TestSlot1 save created a game state", CurrentGameState.IsValid());

	PrevWorldState = CurrentWorldState;
	Listener.Clear();

	const FString TravelOptions = TEXT("GAME=") + FSoftClassPath{ScopedWorld->GetGameMode()->GetClass()}.ToString();
	StateSubsystem->LoadGameFromSlot(Slot1Handle, TravelOptions);
	StateSubsystem->Tick(1.f);
	UTEST_TRUE("TestSlot1 is a current slot", StateSubsystem->GetActiveSaveGameSlot() == Slot1Handle);
	
	ScopedWorld->FinishWorldTravel();
	UTEST_TRUE("TestSlot1 is not saved before cleanup", Listener.bSaveStarted == false && CurrentWorldState == PrevWorldState);
	UTEST_TRUE("TestSlot1 is fully loaded for current slot", Listener.bLoadStarted && Listener.bLoadFinished && Listener.LoadSlot == Slot1Handle);

	PrevWorldState = CurrentWorldState;
    Listener.Clear();
	
	UGameplayStatics::OpenLevelBySoftObjectPtr(*ScopedWorld, TSoftObjectPtr<UWorld>{WorldPath}, true, TravelOptions);
	ScopedWorld->FinishWorldTravel();
	UTEST_TRUE("TestSlot1 is saved before cleanup", Listener.bSaveStarted && Listener.bSaveFinished && Listener.SaveSlot == Slot1Handle && CurrentWorldState != PrevWorldState);
	UTEST_TRUE("TestSlot1 is fully loaded for current slot", Listener.bLoadStarted && Listener.bLoadFinished && Listener.LoadSlot == Slot1Handle);

	ExpectedSlot = Slot2Handle;
	PrevWorldState = CurrentWorldState;
	Listener.Clear();
    
	StateSubsystem->LoadGameWorldFromSlot(Slot2Handle, TSoftObjectPtr<UWorld>{WorldPath});
	StateSubsystem->Tick(1.f);
	UTEST_TRUE("TestSlot2 is a current slot", StateSubsystem->GetActiveSaveGameSlot() == Slot2Handle);

	ScopedWorld->FinishWorldTravel();
	UTEST_TRUE("World state wasn't updated nor saved to TestSlot1 before loading the new world", Listener.bSaveStarted == false && PrevWorldState == CurrentWorldState);
	UTEST_TRUE("TestSlot2 is a current slot", StateSubsystem->GetActiveSaveGameSlot() == Slot2Handle);
	UTEST_TRUE("TestSlot2 is fully loaded", Listener.bLoadStarted == true && Listener.bLoadFinished == true && Listener.LoadSlot == Slot2Handle);
	
	return !HasAnyErrors();
}

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(
	FPersistentStateTest_DestroyedObjects, FPersistentStateAutoTest,
	"PersistentState.DestroyedObjects", AutomationFlags
)

void FPersistentStateTest_DestroyedObjects::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("Default"));
	OutBeautifiedNames.Add(TEXT("World Partition"));
	OutTestCommands.Add(TEXT("/PersistentState/PersistentStateTestMap_Default"));
	OutTestCommands.Add(TEXT("/PersistentState/PersistentStateTestMap_WP"));
}

bool FPersistentStateTest_DestroyedObjects::RunTest(const FString& Parameters)
{
	FPersistentStateAutoTest::RunTest(Parameters);

	const FString SlotName{TEXT("TestSlot")};
	Initialize(Parameters, {SlotName});
	ON_SCOPE_EXIT { Cleanup(); };

	APersistentStateEmptyTestActor* EmptyActor = ScopedWorld->FindActorByTag<APersistentStateEmptyTestActor>(TEXT("EmptyActor"));
	
	APersistentStateTestActor* StaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StaticActor1"));
	APersistentStateTestActor* OtherStaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StaticActor2"));
	UTEST_TRUE("Found static actors", StaticActor && OtherStaticActor && EmptyActor);
	
	APersistentStateTestActor* DynamicActor = ScopedWorld->SpawnActor<APersistentStateTestActor>();
	FPersistentStateObjectId DynamicID = FPersistentStateObjectId::FindObjectId(DynamicActor);
	UTEST_TRUE("Created dynamic actor", DynamicActor && DynamicID.IsValid());

	APersistentStateTestActor* OtherDynamicActor = ScopedWorld->SpawnActor<APersistentStateTestActor>();
	FPersistentStateObjectId OtherDynamicID = FPersistentStateObjectId::FindObjectId(OtherDynamicActor);
	UTEST_TRUE("Created dynamic actor", OtherDynamicActor && OtherDynamicID.IsValid());
	
	ExpectedSlot = StateSubsystem->FindSaveGameSlotByName(FName{SlotName});
	UTEST_TRUE("Found slot", ExpectedSlot.IsValid());

	OtherStaticActor->DynamicComponent = UE::Automation::CreateActorComponent<UPersistentStateTestComponent>(*ScopedWorld, StaticActor);
	OtherDynamicActor->DynamicComponent = UE::Automation::CreateActorComponent<UPersistentStateTestComponent>(*ScopedWorld, StaticActor);

	EmptyActor->Destroy();
	StaticActor->Destroy();
	OtherStaticActor->StaticComponent->DestroyComponent();
	OtherStaticActor->DynamicComponent->DestroyComponent();
	DynamicActor->Destroy();
	OtherDynamicActor->StaticComponent->DestroyComponent();
	OtherDynamicActor->DynamicComponent->DestroyComponent();

	StateSubsystem->SaveGameToSlot(ExpectedSlot);
	StateSubsystem->Tick(1.f);
	
	// add travel option to override game mode for the loaded map. Otherwise it will load default game mode which will not match the current one
	const FString TravelOptions = TEXT("GAME=") + FSoftClassPath{ScopedWorld->GetGameMode()->GetClass()}.ToString();
	StateSubsystem->LoadGameFromSlot(ExpectedSlot, TravelOptions);
	StateSubsystem->Tick(1.f);
	ScopedWorld->FinishWorldTravel();

	EmptyActor = ScopedWorld->FindActorByTag<APersistentStateEmptyTestActor>(TEXT("EmptyActor"));
	StaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StaticActor1"));
	OtherStaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StaticActor2"));
	UTEST_TRUE("Not found destroyed actors", StaticActor == nullptr && EmptyActor == nullptr);
	UTEST_TRUE("Found not destroyed actor", OtherStaticActor && !IsValid(OtherStaticActor->StaticComponent) && !IsValid(OtherStaticActor->DynamicComponent));

	DynamicActor = DynamicID.ResolveObject<APersistentStateTestActor>();
	OtherDynamicActor = OtherDynamicID.ResolveObject<APersistentStateTestActor>();
	UTEST_TRUE("not found destroyed dynamic actor", DynamicActor == nullptr);
	UTEST_TRUE("found other dynamic actor", OtherDynamicActor && !IsValid(OtherDynamicActor->StaticComponent) && !IsValid(OtherDynamicActor->DynamicComponent));
	
	return !HasAnyErrors();
}

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(
	FPersistentStateTest_ShouldSaveState, FPersistentStateAutoTest,
	"PersistentState.ShouldSaveState", AutomationFlags
)

void FPersistentStateTest_ShouldSaveState::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("Default"));
	OutBeautifiedNames.Add(TEXT("World Partition"));
	OutTestCommands.Add(TEXT("/PersistentState/PersistentStateTestMap_Default"));
	OutTestCommands.Add(TEXT("/PersistentState/PersistentStateTestMap_WP"));
}

bool FPersistentStateTest_ShouldSaveState::RunTest(const FString& Parameters)
{
	FPersistentStateAutoTest::RunTest(Parameters);

	const FString SlotName{TEXT("TestSlot")};
	Initialize(Parameters, {SlotName});
	ON_SCOPE_EXIT { Cleanup(); };

	TArray<FPersistentStateObjectId, TInlineAllocator<16>> StaticObjects;
	TArray<FPersistentStateObjectId, TInlineAllocator<16>> DynamicObjects;
	
	{
		APersistentStateEmptyTestActor* StaticActor = ScopedWorld->FindActorByTag<APersistentStateEmptyTestActor>(TEXT("EmptyActor"));
		UTEST_TRUE("Found static actor", StaticActor != nullptr);
		UPersistentStateEmptyTestComponent* StaticComponent = StaticActor->Component;
		UPersistentStateEmptyTestComponent* DynamicComponent = UE::Automation::CreateActorComponent<UPersistentStateEmptyTestComponent>(*ScopedWorld, StaticActor);
	
		FPersistentStateObjectId ActorId = FPersistentStateObjectId::FindObjectId(StaticActor);
		FPersistentStateObjectId StaticComponentId = FPersistentStateObjectId::FindObjectId(StaticComponent);
		FPersistentStateObjectId DynamicComponentId = FPersistentStateObjectId::FindObjectId(DynamicComponent);
		UTEST_TRUE("Static objects located", ActorId.IsValid() && StaticComponentId.IsValid() && DynamicComponentId.IsValid());

		UPersistentStateTestWorldSubsystem* WorldSubsystem = ScopedWorld->GetSubsystem<UPersistentStateTestWorldSubsystem>();
		FPersistentStateObjectId WorldSubsystemId = FPersistentStateObjectId::FindObjectId(WorldSubsystem);
		UPersistentStateTestGameSubsystem* Subsystem = ScopedWorld->GetSubsystem<UPersistentStateTestGameSubsystem>();
		FPersistentStateObjectId GameSubsystemId = FPersistentStateObjectId::FindObjectId(Subsystem);
		UTEST_TRUE("Subsystems located", WorldSubsystemId.IsValid() && GameSubsystemId.IsValid());

		StaticObjects.Append({ActorId, StaticComponentId, WorldSubsystemId, GameSubsystemId});
		DynamicObjects.Append({DynamicComponentId});
	}

	{
		APersistentStateEmptyTestActor* DynamicActor = ScopedWorld->SpawnActor<APersistentStateEmptyTestActor>();
		UPersistentStateEmptyTestComponent* StaticComponent = DynamicActor->Component;
		UPersistentStateEmptyTestComponent* DynamicComponent = UE::Automation::CreateActorComponent<UPersistentStateEmptyTestComponent>(*ScopedWorld, DynamicActor);

		FPersistentStateObjectId ActorId = FPersistentStateObjectId::FindObjectId(DynamicActor);
		FPersistentStateObjectId StaticComponentId = FPersistentStateObjectId::FindObjectId(StaticComponent);
		FPersistentStateObjectId DynamicComponentId = FPersistentStateObjectId::FindObjectId(DynamicComponent);
		UTEST_TRUE("Dynamic objects located", ActorId.IsValid() && StaticComponentId.IsValid() && DynamicComponentId.IsValid());

		// static component is actually dynamic in this sense, because its creation is dependent on a dynamic actor
		DynamicObjects.Append({ActorId, StaticComponentId, DynamicComponentId});
	}
	
	auto AllObjects = StaticObjects;
	AllObjects.Append(DynamicObjects);
	
	ExpectedSlot = StateSubsystem->FindSaveGameSlotByName(FName{SlotName});

	// verify that callbacks are not called for objects that should not be saved
	{
		for (const FPersistentStateObjectId& ObjectId: AllObjects)
		{
			UObject* Object = ObjectId.ResolveObject();
			auto Listener = CastChecked<IPersistentStateCallbackListener>(Object);
			Listener->bShouldSave = false;
		}

		bool bSaveCallbacks = true;
		FDelegateHandle NoneSavedHandle = StateSubsystem->OnSaveStateFinished.AddLambda([&bSaveCallbacks, &AllObjects, this](const FPersistentStateSlotHandle& Slot) -> void
		{
			for (const FPersistentStateObjectId& ObjectId: AllObjects)
			{
				UObject* Object = ObjectId.ResolveObject();
				bSaveCallbacks &= TestTrue("Object located back after save", Object != nullptr);
			
				auto Listener = CastChecked<IPersistentStateCallbackListener>(Object);
				bSaveCallbacks &= TestTrue("Save callbacks NOT executed for explicit slot save", !(Listener->bPreSaveStateCalled || Listener->bPostSaveStateCalled || Listener->bCustomStateSaved));

				Listener->Reset();
			}
		});
		
		StateSubsystem->SaveGameToSlot(ExpectedSlot);
		StateSubsystem->Tick(1.f);
		UTEST_TRUE("Save callbacks NOT executed", bSaveCallbacks);
		
		// add travel option to override game mode for the loaded map. Otherwise it will load default game mode which will not match the current one
		const FString TravelOptions = TEXT("GAME=") + FSoftClassPath{ScopedWorld->GetGameMode()->GetClass()}.ToString();
		StateSubsystem->LoadGameFromSlot(ExpectedSlot, TravelOptions);
		StateSubsystem->Tick(1.f);
		ScopedWorld->FinishWorldTravel();
		
		StateSubsystem->OnSaveStateFinished.Remove(NoneSavedHandle);
		
		UTEST_TRUE("Save callbacks NOT executed", bSaveCallbacks);
		
		for (const FPersistentStateObjectId& ObjectId: StaticObjects)
		{
			UObject* Object = ObjectId.ResolveObject();
			UTEST_TRUE("Static object located back after explicit slot load", Object != nullptr);
		
			auto Listener = CastChecked<IPersistentStateCallbackListener>(Object);
			UTEST_TRUE("Load callbacks NOT executed for explicit slot load", !(Listener->bPreLoadStateCalled || Listener->bPostLoadStateCalled || Listener->bCustomStateLoaded));

			Listener->Reset();
		}
		
		for (const FPersistentStateObjectId& ObjectId: DynamicObjects)
		{
			UObject* Object = ObjectId.ResolveObject();
			UTEST_TRUE("Dynamic Object NOT located back after explicit slot load", Object == nullptr);
		}
	}
	
	return !HasAnyErrors();
}


IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(
	FPersistentStateTest_InterfaceAPI, FPersistentStateAutoTest,
	"PersistentState.ObjectCallbacks", AutomationFlags
)

void FPersistentStateTest_InterfaceAPI::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("Default"));
	OutBeautifiedNames.Add(TEXT("World Partition"));
	OutTestCommands.Add(TEXT("/PersistentState/PersistentStateTestMap_Default"));
	OutTestCommands.Add(TEXT("/PersistentState/PersistentStateTestMap_WP"));
}

bool FPersistentStateTest_InterfaceAPI::RunTest(const FString& Parameters)
{
	FPersistentStateAutoTest::RunTest(Parameters);

	const FString SlotName{TEXT("TestSlot")};
	Initialize(Parameters, {SlotName}, AGameModeBase::StaticClass());
	ON_SCOPE_EXIT { Cleanup(); };

	TArray<FPersistentStateObjectId, TInlineAllocator<16>> ObjectIds;

	{
		APersistentStateEmptyTestActor* StaticActor = ScopedWorld->FindActorByTag<APersistentStateEmptyTestActor>(TEXT("EmptyActor"));
		UTEST_TRUE("Found static actor", StaticActor != nullptr);
		UPersistentStateEmptyTestComponent* StaticComponent = StaticActor->Component;
		UPersistentStateEmptyTestComponent* DynamicComponent = UE::Automation::CreateActorComponent<UPersistentStateEmptyTestComponent>(*ScopedWorld, StaticActor);
	
		FPersistentStateObjectId ActorId = FPersistentStateObjectId::FindObjectId(StaticActor);
		FPersistentStateObjectId StaticComponentId = FPersistentStateObjectId::FindObjectId(StaticComponent);
		FPersistentStateObjectId DynamicComponentId = FPersistentStateObjectId::FindObjectId(DynamicComponent);
		UTEST_TRUE("Static objects located", ActorId.IsValid() && StaticComponentId.IsValid() && DynamicComponentId.IsValid());

		ObjectIds.Append({ActorId, StaticComponentId, DynamicComponentId});
	}

	{
		APersistentStateEmptyTestActor* DynamicActor = ScopedWorld->SpawnActor<APersistentStateEmptyTestActor>();
		UPersistentStateEmptyTestComponent* StaticComponent = DynamicActor->Component;
		UPersistentStateEmptyTestComponent* DynamicComponent = UE::Automation::CreateActorComponent<UPersistentStateEmptyTestComponent>(*ScopedWorld, DynamicActor);

		FPersistentStateObjectId ActorId = FPersistentStateObjectId::FindObjectId(DynamicActor);
		FPersistentStateObjectId StaticComponentId = FPersistentStateObjectId::FindObjectId(StaticComponent);
		FPersistentStateObjectId DynamicComponentId = FPersistentStateObjectId::FindObjectId(DynamicComponent);
		UTEST_TRUE("Dynamic objects located", ActorId.IsValid() && StaticComponentId.IsValid() && DynamicComponentId.IsValid());

		ObjectIds.Append({ActorId, StaticComponentId, DynamicComponentId});
	}

	{
		if (UPersistentStateTestWorldSubsystem* Subsystem = ScopedWorld->GetSubsystem<UPersistentStateTestWorldSubsystem>())
		{
			FPersistentStateObjectId Handle = FPersistentStateObjectId::FindObjectId(Subsystem);
			UTEST_TRUE("World subsystem created and assigned an object handle", Subsystem != nullptr && Handle.IsValid());
		
			ObjectIds.Append({Handle});
		}
	}

	{
		if (UPersistentStateTestGameSubsystem* Subsystem = ScopedWorld->GetSubsystem<UPersistentStateTestGameSubsystem>())
		{
			FPersistentStateObjectId Handle = FPersistentStateObjectId::FindObjectId(Subsystem);
			UTEST_TRUE("Game subsystem created and assigned an object handle", Subsystem != nullptr && Handle.IsValid());
			
			ObjectIds.Append({Handle});
		}
	}
	
	ExpectedSlot = StateSubsystem->FindSaveGameSlotByName(FName{SlotName});

	{
		for (const FPersistentStateObjectId& ObjectId: ObjectIds)
		{
			UObject* Object = ObjectId.ResolveObject();
			auto Listener = CastChecked<IPersistentStateCallbackListener>(Object);
			Listener->bShouldSave = true;
		}
		
		StateSubsystem->SaveGameToSlot(ExpectedSlot);
		StateSubsystem->Tick(1.f);

		for (const FPersistentStateObjectId& ObjectId: ObjectIds)
		{
			UObject* Object = ObjectId.ResolveObject();
			UTEST_TRUE("Object located back after save", Object != nullptr);
		
			auto Listener = CastChecked<IPersistentStateCallbackListener>(Object);
			UTEST_TRUE("Save callbacks executed for explicit slot save", Listener->bPreSaveStateCalled && Listener->bPostSaveStateCalled && Listener->bCustomStateSaved);

			Listener->Reset();
		}
	}
	
	{
		// add travel option to override game mode for the loaded map. Otherwise it will load default game mode which will not match the current one
		const FString TravelOptions = TEXT("GAME=") + FSoftClassPath{ScopedWorld->GetGameMode()->GetClass()}.ToString();
		StateSubsystem->LoadGameFromSlot(ExpectedSlot, TravelOptions);
		StateSubsystem->Tick(1.f);

		FObjectKey OldWorldKey{ScopedWorld->GetWorld()};
		// finish world travel
		ScopedWorld->FinishWorldTravel();

		UTEST_TRUE("world travel happened", OldWorldKey != FObjectKey{ScopedWorld->GetWorld()});

		for (const FPersistentStateObjectId& ObjectId: ObjectIds)
		{
			UObject* Object = ObjectId.ResolveObject();
			UTEST_TRUE("Object located back after full load", Object != nullptr);
		
			auto Listener = CastChecked<IPersistentStateCallbackListener>(Object);
			UTEST_TRUE("Load callbacks executed for explicit slot load", Listener->bPreLoadStateCalled && Listener->bPostLoadStateCalled && Listener->bCustomStateLoaded);

			Listener->Reset();
		}
	}

	{
		bool bSaveCallbacks = true;
		FDelegateHandle AllSavedHandle = StateSubsystem->OnSaveStateFinished.AddLambda([&bSaveCallbacks, &ObjectIds, this](const FPersistentStateSlotHandle& Slot) -> void
		{
			for (const FPersistentStateObjectId& ObjectId: ObjectIds)
			{
				UObject* Object = ObjectId.ResolveObject();
				bSaveCallbacks &= TestTrue("Object located back after save", Object != nullptr);
		
				auto Listener = CastChecked<IPersistentStateCallbackListener>(Object);
				bSaveCallbacks &= TestTrue("Save callbacks executed for explicit slot save", Listener->bPreSaveStateCalled && Listener->bPostSaveStateCalled && Listener->bCustomStateSaved);

				Listener->Reset();
			}
		});
	
		ScopedWorld->AbsoluteWorldTravel(TSoftObjectPtr<UWorld>{WorldPath}, ScopedWorld->GetGameMode()->GetClass());
		StateSubsystem->OnSaveStateFinished.Remove(AllSavedHandle);
		UTEST_TRUE("Save callbacks executed", bSaveCallbacks);
	
		for (const FPersistentStateObjectId& ObjectId: ObjectIds)
		{
			UObject* Object = ObjectId.ResolveObject();
			UTEST_TRUE("Object located back after full load", Object != nullptr);
		
			auto Listener = CastChecked<IPersistentStateCallbackListener>(Object);
			UTEST_TRUE("Load callbacks executed for world travel", Listener->bPreLoadStateCalled && Listener->bPostLoadStateCalled && Listener->bCustomStateLoaded);

			Listener->Reset();
		}
	}
	
	{
		Cleanup();
		// Collect garbage is required to remove old world objects from the engine entirely.
		// Otherwise, there's going to be stable ID collision between new and previous world objects, because package remapping is a thing
		CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		Initialize(Parameters, {SlotName}, APersistentStateGameMode::StaticClass(), SlotName);

		for (const FPersistentStateObjectId& ObjectId: ObjectIds)
		{
			UObject* Object = ObjectId.ResolveObject();
			UTEST_TRUE("Object located back after full load", Object != nullptr);
		
			auto Listener = CastChecked<IPersistentStateCallbackListener>(Object);
			UTEST_TRUE("Load callbacks executed for full world reload", Listener->bPreLoadStateCalled && Listener->bPostLoadStateCalled && Listener->bCustomStateLoaded);

			Listener->Reset();
		}
	}
	
	return !HasAnyErrors();
}

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(
	FPersistentStateTest_Attachment, FPersistentStateAutoTest,
	"PersistentState.ComponentAttachment", AutomationFlags
)

void FPersistentStateTest_Attachment::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("Default"));
	OutBeautifiedNames.Add(TEXT("World Partition"));
	OutTestCommands.Add(TEXT("/PersistentState/PersistentStateTestMap_Default"));
	OutTestCommands.Add(TEXT("/PersistentState/PersistentStateTestMap_WP"));
}

bool FPersistentStateTest_Attachment::RunTest(const FString& Parameters)
{
	FPersistentStateAutoTest::RunTest(Parameters);

	const FString SlotName{TEXT("TestSlot")};
	Initialize(Parameters, {SlotName});
	ON_SCOPE_EXIT { Cleanup(); };

	APersistentStateTestActor* Parent = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StaticActor1"));
	APersistentStateTestActor* Child = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StaticActor2"));
	UTEST_TRUE("Found static actors", Parent && Child);
	
	APersistentStateTestActor* DynamicParent = ScopedWorld->SpawnActor<APersistentStateTestActor>();
	FPersistentStateObjectId DynamicParentId = FPersistentStateObjectId::FindObjectId(DynamicParent);
	
	APersistentStateTestActor* DynamicChild = ScopedWorld->SpawnActor<APersistentStateTestActor>();
	FPersistentStateObjectId DynamicChildId = FPersistentStateObjectId::FindObjectId(DynamicChild);
	UTEST_TRUE("Created dynamic actors", DynamicParent && DynamicChild);
	UTEST_TRUE("Created dynamic actor ids", DynamicParentId.IsValid() && DynamicChildId.IsValid());
	
	ExpectedSlot = StateSubsystem->FindSaveGameSlotByName(FName{SlotName});
	UTEST_TRUE("Found slot", ExpectedSlot.IsValid());

	Child->AttachToActor(Parent, FAttachmentTransformRules::KeepWorldTransform);
	DynamicParent->AttachToActor(Child, FAttachmentTransformRules::KeepWorldTransform);
	DynamicChild->AttachToActor(DynamicParent, FAttachmentTransformRules::KeepWorldTransform);
	UTEST_TRUE("Attach parents are valid", Child->GetAttachParentActor() == Parent && DynamicParent->GetAttachParentActor() == Child && DynamicChild->GetAttachParentActor() == DynamicParent);

	StateSubsystem->SaveGameToSlot(ExpectedSlot);
	StateSubsystem->Tick(1.f);
	
	// add travel option to override game mode for the loaded map. Otherwise it will load default game mode which will not match the current one
	const FString TravelOptions = TEXT("GAME=") + FSoftClassPath{ScopedWorld->GetGameMode()->GetClass()}.ToString();
	StateSubsystem->LoadGameFromSlot(ExpectedSlot, TravelOptions);
	StateSubsystem->Tick(1.f);
	
	ScopedWorld->FinishWorldTravel();

	Parent = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StaticActor1"));
	Child = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StaticActor2"));
	UTEST_TRUE("Found static actors", Parent && Child);
	
	DynamicParent = DynamicParentId.ResolveObject<APersistentStateTestActor>();
	DynamicChild = DynamicChildId.ResolveObject<APersistentStateTestActor>();
	UTEST_TRUE("Created dynamic actors", DynamicParent && DynamicChild);
	
	UTEST_TRUE("Attachment parent is restored", Child->GetAttachParentActor() == Parent);
	UTEST_TRUE("Attachment parent is restored", DynamicParent->GetAttachParentActor() == Child);
	UTEST_TRUE("Attachment parent is restored", DynamicChild->GetAttachParentActor() == DynamicParent);
	
	return !HasAnyErrors();
}

IMPLEMENT_CUSTOM_COMPLEX_AUTOMATION_TEST(
	FPersistentStateTest_ObjectState, FPersistentStateAutoTest,
	"PersistentState.ObjectState", AutomationFlags
)

void FPersistentStateTest_ObjectState::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("Default"));
	OutBeautifiedNames.Add(TEXT("World Partition"));
	OutTestCommands.Add(TEXT("/PersistentState/PersistentStateTestMap_Default"));
	OutTestCommands.Add(TEXT("/PersistentState/PersistentStateTestMap_WP"));
}

bool FPersistentStateTest_ObjectState::RunTest(const FString& Parameters)
{
	FPersistentStateAutoTest::RunTest(Parameters);

	const FString SlotName{TEXT("TestSlot")};
	Initialize(Parameters, {SlotName});
	ON_SCOPE_EXIT { Cleanup(); };
	
	APersistentStateTestActor* StaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StaticActor1"));
	FPersistentStateObjectId StaticId = FPersistentStateObjectId::FindObjectId(StaticActor);
	APersistentStateTestActor* OtherStaticActor = ScopedWorld->FindActorByTag<APersistentStateTestActor>(TEXT("StaticActor2"));
	FPersistentStateObjectId OtherStaticId = FPersistentStateObjectId::FindObjectId(OtherStaticActor);
	UTEST_TRUE("Found static actor handles", StaticActor != nullptr && OtherStaticActor != nullptr && StaticId.IsValid() && OtherStaticId.IsValid() && StaticId != OtherStaticId);
	
	APersistentStateTestActor* DynamicActor = ScopedWorld->SpawnActor<APersistentStateTestActor>();
	FPersistentStateObjectId DynamicId = FPersistentStateObjectId::FindObjectId(DynamicActor);
	
	APersistentStateTestActor* OtherDynamicActor = ScopedWorld->SpawnActor<APersistentStateTestActor>();
	FPersistentStateObjectId OtherDynamicId = FPersistentStateObjectId::FindObjectId(OtherDynamicActor);
	UTEST_TRUE("Found dynamic actor handles", DynamicId.IsValid() && OtherDynamicId.IsValid() && DynamicId != OtherDynamicId);

	UPersistentStateTestGameSubsystem* GameSubsystem = ScopedWorld->GetSubsystem<UPersistentStateTestGameSubsystem>();
	UPersistentStateTestWorldSubsystem* WorldSubsystem = ScopedWorld->GetSubsystem<UPersistentStateTestWorldSubsystem>();
	FPersistentStateObjectId GameSubsystemId = FPersistentStateObjectId::FindObjectId(GameSubsystem);
	FPersistentStateObjectId WorldSubsystemId = FPersistentStateObjectId::FindObjectId(WorldSubsystem);
	UTEST_TRUE("Found subsystem handles", GameSubsystemId.IsValid() && WorldSubsystemId.IsValid());

	auto Init = [this](auto Target, APersistentStateTestActor* Static, APersistentStateTestActor* Dynamic, FName Name, int32 Index)
	{
		Target->StoredInt = Index;
		Target->StoredName = Name;
		Target->StoredString = Name.ToString();
		Target->StoredStaticActor = Static;
		Target->StoredDynamicActor = Dynamic;
		Target->StoredStaticComponent = Static->StaticComponent;
		Target->StoredDynamicComponent = Dynamic->DynamicComponent;
		Target->SetInstanceName(Name);
	};
	
	auto InitTarget = [this, Init](APersistentStateTestActor* Target, APersistentStateTestActor* Static, APersistentStateTestActor* Dynamic, FName Name, int32 Index)
	{
		Init(Target, Static, Dynamic, Name, Index);
		Init(Target->StaticComponent, Static, Dynamic, Name, Index);
		Init(Target->DynamicComponent, Static, Dynamic, Name, Index);
	};

	auto Verify = [this](auto Target, APersistentStateTestActor* Static, APersistentStateTestActor* Dynamic, FName Name, int32 Index)
	{
		UTEST_TRUE(FString::Printf(TEXT("%s: Index matches %d"), *GetNameSafe(Target), Index), Target->StoredInt == Index);
		UTEST_TRUE(FString::Printf(TEXT("%s: String matches %s"), *GetNameSafe(Target), *Name.ToString()), Target->StoredString == Name.ToString());
		UTEST_TRUE(FString::Printf(TEXT("%s: String matches %s"), *GetNameSafe(Target), *Name.ToString()), Target->StoredName == Name);
		UTEST_TRUE(FString::Printf(TEXT("%s: Custom state matches %s"), *GetNameSafe(Target), *Name.ToString()), Target->GetInstanceName() == Name);
		UTEST_TRUE("Actor references match", Target->StoredStaticActor == Static && Target->StoredDynamicActor == Dynamic);
		UTEST_TRUE("Component references match", Target->StoredStaticComponent == Static->StaticComponent && Target->StoredDynamicComponent == Dynamic->DynamicComponent);
		return true;
	};

	auto VerifyActor = [this, Verify](APersistentStateTestActor* Target, APersistentStateTestActor* Static, APersistentStateTestActor* Dynamic, FName Name, int32 Index)
	{
		UTEST_TRUE("Has dynamic component reference", IsValid(Target->DynamicComponent) && Target->DynamicComponent->GetOwner() == Target);
		
		Verify(Target, Static, Dynamic, Name, Index);
		Verify(Target->StaticComponent, Static, Dynamic, Name, Index);
		Verify(Target->DynamicComponent, Static, Dynamic, Name, Index);

		return true;
	};

	StaticActor->DynamicComponent = UE::Automation::CreateActorComponent<UPersistentStateTestComponent>(*ScopedWorld, StaticActor);
	OtherStaticActor->DynamicComponent = UE::Automation::CreateActorComponent<UPersistentStateTestComponent>(*ScopedWorld, OtherStaticActor);
	DynamicActor->DynamicComponent = UE::Automation::CreateActorComponent<UPersistentStateTestComponent>(*ScopedWorld, DynamicActor);
	OtherDynamicActor->DynamicComponent = UE::Automation::CreateActorComponent<UPersistentStateTestComponent>(*ScopedWorld, OtherDynamicActor);
	
	InitTarget(StaticActor, OtherStaticActor, DynamicActor, TEXT("StaticActor"), 1);
	InitTarget(OtherStaticActor, StaticActor, DynamicActor, TEXT("OtherStaticActor"), 2);
	InitTarget(DynamicActor, StaticActor, OtherDynamicActor, TEXT("DynamicActor"), 3);
	InitTarget(OtherDynamicActor, StaticActor, DynamicActor, TEXT("OtherDynamicActor"), 4);

	Init(GameSubsystem, StaticActor, DynamicActor, TEXT("GameSubsystem"), 5);
	Init(WorldSubsystem, StaticActor, DynamicActor, TEXT("WorldSubsystem"), 6);
	
	ExpectedSlot = StateSubsystem->FindSaveGameSlotByName(FName{SlotName});
	UTEST_TRUE("Found slot", ExpectedSlot.IsValid());
	
	StateSubsystem->SaveGameToSlot(ExpectedSlot);
	StateSubsystem->Tick(1.f);
	
	// add travel option to override game mode for the loaded map. Otherwise it will load default game mode which will not match the current one
	const FString TravelOptions = TEXT("GAME=") + FSoftClassPath{ScopedWorld->GetGameMode()->GetClass()}.ToString();
	StateSubsystem->LoadGameFromSlot(ExpectedSlot, TravelOptions);
	StateSubsystem->Tick(1.f);
	
	ScopedWorld->FinishWorldTravel();

	StaticActor = StaticId.ResolveObject<APersistentStateTestActor>();
	OtherStaticActor = OtherStaticId.ResolveObject<APersistentStateTestActor>();
	UTEST_TRUE("Found static actors", StaticActor && OtherStaticActor);
	DynamicActor = DynamicId.ResolveObject<APersistentStateTestActor>();
	OtherDynamicActor = OtherDynamicId.ResolveObject<APersistentStateTestActor>();
	UTEST_TRUE("Found dynamic actors", DynamicActor && OtherDynamicActor);
	GameSubsystem = GameSubsystemId.ResolveObject<UPersistentStateTestGameSubsystem>();
	WorldSubsystem = WorldSubsystemId.ResolveObject<UPersistentStateTestWorldSubsystem>();
	UTEST_TRUE("Found subsystems", GameSubsystem && WorldSubsystem);
	
	UTEST_TRUE("Restored references for StaticActor", VerifyActor(StaticActor, OtherStaticActor, DynamicActor, TEXT("StaticActor"), 1));
	UTEST_TRUE("Restored references for OtherStaticActor", VerifyActor(OtherStaticActor, StaticActor, DynamicActor, TEXT("OtherStaticActor"), 2));
	UTEST_TRUE("Restored references for DynamicActor", VerifyActor(DynamicActor, StaticActor, OtherDynamicActor, TEXT("DynamicActor"), 3));
	UTEST_TRUE("Restored references for OtherDynamicActor", VerifyActor(OtherDynamicActor, StaticActor, DynamicActor, TEXT("OtherDynamicActor"), 4));
	UTEST_TRUE("Restored references for GameSubsystem", Verify(GameSubsystem, StaticActor, DynamicActor, TEXT("GameSubsystem"), 5));
	UTEST_TRUE("Restored references for WorldSubsystem", Verify(WorldSubsystem, StaticActor, DynamicActor, TEXT("WorldSubsystem"), 6));
	
	Cleanup();
	// Collect garbage is required to remove old world objects from the engine entirely.
	// Otherwise, there's going to be stable ID collision between new and previous world objects, because package remapping is a thing
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	Initialize(Parameters, {SlotName}, APersistentStateGameMode::StaticClass(), SlotName);

	StaticActor = StaticId.ResolveObject<APersistentStateTestActor>();
	OtherStaticActor = OtherStaticId.ResolveObject<APersistentStateTestActor>();
	UTEST_TRUE("Found static actors", StaticActor && OtherStaticActor);
	DynamicActor = DynamicId.ResolveObject<APersistentStateTestActor>();
	OtherDynamicActor = OtherDynamicId.ResolveObject<APersistentStateTestActor>();
	UTEST_TRUE("Found dynamic actors", DynamicActor && OtherDynamicActor);
	GameSubsystem = GameSubsystemId.ResolveObject<UPersistentStateTestGameSubsystem>();
	WorldSubsystem = WorldSubsystemId.ResolveObject<UPersistentStateTestWorldSubsystem>();
	UTEST_TRUE("Found subsystems", GameSubsystem && WorldSubsystem);

	UTEST_TRUE("Restored references for StaticActor", VerifyActor(StaticActor, OtherStaticActor, DynamicActor, TEXT("StaticActor"), 1));
	UTEST_TRUE("Restored references for OtherStaticActor", VerifyActor(OtherStaticActor, StaticActor, DynamicActor, TEXT("OtherStaticActor"), 2));
	UTEST_TRUE("Restored references for DynamicActor", VerifyActor(DynamicActor, StaticActor, OtherDynamicActor, TEXT("DynamicActor"), 3));
	UTEST_TRUE("Restored references for OtherDynamicActor", VerifyActor(OtherDynamicActor, StaticActor, DynamicActor, TEXT("OtherDynamicActor"), 4));
	UTEST_TRUE("Restored references for GameSubsystem", Verify(GameSubsystem, StaticActor, DynamicActor, TEXT("GameSubsystem"), 5));
	UTEST_TRUE("Restored references for WorldSubsystem", Verify(WorldSubsystem, StaticActor, DynamicActor, TEXT("WorldSubsystem"), 6));
	
	return !HasAnyErrors();
}

UE_ENABLE_OPTIMIZATION
