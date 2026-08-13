/* Single source of truth for the VPP Forge version.
   Included by src/vppforge.c (self-update check, bridge, About dialog) and by
   resources/vppforge.rc (Windows file properties), so both always agree.
   Bump all three lines together. */
#ifndef VPP_VERSION_H
#define VPP_VERSION_H

#define VPP_VERSION       "1.21.1"
#define VPP_VERSION_COMMA 1,21,1,0

#endif
