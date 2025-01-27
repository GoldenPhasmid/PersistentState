#include "PersistentStateAutomationTest.h"

#include "PersistentStateObjectId.h"

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

	// World partition cleanup requires manual trigger for garbage collection so that behavior is "identical" to the
	// worlds that don't use garbage collection
	// Otherwise GC is not triggered between tests, and @GuidAnnotation becomes polluted with object created by a
	// previous test, which in turn causes ID collision if tests use the same map (which they do)
	// specifically, GLevelStreamingForceGCAfterLevelStreamedOut is 0 for WP and 1 for default worlds, so
	// UWorld::UpdateLevelStreaming during world initialization with WP will not trigger GC
	// @see UWorld.cpp 4374
	GEngine->ForceGarbageCollection(true);
}
