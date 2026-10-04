#pragma once

#include <cstring>

// Embedded Lucide icon set (https://lucide.dev, ISC license). The data
// table lives in the generated lucide_icons.cpp; see tools/gen_lucide.py.

struct LucideIcon {
    const char* name;   // "a-arrow-down" (ASCII, lowercase, no extension)
    const char* body;   // inner SVG markup; wrap with the standard 24x24
                        // stroke='...' wrapper before rasterizing
};

// The table is sorted by name (ASCII); LucideFindIcon binary-searches it
// and returns nullptr for unknown names.
const LucideIcon* LucideFindIcon(const char* name);
int               LucideIconCount();
const LucideIcon& LucideIconAt(int i);
