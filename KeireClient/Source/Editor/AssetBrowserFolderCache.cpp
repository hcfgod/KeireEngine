#include "KeireClient/Editor/AssetBrowserFolderCache.h"

#include <algorithm>
#include <system_error>

namespace KeireEditor
{
    bool AssetBrowserFolderCache::Refresh(const std::filesystem::path& assetRoot) noexcept
    {
        try
        {
            std::vector<std::filesystem::path> folders;
            std::error_code error;
            const std::filesystem::recursive_directory_iterator end;
            for (std::filesystem::recursive_directory_iterator iterator(
                     assetRoot, std::filesystem::directory_options::skip_permission_denied, error);
                 !error && iterator != end; iterator.increment(error))
            {
                std::error_code statusError;
                if (!iterator->is_directory(statusError) || statusError)
                    continue;
                auto relative = std::filesystem::relative(iterator->path(), assetRoot, statusError);
                if (!statusError && !relative.empty() && !relative.is_absolute())
                    folders.push_back(std::move(relative));
            }
            if (error)
                return false;
            std::ranges::sort(folders);
            m_Folders = std::move(folders);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }
} // namespace KeireEditor
