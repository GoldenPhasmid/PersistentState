
#include "PersistentStateTestClasses.h"
#include "PersistentStateSettings.h"

using namespace UE::PersistentState;

constexpr int32 AutomationFlags = EAutomationTestFlags::CriticalPriority | EAutomationTestFlags::ProductFilter | EAutomationTestFlags::ApplicationContextMask;

class FPersistentStateStorageAutoTest: public FAutomationTestBase
{
public:
	FPersistentStateStorageAutoTest(const FString& InName, const bool bInComplexTask)
	: FAutomationTestBase(InName, bInComplexTask)
	{}

	virtual bool RunTest(const FString& Parameters) override { return true; }

	virtual bool Initialize(const TArray<FName>& SlotNames)
	{
		UPersistentStateSettings* Settings = UPersistentStateSettings::GetMutable();
		OriginalSettings = DuplicateObject<UPersistentStateSettings>(Settings, nullptr);
		OriginalSettings->AddToRoot();

		// override default slots with test slot names
		TArray<FPersistentSlotEntry> PersistentSlots;
		for (const FName& SlotName: SlotNames)
		{
			PersistentSlots.Add(FPersistentSlotEntry{SlotName, FText::FromName(SlotName)});
		}

		Settings->PersistentSlots = PersistentSlots;
		Settings->SaveGamePath = TEXT("TestSaveGames");

		// clean up test save directory
		IFileManager::Get().DeleteDirectory(*Settings->GetSaveGamePath(), false);

		Storage = NewObject<UPersistentStateSlotStorage>();
		Storage->AddToRoot();
		Storage->Init();

		return true;
	}

	virtual void Cleanup()
	{
		// clean up test save directory
		UPersistentStateSettings* Settings = UPersistentStateSettings::GetMutable();
		IFileManager::Get().DeleteDirectory(*Settings->GetSaveGamePath(), false);
		
		Settings->SaveGamePath = OriginalSettings->SaveGamePath;
		Settings->PersistentSlots = OriginalSettings->PersistentSlots;
		
		OriginalSettings->RemoveFromRoot();
		OriginalSettings->MarkAsGarbage();
		OriginalSettings = nullptr;

		Storage->Shutdown();
		Storage->RemoveFromRoot();
		Storage->MarkAsGarbage();
		Storage = nullptr;
	}

protected:
	UPersistentStateSettings* OriginalSettings = nullptr;
	UPersistentStateStorage* Storage = nullptr;
};

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPersistentStateTest_StateSlots, FPersistentStateStorageAutoTest, "PersistentState.StateSlotStorage", AutomationFlags)

bool FPersistentStateTest_StateSlots::RunTest(const FString& Parameters)
{
	FPersistentStateStorageAutoTest::RunTest(Parameters);

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
	auto NewSlotHandle = Storage->CreateStateSlot(NewTestSlot, FText::FromName(NewTestSlot));
	UTEST_TRUE("Successfully added new slot", NewSlotHandle.IsValid() && Storage->GetStateSlotByName(NewTestSlot) == NewSlotHandle);

	Storage->GetAvailableStateSlots(AvailableSlots, false);
	UTEST_TRUE("Storage has 3 available slots", AvailableSlots.Num() == 3);

	Storage->GetAvailableStateSlots(AvailableSlots, true);
	UTEST_TRUE("Storage has 1 available slots on disk", AvailableSlots.Num() == 1);

	UTEST_TRUE("CanLoadFromWorldState requires slot on disk",
		!Storage->CanLoadFromStateSlot(SlotHandle) && !Storage->CanLoadFromStateSlot(OtherSlotHandle) && Storage->CanLoadFromStateSlot(NewSlotHandle));

	UTEST_TRUE("CanSaveFromStateSlot requires slot on disk or a persistent slot",
		Storage->CanSaveToStateSlot(SlotHandle) && Storage->CanSaveToStateSlot(OtherSlotHandle) && Storage->CanSaveToStateSlot(NewSlotHandle));

	UTEST_TRUE("Initial world is empty",
		Storage->GetWorldFromStateSlot(SlotHandle) == NAME_None && Storage->GetWorldFromStateSlot(OtherSlotHandle) == NAME_None && Storage->GetWorldFromStateSlot(NewSlotHandle) == NAME_None);

	
	/** saving/loading world state to slots */
	auto CreateWorldState = [](FName WorldName)
	{
		auto WorldState = MakeShared<UE::PersistentState::FWorldState>(WorldName);
		WorldState->Header.InitializeToEmpty();
		WorldState->Header.WorldPackageName = TEXT("/Temp");
		
		return WorldState;
	};

	const FName World{TEXT("TestWorld")};
	const FName OtherWorld{TEXT("OtherTestWorld")};
	const FName LastWorld{TEXT("LastWorld")};
	
	UTEST_TRUE("LoadWorldState failed for all slots", !Storage->LoadWorldState(SlotHandle, World).IsValid() && !Storage->LoadWorldState(OtherSlotHandle, World).IsValid() && !Storage->LoadWorldState(NewSlotHandle, World).IsValid());

	Storage->SaveWorldState(CreateWorldState(World), SlotHandle, SlotHandle);
	Storage->GetAvailableStateSlots(AvailableSlots, true);
	UTEST_TRUE("Slot1 contains data from World1", Storage->LoadWorldState(SlotHandle, World).IsValid() && AvailableSlots.Contains(SlotHandle));
	
	Storage->SaveWorldState(CreateWorldState(OtherWorld), SlotHandle, OtherSlotHandle);
	Storage->GetAvailableStateSlots(AvailableSlots, true);
	UTEST_TRUE("Slot1 still contains data from World1", Storage->LoadWorldState(SlotHandle, World).IsValid());
	UTEST_TRUE("Slot2 contains data from World2", Storage->LoadWorldState(OtherSlotHandle, OtherWorld).IsValid() && AvailableSlots.Contains(OtherSlotHandle));
	
	Storage->SaveWorldState(CreateWorldState(LastWorld), OtherSlotHandle, NewSlotHandle);
	Storage->GetAvailableStateSlots(AvailableSlots, true);
	UTEST_TRUE("Slot3 contains data from World3", Storage->LoadWorldState(NewSlotHandle, LastWorld).IsValid() && AvailableSlots.Contains(NewSlotHandle));

	/** world data is transferred between slots */
	// @todo: not implemented yet
#if 0
	UTEST_TRUE("Slot2 contains data from World1", Storage->LoadWorldState(OtherSlotHandle, World).IsValid());
	UTEST_TRUE("Slot3 contains data from World1", Storage->LoadWorldState(NewSlotHandle, World).IsValid());
	UTEST_TRUE("Slot3 contains data from World2", Storage->LoadWorldState(NewSlotHandle, OtherWorld).IsValid());
#endif

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
