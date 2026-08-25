#include "GameplayModule.h"

namespace
{
    class GameplayModule final : public Keire::EngineModule
    {
      public:
        [[nodiscard]] Keire::ModuleDescriptor Descriptor() const override
        {
            return {.Id = "example.gameplay",
                    .DisplayName = "Example Gameplay",
                    .Version = {1, 0, 0},
                    .SimulationAffecting = true,
                    .DeterministicReplay = true,
                    .ReplayState = Keire::ModuleReplayState::Stateless};
        }

        void Register(Keire::ModuleRegistrationContext& context) override
        {
            context.RegisterMemoryDomain({"Example Gameplay", {}});
            context.RegisterDiagnostic({Keire::DiagnosticId("KEIRE-EXAMPLE-0001"), "Example module diagnostic",
                                        "A diagnostic registered transactionally by the source-module example.",
                                        "KEIRE-EXAMPLE-0001.md"});
        }
    };
} // namespace

Keire::Ref<Keire::EngineModule> CreateGameplayModule() { return Keire::CreateRef<GameplayModule>(); }
