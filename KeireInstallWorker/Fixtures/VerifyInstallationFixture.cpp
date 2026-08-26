#include <chrono>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>

namespace
{
    [[nodiscard]] std::string VerificationMode()
    {
        char* raw = nullptr;
        std::size_t length = 0;
#if defined(_WIN32)
        if (::_dupenv_s(&raw, &length, "KEIRE_INSTALL_VERIFY_FIXTURE_MODE") != 0 || !raw)
        {
            std::free(raw);
            return {};
        }
#else
        const auto* value = std::getenv("KEIRE_INSTALL_VERIFY_FIXTURE_MODE");
        if (!value)
            return {};
        raw = const_cast<char*>(value);
#endif
        std::string result(raw);
#if defined(_WIN32)
        std::free(raw);
#endif
        return result;
    }
} // namespace

int main(const int count, char** values)
{
    if (count != 2 || std::string_view(values[1]) != "--verify-installation")
        return 64;
    const auto mode = VerificationMode();
    if (mode == "fail")
        return 7;
    if (mode == "timeout")
        std::this_thread::sleep_for(std::chrono::seconds(35));
    return 0;
}
