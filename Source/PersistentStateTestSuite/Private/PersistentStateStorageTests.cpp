
#include "AutomationWorld.h"
#include "PersistentStateSerialization.h"
#include "PersistentStateTestClasses.h"
#include "PersistentStateSettings.h"
#include "PersistentStateStatics.h"
#include "PersistentStateSubsystem.h"

using namespace UE::PersistentState;

class FPersistentStateStorageTestBase: public FAutomationTestBase
{
public:
	FPersistentStateStorageTestBase(const FString& InName, const bool bInComplexTask)
	: FAutomationTestBase(InName, bInComplexTask)
	{}

	virtual bool RunTest(const FString& Parameters) override { return true; }

	virtual bool Initialize(const TArray<FName>& SlotNames, bool bDeleteSaveGames = true, bool bForceGameThread = true)
	{
		ScopedWorld = FAutomationWorld::CreateGameWorld();
		
		UPersistentStateSettings* Settings = UPersistentStateSettings::GetMutable();
		OriginalSettings = DuplicateObject<UPersistentStateSettings>(Settings, nullptr);
		OriginalSettings->AddToRoot();

		// override default slots with test slot names
		TArray<FPersistentStateDefaultNamedSlot> DefaultSlots;
		for (FName SlotName: SlotNames)
		{
			DefaultSlots.Add(FPersistentStateDefaultNamedSlot{SlotName, FText::FromName(SlotName), Settings->DefaultSlotDescriptor});
		}

		Settings->DefaultNamedSlots = DefaultSlots;
		Settings->SaveGameDirectory = TEXT("TestSaveGames");
		Settings->bForceGameThread = bForceGameThread;

		if (bDeleteSaveGames)
		{
			// clean up test save directory
			UE::PersistentState::ResetSaveGames(Settings->GetSaveGamePath(), Settings->SaveGameExtension);
		}

		Storage = NewObject<UPersistentStateSlotMockStorage>(*ScopedWorld);
		Storage->AddToRoot();
		Storage->Init();

		return true;
	}

	virtual void Cleanup(bool bDeleteSaveGames = true)
	{
		UPersistentStateSettings* Settings = UPersistentStateSettings::GetMutable();
		if (bDeleteSaveGames)
		{
			// clean up test save directory
			UE::PersistentState::ResetSaveGames(Settings->GetSaveGamePath(), Settings->SaveGameExtension);
		}
		
		GEngine->CopyPropertiesForUnrelatedObjects(OriginalSettings, Settings);
		
		OriginalSettings->RemoveFromRoot();
		OriginalSettings->MarkAsGarbage();
		OriginalSettings = nullptr;

		Storage->Shutdown();
		Storage->RemoveFromRoot();
		Storage->MarkAsGarbage();
		Storage = nullptr;
		
		ScopedWorld.Reset();
	}

protected:
	UPersistentStateSettings* OriginalSettings = nullptr;
	UPersistentStateSlotMockStorage* Storage = nullptr;
	FAutomationWorldPtr ScopedWorld;
};


IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPersistentStateTest_StateSlotSerialization, FPersistentStateStorageTestBase, "PersistentState.StateSlotSerialization", AutomationFlags)

