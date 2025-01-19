#include "PersistentStateAutomationTest.h"

bool FPersistentStateAutoTest::RunTest(const FString& Parameters)
{
	using namespace UE::PersistentState;
	PrevWorldState = CurrentWorldState = nullptr;
	ExpectedSlot = {};
		
	return true;
}

void FPersistentStateAutoTest::Cleanup()
{
	WorldPath.Reset();
	ScopedWorld.Reset();
	StateSubsystem = nullptr;

	UPersistentStateSettings* Settings = UPersistentStateSettings::GetMutable();
	Settings->bEnabled = OriginalSettings->bEnabled;
	Settings->DefaultNamedSlots = OriginalSettings->DefaultNamedSlots;
	Settings->StateStorageClass = OriginalSettings->StateStorageClass;
		
	OriginalSettings->RemoveFromRoot();
	OriginalSettings->MarkAsGarbage();
	OriginalSettings = nullptr;
}
