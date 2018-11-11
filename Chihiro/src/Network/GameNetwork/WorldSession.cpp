/*
  *  Copyright (C) 2016-2016 Xijezu <http://xijezu.com/>
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
#include "WorldSession.h"
#include "World.h"
#include "ClientPackets.h"
#include "AuthNetwork.h"
#include "MemPool.h"
#include "Messages.h"
#include "Scripting/XLua.h"

#include "Encryption/MD5.h"
#include "RegionContainer.h"
#include "NPC.h"
#include "ObjectMgr.h"
#include "Skill.h"
#include "GameRule.h"
#include "AllowedCommandInfo.h"
#include "MixManager.h"
#include "GroupManager.h"
#include "GameContent.h"

// Constructo - give it a socket
WorldSession::WorldSession(XSocket *socket) : _socket(socket), m_nLastPing(sWorld.GetArTime())
{
}

// Close patch file descriptor before leaving
WorldSession::~WorldSession()
{
    if (m_pPlayer)
        onReturnToLobby(nullptr);
}

void WorldSession::OnClose()
{
    if (_accountName.length() > 0)
        sAuthNetwork.SendClientLogoutToAuth(_accountName);
    if (m_pPlayer)
        onReturnToLobby(nullptr);
}

enum eStatus
{
    STATUS_CONNECTED = 0,
    STATUS_AUTHED
};

typedef struct WorldSessionHandler
{
    int                                            cmd;
    eStatus                                        status;
    std::function<void(WorldSession *, XPacket *)> handler;
} WorldSessionHandler;

template<typename T>
WorldSessionHandler declareHandler(eStatus status, void (WorldSession::*handler)(const T *packet))
{
    WorldSessionHandler handlerData{ };
    handlerData.cmd     = T::getId(EPIC_4_1_1);
    handlerData.status  = status;
    handlerData.handler = [handler](WorldSession *instance, XPacket *packet) -> void {
        T                       deserializedPacket;
        MessageSerializerBuffer buffer(packet);
        deserializedPacket.deserialize(&buffer);
        (instance->*handler)(&deserializedPacket);
    };
    return handlerData;
}

const WorldSessionHandler packetHandler[] =
                                  {
                                          declareHandler(STATUS_CONNECTED, &WorldSession::onAuthResult),
                                          declareHandler(STATUS_CONNECTED, &WorldSession::onAccountWithAuth),
                                          declareHandler(STATUS_CONNECTED, &WorldSession::onPing),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onLogoutTimerRequest),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onReturnToLobby),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onCharacterList),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onLogin),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onCharacterName),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onCreateCharacter),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onDeleteCharacter),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onMoveRequest),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onRegionUpdate),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onChatRequest),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onPutOnItem),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onPutOffItem),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onGetSummonSetupInfo),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onContact),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onDialog),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onBuyItem),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onChangeLocation),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onTimeSync),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onGameTime),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onQuery),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onMixRequest),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onTrade),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onUpdate),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onJobLevelUp),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onLearnSkill),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onEquipSummon),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onSellItem),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onSkill),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onSetProperty),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onAttackRequest),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onTakeItem),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onUseItem),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onDropItem),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onRevive),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onSoulStoneCraft),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onStorage),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onBindSkillCard),
                                          declareHandler(STATUS_AUTHED, &WorldSession::onDropQuest),
                                  };
constexpr int             tableSize       = (sizeof(packetHandler) / sizeof(WorldSessionHandler));

constexpr NGemity::Packets ignoredPackets[] =
                                   {
                                           NGemity::Packets::TS_CS_VERSION,
                                           NGemity::Packets::TS_CS_VERSION2,
                                           NGemity::Packets::TS_CS_UNKN,
                                           NGemity::Packets::TS_CS_REPORT,
                                           NGemity::Packets::TS_CS_TARGETING
                                   };

/// Handler for incoming packets
ReadDataHandlerResult WorldSession::ProcessIncoming(XPacket *pRecvPct)
{
            ASSERT(pRecvPct);

    auto _cmd = pRecvPct->GetPacketID();
    int  i    = 0;

    for (i = 0; i < tableSize; i++)
    {
        if ((uint16_t)packetHandler[i].cmd == _cmd && (packetHandler[i].status == STATUS_CONNECTED || (_isAuthed && packetHandler[i].status == STATUS_AUTHED)))
        {
            packetHandler[i].handler(this, pRecvPct);
            break;
        }
    }

    // Report unknown packets in the error log
    if (i == tableSize && std::find(std::begin(ignoredPackets), std::end(ignoredPackets), (NGemity::Packets)_cmd) == std::end(ignoredPackets))
    {
        NG_LOG_DEBUG("network", "Got unknown packet '%d' from '%s'", pRecvPct->GetPacketID(), _socket->GetRemoteIpAddress().to_string().c_str());
        return ReadDataHandlerResult::Ok;
    }
    return ReadDataHandlerResult::Ok;
}

/// TODO: The whole stuff needs a rework, it is working as intended but it's just a dirty hack
void WorldSession::onAccountWithAuth(const TS_CS_ACCOUNT_WITH_AUTH *pGamePct)
{
    auto szAccount = pGamePct->account;
    std::transform(std::begin(szAccount), std::end(szAccount), std::begin(szAccount), ::tolower);
    sAuthNetwork.SendAccountToAuth(*this, szAccount, pGamePct->one_time_key);
}

void WorldSession::_SendResultMsg(uint16 _msg, uint16 _result, int _value)
{
    TS_SC_RESULT resultPct{ };
    resultPct.request_msg_id = _msg;
    resultPct.result         = _result;
    resultPct.value          = _value;
    _socket->SendPacket(resultPct);
}

void WorldSession::onCharacterList(const TS_CS_CHARACTER_LIST */*pGamePct*/)
{
    TS_SC_CHARACTER_LIST characterPct{ };
    characterPct.current_server_time = sWorld.GetArTime();
    characterPct.last_character_idx  = 0;
    _PrepareCharacterList(_accountId, &characterPct.characters);
    _socket->SendPacket(characterPct);
}

/// TODO: Might need to put this in player class?
void WorldSession::_PrepareCharacterList(uint32 account_id, std::vector<LOBBY_CHARACTER_INFO> *_info)
{
    PreparedStatement *stmt = CharacterDatabase.GetPreparedStatement(CHARACTER_GET_CHARACTERLIST);
    stmt->setInt32(0, account_id);
    if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
    {
        do
        {
            LOBBY_CHARACTER_INFO info{ };
            int                  sid = (*result)[0].GetInt32();
            info.name           = (*result)[1].GetString();
            info.race           = (*result)[2].GetInt32();
            info.sex            = (*result)[3].GetInt32();
            info.level          = (*result)[4].GetInt32();
            info.job_level      = (*result)[5].GetInt32();
            info.exp_percentage = (*result)[6].GetInt32();
            info.hp             = (*result)[7].GetInt32();
            info.mp             = (*result)[8].GetInt32();
            info.job            = (*result)[9].GetInt32();
            info.permission     = (*result)[10].GetInt32();
            info.skin_color     = (*result)[11].GetUInt32();
            for (int i = 0; i < 5; i++)
            {
                info.model_id[i] = (*result)[12 + i].GetInt32();
            }
            info.szCreateTime = (*result)[17].GetString();
            info.szDeleteTime = (*result)[18].GetString();
            PreparedStatement *wstmt = CharacterDatabase.GetPreparedStatement(CHARACTER_GET_WEARINFO);
            wstmt->setInt32(0, sid);
            if (PreparedQueryResult wresult = CharacterDatabase.Query(wstmt))
            {
                do
                {
                    int wear_info = (*wresult)[0].GetInt32();
                    info.wear_info[wear_info]              = (*wresult)[1].GetInt32();
                    info.wear_item_enhance_info[wear_info] = (*wresult)[2].GetInt32();
                    info.wear_item_level_info[wear_info]   = (*wresult)[3].GetInt32();
                } while (wresult->NextRow());
            }
            _info->emplace_back(info);
        } while (result->NextRow());
    }
}

void WorldSession::onAuthResult(const TS_AG_CLIENT_LOGIN *pRecvPct)
{
    if (pRecvPct->result == TS_RESULT_SUCCESS)
    {
        _isAuthed     = true;
        _accountId    = pRecvPct->nAccountID;
        _accountName  = pRecvPct->account;
        m_nPermission = pRecvPct->permission;
    }
    Messages::SendResult(this, NGemity::Packets::TS_CS_ACCOUNT_WITH_AUTH, pRecvPct->result, 0);
}

void WorldSession::onLogin(const TS_CS_LOGIN *pRecvPct)
{
    m_pPlayer = sMemoryPool.AllocPlayer();
    m_pPlayer->SetSession(this);
    if (!m_pPlayer->ReadCharacter(pRecvPct->name, _accountId))
    {
        m_pPlayer->DeleteThis();
        return;
    }

    Messages::SendTimeSynch(m_pPlayer);
    sScriptingMgr.RunString(m_pPlayer, string_format("on_login('%s')", m_pPlayer->GetName()));

    TS_SC_LOGIN_RESULT resultPct{ };
    resultPct.result         = 1;
    resultPct.handle         = m_pPlayer->GetHandle();
    resultPct.x              = m_pPlayer->GetPositionX();
    resultPct.y              = m_pPlayer->GetPositionY();
    resultPct.z              = m_pPlayer->GetPositionZ();
    resultPct.layer          = m_pPlayer->GetLayer();
    resultPct.face_direction = m_pPlayer->GetOrientation();
    resultPct.region_size    = sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE);
    resultPct.hp             = m_pPlayer->GetHealth();
    resultPct.mp             = m_pPlayer->GetMana();
    resultPct.max_hp         = m_pPlayer->GetMaxHealth();
    resultPct.max_mp         = m_pPlayer->GetMaxMana();
    resultPct.havoc          = m_pPlayer->GetInt32Value(UNIT_FIELD_HAVOC);
    resultPct.max_havoc      = m_pPlayer->GetInt32Value(UNIT_FIELD_HAVOC);
    resultPct.sex            = m_pPlayer->GetInt32Value(UNIT_FIELD_SEX);
    resultPct.race           = m_pPlayer->GetRace();
    resultPct.skin_color     = m_pPlayer->GetUInt32Value(UNIT_FIELD_SKIN_COLOR);
    resultPct.faceId         = m_pPlayer->GetInt32Value(UNIT_FIELD_MODEL + 1);
    resultPct.hairId         = m_pPlayer->GetInt32Value(UNIT_FIELD_MODEL);
    resultPct.name           = pRecvPct->name;
    resultPct.cell_size      = sWorld.getIntConfig(CONFIG_CELL_SIZE);
    resultPct.guild_id       = m_pPlayer->GetGuildID();

    GetSocket()->SendPacket(resultPct);
    m_pPlayer->SendLoginProperties();
}

