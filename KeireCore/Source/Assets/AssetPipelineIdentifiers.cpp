#include "Keire/Assets/AssetPipeline.h"

namespace Keire
{
    AssetOperationCancelled::AssetOperationCancelled() : std::runtime_error("Asset operation was cancelled.") {}

    ExternalAssetImportReceiptId ExternalAssetImportReceiptId::Parse(const std::string_view value)
    {
        return ExternalAssetImportReceiptId(AssetId::Parse(value));
    }

    std::string ExternalAssetImportReceiptId::ToString() const { return m_Value.ToString(); }

    AssetTrashId AssetTrashId::Parse(const std::string_view value) { return AssetTrashId(AssetId::Parse(value)); }

    std::string AssetTrashId::ToString() const { return m_Value.ToString(); }
} // namespace Keire
