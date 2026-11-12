#ifndef INIMAP_MATCHMAPEXPORT_H
#define INIMAP_MATCHMAPEXPORT_H

#include <string>

class Game;

/// Ordinary, editable scenario maps made from a custom match, not savegames.
namespace MatchMapExport {

enum class Moment { Start, Current };

/// Campaigns (including the campaign mission picker and co-op campaigns) are excluded.
bool available(const Game& game);
/// A portable filename stem, at most 64 UTF-8 bytes, retaining a generated map's seed.
std::string defaultName(const Game& game);
/// Start returns the embedded original INI verbatim; Current captures the live world.
std::string mapData(const Game& game, Moment moment);
int playerCount(const std::string& mapData);
/// Validates a filename stem (an optional .ini suffix is accepted) and selects the user map folder.
std::string destination(const std::string& mapName, const std::string& mapData);
/// Writes exact bytes, refusing an existing destination unless overwrite is explicitly true.
/// Invalid input and filesystem failures are reported as std::runtime_error.
void save(const std::string& path, const std::string& mapData, bool overwrite = false);

} // namespace MatchMapExport

#endif // INIMAP_MATCHMAPEXPORT_H