void WorldSession::onMoveRequest(const TS_CS_MOVE_REQUEST *pRecvPct)
{

    std::vector<Position> vPctInfo{ }, vMoveInfo{ };

    if (m_pPlayer == nullptr || m_pPlayer->GetHealth() == 0 || !m_pPlayer->IsInWorld() || pRecvPct->move_infos.size() == 0)
        return;

    for (const auto &mi : pRecvPct->move_infos)
    {
        Position pos{ };
        pos.SetCurrentXY(mi.tx, mi.ty);
        vPctInfo.push_back(pos);
    }

    int      speed;
    float    distance;
    Position npos{ };
    Position curPosFromClient{ };
    Position wayPoint{ };

    uint ct = sWorld.GetArTime();
    speed = m_pPlayer->GetMoveSpeed() / 7;
    auto mover = dynamic_cast<Unit *>(m_pPlayer);

    if (pRecvPct->handle == 0 || pRecvPct->handle == m_pPlayer->GetHandle())
    {
        // Set Speed if ride
    }
    else
    {
        mover = m_pPlayer->GetSummonByHandle(pRecvPct->handle);
        if (mover != nullptr && mover->GetHandle() == pRecvPct->handle)
        {
            npos.m_positionX = pRecvPct->x;
            npos.m_positionY = pRecvPct->y;
            npos.m_positionZ = 0;

            distance = npos.GetExactDist2d(m_pPlayer);
            if (distance >= 1800.0f)
            {
                Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_TOO_FAR, 0);
                return;
            }

            if (distance < 120.0f)
            {
                speed = (int)((float)speed * 1.1f);
            }
            else
            {
                speed = (int)((float)speed * 2.0f);
            }
        }
    }

    if (mover == nullptr)
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_EXIST, 0);
        return;
    }

    npos.m_positionX = pRecvPct->x;
    npos.m_positionY = pRecvPct->y;
    npos.m_positionZ = 0.0f;

    if (pRecvPct->x < 0.0f || sWorld.getIntConfig(CONFIG_MAP_WIDTH) < pRecvPct->x
        || pRecvPct->y < 0.0f || sWorld.getIntConfig(CONFIG_MAP_HEIGHT) < pRecvPct->y
        || mover->GetExactDist2d(&npos) > 525.0f)
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_ACCESS_DENIED, 0);
        return;
    }
    if (speed < 1)
        speed = 1;

    wayPoint.m_positionX  = pRecvPct->x;
    wayPoint.m_positionY  = pRecvPct->y;
    wayPoint.m_positionZ  = 0.0f;
    wayPoint._orientation = 0.0f;

    for (auto &mi : vPctInfo)
    {
        if (mover->IsPlayer() && GameContent::CollisionToLine(wayPoint.GetPositionX(), wayPoint.GetPositionY(), mi.GetPositionX(), mi.GetPositionY()))
        {
            Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_ACCESS_DENIED, 0);
            return;
        }
        curPosFromClient.m_positionX = mi.m_positionX;
        curPosFromClient.m_positionY = mi.m_positionY;
        curPosFromClient.m_positionZ = 0.0f;
        wayPoint.m_positionX         = curPosFromClient.m_positionX;
        wayPoint.m_positionY         = curPosFromClient.m_positionY;
        wayPoint.m_positionZ         = curPosFromClient.m_positionZ;
        wayPoint._orientation        = curPosFromClient._orientation;
        if (mi.m_positionX < 0.0f || sWorld.getIntConfig(CONFIG_MAP_WIDTH) < mi.m_positionX ||
            mi.m_positionY < 0.0f || sWorld.getIntConfig(CONFIG_MAP_HEIGHT) < mi.m_positionY)
        {
            Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_ACCESS_DENIED, 0);
            return;
        }
        vMoveInfo.emplace_back(wayPoint);
    }

    if (vMoveInfo.empty())
        return;

    Position cp = vMoveInfo.back();
    if (mover->IsPlayer() && GameContent::IsBlocked(cp.GetPositionX(), cp.GetPositionY()))
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_ACCESS_DENIED, 0);
        return;
    }

    if (mover->IsInWorld())
    {
        if (mover->GetTargetHandle() != 0)
            mover->CancelAttack();
        if (mover->m_nMovableTime <= ct)
        {
            if (mover->IsActable() && mover->IsMovable() && mover->IsInWorld())
            {
                auto tpos2 = mover->GetCurrentPosition(pRecvPct->cur_time);
                if (!vMoveInfo.empty())
                {
                    cp = vMoveInfo.back();
                    npos.m_positionX  = cp.GetPositionX();
                    npos.m_positionY  = cp.GetPositionY();
                    npos.m_positionZ  = cp.GetPositionZ();
                    npos._orientation = cp.GetOrientation();
                }
                else
                {
                    npos.m_positionX  = 0.0f;
                    npos.m_positionY  = 0.0f;
                    npos.m_positionZ  = 0.0f;
                    npos._orientation = 0.0f;
                }
                if (mover->GetHandle() != m_pPlayer->GetHandle()
                    || sWorld.getFloatConfig(CONFIG_MAP_LENGTH) / 5.0 >= tpos2.GetExactDist2d(&cp)
                    /*|| !m_pPlayer.m_bAutoUsed*/
                    || m_pPlayer->m_nWorldLocationId != 110900)
                {
                    if (vMoveInfo.empty() || sWorld.getFloatConfig(CONFIG_MAP_LENGTH) >= m_pPlayer->GetCurrentPosition(ct).GetExactDist2d(&npos))
                    {
                        if (mover->HasFlag(UNIT_FIELD_STATUS, STATUS_MOVE_PENDED))
                            mover->RemoveFlag(UNIT_FIELD_STATUS, STATUS_MOVE_PENDED);
                        npos.m_positionX = pRecvPct->x;
                        npos.m_positionY = pRecvPct->y;
                        npos.m_positionZ = 0.0f;

                        sWorld.SetMultipleMove(mover, npos, vMoveInfo, speed, true, ct, true);
                        // TODO: Mount
                    }
                }
                return;
            } //if (true /* IsActable() && IsMovable() && isInWorld*/)
            Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_ACTABLE, 0);
            return;
        }
        if (!mover->SetPendingMove(vMoveInfo, (uint8_t)speed))
        {
            Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_ACTABLE, 0);
            return;
        }
    } // is in world
}

void WorldSession::onReturnToLobby(const TS_CS_RETURN_LOBBY *pRecvPct)
{
    if (m_pPlayer != nullptr)
    {
        m_pPlayer->LogoutNow(2);
        m_pPlayer->CleanupsBeforeDelete();
        m_pPlayer->DeleteThis();
        m_pPlayer = nullptr;
    }
    if (pRecvPct != nullptr)
        _SendResultMsg(pRecvPct->id, 0, 0);
}

void WorldSession::onCreateCharacter(const TS_CS_CREATE_CHARACTER *pRecvPct)
{
    if (checkCharacterName(pRecvPct->character.name))
    {
        uint8_t           j       = 0;
        PreparedStatement *stmt   = CharacterDatabase.GetPreparedStatement(CHARACTER_ADD_CHARACTER);
        stmt->setString(j++, pRecvPct->character.name);
        stmt->setString(j++, _accountName);
        stmt->setInt32(j++, _accountId);
        stmt->setInt32(j++, 0);
        stmt->setInt32(j++, 0);
        stmt->setInt32(j++, 0);
        stmt->setInt32(j++, 0);
        stmt->setInt32(j++, 0);
        stmt->setInt32(j++, pRecvPct->character.race);
        stmt->setInt32(j++, pRecvPct->character.sex);
        stmt->setInt32(j++, 0);
        stmt->setInt32(j++, pRecvPct->character.job);
        stmt->setInt32(j++, pRecvPct->character.job_level);
        stmt->setInt32(j++, pRecvPct->character.exp_percentage);
        stmt->setInt32(j++, 320);
        stmt->setInt32(j++, 320);
        stmt->setUInt32(j++, pRecvPct->character.skin_color);
        for (const auto &i : pRecvPct->character.model_id)
        {
            stmt->setUInt32(j++, i);
        }
        auto            playerUID = sWorld.GetPlayerIndex();
        stmt->setUInt32(j, playerUID);
        CharacterDatabase.Query(stmt);

        int m_wear_item       = pRecvPct->character.wear_info[2];
        int nDefaultBagCode   = 490001;
        int nDefaultArmorCode = 220100;
        if (m_wear_item == 602)
            nDefaultArmorCode = 220109;

        int nDefaultWeaponCode = 106100;
        if (pRecvPct->character.race == 3)
        {
            nDefaultArmorCode     = 240100;
            if (m_wear_item == 602)
                nDefaultArmorCode = 240109;
            nDefaultWeaponCode    = 112100;
        }
        else
        {
            if (pRecvPct->character.race == 5)
            {
                nDefaultArmorCode     = 230100;
                if (m_wear_item == 602)
                    nDefaultArmorCode = 230109;
                nDefaultWeaponCode    = 103100;
            }
        }

        auto itemStmt = CharacterDatabase.GetPreparedStatement(CHARACTER_ADD_DEFAULT_ITEM);
        itemStmt->setInt32(0, sWorld.GetItemIndex());
        itemStmt->setInt32(1, playerUID);
        itemStmt->setInt32(2, nDefaultWeaponCode);
        itemStmt->setInt32(3, WEAR_WEAPON);
        CharacterDatabase.Execute(itemStmt);

        itemStmt = CharacterDatabase.GetPreparedStatement(CHARACTER_ADD_DEFAULT_ITEM);
        itemStmt->setInt32(0, sWorld.GetItemIndex());
        itemStmt->setInt32(1, playerUID);
        itemStmt->setInt32(2, nDefaultArmorCode);
        itemStmt->setInt32(3, WEAR_ARMOR);
        CharacterDatabase.Execute(itemStmt);

        itemStmt = CharacterDatabase.GetPreparedStatement(CHARACTER_ADD_DEFAULT_ITEM);
        itemStmt->setInt32(0, sWorld.GetItemIndex());
        itemStmt->setInt32(1, playerUID);
        itemStmt->setInt32(2, nDefaultBagCode);
        itemStmt->setInt32(3, WEAR_BAG_SLOT);
        CharacterDatabase.Execute(itemStmt);

        _SendResultMsg(pRecvPct->id, TS_RESULT_SUCCESS, 0);
        return;
    }
    _SendResultMsg(pRecvPct->id, TS_RESULT_ALREADY_EXIST, 0);
}

