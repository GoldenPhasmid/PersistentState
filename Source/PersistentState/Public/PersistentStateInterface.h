#pragma once

#include "CoreMinimal.h"
#include "InstancedStruct.h"
#include "StructView.h"
#include "UObject/Interface.h"

#include "PersistentStateInterface.generated.h"

struct FPersistentStateCustomData;
class UGamePersistentStateManager;
struct FInstancedStruct;

UINTERFACE(MinimalAPI, meta = (CannotImplementInterfaceInBlueprint))
class UPersistentStateObject : public UInterface
{
	GENERATED_BODY()
};

/**
* Makes class implementing this interface visible to persistent state system. If a world object implements this
* interface then it will be included in persistent game state.
* Persistent state is:
* - Class (for runtime created actors)
* - Transform (for movable and runtime created actors)
* - Velocity (for moving actors)
* - Owner information (for static actors that changed their owner and runtime created actors)
* - Attachment information (for static actors that changed their attachment and runtime created actors)
* - Any properties marked as SaveGame
* - custom data type used in SaveCustomObjectState/LoadCustomObjectData
*
* When implementing interface, it is required to call NotifyInitialized for actors, components and other objects
* to notify state system:
* 
* 
* void AMyActor::PostRegisterAllComponents()
* {
*	Super::PostRegisterAllComponents();
*	
*	IPersistentStateObject::NotifyInitialized(*this);
* }
*
* void UMyComponent::OnRegister()
* {
*	Super::OnRegister();
*
*	IPersistentStateObject::NotifyInitialized(*this);
* }
*/
class PERSISTENTSTATE_API IPersistentStateObject
{
	GENERATED_BODY()
public:

	/** initialization callback for persistent state system */
	static void NotifyObjectInitialized(UObject& This);
	
	/**
	 * Allows the object to override its name to some stable name, so automatically spawned actors (like player pawn,
	 * controller, game state, etc.) have the same object native name between runs.
	 */
	virtual FName GetStableName() const { return NAME_None; }

	/** allows to skip saving object at runtime. In general, this flag should not change from true to false */
	virtual bool ShouldSaveState() const { return true; }

	/**
	 * called right before object state is restored from persistent state
	 * @note that actor is not yet constructed and its components are not registered
	 */
	virtual void PreLoadState() { }

	/**
	 * called right after object state is restored from persistent state
	 * @note that actor is not yet constructed and its components are not registered
	 */
	virtual void PostLoadState() { }

	/**
	 * called right before object state is saved to a persistent state record
	 * saving is caused either manually or from level streaming
	 */
	virtual void PreSaveState() { }

	/**
	 * called right after object state is saved to a persistent state record
	 * saving is caused either manually or from level streaming
	 */
	virtual void PostSaveState() { }

	/** restore custom object state from a user-defined struct. Supports instanced structs */
    virtual void LoadCustomObjectState(FConstStructView State) {}

    /** save custom object state in a user-defined struct. Supports instanced structs */
    virtual FConstStructView SaveCustomObjectState() { return FConstStructView{}; }
};