bool FPersistentStateTest_StateSlotSerialization::RunTest(const FString& Parameters)
{
	FPersistentStateStorageTestBase::RunTest(Parameters);

	const FName TestSlot1{TEXT("TestSlot1")};
	const FName TestSlot2{TEXT("TestSlot2")};
	Initialize({TestSlot1, TestSlot2});
	ON_SCOPE_EXIT { Cleanup(); };

	const FName NewTestSlot{TEXT("NewTestSlot")};
	Storage->CreateStateSlot(NewTestSlot, FText::FromName(NewTestSlot), nullptr);
	FPersistentStateSlotSharedRef StateSlot = Storage->GetSlotUnsafe(NewTestSlot);

	auto SerializeSlot = [this, StateSlot](bool bBinary, bool bDelta)
	{
		TArray<uint8> Buffer;
		UScriptStruct* Struct = FPersistentStateSlot::StaticStruct();
		FPersistentStateSlot DefaultSlot;
		FPersistentStateSlot* Defaults = bDelta ? &DefaultSlot : nullptr;

		{
			FPersistentStateMemoryWriter Writer{Buffer, true};
			Writer.SetWantBinaryPropertySerialization(bBinary);

			FPersistentStateSaveGameArchive SaveGameArchive{Writer};
			Struct->SerializeItem(SaveGameArchive, StateSlot.Get(), Defaults);
		}

		FPersistentStateSlot LoadedSlot;

		{
			FPersistentStateMemoryReader Reader{Buffer, true};
			Reader.SetWantBinaryPropertySerialization(bBinary);
			
			FPersistentStateSaveGameArchive SaveGameArchive{Reader};
			Struct->SerializeItem(SaveGameArchive, &LoadedSlot, Defaults);
		}
		
		UTEST_TRUE("StateSlot is identical", (*StateSlot) == LoadedSlot);
		return !HasAnyErrors();
	};

	UTEST_TRUE("Serialize: text,	no delta",	SerializeSlot(false, false));
	UTEST_TRUE("Serialize: binary,	no delta",	SerializeSlot(true, false));
	UTEST_TRUE("Serialize: text,	delta",		SerializeSlot(false, true));
	UTEST_TRUE("Serialize: binary,	delta",		SerializeSlot(true, true));
	
	auto SerializeSlotStructured = [this, StateSlot](bool bBinary, bool bDelta)
	{
		TArray<uint8> Buffer;
		UScriptStruct* Struct = FPersistentStateSlot::StaticStruct();
		FPersistentStateSlot DefaultSlot;
		FPersistentStateSlot* Defaults = bDelta ? &DefaultSlot : nullptr;
		
		{
			FPersistentStateMemoryWriter Writer{Buffer, true};
			Writer.SetWantBinaryPropertySerialization(bBinary);

			FPersistentStateSaveGameArchive SaveGameArchive{Writer};
			FBinaryArchiveFormatter Formatter{SaveGameArchive};
			FStructuredArchive StructuredArchive{Formatter};
			FStructuredArchive::FRecord RootRecord = StructuredArchive.Open().EnterRecord();
			
			Struct->SerializeItem(RootRecord.EnterField(TEXT("StateSlot")), StateSlot.Get(), Defaults);
		}

		FPersistentStateSlot LoadedSlot;
		{
			FPersistentStateMemoryReader Reader{Buffer, true};
			Reader.SetWantBinaryPropertySerialization(bBinary);

			FPersistentStateSaveGameArchive SaveGameArchive{Reader};
			FBinaryArchiveFormatter Formatter{SaveGameArchive};
			FStructuredArchive StructuredArchive{Formatter};
			FStructuredArchive::FRecord RootRecord = StructuredArchive.Open().EnterRecord();
			
			Struct->SerializeItem(RootRecord.EnterField(TEXT("StateSlot")), &LoadedSlot, Defaults);
		}

		UTEST_TRUE("StateSlot is identical", (*StateSlot) == LoadedSlot);
		return !HasAnyErrors();
	};

	UTEST_TRUE("Serialize Structured: text,		no delta", SerializeSlotStructured(false, false));
	UTEST_TRUE("Serialize Structured: binary,	no delta", SerializeSlotStructured(true, false));
	UTEST_TRUE("Serialize Structured: text,		delta", SerializeSlotStructured(false, true));
	UTEST_TRUE("Serialize Structured: binary,	delta", SerializeSlotStructured(true, true));

	return !HasAnyErrors();
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPersistentStateTest_StateSlotOperations, FPersistentStateStorageTestBase, "PersistentState.StateSlotOperations", AutomationFlags)

bool FPersistentStateTest_StateSlotOperations::RunTest(const FString& Parameters)
{
	FPersistentStateStorageTestBase::RunTest(Parameters);
	
	constexpr bool bDeleteSaveGames = true, bForceGameThread = false;
	Initialize({}, bDeleteSaveGames, bForceGameThread);
	ON_SCOPE_EXIT { Cleanup(); };

	Storage->WaitUntilTasksComplete();
	Storage->UpdateAvailableStateSlots({});

	const FName NewTestSlot{TEXT("NewTestSlot")};
	auto RuntimeSlotHandle = Storage->CreateStateSlot(NewTestSlot, FText::FromName(NewTestSlot), nullptr);
	Storage->WaitUntilTasksComplete();
	
	TArray<FPersistentStateSlotHandle> AvailableSlots;
	Storage->GetAvailableStateSlots(AvailableSlots, false);
	UTEST_TRUE("Storage has 1 available slot on disk", AvailableSlots.Num() == 1);
	UTEST_TRUE("Runtime create state slot exists", AvailableSlots.Contains(RuntimeSlotHandle));
	
	Storage->GetAvailableStateSlots(AvailableSlots, true);
	UTEST_TRUE("Storage has 1 available slot on disk", AvailableSlots.Num() == 1);
	UTEST_TRUE("Runtime create state slot exists", AvailableSlots.Contains(RuntimeSlotHandle));

	Storage->UpdateAvailableStateSlots({});
	Storage->RemoveStateSlot(RuntimeSlotHandle);
	Storage->WaitUntilTasksComplete();

	Storage->GetAvailableStateSlots(AvailableSlots, false);
	UTEST_TRUE("Storage has 0 available slots on disk", AvailableSlots.Num() == 0);
	UTEST_TRUE("Runtime create state slot is destroyed", !AvailableSlots.Contains(RuntimeSlotHandle));
	
	Storage->GetAvailableStateSlots(AvailableSlots, true);
	UTEST_TRUE("Storage has 0 available slots on disk", AvailableSlots.Num() == 0);
	UTEST_TRUE("Runtime create state slot is destroyed", !AvailableSlots.Contains(RuntimeSlotHandle));
	
	return !HasAnyErrors();
}

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPersistentStateTest_StateStorageAPI, FPersistentStateStorageTestBase, "PersistentState.StateStorageAPI", AutomationFlags)

