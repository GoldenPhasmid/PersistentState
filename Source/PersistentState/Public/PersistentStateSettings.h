#pragma once

#include "CoreMinimal.h"

#include "PersistentStateSettings.generated.h"

enum class EManagerStorageType : uint8;
class UPersistentStateStorage;

USTRUCT(BlueprintType)
struct PERSISTENTSTATE_API FPersistentSlotEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, meta = (Validate))
	FName SlotName;

	UPROPERTY(EditAnywhere, meta = (Validate))
	FText Title;
};

/**
 * Persistent State Settings
 * Any changes to state settings may break save compatibility and discoverability.
 */
UCLASS(Config = Game, DefaultConfig)
class PERSISTENTSTATE_API UPersistentStateSettings: public UDeveloperSettings
{
	GENERATED_BODY()
public:
	UPersistentStateSettings(const FObjectInitializer& Initializer);

	static UPersistentStateSettings* GetMutable()
	{
		return GetMutableDefault<UPersistentStateSettings>();
	}
	
	static const UPersistentStateSettings* Get()
	{
		return GetDefault<UPersistentStateSettings>();
	}

	static bool IsDefaultNamedSlot(FName SlotName);

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

	EManagerStorageType CanCreateManagerState() const;
	bool CanCreateProfileState() const;
	bool CanCreateGameState() const;
	bool CanCreateWorldState() const;
	bool ShouldCacheSlotState() const;
	bool UseGameThread() const;
	
	/** state storage implementation used by state subsystem */
	UPROPERTY(EditAnywhere, Config, meta = (Validate))
	TSubclassOf<UPersistentStateStorage> StateStorageClass;

	/** a list of default slots that "should" be created at the start of the game by storage implementation */
	UPROPERTY(EditAnywhere, Config, meta = (Validate))
	TArray<FPersistentSlotEntry> DefaultNamedSlots;

	UPROPERTY(EditAnywhere, Config)
	FName StartupSlotName = NAME_None;

	/** save game path */
	UPROPERTY(EditAnywhere, Config, meta = (Validate))
	FString SaveGamePath{TEXT("SaveGames")};

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