bool WorldSession::checkCharacterName(const std::string &szName)
{
    PreparedStatement *stmt = CharacterDatabase.GetPreparedStatement(CHARACTER_GET_NAMECHECK);
    stmt->setString(0, szName);
    if (PreparedQueryResult result = CharacterDatabase.Query(stmt))
    {
        return false;
    }
    return true;
}

void WorldSession::onCharacterName(const TS_CS_CHECK_CHARACTER_NAME *pRecvPct)
{
    if (!checkCharacterName(pRecvPct->name))
    {
        _SendResultMsg(pRecvPct->id, TS_RESULT_ALREADY_EXIST, 0);
        return;
    }
    _SendResultMsg(pRecvPct->id, TS_RESULT_SUCCESS, 0);
}

void WorldSession::onChatRequest(const TS_CS_CHAT_REQUEST *pRectPct)
{
    if (pRectPct->type != CHAT_WHISPER && pRectPct->message[0] == 47)
    {
        sAllowedCommandInfo.Run(m_pPlayer, pRectPct->message);
        return;
    }

    switch (pRectPct->type)
    {
        // local chat message: msg
        case CHAT_NORMAL:
            Messages::SendLocalChatMessage(0, m_pPlayer->GetHandle(), pRectPct->message, 0);
            break;
            // Ad chat message: $msg
        case CHAT_ADV:
            Messages::SendGlobalChatMessage(2, m_pPlayer->GetName(), pRectPct->message, 0);
            break;

            // Whisper chat message: "player msg
        case CHAT_WHISPER:
        {
            auto target = Player::FindPlayer(pRectPct->szTarget);
            if (target != nullptr)
            {
                // Todo: Denal check
                Messages::SendChatMessage((m_pPlayer->GetPermission() > 0 ? 7 : 3), m_pPlayer->GetName(), target, pRectPct->message);
                Messages::SendResult(m_pPlayer, pRectPct->id, TS_RESULT_SUCCESS, 0);
                return;
            }
            Messages::SendResult(m_pPlayer, pRectPct->id, TS_RESULT_NOT_EXIST, 0);
        }
            break;
            // Global chat message: !msg
        case CHAT_GLOBAL:
            Messages::SendGlobalChatMessage(m_pPlayer->GetPermission() > 0 ? 6 : 4, m_pPlayer->GetName(), pRectPct->message, 0);
            break;
        case CHAT_PARTY:
        {
            if (m_pPlayer->GetPartyID() != 0)
            {
                sGroupManager.DoEachMemberTag(m_pPlayer->GetPartyID(), [=, &pRectPct](PartyMemberTag &tag) {
                    if (tag.bIsOnline)
                    {
                        auto player = Player::FindPlayer(tag.strName);
                        if (player != nullptr)
                        {
                            Messages::SendChatMessage(0xA, m_pPlayer->GetName(), player, pRectPct->message);
                        }
                    }
                });
            }
        }
            break;
        default:
            break;
    }
}

void WorldSession::onLogoutTimerRequest(const TS_CS_REQUEST_LOGOUT *pRecvPct)
{
    Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_SUCCESS, 0);
}

void WorldSession::onPutOnItem(const TS_CS_PUTON_ITEM *pRecvPct)
{
    if (m_pPlayer->GetHealth() != 0)
    {
        auto ci = sMemoryPool.GetObjectInWorld<Item>(pRecvPct->item_handle);
        if (ci != nullptr)
        {
            if (!ci->IsWearable() || m_pPlayer->FindItemBySID(ci->m_Instance.UID) == nullptr)
            {
                Messages::SendResult(m_pPlayer, NGemity::Packets::TS_CS_PUTON_ITEM, TS_RESULT_ACCESS_DENIED, 0);
                return;
            }

            auto unit = m_pPlayer->As<Unit>();
            if (pRecvPct->target_handle != 0)
            {
                auto summon = sMemoryPool.GetObjectInWorld<Summon>(pRecvPct->target_handle);
                if (summon == nullptr || summon->GetMaster()->GetHandle() != m_pPlayer->GetHandle())
                {
                    Messages::SendResult(m_pPlayer, NGemity::Packets::TS_CS_PUTON_ITEM, TS_RESULT_NOT_EXIST, 0);
                    return;
                }
                unit = summon;
            }

            if (unit->Puton((ItemWearType)pRecvPct->position, ci) == 0)
            {
                unit->CalculateStat();
                Messages::SendStatInfo(m_pPlayer, unit);
                Messages::SendResult(m_pPlayer, NGemity::Packets::TS_CS_PUTON_ITEM, TS_RESULT_SUCCESS, 0);
                if (unit->IsPlayer())
                {
                    m_pPlayer->SendWearInfo();
                }
            }
        }
    }
}

void WorldSession::onPutOffItem(const TS_CS_PUTOFF_ITEM *pRecvPct)
{
    if (m_pPlayer->GetHealth() == 0)
    {
        Messages::SendResult(m_pPlayer, NGemity::Packets::TS_CS_PUTOFF_ITEM, 5, 0);
        return;
    }

    auto unit = m_pPlayer->As<Unit>();
    if (pRecvPct->target_handle != 0)
    {
        auto summon = sMemoryPool.GetObjectInWorld<Summon>(pRecvPct->target_handle);
        if (summon == nullptr || summon->GetMaster()->GetHandle() != m_pPlayer->GetHandle())
        {
            Messages::SendResult(m_pPlayer, NGemity::Packets::TS_CS_PUTON_ITEM, TS_RESULT_NOT_EXIST, 0);
            return;
        }
        unit = summon;
    }

    Item *curitem = unit->GetWornItem((ItemWearType)pRecvPct->position);
    if (curitem == nullptr)
    {
        Messages::SendResult(m_pPlayer, NGemity::Packets::TS_CS_PUTOFF_ITEM, 1, 0);
    }
    else
    {
        uint16_t por = unit->Putoff((ItemWearType)pRecvPct->position);
        unit->CalculateStat();
        Messages::SendStatInfo(m_pPlayer, unit);
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_SUCCESS, 0);
        if (por == 0)
        {
            if (unit->IsPlayer())
            {
                m_pPlayer->SendWearInfo();
            }
        }
    }
}

void WorldSession::onRegionUpdate(const TS_CS_REGION_UPDATE *pRecvPct)
{
    if (m_pPlayer == nullptr)
        return;

    if (m_pPlayer->IsInWorld())
    {
        sWorld.onRegionChange(m_pPlayer, pRecvPct->update_time, pRecvPct->bIsStopMessage);
    }
}

void WorldSession::onGetSummonSetupInfo(const TS_CS_GET_SUMMON_SETUP_INFO *pRecvPct)
{
    Messages::SendCreatureEquipMessage(m_pPlayer, pRecvPct->show_dialog);
}

void WorldSession::onContact(const TS_CS_CONTACT *pRecvPct)
{
    auto npc = sMemoryPool.GetObjectInWorld<NPC>(pRecvPct->handle);

    if (npc != nullptr)
    {
        m_pPlayer->SetLastContact("npc", pRecvPct->handle);
        sScriptingMgr.RunString(m_pPlayer, npc->m_pBase->contact_script);
    }
}

void WorldSession::onDialog(const TS_CS_DIALOG *pRecvPct)
{
    if (pRecvPct->trigger.empty())
        return;

    if (!m_pPlayer->IsValidTrigger(pRecvPct->trigger))
    {
        if (!m_pPlayer->IsFixedDialogTrigger(pRecvPct->trigger))
        {
            NG_LOG_ERROR("scripting", "INVALID SCRIPT TRIGGER!!! [%s][%s]", m_pPlayer->GetName(), pRecvPct->trigger.c_str());
            return;
        }
    }

    //auto npc = dynamic_cast<NPC *>(sMemoryPool.getPtrFromId(m_pPlayer->GetLastContactLong("npc")));
    auto npc = sMemoryPool.GetObjectInWorld<NPC>(m_pPlayer->GetLastContactLong("npc"));
    if (npc == nullptr)
    {
        NG_LOG_TRACE("scripting", "onDialog: NPC not found!");
        return;
    }

    sScriptingMgr.RunString(m_pPlayer, pRecvPct->trigger);
    if (m_pPlayer->HasDialog())
        m_pPlayer->ShowDialog();
}

void WorldSession::onBuyItem(const TS_CS_BUY_ITEM *pRecvPct)
{
    auto szMarketName = m_pPlayer->GetLastContactStr("market");
    auto buy_count    = pRecvPct->buy_count;
    if (buy_count == 0)
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_UNKNOWN, 0);
        return;
    }

    auto market = sObjectMgr.GetMarketInfo(szMarketName);
    if (market->empty())
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_UNKNOWN, 0);
        return;
    }

    bool bJoinable{false};
    Item *pNewItem{nullptr};

    for (auto &mt : *market)
    {
        if (mt.code == pRecvPct->item_code)
        {
            auto ibs = sObjectMgr.GetItemBase((uint32_t)pRecvPct->item_code);
            if (ibs == nullptr)
                continue;
            if (ibs->flaglist[FLAG_DUPLICATE] == 1)
            {
                bJoinable = true;
            }
            else
            {
                bJoinable     = false;
                if (buy_count != 1)
                    buy_count = 1;
            }

            auto nTotalPrice = (int)floor(buy_count * mt.price_ratio);
            if (nTotalPrice / buy_count != mt.price_ratio || m_pPlayer->GetGold() < nTotalPrice || nTotalPrice < 0)
            {
                Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_ENOUGH_MONEY, 0);
                return;
            }

            if (m_pPlayer->m_Attribute.nMaxWeight - m_pPlayer->GetFloatValue(PLAYER_FIELD_WEIGHT) < ibs->weight * buy_count)
            {
                Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_TOO_HEAVY, pRecvPct->item_code);
                return;
            }
            uint32_t uid{0};

            auto result = m_pPlayer->ChangeGold(m_pPlayer->GetGold() - nTotalPrice);
            if (result != TS_RESULT_SUCCESS)
            {
                Messages::SendResult(m_pPlayer, pRecvPct->id, result, 0);
                return;
            }

            if (bJoinable)
            {
                auto item = Item::AllocItem(0, mt.code, buy_count, BY_MARKET, -1, -1, -1, 0, 0, 0, 0, 0);
                if (item == nullptr)
                {
                    NG_LOG_ERROR("entities.item", "ItemID Invalid! %d", mt.code);
                    return;
                }
                pNewItem = m_pPlayer->PushItem(item, buy_count, false);

                if (pNewItem != nullptr && pNewItem->GetHandle() != item->GetHandle())
                    Item::PendFreeItem(item);
            }
            else
            {
                for (int i = 0; i < buy_count; i++)
                {
                    auto item = Item::AllocItem(0, mt.code, 1, BY_MARKET, -1, -1, -1, 0, 0, 0, 0, 0);
                    if (item == nullptr)
                    {
                        NG_LOG_ERROR("entities.item", "ItemID Invalid! %d", mt.code);
                        return;
                    }
                    pNewItem = m_pPlayer->PushItem(item, buy_count, false);
                    if (pNewItem != nullptr && pNewItem->GetHandle() != item->GetHandle())
                        Item::PendFreeItem(item);
                }
            }
            Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_SUCCESS, pRecvPct->item_code);
            TS_SC_NPC_TRADE_INFO resultPct{ };
            resultPct.is_sell          = false;
            resultPct.code             = pRecvPct->item_code;
            resultPct.count            = buy_count;
            resultPct.price            = nTotalPrice;
            resultPct.huntaholic_point = mt.huntaholic_ratio;
            resultPct.target           = m_pPlayer->GetLastContactLong("npc");
            GetSocket()->SendPacket(resultPct);
        }
    }

}