bool FPersistentStateTest_StateStorageAPI::RunTest(const FString& Parameters)
{
	FPersistentStateStorageTestBase::RunTest(Parameters);

	const FName TestSlot1{TEXT("TestSlot1")};
	const FName TestSlot2{TEXT("TestSlot2")};
	Initialize({TestSlot1, TestSlot2});
	ON_SCOPE_EXIT { Cleanup(); };
	
	/** storage initialization, queries for state slots, creating new state slots */
	auto SlotHandle = Storage->GetStateSlotByName(TestSlot1);
	auto OtherSlotHandle = Storage->GetStateSlotByName(TestSlot2);
	UTEST_TRUE("Found slots", SlotHandle.IsValid() && OtherSlotHandle.IsValid());

	TArray<FPersistentStateSlotHandle> AvailableSlots;
	Storage->GetAvailableStateSlots(AvailableSlots, false);

	UTEST_TRUE("Storage has 2 available slots", AvailableSlots.Num() == 2);
	UTEST_TRUE("Slot handles match", AvailableSlots.Contains(SlotHandle) && AvailableSlots.Contains(OtherSlotHandle));

	Storage->GetAvailableStateSlots(AvailableSlots, true);
	UTEST_TRUE("Storage has 0 available slots on disk", AvailableSlots.Num() == 0);

	const FName NewTestSlot{TEXT("NewTestSlot")};
	auto NewSlotHandle = Storage->CreateStateSlot(NewTestSlot, FText::FromName(NewTestSlot), nullptr);
	UTEST_TRUE("Successfully added new slot", NewSlotHandle.IsValid() && Storage->GetStateSlotByName(NewTestSlot) == NewSlotHandle);

	Storage->GetAvailableStateSlots(AvailableSlots, false);
	UTEST_TRUE("Storage has 3 available slots", AvailableSlots.Num() == 3);

	Storage->GetAvailableStateSlots(AvailableSlots, true);
	UTEST_TRUE("Storage has 1 available slots on disk", AvailableSlots.Num() == 1);

	constexpr bool bDeleteSaveGames = false;
	Cleanup(bDeleteSaveGames);
	Initialize({TestSlot1, TestSlot2}, bDeleteSaveGames);

	// new storage object has named slots and runtime created slot
	Storage->GetAvailableStateSlots(AvailableSlots, false);
	UTEST_TRUE("Storage has 3 available slots", AvailableSlots.Num() == 3);

	Storage->GetAvailableStateSlots(AvailableSlots, true);
	UTEST_TRUE("Storage has 1 available slots on disk", AvailableSlots.Num() == 1);

	/** saving/loading world state to slots */
	auto CreateWorldState = [](FName WorldName)
	{
		FWorldStateSharedRef WorldState = MakeShared<FWorldState>(FWorldState::CreateSaveState());
		WorldState->Header.World = WorldName.ToString();
		WorldState->Header.WorldPackage = TEXT("/Temp");
		
		return WorldState;
	};
	
	const FName World{TEXT("TestWorld")};
	const FName OtherWorld{TEXT("OtherTestWorld")};
	const FName LastWorld{TEXT("LastWorld")};

	UTEST_TRUE("CanLoadFromWorldState requires slot on disk",
	!Storage->CanLoadFromStateSlot(SlotHandle, World) && !Storage->CanLoadFromStateSlot(OtherSlotHandle, World) && !Storage->CanLoadFromStateSlot(NewSlotHandle, World));

	UTEST_TRUE("CanSaveFromStateSlot requires slot on disk or a persistent slot",
		Storage->CanSaveToStateSlot(SlotHandle, World) && Storage->CanSaveToStateSlot(OtherSlotHandle, World) && Storage->CanSaveToStateSlot(NewSlotHandle, World));

	FGameStateSharedRef LoadedGameState = nullptr;
	FWorldStateSharedRef LoadedWorldState = nullptr;
	FLoadCompletedDelegate LoadDelegate = FLoadCompletedDelegate::CreateLambda(
	[&LoadedWorldState, &LoadedGameState](FGameStateSharedRef InGameState, FWorldStateSharedRef InWorldState)
	{
		LoadedGameState = InGameState;
		LoadedWorldState = InWorldState;
	});

	{
		// expected 2 errors from default named slots, as they do not require associated file path before the first save
		AddExpectedError(TEXT("doesn't have associated file path."), EAutomationExpectedErrorFlags::MatchType::Contains, 2);
		Storage->LoadState(SlotHandle, World, LoadDelegate);
		UTEST_TRUE("LoadWorldState failed", !LoadedWorldState.IsValid());

		Storage->LoadState(OtherSlotHandle, OtherWorld, LoadDelegate);
		UTEST_TRUE("LoadWorldState failed", !LoadedWorldState.IsValid());

		Storage->LoadState(NewSlotHandle, LastWorld, LoadDelegate);
		UTEST_TRUE("LoadWorldState failed", !LoadedWorldState.IsValid());
	}

	FSaveCompletedDelegate SaveDelegate;
	FGameStateSharedRef DefaultGameState = MakeShared<FGameState>(FGameState::CreateSaveState());
	
	Storage->SaveState(DefaultGameState, CreateWorldState(World), SlotHandle, SlotHandle, SaveDelegate);
	Storage->GetAvailableStateSlots(AvailableSlots, true);

	Storage->LoadState(SlotHandle, World, LoadDelegate);
	UTEST_TRUE("Slot1 contains data from World1", LoadedWorldState.IsValid() && AvailableSlots.Contains(SlotHandle));
	LoadedWorldState.Reset();
	
	Storage->SaveState(DefaultGameState, CreateWorldState(OtherWorld), SlotHandle, OtherSlotHandle, SaveDelegate);
	Storage->LoadState(OtherSlotHandle, OtherWorld, LoadDelegate);
	UTEST_TRUE("Slot2 contains data from World2", LoadedWorldState.IsValid());
	LoadedWorldState.Reset();
	
	Storage->GetAvailableStateSlots(AvailableSlots, true);
	UTEST_TRUE("Slot2 contains data from World2", AvailableSlots.Contains(OtherSlotHandle));
	
	Storage->SaveState(DefaultGameState, CreateWorldState(LastWorld), OtherSlotHandle, NewSlotHandle, SaveDelegate);
	Storage->LoadState(NewSlotHandle, LastWorld, LoadDelegate);
	UTEST_TRUE("Slot3 contains data from World3", LoadedWorldState.IsValid());
	LoadedWorldState.Reset();
	
	Storage->GetAvailableStateSlots(AvailableSlots, true);
	UTEST_TRUE("Slot3 contains data from World3", AvailableSlots.Contains(NewSlotHandle));

	/** world data is transferred between slots */
	Storage->LoadState(OtherSlotHandle, World, LoadDelegate);
	UTEST_TRUE("Slot2 contains data from World1", LoadedWorldState.IsValid());
	LoadedWorldState.Reset();

	Storage->LoadState(NewSlotHandle, World, LoadDelegate);
	UTEST_TRUE("Slot3 contains data from World1", LoadedWorldState.IsValid());
	LoadedWorldState.Reset();

	Storage->LoadState(NewSlotHandle, OtherWorld, LoadDelegate);
	UTEST_TRUE("Slot3 contains data from World2", LoadedWorldState.IsValid());
	LoadedWorldState.Reset();

	/** removing state slot and associated data on disk */
	Storage->RemoveStateSlot(NewSlotHandle);
	Storage->GetAvailableStateSlots(AvailableSlots, true);
	UTEST_TRUE("Slot3 data is removed from disk", !AvailableSlots.Contains(NewSlotHandle));
	Storage->GetAvailableStateSlots(AvailableSlots, false);
	UTEST_TRUE("Slot3 is removed", !AvailableSlots.Contains(NewSlotHandle));

	Storage->RemoveStateSlot(OtherSlotHandle);
	Storage->GetAvailableStateSlots(AvailableSlots, true);
	UTEST_TRUE("Slot2 data is removed from disk", !AvailableSlots.Contains(OtherSlotHandle));
	Storage->GetAvailableStateSlots(AvailableSlots, false);
	UTEST_TRUE("Slot2 is not removed, because it is persistent", AvailableSlots.Contains(OtherSlotHandle));

	Storage->RemoveStateSlot(SlotHandle);
	Storage->GetAvailableStateSlots(AvailableSlots, true);
	UTEST_TRUE("Zero slots on the disk", AvailableSlots.Num() == 0);
	
	return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPersistentStateTest_ActiveStateSlot, "PersistentState.ActiveStateSlot", AutomationFlags)

