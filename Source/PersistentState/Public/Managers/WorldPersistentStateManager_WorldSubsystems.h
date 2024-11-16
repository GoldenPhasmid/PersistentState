#pragma once

#include "CoreMinimal.h"
#include "PersistentStateObjectId.h"
#include "WorldPersistentStateManager.h"

#include "WorldPersistentStateManager_WorldSubsystems.generated.h"

struct FPersistentStateObjectId;

USTRUCT()
struct PERSISTENTSTATE_API FWorldSubsystemPersistentState: public FPersistentStateBase
{
	GENERATED_BODY()
public:
	
	FWorldSubsystemPersistentState() = default;
	explicit FWorldSubsystemPersistentState(const UWorldSubsystem* Subsystem);
	explicit FWorldSubsystemPersistentState(const FPersistentStateObjectId& InHandle)
		: Handle(InHandle)
	{}

	void Load();
	void Save();

	UPROPERTY(meta = (AlwaysLoaded))
	FPersistentStateObjectId Handle;

	UPROPERTY(meta = (AlwaysLoaded))
	bool bStateSaved = false;
};

FORCEINLINE bool operator==(const FWorldSubsystemPersistentState& State, const FPersistentStateObjectId& Handle)
{
	return State.Handle == Handle;
}

UCLASS()
class UWorldPersistentStateManager_WorldSubsystems: public UWorldPersistentStateManager
{
	GENERATED_BODY()
public:
	virtual void Init(UWorld* World) override;
	virtual void Cleanup(UWorld* World) override;
	virtual void NotifyObjectInitialized(UObject& Object) override;
	virtual void SaveGameState() override;

protected:

	UPROPERTY()
	TArray<FWorldSubsystemPersistentState> Subsystems;
};
