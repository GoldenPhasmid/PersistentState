#pragma once

#include "AutomationCommon.h"
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

	template <typename TGameMode = APersistentStateTestGameMode>
	void Initialize(const FString& WorldPackage, const TArray<FString>& SlotNames, FString StartupSlotName = TEXT(""), TFunction<void(UWorld*)> InitWorldCallback = {})
	{
		UPersistentStateSettings* Settings = UPersistentStateSettings::GetMutable();
		OriginalSettings = DuplicateObject<UPersistentStateSettings>(Settings, nullptr);
		OriginalSettings->AddToRoot();

		// enable subsystem
		Settings->bEnabled = true;
		// override default slots with test slot names
		TArray<FPersistentSlotEntry> DefaultSlots;
		for (const FString& SlotName: SlotNames)
		{
			DefaultSlots.Add(FPersistentSlotEntry{FName{SlotName}, FText::FromString(SlotName)});
		}
		Settings->DefaultNamedSlots = DefaultSlots;
		Settings->StartupSlotName = FName{StartupSlotName};
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
		.SetGameMode<TGameMode>()
		.EnableSubsystem<UPersistentStateSubsystem>()
		.EnableSubsystem<UPersistentStateTestWorldSubsystem>()
		.Create();

		StateSubsystem = ScopedWorld->GetSubsystem<UPersistentStateSubsystem>();

		InitializeImpl(WorldPackage);
	}

	virtual void Cleanup();

protected:
	virtual void InitializeImpl(const FString& Parameters) {}

	FSoftObjectPath WorldPath;
	FAutomationWorldPtr ScopedWorld;
	UPersistentStateSettings* OriginalSettings = nullptr;
	UPersistentStateSubsystem* StateSubsystem = nullptr;
};