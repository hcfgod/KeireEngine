#include "Keire/Layer.h"

#include "Keire/Application.h"

#include <algorithm>
#include <exception>
#include <stdexcept>

namespace Keire
{
    class LayerStack::Impl final
    {
      public:
        class TraversalScope final
        {
          public:
            explicit TraversalScope(Impl& implementation) noexcept : m_Implementation(implementation)
            {
                ++m_Implementation.TraversalDepth;
            }

            ~TraversalScope() { --m_Implementation.TraversalDepth; }

            TraversalScope(const TraversalScope&) = delete;
            TraversalScope& operator=(const TraversalScope&) = delete;

          private:
            Impl& m_Implementation;
        };

        struct LayerRecord
        {
            LayerId Id;
            std::unique_ptr<Layer> Instance;
            bool Attached = false;
        };

        enum class PendingKind : std::uint8_t
        {
            AddLayer,
            AddOverlay,
            Remove
        };

        struct PendingOperation
        {
            PendingKind Kind;
            LayerId Id;
            std::unique_ptr<Layer> Instance;
        };

        explicit Impl(Application& application) : Owner(&application) {}

        Application* Owner;
        std::vector<LayerRecord> Layers;
        std::vector<PendingOperation> PendingOperations;
        std::size_t OverlayStart = 0;
        std::uint64_t NextLayerId = 1;
        std::size_t TraversalDepth = 0;
        bool IsActive = false;

        [[nodiscard]] bool IsTraversing() const noexcept { return TraversalDepth != 0; }
    };

    LayerStack::LayerStack(Application& application) : m_Impl(std::make_unique<Impl>(application)) {}

    LayerStack::~LayerStack()
    {
        if (m_Impl->IsActive)
        {
            std::terminate();
        }
    }

    LayerId LayerStack::PushLayer(std::unique_ptr<Layer> layer)
    {
        m_Impl->Owner->RequireOwnerThread("PushLayer");
        if (!layer)
        {
            throw std::invalid_argument("Cannot push a null layer.");
        }
        if (!m_Impl->Owner->CanModifyLayers())
        {
            throw std::logic_error("Cannot push a layer after the application has stopped.");
        }

        const LayerId id(m_Impl->NextLayerId++);
        if (m_Impl->IsTraversing())
        {
            m_Impl->PendingOperations.push_back({Impl::PendingKind::AddLayer, id, std::move(layer)});
            return id;
        }

        const auto index = m_Impl->OverlayStart;
        m_Impl->Layers.insert(m_Impl->Layers.begin() + static_cast<std::ptrdiff_t>(index),
                              {id, std::move(layer), false});
        if (m_Impl->IsActive)
        {
            const auto pendingSize = m_Impl->PendingOperations.size();
            try
            {
                Impl::TraversalScope traversal(*m_Impl);
                m_Impl->Layers[index].Instance->Attach(*m_Impl->Owner);
                m_Impl->Layers[index].Attached = true;
            }
            catch (...)
            {
                m_Impl->PendingOperations.resize(pendingSize);
                m_Impl->Layers.erase(m_Impl->Layers.begin() + static_cast<std::ptrdiff_t>(index));
                throw;
            }
        }

        ++m_Impl->OverlayStart;
        return id;
    }

    LayerId LayerStack::PushOverlay(std::unique_ptr<Layer> overlay)
    {
        m_Impl->Owner->RequireOwnerThread("PushOverlay");
        if (!overlay)
        {
            throw std::invalid_argument("Cannot push a null overlay.");
        }
        if (!m_Impl->Owner->CanModifyLayers())
        {
            throw std::logic_error("Cannot push an overlay after the application has stopped.");
        }

        const LayerId id(m_Impl->NextLayerId++);
        if (m_Impl->IsTraversing())
        {
            m_Impl->PendingOperations.push_back({Impl::PendingKind::AddOverlay, id, std::move(overlay)});
            return id;
        }

        m_Impl->Layers.push_back({id, std::move(overlay), false});
        if (m_Impl->IsActive)
        {
            const auto pendingSize = m_Impl->PendingOperations.size();
            try
            {
                Impl::TraversalScope traversal(*m_Impl);
                m_Impl->Layers.back().Instance->Attach(*m_Impl->Owner);
                m_Impl->Layers.back().Attached = true;
            }
            catch (...)
            {
                m_Impl->PendingOperations.resize(pendingSize);
                m_Impl->Layers.pop_back();
                throw;
            }
        }

        return id;
    }

