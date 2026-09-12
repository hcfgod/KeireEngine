#pragma once

#include "KeireClient/Editor/ShaderGraphDocument.h"

#include <functional>
#include <optional>
#include <string>

namespace KeireEditor
{
    struct ShaderGraphNodePropertyDraft
    {
        std::optional<Keire::AssetId> Node;
        std::string Name;
        std::string Symbol;
        std::string Include;
        std::string Function;
        std::string Description;
        std::string Category;
        std::string Comment;
        double SortPriority = 0.0;
        double Minimum = 0.0;
        double Maximum = 1.0;
        double Step = 0.01;
        bool HasMinimum = false;
        bool HasMaximum = false;
        bool HasStep = false;
        bool HighDynamicRange = false;
        bool CommentPinned = false;
        bool Dirty = false;
    };

    class ShaderGraphSaveState final
    {
      public:
        ShaderGraphNodePropertyDraft Draft;
        std::optional<Keire::AssetId> RequestedAsset;

        void Stage(const ShaderGraphDocument& document, const Keire::ShaderGraphNode& node);
        void Apply(ShaderGraphDocument& document);
        void Request(const ShaderGraphDocument& document);
        void Update(ShaderGraphDocument& document, const std::function<void()>& save);
        void Reset() noexcept;
    };
} // namespace KeireEditor