void WorldSession::onDeleteCharacter(const TS_CS_DELETE_CHARACTER *pRecvPct)
{
    auto stmt = CharacterDatabase.GetPreparedStatement(CHARACTER_DEL_CHARACTER);
    stmt->setString(0, pRecvPct->name);
    stmt->setInt32(1, _accountId);
    CharacterDatabase.Execute(stmt);
    // Send result message with WorldSession, player is not set yet
    Messages::SendResult(this, pRecvPct->id, TS_RESULT_SUCCESS, 0);
}

void WorldSession::onChangeLocation(const TS_CS_CHANGE_LOCATION *pRecvPct)
{
    m_pPlayer->ChangeLocation(pRecvPct->x, pRecvPct->y, true, true);
}

void WorldSession::onTimeSync(const TS_TIMESYNC *pRecvPct)
{
    uint ct = sWorld.GetArTime();
    m_pPlayer->m_TS.onEcho(ct - pRecvPct->time);
    if (m_pPlayer->m_TS.m_vT.size() >= 4)
    {
        TS_SC_SET_TIME timePct{ };
        timePct.gap = m_pPlayer->m_TS.GetInterval();
        GetSocket()->SendPacket(timePct);
    }
    else
    {
        Messages::SendTimeSynch(m_pPlayer);
    }
}

void WorldSession::onGameTime(const TS_CS_GAME_TIME */*pRecvPct*/)
{
    Messages::SendGameTime(m_pPlayer);
}

void WorldSession::onQuery(const TS_CS_QUERY *pRecvPct)
{
    auto obj = sMemoryPool.GetObjectInWorld<WorldObject>(pRecvPct->handle);
    if (obj != nullptr && obj->IsInWorld() && obj->GetLayer() == m_pPlayer->GetLayer() && sRegion.IsVisibleRegion(obj, m_pPlayer) != 0)
    {
        Messages::sendEnterMessage(m_pPlayer, obj, false);
    }
}

void WorldSession::onUpdate(const TS_CS_UPDATE *pRecvPct)
{
    auto unit = dynamic_cast<Unit *>(m_pPlayer);
    if (pRecvPct->handle != m_pPlayer->GetHandle())
    {
        // Do Summon stuff here
    }
    if (unit != nullptr)
    {
        unit->OnUpdate();
        return;
    }
}

void WorldSession::onJobLevelUp(const TS_CS_JOB_LEVEL_UP *pRecvPct)
{
    auto cr = m_pPlayer->As<Unit>();
    if (cr == nullptr)
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_EXIST, pRecvPct->target);
        return;
    }
    if (cr->IsPlayer() && cr->GetHandle() != m_pPlayer->GetHandle())
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_ACCESS_DENIED, pRecvPct->target);
        return;
    }
    int jp = sObjectMgr.GetNeedJpForJobLevelUp(cr->GetCurrentJLv(), m_pPlayer->GetJobDepth());
    if (cr->GetJP() < jp)
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_ENOUGH_JP, pRecvPct->target);
        return;
    }
    if (jp == 0)
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_LIMIT_MAX, pRecvPct->target);
        return;
    }
    cr->SetJP(cr->GetJP() - jp);
    cr->SetCurrentJLv(cr->GetCurrentJLv() + 1);
    cr->CalculateStat();
    if (cr->IsPlayer())
    {
        dynamic_cast<Player *>(cr)->Save(true);
    }
    else
    {
        // Summon
    }

    m_pPlayer->Save(true);
    Messages::SendPropertyMessage(m_pPlayer, cr, "job_level", cr->GetCurrentJLv());
    Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_SUCCESS, pRecvPct->target);
}

void WorldSession::onLearnSkill(const TS_CS_LEARN_SKILL *pRecvPct)
{
    if (m_pPlayer == nullptr)
        return;

    auto   target       = m_pPlayer->As<Unit>();
    ushort result{ };
    int    jobID{ };
    int    value{ };

    if (m_pPlayer->GetHandle() != pRecvPct->target)
    {
        auto summon = sMemoryPool.GetObjectInWorld<Summon>(pRecvPct->target);
        if (summon == nullptr || !summon->IsSummon() || summon->GetMaster() == nullptr || summon->GetMaster()->GetHandle() != m_pPlayer->GetHandle())
        {
            Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_ACCESS_DENIED, 0);
            return;
        }
        target = summon;
    }
    int    currentLevel = target->GetBaseSkillLevel(pRecvPct->skill_id) + 1;
    //if(skill_level == currentLevel)
    //{
    result = GameContent::IsLearnableSkill(target, pRecvPct->skill_id, currentLevel, jobID);
    if (result == TS_RESULT_SUCCESS)
    {
        target->RegisterSkill(pRecvPct->skill_id, currentLevel, 0, jobID);
        target->CalculateStat();
    }
    Messages::SendResult(m_pPlayer, pRecvPct->id, result, value);
    //}
}

void WorldSession::onEquipSummon(const TS_EQUIP_SUMMON *pRecvPct)
{
    if (m_pPlayer == nullptr)
        return;

    if (false /*IsItemUseable()*/)
        return;

    int nCFL = m_pPlayer->GetCurrentSkillLevel(SKILL_CREATURE_CONTROL);
    if (nCFL < 0)
        return;

    if (nCFL > 6)
        nCFL = 6;

    Item     *pItem  = nullptr;
    Summon   *summon = nullptr;
    for (int i       = 0; i < nCFL; ++i)
    {
        bool bFound = false;
        pItem = nullptr;
        if (pRecvPct->card_handle[i] != 0)
        {
            pItem = m_pPlayer->FindItemByHandle(pRecvPct->card_handle[i]);
            if (pItem != nullptr && pItem->m_pItemBase != nullptr)
            {
                if (pItem->m_pItemBase->group != 13 ||
                    m_pPlayer->GetHandle() != pItem->m_Instance.OwnerHandle ||
                    (pItem->m_Instance.Flag & (uint)ITEM_FLAG_SUMMON) == 0)
                    continue;
            }
        }
        for (int j = 0; j < nCFL; j++)
        {
            if (pItem != nullptr)
            {
                // Belt Slot Card
            }
        }
        if (bFound)
            continue;

        if (m_pPlayer->m_aBindSummonCard[i] != nullptr)
        {
            if (pItem == nullptr || m_pPlayer->m_aBindSummonCard[i]->m_nHandle != pItem->m_nHandle)
            {
                summon = m_pPlayer->m_aBindSummonCard[i]->m_pSummon;
                if (pRecvPct->card_handle[i] == 0)
                    m_pPlayer->m_aBindSummonCard[i] = nullptr;
                if (summon != nullptr && !summon->IsInWorld())
                {
                    for (int k = 0; k < 24; ++k)
                    {
                        if (summon->GetWornItem((ItemWearType)k) != nullptr)
                            summon->Putoff((ItemWearType)k);
                    }
                }
            }
        }

        if (pItem != nullptr)
        {
            if ((pItem->m_Instance.Flag & ITEM_FLAG_SUMMON) != 0)
            {
                summon = pItem->m_pSummon;
                if (summon == nullptr)
                {
                    summon = sMemoryPool.AllocNewSummon(m_pPlayer, pItem);
                    summon->SetFlag(UNIT_FIELD_STATUS, STATUS_LOGIN_COMPLETE);
                    m_pPlayer->AddSummon(summon, true);
                    Messages::SendItemMessage(m_pPlayer, pItem);

                    Summon::DB_InsertSummon(m_pPlayer, summon);
                    sScriptingMgr.RunString(m_pPlayer, string_format("on_first_summon( %d, %d)", summon->GetSummonCode(), summon->GetHandle()));
                    summon->SetCurrentJLv(summon->GetLevel());
                    summon->CalculateStat();
                }
                summon->m_cSlotIdx = (uint8_t)i;
                summon->CalculateStat();
            }
            m_pPlayer->m_aBindSummonCard[i] = pItem;
        }
    }
    if (nCFL > 1)
    {
        for (int i = 0; i < nCFL; ++i)
        {
            if (m_pPlayer->m_aBindSummonCard[i] == nullptr)
            {
                for (int x = i + 1; x < nCFL; ++x)
                {
                    if (m_pPlayer->m_aBindSummonCard[x] != nullptr)
                    {
                        m_pPlayer->m_aBindSummonCard[i] = m_pPlayer->m_aBindSummonCard[x];
                        m_pPlayer->m_aBindSummonCard[x] = nullptr;
                    }
                }
            }
        }
    }
    Messages::SendCreatureEquipMessage(m_pPlayer, pRecvPct->open_dialog);
}

void WorldSession::onSellItem(const TS_CS_SELL_ITEM *pRecvPct)
{
    if (m_pPlayer == nullptr)
        return;

    auto item = m_pPlayer->FindItemByHandle(pRecvPct->handle);
    if (item == nullptr || item->m_pItemBase == nullptr || item->m_Instance.OwnerHandle != m_pPlayer->GetHandle() || !item->IsInInventory())
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_EXIST, 0);
        return;
    }
    if (pRecvPct->sell_count == 0)
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_UNKNOWN, 0);
        return;
    }
    //if(!m_pPlayer.IsSelllable) @todo

    auto nPrice        = GameContent::GetItemSellPrice(item->m_pItemBase->price, item->m_pItemBase->rank, item->m_Instance.nLevel, item->m_Instance.Code >= 602700 && item->m_Instance.Code <= 602799);
    auto nResultCount  = item->m_Instance.nCount - pRecvPct->sell_count;
    auto nEnhanceLevel = (item->m_Instance.nLevel + 100 * item->m_Instance.nEnhance);

    if (!m_pPlayer->IsSellable(item) || nResultCount < 0)
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_EXIST, item->m_Instance.Code);
        return;
    }
    if (m_pPlayer->GetGold() + pRecvPct->sell_count * nPrice > MAX_GOLD_FOR_INVENTORY)
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_TOO_MUCH_MONEY, item->m_Instance.Code);
        return;
    }
    if (m_pPlayer->GetGold() + pRecvPct->sell_count * nPrice < 0)
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_ACTABLE, item->m_Instance.Code);
        return;
    }
    auto code = item->m_Instance.Code;
    if (!m_pPlayer->EraseItem(item, pRecvPct->sell_count))
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_ACTABLE, item->GetHandle());
        return;
    }
    if (m_pPlayer->ChangeGold(m_pPlayer->GetGold() + pRecvPct->sell_count * nPrice) != 0)
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_TOO_MUCH_MONEY, item->m_Instance.Code);
        return;
    }

    Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_SUCCESS, item->GetHandle());
    TS_SC_NPC_TRADE_INFO tradePct{ };
    tradePct.is_sell = true;
    tradePct.code    = code;
    tradePct.code    = pRecvPct->sell_count;
    tradePct.price   = pRecvPct->sell_count * nPrice;
    tradePct.target  = m_pPlayer->GetLastContactLong("npc");
    m_pPlayer->SendPacket(tradePct);
}

