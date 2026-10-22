#pragma once

#include <functional>

namespace bridgerack::MicPermission
{
    enum class Status { granted, notAsked, denied };

    /** Whether macOS lets this app read audio inputs. Always granted on other systems. */
    Status get();

    /** Shows the system prompt if the user hasn't been asked yet; calls back on the message thread. */
    void request (std::function<void (bool granted)>);

    /** Opens System Settings > Privacy & Security > Microphone. */
    void openSettings();
}