    bool LayerStack::Remove(const LayerId id)
    {
        m_Impl->Owner->RequireOwnerThread("RemoveLayer");
        const auto existing = std::find_if(m_Impl->Layers.begin(), m_Impl->Layers.end(),
                                           [id](const auto& record) { return record.Id == id; });
        const auto pending = std::find_if(m_Impl->PendingOperations.begin(), m_Impl->PendingOperations.end(),
                                          [id](const auto& operation) { return operation.Id == id; });
        if (existing == m_Impl->Layers.end() && pending == m_Impl->PendingOperations.end())
        {
            return false;
        }
        if (m_Impl->IsTraversing())
        {
            m_Impl->PendingOperations.push_back({Impl::PendingKind::Remove, id, {}});
            return true;
        }

        if (pending != m_Impl->PendingOperations.end())
        {
            m_Impl->PendingOperations.erase(pending);
            return true;
        }

        const auto index = static_cast<std::size_t>(std::distance(m_Impl->Layers.begin(), existing));
        if (existing->Attached)
        {
            Impl::TraversalScope traversal(*m_Impl);
            existing->Instance->Detach();
        }
        m_Impl->Layers.erase(existing);
        if (index < m_Impl->OverlayStart)
        {
            --m_Impl->OverlayStart;
        }
        return true;
    }

    std::size_t LayerStack::Size() const noexcept { return m_Impl->Layers.size(); }

    bool LayerStack::Active() const noexcept { return m_Impl->IsActive; }

    void LayerStack::Activate()
    {
        if (m_Impl->IsActive)
        {
            throw std::logic_error("LayerStack is already active.");
        }
        m_Impl->IsActive = true;
        ApplyPending();
    }

    void LayerStack::ApplyPending()
    {
        if (m_Impl->IsTraversing())
        {
            return;
        }

        // Only mutations that existed at this safe boundary may become visible here. OnAttach can request more
        // structural changes, but those belong to the next boundary just like mutations from any other traversal.
        const auto pendingOperationCount = m_Impl->PendingOperations.size();
        for (auto& record : m_Impl->Layers)
        {
            if (!record.Attached && m_Impl->IsActive)
            {
                const auto pendingSize = m_Impl->PendingOperations.size();
                try
                {
                    Impl::TraversalScope traversal(*m_Impl);
                    record.Instance->Attach(*m_Impl->Owner);
                    record.Attached = true;
                }
                catch (...)
                {
                    m_Impl->PendingOperations.resize(pendingSize);
                    throw;
                }
            }
        }

        std::vector<Impl::PendingOperation> operations;
        operations.reserve(pendingOperationCount);
        for (std::size_t index = 0; index < pendingOperationCount; ++index)
            operations.push_back(std::move(m_Impl->PendingOperations[index]));
        m_Impl->PendingOperations.erase(m_Impl->PendingOperations.begin(),
                                        m_Impl->PendingOperations.begin() +
                                            static_cast<std::ptrdiff_t>(pendingOperationCount));
        for (auto& operation : operations)
        {
            if (operation.Kind == Impl::PendingKind::Remove)
            {
                (void)Remove(operation.Id);
                continue;
            }

            std::size_t index = 0;
            if (operation.Kind == Impl::PendingKind::AddLayer)
            {
                index = m_Impl->OverlayStart;
                m_Impl->Layers.insert(m_Impl->Layers.begin() + static_cast<std::ptrdiff_t>(m_Impl->OverlayStart),
                                      {operation.Id, std::move(operation.Instance), false});
            }
            else
            {
                index = m_Impl->Layers.size();
                m_Impl->Layers.push_back({operation.Id, std::move(operation.Instance), false});
            }

            if (m_Impl->IsActive)
            {
                const auto pendingSize = m_Impl->PendingOperations.size();
                try
                {
                    Impl::TraversalScope traversal(*m_Impl);
                    m_Impl->Layers[index].Instance->Attach(*m_Impl->Owner);
                    m_Impl->Layers[index].Attached = true;
                }
                catch (...)
                {
                    m_Impl->PendingOperations.resize(pendingSize);
                    m_Impl->Layers.erase(m_Impl->Layers.begin() + static_cast<std::ptrdiff_t>(index));
                    throw;
                }
            }

            if (operation.Kind == Impl::PendingKind::AddLayer)
                ++m_Impl->OverlayStart;
        }
    }

