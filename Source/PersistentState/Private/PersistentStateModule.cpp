
#include "PersistentStateDefines.h"

#include "SaveGameSystem.h"
#include "Modules/ModuleManager.h"

class FPersistentStateModule: public IPersistentStateModule
{
public:
	virtual ISaveGameSystem* GetSaveGameSystem() override;
};

ISaveGameSystem* FPersistentStateModule::GetSaveGameSystem()
{
	static FGenericSaveGameSystem SaveGameSystem{};
	return &SaveGameSystem;
}

IMPLEMENT_MODULE(FPersistentStateModule, PersistentState)
