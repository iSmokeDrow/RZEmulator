#ifndef PACKETS_TS_SC_REQUEST_SECURITY_NO_H
#define PACKETS_TS_SC_REQUEST_SECURITY_NO_H

#include "Server/Packets/PacketDeclaration.h"

#define TS_SC_REQUEST_SECURITY_NO_DEF(_) \
	_(simple)(int32_t, mode)

CREATE_PACKET(TS_SC_REQUEST_SECURITY_NO, 9004);

#endif // PACKETS_TS_SC_REQUEST_SECURITY_NO_H