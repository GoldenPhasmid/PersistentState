#pragma once

#include "AutomationWorld.h"
#include "PersistentStateSettings.h"
#include "PersistentStateSubsystem.h"
#include "PersistentStateTestClasses.h"

class FPersistentStateAutoTest: public FAutomationTestBase
{
public:
	FPersistentStateAutoTest(const FString& InName, const bool bInComplexTask)
		: FAutomationTestBase(InName, bInComplexTask)
	{}
	
	virtual bool RunTest(const FString& Parameters) override;
	
	void Initialize(
		const FString& WorldPackage, const TArray<FString>& SlotNames, TSubclassOf<AGameModeBase> GameModeClass = nullptr,
		FString StartupSlotName = TEXT(""), TFunction<void(UWorld*)> InitWorldCallback = {}
	);

	virtual void Cleanup();

protected:
	virtual void InitializeImpl(const FString& Parameters) {}

	FSoftObjectPath WorldPath;
	FAutomationWorldPtr ScopedWorld;
	UPersistentStateSettings* OriginalSettings = nullptr;
	UPersistentStateSubsystem* StateSubsystem = nullptr;
};