void WorldSession::onSkill(const TS_CS_SKILL *pRecvPct)
{
    if (m_pPlayer == nullptr)
        return;

    if (m_pPlayer->GetHealth() == 0)
        return;

    WorldObject *pTarget{nullptr};
    Position    pos{ };
    pos.Relocate(pRecvPct->x, pRecvPct->y, pRecvPct->z);

    auto pCaster = m_pPlayer->As<Unit>();
    if (pRecvPct->caster != m_pPlayer->GetHandle())
        pCaster = m_pPlayer->GetSummonByHandle(pRecvPct->caster);

    if (pCaster == nullptr || !pCaster->IsInWorld())
    {
        Messages::SendSkillCastFailMessage(m_pPlayer, pRecvPct->caster, pRecvPct->target, pRecvPct->skill_id, pRecvPct->skill_level, pos, TS_RESULT_NOT_EXIST);
        return;
    }
    auto base = sObjectMgr.GetSkillBase(pRecvPct->skill_id);
    if (base == nullptr || base->id == 0 || base->is_valid == 0 || base->is_valid == 2)
    {
        Messages::SendSkillCastFailMessage(m_pPlayer, pRecvPct->caster, pRecvPct->target, pRecvPct->skill_id, pRecvPct->skill_level, pos, TS_RESULT_ACCESS_DENIED);
        return;
    }
    /// @todo isCastable
    if (pRecvPct->target != 0)
    {
        //pTarget = dynamic_cast<WorldObject*>(sMemoryPool.getPtrFromId(target));
        pTarget = sMemoryPool.GetObjectInWorld<WorldObject>(pRecvPct->target);
        if (pTarget == nullptr)
        {
            Messages::SendSkillCastFailMessage(m_pPlayer, pRecvPct->caster, pRecvPct->target, pRecvPct->skill_id, pRecvPct->skill_level, pos, TS_RESULT_NOT_EXIST);
            return;
        }
    }

    auto ct = sWorld.GetArTime();
    if (pCaster->IsMoving(ct))
    {
        Messages::SendSkillCastFailMessage(m_pPlayer, pRecvPct->caster, pRecvPct->target, pRecvPct->skill_id, pRecvPct->skill_level, pos, TS_RESULT_NOT_ACTABLE);
        return;
    }

    // @todo is_spell_act
    auto skill = pCaster->GetSkill(pRecvPct->skill_id);
    if (skill != nullptr && skill->m_nSkillUID != -1)
    {
        //if(skill_level > skill->skill_level /* +skill.m_nSkillLevelAdd*/)
        //skill_level = skill_level + skill.m_nSkillLevelAdd;
        int res = pCaster->CastSkill(pRecvPct->skill_id, pRecvPct->skill_level, pRecvPct->target, pos, pCaster->GetLayer(), false);
        if (res != 0)
        {
            Messages::SendSkillCastFailMessage(m_pPlayer, pRecvPct->caster, pRecvPct->target, pRecvPct->skill_id, pRecvPct->skill_level, pos, res);
            if (skill->m_SkillBase->is_harmful != 0 && pCaster->GetTargetHandle() != 0)
                pCaster->StartAttack(pRecvPct->target, false);
        }
    }
}

void WorldSession::onSetProperty(const TS_CS_SET_PROPERTY *pRecvPct)
{
    if (pRecvPct->name != "client_info"s)
        return;

    std::string value = pRecvPct->string_value;
    m_pPlayer->SetClientInfo(value);
}

void WorldSession::KickPlayer()
{
    if (_socket)
        _socket->CloseSocket();
}

void WorldSession::onAttackRequest(const TS_CS_ATTACK_REQUEST *pRecvPct)
{
    if (m_pPlayer == nullptr)
        return;

    if (m_pPlayer->GetHealth() == 0)
        return;

    auto unit = dynamic_cast<Unit *>(m_pPlayer);
    if (pRecvPct->handle != m_pPlayer->GetHandle())
        unit = m_pPlayer->GetSummonByHandle(pRecvPct->handle);
    if (unit == nullptr)
    {
        Messages::SendCantAttackMessage(m_pPlayer, pRecvPct->handle, pRecvPct->target_handle, TS_RESULT_NOT_OWN);
        return;
    }

    if (pRecvPct->target_handle == 0)
    {
        if (unit->GetTargetHandle() != 0)
            unit->CancelAttack();
        return;
    }

    auto pTarget = sMemoryPool.GetObjectInWorld<Unit>(pRecvPct->target_handle);
    if (pTarget == nullptr)
    {
        if (unit->GetTargetHandle() != 0)
        {
            unit->EndAttack();
            return;
        }
        Messages::SendCantAttackMessage(m_pPlayer, pRecvPct->handle, pRecvPct->target_handle, TS_RESULT_NOT_EXIST);
        return;
    }

    if (!unit->IsEnemy(pTarget, false))
    {
        if (unit->GetTargetHandle() != 0)
        {
            unit->EndAttack();
            return;
        }
        Messages::SendCantAttackMessage(m_pPlayer, pRecvPct->id, pRecvPct->target_handle, 0);
        return;
    }

    if ((unit->IsUsingCrossBow() || unit->IsUsingBow()) && unit->IsPlayer() && unit->GetBulletCount() < 1)
    {
        Messages::SendCantAttackMessage(m_pPlayer, pRecvPct->id, pRecvPct->target_handle, 0);
        return;
    }

    unit->StartAttack(pRecvPct->target_handle, true);
}

void WorldSession::onCancelAction(const TS_CS_CANCEL_ACTION *pRecvPct)
{
    if (m_pPlayer == nullptr)
        return;

    Unit *cancellor = m_pPlayer->GetSummonByHandle(pRecvPct->handle);
    if (cancellor == nullptr || !cancellor->IsInWorld())
        cancellor = dynamic_cast<Unit *>(m_pPlayer);
    if (cancellor->GetHandle() == pRecvPct->handle)
    {
        if (cancellor->m_castingSkill != nullptr)
        {
            cancellor->CancelSkill();
        }
        else
        {
            if (cancellor->GetTargetHandle() != 0)
                cancellor->CancelAttack();
        }
    }
}

void WorldSession::onTakeItem(const TS_CS_TAKE_ITEM *pRecvPct)
{
    if (m_pPlayer == nullptr)
        return;
    uint ct = sWorld.GetArTime();

    auto item = sMemoryPool.GetObjectInWorld<Item>(pRecvPct->item_handle);
    if (item == nullptr || !item->IsInWorld())
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_EXIST, pRecvPct->item_handle);
        return;
    }

    // TODO: Weight
    if (item->m_Instance.OwnerHandle != 0)
    {
        NG_LOG_ERROR("WorldSession::onTakeItem(): OwnerHandle not null: %s, handle: %u", m_pPlayer->GetName(), item->GetHandle());
        return;
    }

    auto pos = m_pPlayer->GetPosition();
    if (GameRule::GetPickableRange() < item->GetExactDist2d(&pos))
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_TOO_FAR, pRecvPct->item_handle);
        return;
    }

    if (item->IsQuestItem() && !m_pPlayer->IsTakeableQuestItem(item->m_Instance.Code))
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_ACCESS_DENIED, pRecvPct->item_handle);
        return;
    }

    auto drop_duration = ct - item->m_nDropTime;
    uint ry            = 3000;

    for (int i = 0; i < 3; i++)
    {
        if (item->m_pPickupOrder.hPlayer[i] == 0 && item->m_pPickupOrder.nPartyID[i] == 0)
            break;

        if (item->m_pPickupOrder.nPartyID[i] <= 0 || item->m_pPickupOrder.nPartyID[i] != m_pPlayer->GetPartyID())
        {
            if (item->m_pPickupOrder.hPlayer[i] != m_pPlayer->GetHandle())
            {
                if (drop_duration < ry)
                {
                    Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_ACCESS_DENIED, pRecvPct->item_handle);
                    return;
                }
                ry += 1000;
            }
        }
    }

    if (item->m_Instance.Code == 0)
    {
        if (m_pPlayer->GetPartyID() == 0)
        {
            if (m_pPlayer->GetGold() + item->m_Instance.nCount > MAX_GOLD_FOR_INVENTORY)
            {
                Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_TOO_MUCH_MONEY, pRecvPct->item_handle);
                return;
            }
        }
    }

    TS_SC_TAKE_ITEM_RESULT resultPct{ };
    resultPct.item_handle = pRecvPct->item_handle;
    resultPct.item_taker  = m_pPlayer->GetHandle();
    sWorld.Broadcast((uint)(m_pPlayer->GetPositionX() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)),
                     (uint)(m_pPlayer->GetPositionY() / sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE)), m_pPlayer->GetLayer(), resultPct);
    if (sWorld.RemoveItemFromWorld(item))
    {
        if (m_pPlayer->GetPartyID() != 0)
        {
            if (item->m_Instance.Code != 0)
            {
                ///- Actual Item
                sWorld.procPartyShare(m_pPlayer, item);
            }
            else
            {
                ///- Gold
                std::vector<Player *> vList{ };
                sGroupManager.GetNearMember(m_pPlayer, 400.0f, vList);
                auto incGold = (int64)(item->m_Instance.nCount / (!vList.empty() ? vList.size() : 1));

                for (auto &np : vList)
                {
                    auto nNewGold = incGold + np->GetGold();
                    np->ChangeGold(nNewGold);
                }
                Item::PendFreeItem(item);
            }
            return;
        }
        uint nih = sWorld.procAddItem(m_pPlayer, item, false);
        if (nih != 0)
        { // nih = new item handle
            Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_SUCCESS, nih);
            return;
        }
        Item::PendFreeItem(item);
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_ACCESS_DENIED, pRecvPct->item_handle);
    }
}

