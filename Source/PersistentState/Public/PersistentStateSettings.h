#pragma once

#include "CoreMinimal.h"

#include "PersistentStateSettings.generated.h"

class UPersistentStateSlotDescriptor;
enum class EManagerStorageType : uint8;
class UPersistentStateStorage;

/**
 * 
 */
USTRUCT(BlueprintType)
struct PERSISTENTSTATE_API FPersistentStateDefaultNamedSlot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, meta = (Validate))
	FName SlotName;

	UPROPERTY(EditAnywhere, meta = (Validate))
	FText Title;
	
	UPROPERTY(EditAnywhere, meta = (Validate))
	TSubclassOf<UPersistentStateSlotDescriptor> Descriptor;
};

/**
 * Persistent State Settings
 * Any changes to state settings may break save compatibility and potential loss for already created saves
 */
UCLASS(Config = Game, DefaultConfig)
class PERSISTENTSTATE_API UPersistentStateSettings: public UDeveloperSettings
{
	GENERATED_BODY()
public:
	UPersistentStateSettings(const FObjectInitializer& Initializer);

	virtual void PostLoad() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	static const UPersistentStateSettings* Get();
	static UPersistentStateSettings* GetMutable();

	/** @return absolute save game path in Saved directory */
	FString GetSaveGamePath() const;
	/** @return save game extension */
	FString GetSaveGameExtension() const;
	/**
	 * @return full save game file path for a given slot name,
	 * SaveGamePath/SlotName.SaveGameExtension
	 */
	FString GetSaveGameFilePath(FName SlotName) const;
	/**
	 * @return full screenshot file path for a given slot name,
	 * SaveGamePath/SlotName.ScreenshotFileExtension
	 */
	FString GetScreenshotFilePath(FName SlotName) const;

	bool IsEnabled() const;
	bool HasValidConfiguration() const;
	EManagerStorageType CanCreateManagerState() const;
	bool CanCreateProfileState() const;
	bool CanCreateGameState() const;
	bool CanCreateWorldState() const;
	bool ShouldCacheSlotState() const;
	bool UseGameThread() const;
	
	
	/** state storage implementation used by state subsystem */
	UPROPERTY(EditAnywhere, Config, meta = (Validate))
	TSubclassOf<UPersistentStateStorage> StateStorageClass;

	/** default state slot descriptor */
	UPROPERTY(EditAnywhere, Config, meta = (Validate))
	TSubclassOf<UPersistentStateSlotDescriptor> DefaultSlotDescriptor;

	/** a list of default slots that "should" be created at the start of the game by storage implementation */
	UPROPERTY(EditAnywhere, Config, meta = (Validate))
	TArray<FPersistentStateDefaultNamedSlot> DefaultNamedSlots;

	/** If set, persistent state will always load this slot during game instance initialization */
	UPROPERTY(EditAnywhere, Config)
	FName StartupSlotName = NAME_None;
	
	/**
	 * Save game directory, relative to the Saved folder
	 * Use @GetSaveGamePath to retrieve a full path to the save game directory in a file system
	 */
	UPROPERTY(EditAnywhere, Config, meta = (Validate))
	FString SaveGameDirectory{TEXT("SaveGames")};
	
	/** save game extension */
	UPROPERTY(EditAnywhere, Config, meta = (Validate))
	FString SaveGameExtension{TEXT(".sav")};

	/** screenshot extension */
	UPROPERTY(EditAnywhere, Config, meta = (EditCondition = "bCaptureScreenshot", DisplayAfter = "bCaptureScreenshot"))
	FString ScreenshotExtension{TEXT(".png")};

	/** screenshot resolution */
	UPROPERTY(EditAnywhere, Config, meta = (EditCondition = "bCaptureScreenshot", DisplayAfter = "bCaptureScreenshot"))
	FIntPoint ScreenshotResolution{600, 400};

	/**
	 * Controls whether persistent state subsystem is created
	 * If set to false, persistent state functionality is fully disabled
	 */
	UPROPERTY(EditAnywhere, Config)
	uint8 bEnabled : 1 = true;

	/** If true, save/load operations run synchronously on game thread by default. Otherwise, UE tasks system is used */
	UPROPERTY(EditAnywhere, Config)
	uint8 bForceGameThread : 1 = false;

	/**
	 * If true, most recently loaded/saved slot state will be cached inside slot storage.
	 * If enabled, improves performance when reloading the level (because world state is cached) or traveling to a new world
	 * (because game state is cached) for the cost of storage memory
	 */
	UPROPERTY(EditAnywhere, Config)
	uint8 bCacheSlotState: 1 = true;

	/**
	 * If set, profile state will be created from available manager classes
	 * Set to false it if you don't require profile state
	 */
	UPROPERTY(EditAnywhere, Config, meta = (EditCondition = "bEnabled"))
	uint8 bStoreProfileState : 1 = true;

	/**
	 * If set, game state will be created from available manager classes
	 * Set to false it if you don't require game state
	 */
	UPROPERTY(EditAnywhere, Config, meta = (EditCondition = "bEnabled"))
	uint8 bStoreGameState : 1 = true;

	/**
	 * If set, world state will be created from available manager classes
	 * Set to false it if you don't require world state
	 */
	UPROPERTY(EditAnywhere, Config, meta = (EditCondition = "bEnabled"))
	uint8 bStoreWorldState : 1 = true;

	/** If set, SaveGameToSlot also captures a screenshot that is saved as a separate file in an image format */
	UPROPERTY(EditAnywhere, Config, meta = (EditCondition = "bEnabled"))
	uint8 bCaptureScreenshot: 1 = false;

	/** If set, screenshot captures UI as well */
	UPROPERTY(EditAnywhere, Config, meta = (EditCondition = "bEnabled && bCaptureScreenshot"))
	uint8 bCaptureUI: 1 = false;
};
