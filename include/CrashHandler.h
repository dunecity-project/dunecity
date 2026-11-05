#ifndef CRASHHANDLER_H
#define CRASHHANDLER_H

/**
 * @file CrashHandler.h
 * @brief Crash reporting and signal handling for release builds
 * 
 * Provides crash detection and logging for unhandled signals (SIGSEGV, SIGABRT, etc.)
 * that would otherwise result in silent crashes to desktop.
 */

// Automated native probes opt out before startup; ordinary game launches keep
// their error dialogs. Logging and failure exit codes are unchanged.
void setErrorDialogsEnabled(bool enabled);
bool errorDialogsEnabled();

/**
 * Install signal handlers for crash detection
 * 
 * This should be called early in main(), after SDL initialization but before
 * any game logic that could potentially crash.
 * 
 * @param logPath Path to the log file where crash reports will be written
 *                (typically "Dune Legacy.log" in user config directory)
 */
void installCrashHandlers(const char* logPath);

/**
 * Write current game state to crash log
 * 
 * Called automatically by signal handler when a crash occurs.
 * Writes game mode, cycle count, player count, map name, etc.
 * 
 * This function is crash-safe and uses minimal allocations.
 */
void writeCrashGameState();

/**
 * Register the game instance for crash reporting
 * 
 * Should be called from Game constructor to allow crash handler
 * to access game state information.
 * 
 * @param game Pointer to the current Game instance
 */
void registerGameForCrashReporting(void* game);

#endif // CRASHHANDLER_H

