#pragma once
#include "AccessoryImageLoader.h"
#include "HeadphoneClient.h"

namespace winrt::EGoTouchSettings::implementation::AccessoryImages {

// Read only the bundled official image matching the confirmed model and color.
// Call off the UI thread; absence/corruption returns an empty asset for the icon
// fallback. This resolver performs no network requests and guesses no color.
Asset ResolveHeadphone(const HuaweiHeadphones::Snapshot& state);

}
