
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

	virtual bool RunTest(const FString& Parameters) override
	{
		UPersistentStateSettings* Settings = UPersistentStateSettings::GetMutable();
		OriginalSettings = DuplicateObject<UPersistentStateSettings>(Settings, nullptr);
		OriginalSettings->AddToRoot();
		
		Settings->SaveGamePath = TEXT("TestSaveGame");

		// clean up test save directory
		IFileManager::Get().DeleteDirectory(*UPersistentStateSettings::Get()->GetSaveGamePath(), false);

		Storage = NewObject<UPersistentStateSlotStorage>();
		Storage->AddToRoot();
		Storage->Init();

		return true;
	}

	virtual void Cleanup()
	{
		// clean up test save directory
		IFileManager::Get().DeleteDirectory(*UPersistentStateSettings::Get()->GetSaveGamePath(), false);

		UPersistentStateSettings* Settings = UPersistentStateSettings::GetMutable();
		Settings->SaveGamePath = OriginalSettings->SaveGamePath;
		
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

IMPLEMENT_CUSTOM_SIMPLE_AUTOMATION_TEST(FPersistentStateTest_StateSlots, FPersistentStateStorageAutoTest, "PersistentState.Storage.Slots", AutomationFlags)

bool FPersistentStateTest_StateSlots::RunTest(const FString& Parameters)
{
	FPersistentStateStorageAutoTest::RunTest(Parameters);
	ON_SCOPE_EXIT { Cleanup(); };

	return !HasAnyErrors();
}