void WorldSession::onUseItem(const TS_CS_USE_ITEM *pRecvPct)
{
    uint ct = sWorld.GetArTime();

    auto item = m_pPlayer->FindItemByHandle(pRecvPct->item_handle);
    if (item == nullptr || item->m_Instance.OwnerHandle != m_pPlayer->GetHandle())
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_EXIST, pRecvPct->item_handle);
        return;
    }

    if (item->m_pItemBase->type != TYPE_USE && false /*!item->IsUsingItem()*/)
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_ACCESS_DENIED, pRecvPct->item_handle);
        return;
    }

    if ((item->m_pItemBase->flaglist[FLAG_MOVE] == 0 && m_pPlayer->IsMoving(ct)))
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_ACTABLE, pRecvPct->item_handle);
        return;
    }

    // IsUsableSecroute

    // IsUsableraid

    // Eventmap

    uint16 nResult = m_pPlayer->IsUseableItem(item, nullptr);
    if (nResult != TS_RESULT_SUCCESS)
    {
        if (nResult == TS_RESULT_COOL_TIME)
            Messages::SendItemCoolTimeInfo(m_pPlayer);
        Messages::SendResult(m_pPlayer, pRecvPct->id, nResult, pRecvPct->item_handle);
        return;
    }

    if (item->m_pItemBase->flaglist[FLAG_TARGET_USE] == 0)
    {
        nResult = m_pPlayer->UseItem(item, nullptr, pRecvPct->szParameter);
        Messages::SendResult(m_pPlayer, pRecvPct->id, nResult, pRecvPct->item_handle);
        if (nResult != 0)
            return;
    }
    else
    {
        //auto unit = dynamic_cast<Unit*>(sMemoryPool.getPtrFromId(target_handle));
        auto unit = sMemoryPool.GetObjectInWorld<Unit>(pRecvPct->target_handle);
        if (unit == nullptr || unit->GetHandle() == 0)
        {
            Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_EXIST, pRecvPct->target_handle);
            return;
        }
        nResult = m_pPlayer->IsUseableItem(item, unit);
        if (nResult == TS_RESULT_SUCCESS)
        {
            nResult = m_pPlayer->UseItem(item, unit, pRecvPct->szParameter);
            Messages::SendResult(m_pPlayer, pRecvPct->id, nResult, pRecvPct->item_handle);
        }

        if (nResult != TS_RESULT_SUCCESS)
        {
            return;
        }
    }

    TS_SC_USE_ITEM_RESULT resultPct{ };
    resultPct.item_handle   = pRecvPct->item_handle;
    resultPct.target_handle = pRecvPct->target_handle;
    m_pPlayer->SendPacket(resultPct);
}

bool WorldSession::Update(uint /*diff*/)
{
    if (_socket == nullptr)
        return false;

    if (_accountId != 0 && (m_nLastPing > 0 && m_nLastPing + 30000 < sWorld.GetArTime()))
    {
        NG_LOG_DEBUG("server.worldserver", "Kicking Account [%d : %s] due to inactivity.", _accountId, _accountName.c_str());
        return false;
    }

    return true;
}

void WorldSession::onRevive(const TS_CS_RESURRECTION *)
{
    if (m_pPlayer == nullptr)
        return;

    if (m_pPlayer->GetHealth() != 0)
        return;

    sScriptingMgr.RunString(m_pPlayer, string_format("revive_in_town(%d)", 0));
}

void WorldSession::onDropItem(const TS_CS_DROP_ITEM *pRecvPct)
{
    auto item = sMemoryPool.GetObjectInWorld<Item>(pRecvPct->item_handle);
    if (item != nullptr && item->IsDropable() && pRecvPct->count > 0 && (item->m_pItemBase->group != GROUP_SUMMONCARD || !(item->m_Instance.Flag & ITEM_FLAG_SUMMON)))
    {
        m_pPlayer->DropItem(m_pPlayer, item, pRecvPct->count);
        Messages::SendDropResult(m_pPlayer, pRecvPct->item_handle, true);
    }
    else
    {
        Messages::SendDropResult(m_pPlayer, pRecvPct->item_handle, false);
    }
}

void WorldSession::onMixRequest(const TS_CS_MIX *pRecvPct)
{
    if (pRecvPct->sub_items.size() > 9)
    {
        KickPlayer();
        return;
    }

    auto pMainItem = sMixManager.check_mixable_item(m_pPlayer, pRecvPct->main_item.handle, 1);
    if (pRecvPct->main_item.handle != 0 && pMainItem == nullptr)
        return;

    std::vector<Item *> pSubItem{ };
    std::vector<uint16> pCountList{ };
    if (pRecvPct->sub_items.size() != 0)
    {
        for (auto &mixInfo : pRecvPct->sub_items)
        {
            auto item = sMixManager.check_mixable_item(m_pPlayer, mixInfo.handle, mixInfo.count);
            if (item == nullptr)
                return;
            pSubItem.emplace_back(item);
            pCountList.emplace_back(mixInfo.count);
        }
    }

    auto mb = sMixManager.GetProperMixInfo(pMainItem, pRecvPct->sub_items.size(), pSubItem, pCountList);
    if (mb == nullptr)
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_INVALID_ARGUMENT, 0);
        return;
    }

    switch (mb->type)
    {
        case 0:
            break;
        case MIX_ENHANCE: //EnchantItem without E-Protect Powder
            sMixManager.EnhanceItem(mb, m_pPlayer, pMainItem, pRecvPct->sub_items.size(), pSubItem, pCountList);
            return;
        case MIX_ENHANCE_SKILL_CARD:
            sMixManager.EnhanceSkillCard(mb, m_pPlayer, pRecvPct->sub_items.size(), pSubItem);
            return;
        case MIX_ENHANCE_WITHOUT_FAIL: //EnchantItem WITH E-Protect Powder
            sMixManager.EnhanceItem(mb, m_pPlayer, pMainItem, pRecvPct->sub_items.size(), pSubItem, pCountList);
            return;
        case MIX_ADD_LEVEL_SET_FLAG:
            sMixManager.MixItem(mb, m_pPlayer, pMainItem, pRecvPct->sub_items.size(), pSubItem, pCountList);
            return;
        case MIX_RESTORE_ENHANCE_SET_FLAG:
            sMixManager.RepairItem(m_pPlayer, pMainItem, pRecvPct->sub_items.size(), pSubItem, pCountList);
            return;
        default:
            break;
    }
}

void WorldSession::onSoulStoneCraft(const TS_CS_SOULSTONE_CRAFT *pRecvPct)
{

    if (m_pPlayer->GetLastContactLong("SoulStoneCraft") == 0)
        return;

    auto nPrevGold = m_pPlayer->GetGold();
    auto pItem     = m_pPlayer->FindItemByHandle(pRecvPct->craft_item_handle);
    if (pItem == nullptr)
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_EXIST, pRecvPct->craft_item_handle);
        return;
    }

    int nSocketCount = pItem->m_pItemBase->socket;
    if (nSocketCount < 1 || nSocketCount > 4)
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_ACCESS_DENIED, pRecvPct->craft_item_handle);
        return;
    }

    int  nMaxReplicatableCount = nSocketCount == 4 ? 2 : 1;
    int  nCraftCost            = 0;
    bool bIsValid              = false;
    Item *pSoulStoneList[sizeof(pRecvPct->soulstone_handle)]{nullptr};

    for (int i = 0; i < nSocketCount; ++i)
    {
        if (pRecvPct->soulstone_handle[i] != 0)
        {
            pSoulStoneList[i] = m_pPlayer->FindItemByHandle(pRecvPct->soulstone_handle[i]);
            if (pSoulStoneList[i] == nullptr)
            {
                Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_ACCESS_DENIED, pRecvPct->soulstone_handle[i]);
                return;
            }
            if (pSoulStoneList[i]->m_pItemBase->type != TYPE_SOULSTONE
                || pSoulStoneList[i]->m_pItemBase->group != GROUP_SOULSTONE
                || pSoulStoneList[i]->m_pItemBase->iclass != CLASS_SOULSTONE)
            {
                Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_ACTABLE, pRecvPct->soulstone_handle[i]);
                return;
            }

            int      nReplicatedCount = 0;
            for (int k                = 0; k < pItem->m_pItemBase->socket; ++k)
            {
                if (pItem->m_Instance.Socket[k] != 0 && k != i)
                {
                    auto ibs = sObjectMgr.GetItemBase(pItem->m_Instance.Socket[k]);
                    if (ibs->base_type[0] == pSoulStoneList[i]->m_pItemBase->base_type[0]
                        && ibs->base_type[1] == pSoulStoneList[i]->m_pItemBase->base_type[1]
                        && ibs->base_type[2] == pSoulStoneList[i]->m_pItemBase->base_type[2]
                        && ibs->base_type[3] == pSoulStoneList[i]->m_pItemBase->base_type[3]
                        && ibs->base_var[0][0] == pSoulStoneList[i]->m_pItemBase->base_var[0][0]
                        && ibs->base_var[1][0] == pSoulStoneList[i]->m_pItemBase->base_var[1][0]
                        && ibs->base_var[2][0] == pSoulStoneList[i]->m_pItemBase->base_var[2][0]
                        && ibs->base_var[3][0] == pSoulStoneList[i]->m_pItemBase->base_var[3][0]
                        && ibs->opt_type[0] == pSoulStoneList[i]->m_pItemBase->opt_type[0]
                        && ibs->opt_type[1] == pSoulStoneList[i]->m_pItemBase->opt_type[1]
                        && ibs->opt_type[2] == pSoulStoneList[i]->m_pItemBase->opt_type[2]
                        && ibs->opt_type[3] == pSoulStoneList[i]->m_pItemBase->opt_type[3]
                        && ibs->opt_var[0][0] == pSoulStoneList[i]->m_pItemBase->opt_var[0][0]
                        && ibs->opt_var[1][0] == pSoulStoneList[i]->m_pItemBase->opt_var[1][0]
                        && ibs->opt_var[2][0] == pSoulStoneList[i]->m_pItemBase->opt_var[2][0]
                        && ibs->opt_var[3][0] == pSoulStoneList[i]->m_pItemBase->opt_var[3][0])
                    {
                        nReplicatedCount++;
                        if (nReplicatedCount >= nMaxReplicatableCount)
                        {
                            Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_ALREADY_EXIST, 0);
                            return;
                        }
                    }
                }
            }
            nCraftCost += pSoulStoneList[i]->m_pItemBase->price / 10;
            bIsValid                  = true;
        }
    }
    if (!bIsValid)
    {
        // maybe log here?
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_INVALID_ARGUMENT, 0);
        return;
    }
    if (nPrevGold < nCraftCost || m_pPlayer->ChangeGold(nPrevGold - nCraftCost) != TS_RESULT_SUCCESS)
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_ENOUGH_MONEY, 0);
        return;
    }

    int      nEndurance = 0;
    for (int i          = 0; i < 4; ++i)
    {
        if (pItem->m_Instance.Socket[i] != 0 || pSoulStoneList[i] != nullptr)
        {
            if (pSoulStoneList[i] != nullptr)
            {
                nEndurance += pSoulStoneList[i]->m_Instance.nCurrentEndurance;
                pItem->m_Instance.Socket[i] = pSoulStoneList[i]->m_Instance.Code;
                m_pPlayer->EraseItem(pSoulStoneList[i], 1);
            }
            else
            {
                nEndurance += pItem->m_Instance.nCurrentEndurance;
            }
        }
    }

    pItem->SetCurrentEndurance(nEndurance);
    m_pPlayer->Save(false);
    pItem->DBUpdate();
    m_pPlayer->SetLastContact("SoulStoneCraft", 0);
    Messages::SendItemMessage(m_pPlayer, pItem);
    m_pPlayer->CalculateStat();
    Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_SUCCESS, 0);
}

