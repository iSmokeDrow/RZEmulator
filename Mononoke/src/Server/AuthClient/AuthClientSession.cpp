/*
 *  Copyright (C) 2017-2019 NGemity <https://ngemity.org/>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 3 of the License, or (at your
 *  option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "Common.h"
#include "DatabaseEnv.h"
#include "AuthClientSession.h"
#include "PlayerList.h"
#include "GameList.h"
#include "XPacket.h"
#include "Encryption/MD5.h"
#include "AuthGameSession.h"
#include "Util.h"
#include "Config.h"

// Constructo - give it a socket
AuthClientSession::AuthClientSession(boost::asio::ip::tcp::socket &&socket) : XSocket(std::move(socket)), m_pPlayer(nullptr)
{
    _desCipther.Init("MERONG");
}

// Close patch file descriptor before leaving
AuthClientSession::~AuthClientSession()
{
}

void AuthClientSession::OnClose()
{
    if (m_pPlayer == nullptr)
        return;
    auto g = sPlayerMapList.GetPlayer(m_pPlayer->szLoginName);
    if (g != nullptr && g->nAccountID == m_pPlayer->nAccountID && !g->bIsInGame)
    {
        sPlayerMapList.RemovePlayer(g->szLoginName);
        delete m_pPlayer;
    }
}

enum eStatus
{
    STATUS_CONNECTED = 0,
    STATUS_AUTHED
};

typedef struct AuthHandler
{
    int cmd;
    eStatus status;
    std::function<void(AuthClientSession *, XPacket *)> handler;
} AuthHandler;

template <typename T>
AuthHandler declareHandler(eStatus status, void (AuthClientSession::*handler)(const T *packet))
{
    AuthHandler handlerData{};
    handlerData.cmd = T::getId(EPIC_4_1_1);
    handlerData.status = status;
    handlerData.handler = [handler](AuthClientSession *instance, XPacket *packet) -> void {
        T deserializedPacket;
        MessageSerializerBuffer buffer(packet);
        deserializedPacket.deserialize(&buffer);
        (instance->*handler)(&deserializedPacket);
    };

    return handlerData;
}

const AuthHandler packetHandler[] = {
    declareHandler(STATUS_CONNECTED, &AuthClientSession::HandleVersion),
    declareHandler(STATUS_CONNECTED, &AuthClientSession::HandleLoginPacket),
    declareHandler(STATUS_AUTHED, &AuthClientSession::HandleServerList),
    declareHandler(STATUS_AUTHED, &AuthClientSession::HandleSelectServer),
};

constexpr int tableSize = sizeof(packetHandler) / sizeof(AuthHandler);

/// Handler for incoming packets
ReadDataHandlerResult AuthClientSession::ProcessIncoming(XPacket *pRecvPct)
{
    ASSERT(pRecvPct);

    auto _cmd = pRecvPct->GetPacketID();

    int i = 0;
    for (i = 0; i < tableSize; i++)
    {
        if ((uint16_t)packetHandler[i].cmd == _cmd && (packetHandler[i].status == STATUS_CONNECTED || (_isAuthed && packetHandler[i].status == STATUS_AUTHED)))
        {
            packetHandler[i].handler(this, pRecvPct);
            break;
        }
    }
    // Report unknown packets in the error log
    if (i == tableSize && _cmd != static_cast<int>(NGemity::Packets::TS_CS_PING))
    {
        NG_LOG_DEBUG("network", "Got unknown packet '%d' from '%s'", pRecvPct->GetPacketID(), GetRemoteIpAddress().to_v4().to_string().c_str());
        return ReadDataHandlerResult::Error;
    }
    return ReadDataHandlerResult::Ok;
}

void AuthClientSession::HandleLoginPacket(const TS_CA_ACCOUNT *pRecvPct)
{	
    std::string szPassword((char *)(pRecvPct->passwordDes.password), sConfigMgr->getCachedConfig().packetVersion >= EPIC_5_1 ? 61 : 32);
    _desCipther.Decrypt(&szPassword[0], (int)szPassword.length());
    szPassword.erase(std::remove(szPassword.begin(), szPassword.end(), '\0'), szPassword.end());
    szPassword.insert(0, "2011"); // @todo: md5 key
    szPassword = md5(szPassword);

    // SQL part
    PreparedStatement *stmt = LoginDatabase.GetPreparedStatement(LOGIN_GET_ACCOUNT);
    stmt->setString(0, pRecvPct->account);
    stmt->setString(1, szPassword);
    if (PreparedQueryResult dbResult = LoginDatabase.Query(stmt))
    {
        m_pPlayer = new Player{};
        m_pPlayer->nAccountID = (*dbResult)[0].GetUInt32();
        m_pPlayer->szLoginName = (*dbResult)[1].GetString();
        m_pPlayer->nLastServerIDX = (*dbResult)[2].GetUInt32();
        m_pPlayer->bIsBlocked = (*dbResult)[3].GetBool();
        m_pPlayer->nPermission = (*dbResult)[4].GetInt32();
        m_pPlayer->bIsInGame = false;
        
        NG_LOG_DEBUG("server.authserver", "Player/Client %s attempting to login...", m_pPlayer->szLoginName.c_str());

        std::transform(m_pPlayer->szLoginName.begin(), m_pPlayer->szLoginName.end(), m_pPlayer->szLoginName.begin(), ::tolower);

        if (m_pPlayer->bIsBlocked)
        {
            SendResultMsg(pRecvPct->getReceivedId(), TS_RESULT_ACCESS_DENIED, 0);
            return;
        }

        auto pOldPlayer = sPlayerMapList.GetPlayer(m_pPlayer->szLoginName);
        if (pOldPlayer != nullptr)
        {
            if (pOldPlayer->bIsInGame)
            {
                auto game = sGameMapList.GetGame(static_cast<uint32_t>(pOldPlayer->nGameIDX));
                if (game != nullptr && game->m_pSession != nullptr)
                    game->m_pSession->KickPlayer(pOldPlayer);
            }
            SendResultMsg(pRecvPct->getReceivedId(), TS_RESULT_ALREADY_EXIST, 0);
            sPlayerMapList.RemovePlayer(pOldPlayer->szLoginName);
            delete pOldPlayer;
        }

        _isAuthed = true;
        sPlayerMapList.AddPlayer(m_pPlayer);
        SendResultMsg(pRecvPct->getReceivedId(), TS_RESULT_SUCCESS, 1);
          
        NG_LOG_DEBUG("server.authserver", "Player/Client %s logged in successfully!", m_pPlayer->szLoginName.c_str());
        
        return;
    }
    SendResultMsg(pRecvPct->getReceivedId(), TS_RESULT_NOT_EXIST, 0);
}

void AuthClientSession::HandleVersion(const TS_CA_VERSION *pRecvPct)
{
    NG_LOG_TRACE("network", "[Version] Client version is %s", pRecvPct->szVersion.c_str());
}

void AuthClientSession::HandleServerList(const TS_CA_SERVER_LIST *pRecvPct)
{
    NG_SHARED_GUARD readGuard(*sGameMapList.GetGuard());
    auto map = sGameMapList.GetMap();
    TS_AC_SERVER_LIST serverList{};
    for (auto &x : *map)
    {
		std::string extIPStr = x.second->server_external_ip;
		if (extIPStr.compare("0.0.0.0") != 0)
		{
			NG_LOG_DEBUG("server.authserver", "Spoofing External Address! : %s:%d", extIPStr, x.second->server_port);
			x.second->server_ip = extIPStr;
		}
				
        serverList.servers.emplace_back(*x.second);
        NG_LOG_DEBUG("server.authserver", "Server added to serverList:\nID:%d Name:%s IP:%s Port:%d", x.second->server_idx, x.second->server_name, x.second->server_ip, x.second->server_port);
    }
    
    NG_LOG_DEBUG("server.authserver", "Sending serverList (count = %d) to client...", serverList.servers.size());
    SendPacket(serverList);
}

void AuthClientSession::HandleSelectServer(const TS_CA_SELECT_SERVER *pRecvPct)
{
    m_pPlayer->nGameIDX = pRecvPct->server_idx;
    m_pPlayer->nOneTimeKey = ((uint64_t)rand32()) * rand32() * rand32() * rand32();
    m_pPlayer->bIsInGame = true;
    bool bExist = sGameMapList.GetGame((uint32_t)m_pPlayer->nGameIDX) != 0;

    TS_AC_SELECT_SERVER resultPct{};
    resultPct.result = (bExist ? TS_RESULT_SUCCESS : TS_RESULT_NOT_EXIST);
    resultPct.one_time_key = (bExist ? m_pPlayer->nOneTimeKey : 0);
    resultPct.pending_time = 0;
    
    NG_LOG_DEBUG("server.authserver", "Server Selection Result for player: %s\n%s\nOneTimeKey: %d", m_pPlayer->szLoginName ,(bExist ? "TS_RESULT_SUCCESS" : "TS_RESULT_NOT_EXIST"), resultPct.one_time_key);
    
    SendPacket(resultPct);
}

void AuthClientSession::SendResultMsg(uint16_t pctID, uint16_t result, uint32_t value)
{
    TS_AC_RESULT resultMsg{};
    resultMsg.request_msg_id = pctID;
    resultMsg.result = result;
    resultMsg.login_flag = value;
    SendPacket(resultMsg);
}
