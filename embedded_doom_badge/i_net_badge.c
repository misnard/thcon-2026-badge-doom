#include <stdlib.h>
#include <string.h>

#include "d_net.h"
#include "doomstat.h"

void I_NetCmd(void) {
}

void I_InitNetwork(void) {
    doomcom = malloc(sizeof(*doomcom));
    memset(doomcom, 0, sizeof(*doomcom));
    doomcom->ticdup = 1;
    netgame = false;
    doomcom->id = DOOMCOM_ID;
    doomcom->numplayers = doomcom->numnodes = 1;
    doomcom->deathmatch = false;
    doomcom->consoleplayer = 0;
}
