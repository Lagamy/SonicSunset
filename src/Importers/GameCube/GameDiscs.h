#pragma once
#include <string>
#include "GCDisc.h"

inline std::string shadowDiscPath = "Disks/Shadow2005.iso";
inline std::string sadxDiscPath = "Disks/SADX.iso";

namespace Globals {
	/* The opened disc images, kept open so resources can be loaded on demand from resource keys. */
	namespace Discs {
		inline GameCube::GCDisc sadx;
		inline GameCube::GCDisc shadow;
	}
}
