#ifndef PACKETS_TS_SC_HUNTAHOLIC_BEGIN_HUNTING_H
#define PACKETS_TS_SC_HUNTAHOLIC_BEGIN_HUNTING_H

#include "Server/Packets/PacketDeclaration.h"

#define TS_SC_HUNTAHOLIC_BEGIN_HUNTING_DEF(_) \
	_(simple)(uint32_t, begin_time)

CREATE_PACKET(TS_SC_HUNTAHOLIC_BEGIN_HUNTING, 4009);

#endif // PACKETS_TS_SC_HUNTAHOLIC_BEGIN_HUNTING_H
