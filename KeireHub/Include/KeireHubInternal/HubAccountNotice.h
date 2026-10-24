#pragma once

#include "KeireHub/HubAccountWorkflow.h"

#include <string>
#include <string_view>

namespace KeireHub
{
    inline constexpr std::string_view BrowserSignInProgressNotice =
        "The authorization code was accepted and is being exchanged securely.";

    inline void ApplyBrowserSignInCompletionNotice(const HubAccountWorkflowSnapshot& account, std::string& notice,
                                                   bool& noticeError)
    {
        // Only replace our transient progress notice; unrelated actions keep ownership of the banner.
        if (notice != BrowserSignInProgressNotice || account.Busy || account.BrowserSignInPending)
        {
            return;
        }
        if (account.Failure)
        {
            notice = account.Failure->Message;
            noticeError = true;
        }
        else if (account.SignedIn)
        {
            notice = "Signed in through your browser.";
            noticeError = false;
        }
        else
        {
            notice = account.Message.empty() ? "Browser sign-in is no longer in progress." : account.Message;
            noticeError = false;
        }
    }
} // namespace KeireHub