void WorldSession::onStorage(const TS_CS_STORAGE *pRecvPct)
{
    if (m_pPlayer == nullptr)
        return;

    if (!m_pPlayer->m_bIsUsingStorage || m_pPlayer->m_castingSkill != nullptr || m_pPlayer->GetUInt32Value(PLAYER_FIELD_TRADE_TARGET) != 0 || !m_pPlayer->IsActable())
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_ACTABLE, pRecvPct->item_handle);
        return;
    }

    switch ((STORAGE_MODE)pRecvPct->mode)
    {
        case ITEM_INVENTORY_TO_STORAGE:
        case ITEM_STORAGE_TO_INVENTORY:
        {
            if (pRecvPct->count <= 0)
            {
                Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_ENOUGH_MONEY, pRecvPct->item_handle);
                return;
            }

            auto *pItem = sMemoryPool.GetObjectInWorld<Item>(pRecvPct->item_handle);
            if (pItem == nullptr)
            {
                Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_EXIST, pRecvPct->item_handle);
                return;
            }
            if (pItem->m_Instance.OwnerHandle != m_pPlayer->GetHandle())
            {
                Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_ACCESS_DENIED, pRecvPct->item_handle);
                return;
            }
            if (pItem->IsInInventory() && pRecvPct->mode == ITEM_INVENTORY_TO_STORAGE)
            {
                if (pItem->m_pSummon != nullptr)
                {
                    for (const auto &v : m_pPlayer->m_aBindSummonCard)
                    {
                        if (v == pItem)
                        {
                            Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_ACCESS_DENIED, pItem->GetHandle());
                            return;
                        }
                    }
                }
                /*if((pItem->m_Instance.Flag & 0x40) == 0 || m_pPlayer->FindStorageItem(pItem->m_Instance.Code) == nullptr)
                {
                    Messages::SendResult(m_pPlayer, pRecvPct->GetPacketID(), TS_RESULT_TOO_HEAVY, handle); // too heavy??
                    return;
                }*/
                m_pPlayer->MoveInventoryToStorage(pItem, pRecvPct->count);
            }
            else if (pItem->IsInStorage() && pRecvPct->mode == ITEM_STORAGE_TO_INVENTORY)
            {
                m_pPlayer->MoveStorageToInventory(pItem, pRecvPct->count);
            }
            m_pPlayer->Save(true);
            return;
        }
        case GOLD_INVENTORY_TO_STORAGE: // 2
        {
            if (m_pPlayer->GetGold() < pRecvPct->count)
                return;
            if (m_pPlayer->GetStorageGold() + pRecvPct->count > MAX_GOLD_FOR_STORAGE)
            {
                Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_TOO_MUCH_MONEY, pRecvPct->item_handle);
                return;
            }
            auto nGold = m_pPlayer->GetGold();
            if (m_pPlayer->ChangeGold(nGold - pRecvPct->count) == TS_RESULT_SUCCESS && m_pPlayer->ChangeStorageGold(m_pPlayer->GetStorageGold() + pRecvPct->count) == TS_RESULT_SUCCESS)
            {
                m_pPlayer->Save(true);
                return;
            }
        }
            return;
        case GOLD_STORAGE_TO_INVENTORY:
        {
            if (m_pPlayer->GetStorageGold() < pRecvPct->count)
                return;
            if (m_pPlayer->GetGold() + pRecvPct->count > MAX_GOLD_FOR_INVENTORY)
            {
                Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_TOO_MUCH_MONEY, pRecvPct->item_handle);
                return;
            }
            auto nGold = m_pPlayer->GetStorageGold();
            if (m_pPlayer->ChangeStorageGold(nGold - pRecvPct->count) == TS_RESULT_SUCCESS && m_pPlayer->ChangeGold(m_pPlayer->GetGold() + pRecvPct->count) == TS_RESULT_SUCCESS)
            {
                m_pPlayer->Save(true);
                return;
            }
            return;
        }
        case STORAGE_CLOSE:
            m_pPlayer->m_bIsUsingStorage = false;
            return;
        default:
            break;
    }
}

void WorldSession::onBindSkillCard(const TS_CS_BIND_SKILLCARD *pRecvPct)
{
    if (m_pPlayer == nullptr)
        return;

    auto pItem = sMemoryPool.GetObjectInWorld<Item>(pRecvPct->item_handle);
    if (pItem == nullptr)
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_EXIST, pRecvPct->item_handle);
        return;
    }
    if (pRecvPct->target_handle != m_pPlayer->GetHandle())
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_ACTABLE, pRecvPct->target_handle);
        return;
    }
    if (!pItem->IsInInventory() || pItem->m_Instance.OwnerHandle != m_pPlayer->GetHandle() || pItem->m_pItemBase->group != GROUP_SKILLCARD || pItem->m_hBindedTarget != 0)
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_ACCESS_DENIED, pRecvPct->item_handle);
        return;
    }

    auto pSkill = m_pPlayer->GetSkill(pItem->m_pItemBase->skill_id);
    if (pSkill != nullptr && pSkill->GetSkillEnhance() == 0)
    {
        m_pPlayer->BindSkillCard(pItem);
    }
}

void WorldSession::onUnBindSkilLCard(const TS_CS_UNBIND_SKILLCARD *pRecvPct)
{
    auto pItem = sMemoryPool.GetObjectInWorld<Item>(pRecvPct->item_handle);
    if (pItem == nullptr)
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_EXIST, pRecvPct->item_handle);
        return;
    }
    if (pRecvPct->target_handle != m_pPlayer->GetHandle())
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_ACTABLE, pRecvPct->target_handle);
        return;
    }
    if (!pItem->IsInInventory() || pItem->m_Instance.OwnerHandle != m_pPlayer->GetHandle() || pItem->m_pItemBase->group != GROUP_SKILLCARD || pItem->m_hBindedTarget == 0)
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_ACCESS_DENIED, pRecvPct->item_handle);
        return;
    }

    m_pPlayer->UnBindSkillCard(pItem);
}

void WorldSession::onTrade(const TS_TRADE *pRecvPct)
{
    if (m_pPlayer == nullptr)
        return;

    if (m_pPlayer->m_bIsUsingStorage)
        return;

    if (m_pPlayer->bIsMoving && m_pPlayer->IsInWorld())
    {
        m_pPlayer->CancelTrade(false);
        return;
    }

    if (pRecvPct->target_player == m_pPlayer->GetHandle())
    {
        if (m_pPlayer->GetTargetHandle() != 0)
        {
            auto pTarget = sMemoryPool.GetObjectInWorld<Player>(m_pPlayer->GetTargetHandle());
            pTarget->CancelTrade(false);
        }
        m_pPlayer->CancelTrade(false);
        return;
    }

    switch (pRecvPct->mode)
    {
        case TM_REQUEST_TRADE:
            onRequestTrade(pRecvPct->target_player);
            break;
        case TM_ACCEPT_TRADE:
            onAcceptTrade(pRecvPct->target_player);
            break;
        case TM_CANCEL_TRADE:
            onCancelTrade();
            break;
        case TM_REJECT_TRADE:
            onRejectTrade(pRecvPct->target_player);
            break;
        case TM_ADD_ITEM:
            onAddItem(pRecvPct->target_player, pRecvPct);
            break;
        case TM_REMOVE_ITEM:
            onRemoveItem(pRecvPct->target_player, pRecvPct);
            break;
        case TM_ADD_GOLD:
            onAddGold(pRecvPct->target_player, pRecvPct);
            break;
        case TM_FREEZE_TRADE:
            onFreezeTrade();
            break;
        case TM_CONFIRM_TRADE:
            onConfirmTrade(pRecvPct->target_player);
            break;
        default:
            return;
    }
}

void WorldSession::onRequestTrade(uint32 hTradeTarget)
{
    auto tradeTarget = sMemoryPool.GetObjectInWorld<Player>(hTradeTarget);
    if (!isValidTradeTarget(tradeTarget))
        return;

    if (tradeTarget->m_bTrading || m_pPlayer->m_bTrading)
        Messages::SendResult(m_pPlayer, NGemity::Packets::TS_TRADE, TS_ResultCode::TS_RESULT_ACCESS_DENIED, tradeTarget->GetHandle());
    else if (!m_pPlayer->IsTradableWith(tradeTarget))
        Messages::SendResult(m_pPlayer, NGemity::Packets::TS_TRADE, TS_ResultCode::TS_RESULT_PK_LIMIT, tradeTarget->GetHandle());
    else
    {
        XPacket tradePct(NGemity::Packets::TS_TRADE);
        tradePct << m_pPlayer->GetHandle();
        tradePct << (uint8)TM_REQUEST_TRADE; // mode
        tradeTarget->SendPacket(tradePct);
    }
}

void WorldSession::onAcceptTrade(uint32 hTradeTarget)
{
    auto tradeTarget = sMemoryPool.GetObjectInWorld<Player>(hTradeTarget);
    if (!isValidTradeTarget(tradeTarget))
        return;

    if (m_pPlayer->m_bTrading || tradeTarget->m_bTrading || m_pPlayer->m_hTamingTarget || tradeTarget->m_hTamingTarget)
    {
        Messages::SendResult(m_pPlayer, NGemity::Packets::TS_TRADE, TS_ResultCode::TS_RESULT_ACCESS_DENIED, 0);
    }
    else
    {
        m_pPlayer->StartTrade(tradeTarget->GetHandle());
        tradeTarget->StartTrade(m_pPlayer->GetHandle());

        XPacket tradePlayerPct(NGemity::Packets::TS_TRADE);
        tradePlayerPct << tradeTarget->GetHandle();
        tradePlayerPct << (uint8)TM_BEGIN_TRADE; // mode
        m_pPlayer->SendPacket(tradePlayerPct);

        XPacket tradeTargetPct(NGemity::Packets::TS_TRADE);
        tradeTargetPct << m_pPlayer->GetHandle();
        tradeTargetPct << (uint8)TM_BEGIN_TRADE; // mode
        tradeTarget->SendPacket(tradeTargetPct);
    }
}

