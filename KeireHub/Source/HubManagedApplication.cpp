#include "KeireHub/HubApplicationFactory.h"

#include "KeireHub/HubLayerFactory.h"

#include <utility>

namespace
{
    class HubApplication final : public Keire::Application
    {
      public:
        HubApplication(Keire::ApplicationSpecification specification, std::filesystem::path executable,
                       const bool smoke, std::optional<KeireHub::HubActivationRequest> pendingStartupActivation,
                       std::shared_ptr<KeireHub::HubInstanceCoordinator> instance)
            : Keire::Application(std::move(specification)), m_Executable(std::move(executable)), m_Smoke(smoke),
              m_PendingStartupActivation(std::move(pendingStartupActivation)), m_Instance(std::move(instance))
        {
            if (!m_Instance->IsPrimary())
                RequestExit();
        }

      protected:
        void OnInitialize() override
        {
            if (!m_Instance->IsPrimary())
            {
                RequestExit();
                return;
            }
            (void)PushOverlay(KeireHub::Detail::CreateHubLayer(m_Executable, m_Smoke,
                                                               std::move(m_PendingStartupActivation), m_Instance));
        }

      private:
        std::filesystem::path m_Executable;
        bool m_Smoke = false;
        std::optional<KeireHub::HubActivationRequest> m_PendingStartupActivation;
        std::shared_ptr<KeireHub::HubInstanceCoordinator> m_Instance;
    };
} // namespace

namespace KeireHub
{
    std::unique_ptr<Keire::Application>
    CreateHubApplication(Keire::ApplicationSpecification specification, std::filesystem::path executable,
                         const bool smoke, std::optional<HubActivationRequest> pendingStartupActivation,
                         std::shared_ptr<HubInstanceCoordinator> instance)
    {
        return std::make_unique<HubApplication>(std::move(specification), executable, smoke,
                                                std::move(pendingStartupActivation), std::move(instance));
    }
} // namespace KeireHub