    void LayerStack::Deactivate() noexcept
    {
        m_Impl->IsActive = false;
        m_Impl->PendingOperations.clear();
        Impl::TraversalScope traversal(*m_Impl);
        for (auto iterator = m_Impl->Layers.rbegin(); iterator != m_Impl->Layers.rend(); ++iterator)
        {
            if (iterator->Attached)
            {
                iterator->Instance->Detach();
                iterator->Attached = false;
            }
        }
        m_Impl->PendingOperations.clear();
        m_Impl->Layers.clear();
        m_Impl->OverlayStart = 0;
    }

    void LayerStack::FixedUpdate(const Time& time)
    {
        Impl::TraversalScope traversal(*m_Impl);
        for (const auto& record : m_Impl->Layers)
        {
            record.Instance->OnFixedUpdate(time);
            if (m_Impl->Owner->ExitRequested())
            {
                break;
            }
        }
    }

    void LayerStack::Update(const Time& time)
    {
        Impl::TraversalScope traversal(*m_Impl);
        for (const auto& record : m_Impl->Layers)
        {
            record.Instance->OnUpdate(time);
            if (m_Impl->Owner->ExitRequested())
            {
                break;
            }
        }
    }

    void LayerStack::Ui(UiFrame& frame)
    {
        Impl::TraversalScope traversal(*m_Impl);
        for (const auto& record : m_Impl->Layers)
        {
            record.Instance->OnUi(frame);
            if (m_Impl->Owner->ExitRequested())
            {
                break;
            }
        }
    }

    EventFlow LayerStack::Dispatch(const EventView& event)
    {
        Impl::TraversalScope traversal(*m_Impl);
        for (auto iterator = m_Impl->Layers.rbegin(); iterator != m_Impl->Layers.rend(); ++iterator)
        {
            if (iterator->Instance->OnEvent(event) == EventFlow::Handled)
            {
                return EventFlow::Handled;
            }
        }
        return EventFlow::Continue;
    }

    Layer::Layer(std::string name) : m_Name(std::move(name))
    {
        if (m_Name.empty())
        {
            throw std::invalid_argument("Layer name must not be empty.");
        }
    }

    Layer::~Layer()
    {
        if (m_Application)
        {
            std::terminate();
        }
    }

    Application& Layer::Owner()
    {
        if (!m_Application)
        {
            throw std::logic_error("Layer is not attached to an Application.");
        }
        return *m_Application;
    }

    const Application& Layer::Owner() const
    {
        if (!m_Application)
        {
            throw std::logic_error("Layer is not attached to an Application.");
        }
        return *m_Application;
    }

    Ref<EventBus> Layer::EventSystem() const { return Owner().Events(); }

    void Layer::ListenAny(EventBus::AnyCallback callback, const EventPriority priority)
    {
        if (m_Detaching)
        {
            return;
        }
        m_Subscriptions.push_back(EventSystem()->SubscribeAny(std::move(callback), priority));
    }

    void Layer::Attach(Application& application)
    {
        if (m_Application)
        {
            throw std::logic_error("Layer is already attached to an Application.");
        }
        m_Application = &application;
        try
        {
            OnAttach();
        }
        catch (...)
        {
            m_Subscriptions.clear();
            m_Application = nullptr;
            throw;
        }
    }

    void Layer::Detach() noexcept
    {
        m_Detaching = true;
        m_Subscriptions.clear();
        OnDetach();
        m_Subscriptions.clear();
        m_Application = nullptr;
        m_Detaching = false;
    }
} // namespace Keire