void WorldSession::onCancelTrade()
{
    if (!m_pPlayer->m_bTrading)
        return;

    auto tradeTarget = m_pPlayer->GetTradeTarget();
    if (tradeTarget != nullptr)
        tradeTarget->CancelTrade(true);

    m_pPlayer->CancelTrade(true);
}

void WorldSession::onRejectTrade(uint32 hTradeTarget)
{
    auto tradeTarget = sMemoryPool.GetObjectInWorld<Player>(hTradeTarget);
    if (!isValidTradeTarget(tradeTarget))
        return;

    XPacket tradePct(NGemity::Packets::TS_TRADE);
    tradePct << m_pPlayer->GetHandle();
    tradePct << (uint8)TM_REJECT_TRADE;
    tradeTarget->SendPacket(tradePct);
}

void WorldSession::onAddItem(uint32 hTradeTarget, const TS_TRADE *pRecvPct)
{
    auto tradeTarget = sMemoryPool.GetObjectInWorld<Player>(hTradeTarget);
    if (!isValidTradeTarget(tradeTarget))
        return;

    if (!m_pPlayer->m_bTradeFreezed)
    {
        auto item  = m_pPlayer->FindItemByHandle(pRecvPct->item_info.base_info.handle);
        auto count = pRecvPct->item_info.base_info.count;

        if (item == nullptr || item->m_pItemBase == nullptr)
            return;

        if (count <= 0 || count > item->m_Instance.nCount)
        {
            NG_LOG_ERROR("trade", "Add Trade Bug [%s:%d]", m_pPlayer->m_szAccount.c_str(), m_pPlayer->GetHandle());
            // Register block account in game rule?
            Messages::SendResult(m_pPlayer, NGemity::Packets::TS_TRADE, TS_ResultCode::TS_RESULT_NOT_EXIST, 0);
            return;
        }

        if (!m_pPlayer->IsTradable(item))
        {
            Messages::SendResult(m_pPlayer, NGemity::Packets::TS_TRADE, TS_ResultCode::TS_RESULT_ACCESS_DENIED, 0);
            return;
        }

        if (m_pPlayer->AddItemToTradeWindow(item, count))
        {
            Messages::SendTradeItemInfo(TM_ADD_ITEM, item, count, m_pPlayer, tradeTarget);
        }
    }
}

void WorldSession::onRemoveItem(uint32 hTradeTarget, const TS_TRADE *pRecvPct)
{
    auto tradeTarget = sMemoryPool.GetObjectInWorld<Player>(hTradeTarget);
    if (!isValidTradeTarget(tradeTarget))
    {
        m_pPlayer->CancelTrade(false);
        return;
    }

    if (!m_pPlayer->m_bTradeFreezed)
    {
        auto item  = m_pPlayer->FindItemByHandle(pRecvPct->item_info.base_info.handle);
        auto count = pRecvPct->item_info.base_info.count;

        if (item == nullptr || item->m_pItemBase == nullptr)
            return;

        if (m_pPlayer->RemoveItemFromTradeWindow(item, count))
        {
            Messages::SendTradeItemInfo(TM_REMOVE_ITEM, item, count, m_pPlayer, tradeTarget);
        }
        else
        {
            Messages::SendResult(m_pPlayer, NGemity::Packets::TS_TRADE, TS_ResultCode::TS_RESULT_NOT_EXIST, 0);
        }
    }
}

void WorldSession::onAddGold(uint32 hTradeTarget, const TS_TRADE *pRecvPct)
{
    if (!m_pPlayer->m_bTradeFreezed)
    {
        auto tradeTarget = sMemoryPool.GetObjectInWorld<Player>(hTradeTarget);
        if (!isValidTradeTarget(tradeTarget))
            return;

        int64 gold = pRecvPct->item_info.base_info.count;
        if (gold <= 0)
        {
            NG_LOG_ERROR("trade", "Add gold Trade Bug [%s:%d]", m_pPlayer->m_szAccount.c_str(), m_pPlayer->GetHandle());
            // Register block account in game rule?
            Messages::SendResult(m_pPlayer, NGemity::Packets::TS_TRADE, TS_ResultCode::TS_RESULT_NOT_EXIST, 0);
            return;
        }

        m_pPlayer->AddGoldToTradeWindow(gold);

        XPacket tradePct(NGemity::Packets::TS_TRADE);
        tradePct << m_pPlayer->GetHandle();
        tradePct << (uint8)TM_ADD_GOLD;
        tradePct << (uint32)0; // Handle
        tradePct << (int32)0; // Code
        tradePct << (int64)0; // ID
        tradePct << (int32)gold;
        tradePct.fill("", 44);
        tradeTarget->SendPacket(tradePct);
        m_pPlayer->SendPacket(tradePct);
    }
    else
    {
        m_pPlayer->CancelTrade(false);
    }
}

void WorldSession::onFreezeTrade()
{
    auto tradeTarget = m_pPlayer->GetTradeTarget();
    if (tradeTarget != nullptr)
    {
        m_pPlayer->FreezeTrade();

        XPacket tradePct(NGemity::Packets::TS_TRADE);
        tradePct << m_pPlayer->GetHandle();
        tradePct << (uint8)TM_FREEZE_TRADE;
        tradeTarget->SendPacket(tradePct);
        m_pPlayer->SendPacket(tradePct);
    }
    else
        m_pPlayer->CancelTrade(false);
}

void WorldSession::onConfirmTrade(uint hTradeTarget)
{
    auto tradeTarget = sMemoryPool.GetObjectInWorld<Player>(hTradeTarget);
    if (!isValidTradeTarget(tradeTarget))
        return;

    if (!m_pPlayer->m_bTrading
        || !tradeTarget->m_bTrading
        || !m_pPlayer->m_bTradeFreezed
        || !tradeTarget->m_bTradeFreezed
        || tradeTarget->GetTradeTarget() != m_pPlayer)
    {
        m_pPlayer->CancelTrade(true);
        tradeTarget->CancelTrade(true);
        return;
    }

    if (m_pPlayer->m_bTradeAccepted)
        return;

    m_pPlayer->ConfirmTrade();

    XPacket tradePct(NGemity::Packets::TS_TRADE);
    tradePct << m_pPlayer->GetHandle();
    tradePct << (uint8)TM_CONFIRM_TRADE;
    tradeTarget->SendPacket(tradePct);
    m_pPlayer->SendPacket(tradePct);

    if (!tradeTarget->m_bTradeAccepted)
        return;

    if (!m_pPlayer->CheckTradeWeight()
        || !tradeTarget->CheckTradeWeight())
    {
        m_pPlayer->CancelTrade(false);
        tradeTarget->CancelTrade(false);

        Messages::SendResult(m_pPlayer, NGemity::Packets::TS_TRADE, TS_RESULT_TOO_HEAVY, 0);
        Messages::SendResult(tradeTarget, NGemity::Packets::TS_TRADE, TS_RESULT_TOO_HEAVY, 0);

        return;
    }

    if (!m_pPlayer->CheckTradeItem()
        || !tradeTarget->CheckTradeItem())
    {
        m_pPlayer->CancelTrade(false);
        tradeTarget->CancelTrade(false);

        Messages::SendResult(m_pPlayer, NGemity::Packets::TS_TRADE, TS_RESULT_ACCESS_DENIED, 0);
        Messages::SendResult(tradeTarget, NGemity::Packets::TS_TRADE, TS_RESULT_ACCESS_DENIED, 0);

        return;
    }

    int64 tradeGold       = m_pPlayer->GetGold() + tradeTarget->GetTradeGold();
    int64 tradeTargetGold = tradeTarget->GetGold() + m_pPlayer->GetTradeGold();

    bool bExceedGold            = tradeGold > MAX_GOLD_FOR_INVENTORY;
    bool bExceedGoldTradeTarget = tradeTargetGold > MAX_GOLD_FOR_INVENTORY;

    if (bExceedGold || bExceedGoldTradeTarget)
    {
        m_pPlayer->CancelTrade(false);
        tradeTarget->CancelTrade(false);

        if (bExceedGold)
        {
            Messages::SendResult(m_pPlayer, NGemity::Packets::TS_TRADE, TS_RESULT_TOO_MUCH_MONEY, m_pPlayer->GetHandle());
            Messages::SendResult(tradeTarget, NGemity::Packets::TS_TRADE, TS_RESULT_TOO_MUCH_MONEY, m_pPlayer->GetHandle());
        }

        if (bExceedGoldTradeTarget)
        {
            Messages::SendResult(m_pPlayer, NGemity::Packets::TS_TRADE, TS_RESULT_TOO_MUCH_MONEY, tradeTarget->GetHandle());
            Messages::SendResult(tradeTarget, NGemity::Packets::TS_TRADE, TS_RESULT_TOO_MUCH_MONEY, tradeTarget->GetHandle());
        }
    }
    else
    {
        if (m_pPlayer->m_bTrading
            && tradeTarget->m_bTrading
            && m_pPlayer->m_bTradeFreezed
            && tradeTarget->m_bTradeFreezed
            && m_pPlayer->GetTradeTarget() == tradeTarget
            && tradeTarget->GetTradeTarget() == m_pPlayer
            && m_pPlayer->ProcessTrade())
        {
            XPacket tradePct(NGemity::Packets::TS_TRADE);
            tradePct << m_pPlayer->GetHandle();
            tradePct << (uint8)TM_PROCESS_TRADE;
            tradeTarget->SendPacket(tradePct);
            m_pPlayer->SendPacket(tradePct);
        }
        m_pPlayer->CancelTrade(false);
        tradeTarget->CancelTrade(false);
    }
}

bool WorldSession::isValidTradeTarget(Player *pTradeTarget)
{
    return !(pTradeTarget == nullptr || !pTradeTarget->IsInWorld() || pTradeTarget->GetExactDist2d(m_pPlayer) > sWorld.getIntConfig(CONFIG_MAP_REGION_SIZE));
}

void WorldSession::onDropQuest(const TS_CS_DROP_QUEST *pRecvPct)
{
    if (m_pPlayer == nullptr)
        return;

    if (m_pPlayer->DropQuest(pRecvPct->code))
    {
        Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_SUCCESS, 0);
        return;
    }
    Messages::SendResult(m_pPlayer, pRecvPct->id, TS_RESULT_NOT_ACTABLE, 0);
}

void WorldSession::onPing(const TS_CS_PING *)
{
    m_nLastPing = sWorld.GetArTime();
}