bool FPersistentStateTest_ActiveStateSlot::RunTest(const FString& Parameters)
{
	PrevWorldState = CurrentWorldState = nullptr;
	ExpectedSlot = {};

	auto Settings = UPersistentStateSettings::GetMutable();
	const FName StateSlot{TEXT("TestSlot")};
	const FPersistentStateDefaultNamedSlot NamedSlot{StateSlot, FText::FromName(StateSlot), Settings->DefaultSlotDescriptor};
	
	FGuardValue_Bitfield(Settings->bEnabled, true);
	TGuardValue Guard1{Settings->DefaultNamedSlots, TArray{NamedSlot}};
	TGuardValue Guard2{Settings->StateStorageClass, UPersistentStateFakeStorage::StaticClass()};

	auto InitParams = FAutomationWorldInitParams{EWorldType::Game, EWorldInitFlags::WithGameInstance}.EnableSubsystem<UPersistentStateSubsystem>();
	FAutomationWorldPtr ScopedWorld = InitParams.Create();

	UPersistentStateSubsystem* StateSubsystem = ScopedWorld->GetSubsystem<UPersistentStateSubsystem>();
	UTEST_TRUE("Current slot is empty", !StateSubsystem->GetActiveSaveGameSlot().IsValid());
	UTEST_TRUE("persistent slot is found", StateSubsystem->FindSaveGameSlotByName(StateSlot).IsValid());

	TArray<FPersistentStateSlotHandle> Slots;
	StateSubsystem->GetSaveGameSlots(Slots);

	UTEST_TRUE("one slot is available", Slots.Num() == 1 && Slots[0].GetSlotName() == StateSlot);
	
	const FName NewStateSlot{TEXT("NewTestSlot")};
	StateSubsystem->CreateSaveGameSlot(NewStateSlot, FText::FromName(NewStateSlot));
	
	StateSubsystem->GetSaveGameSlots(Slots);
	UTEST_TRUE("two slots are available", Slots.Num() == 2);

	auto PersistentSlotHandle = StateSubsystem->FindSaveGameSlotByName(StateSlot);
	auto NewSlotHandle = StateSubsystem->FindSaveGameSlotByName(NewStateSlot);
	UTEST_TRUE("Current slot is empty", !StateSubsystem->GetActiveSaveGameSlot().IsValid());
	UTEST_TRUE("persistent slot is found", PersistentSlotHandle.IsValid());
	UTEST_TRUE("new slot is found", NewSlotHandle.IsValid());

	UTEST_TRUE("SaveGame failed because no state slot is set", StateSubsystem->SaveGame() == false);
	ExpectedSlot = PersistentSlotHandle;
	UTEST_TRUE("SaveGame succeeded", StateSubsystem->SaveGameToSlot(PersistentSlotHandle) == true);
	StateSubsystem->Tick(1.f);
	UTEST_TRUE("Current slot is persistent slot", StateSubsystem->GetActiveSaveGameSlot() == PersistentSlotHandle);
	ExpectedSlot = NewSlotHandle;
	UTEST_TRUE("SaveGame succeeded", StateSubsystem->SaveGameToSlot(NewSlotHandle) == true);
	StateSubsystem->Tick(1.f);
	UTEST_TRUE("Current slot is new slot", StateSubsystem->GetActiveSaveGameSlot() == NewSlotHandle);
	
	ScopedWorld.Reset();
	TGuardValue Guard3{Settings->StartupSlotName, StateSlot};

	ExpectedSlot = PersistentSlotHandle;
	ScopedWorld = InitParams.Create();
	StateSubsystem = ScopedWorld->GetSubsystem<UPersistentStateSubsystem>();

	auto CurrentSlotHandle = StateSubsystem->GetActiveSaveGameSlot();
	UTEST_TRUE("Current slot is persistent slot", CurrentSlotHandle.IsValid() && CurrentSlotHandle == PersistentSlotHandle);
	
	return !HasAnyErrors();
}

