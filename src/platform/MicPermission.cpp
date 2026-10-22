#include "MicPermission.h"

// Only macOS gates audio input behind a privacy permission.
namespace bridgerack::MicPermission
{
    Status get()                                  { return Status::granted; }
    void request (std::function<void (bool)> cb)  { cb (true); }
    void openSettings()                           {}
}
