#ifndef DISPLAYVERSION_H
#define DISPLAYVERSION_H

#include <string>

// Read only the application's Resources/air-dev-version.txt, never a mod or
// user override. Official releases ignore it; missing/empty resources preserve
// DUNECITY_DISPLAY_VERSION. The optional final LF/CRLF is not part of the label.
std::string loadDisplayVersion(const std::string& resourceDirectory);

// Display/logging only: VERSION and VERSIONSTRING remain compatibility IDs.
// Initialized once, then returned by reference without allocation or file I/O.
// Crash handlers must retain c_str() before installing their signal callbacks,
// rather than invoking this potentially first-use initializer from a signal.
const std::string& getDisplayVersion();

#endif // DISPLAYVERSION_H
