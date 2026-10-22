#include "MicPermission.h"

#include <juce_events/juce_events.h>
#include <juce_core/juce_core.h>

#import <AVFoundation/AVFoundation.h>

namespace bridgerack::MicPermission
{

Status get()
{
    switch ([AVCaptureDevice authorizationStatusForMediaType: AVMediaTypeAudio])
    {
        case AVAuthorizationStatusAuthorized:    return Status::granted;
        case AVAuthorizationStatusNotDetermined: return Status::notAsked;
        case AVAuthorizationStatusDenied:
        case AVAuthorizationStatusRestricted:
        default:                                 return Status::denied;
    }
}

void request (std::function<void (bool)> callback)
{
    [AVCaptureDevice requestAccessForMediaType: AVMediaTypeAudio
                             completionHandler: ^(BOOL granted)
                             {
                                 juce::MessageManager::callAsync ([callback, granted] { callback (granted); });
                             }];
}

void openSettings()
{
    juce::URL ("x-apple.systempreferences:com.apple.preference.security?Privacy_Microphone").launchInDefaultBrowser();
}

} // namespace bridgerack::MicPermission
