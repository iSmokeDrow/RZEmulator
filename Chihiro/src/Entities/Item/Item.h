#pragma once
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
#include "DatabaseTemplates.h"
#include "ItemFields.h"
#include "Object.h"

class XPacket;
class Unit;
class Summon;

class ItemInstance
{
public:
  void Copy(const ItemInstance &pFrom);

  uint OwnerHandle{0};     // 0x0
  uint OwnSummonHandle{0}; // 0x4
  int64_t UID{0};          // 0x8
  int Code{0};             // 0x10
  int nIdx{0};             // 0x14
  int nLevel{0};           // 0x18
  int nEnhance{0};         // 0x1C
  int nEndurance{0};
  int nCurrentEndurance{0};
  int nOwnerUID{0};      // 0x20
  int nOwnSummonUID{0};  // 0x24
  int nAuctionID{0};     // 0x28
  int nItemKeepingID{0}; // 0x2C
  int64_t nCount{0};     // 0x30
  int64_t tExpire{0};    // 0x40
  //Elemental::Type eElementalEffectType;         // 0x48
  int Flag{0};                            // 0x60
  GenerateCode GenerateInfo = BY_UNKNOWN; // 0x64
  ItemWearType nWearInfo{WEAR_CANTWEAR};  // 0x68
  int Socket[4]{0};
};

class Item : public WorldObject
{
public:
  enum _TARGET_TYPE
  {
    TARGET_TYPE_PLAYER = 0,
    TARGET_TYPE_SUMMON = 1,
    TARGET_TYPE_MONSTER = 2,
    TARGET_TYPE_NPC = 3,
    TARGET_TYPE_UNKNOWN = 4
  };

  static void EnterPacket(XPacket &pEnterPct, Item *pItem);
  static Item *AllocItem(uint64_t uid, int code, int64_t cnt, GenerateCode info = BY_BASIC, int level = -1, int enhance = -1,
                         int flag = -1, int socket_0 = 0, int socket_1 = 0, int socket_2 = 0, int socket_3 = 0, int remain_time = 0);
  static void PendFreeItem(Item *pItem);

  Item();
  // Deleting the copy & assignment operators
  // Better safe than sorry
  Item(const Item &) = delete;
  Item &operator=(const Item &) = delete;

  void SetCount(int64_t count);
  bool IsDropable();
  bool IsWearable();
  int32_t GetItemCode() const { return m_Instance.Code; }
  int GetItemGroup() const { return m_pItemBase != nullptr ? m_pItemBase->group : 0; }
  ItemClass GetItemClass() const { return ((GetItemBase() != nullptr) ? static_cast<ItemClass>(GetItemBase()->iclass) : ItemClass::CLASS_ETC); }
  ItemWearType GetWearInfo() const { return m_Instance.nWearInfo; }
  ItemTemplate *GetItemBase() const { return m_pItemBase; }
  int GetCurrentEndurance() const { return m_Instance.nCurrentEndurance; }
  int GetItemEnhance() const { return m_Instance.nEnhance; }
  inline uint32_t GetOwnerHandle() const { return m_Instance.OwnerHandle; }
  inline int64_t GetCount() const { return m_Instance.nCount; }

  inline void SetWearInfo(ItemWearType wear_info)
  {
    m_Instance.nWearInfo = wear_info;
    m_bIsNeedUpdateToDB = true;
  }

  inline void SetOwnSummonInfo(uint32_t handle, int UID)
  {
    m_Instance.OwnSummonHandle = handle;
    m_Instance.nOwnSummonUID = UID;
  }
  inline int GetOwnSummonUID() const { return m_Instance.nOwnSummonUID; }
  Summon *GetSummon() const { return m_pSummon; }
  void SetBindedCreatureHandle(uint32_t target) { m_hBindedTarget = target; }

  bool IsTradable();
  bool IsExpireItem() const;
  bool IsJoinable() const;
  bool IsQuestItem() const;
  float GetWeight() const;
  void CopyFrom(const Item *pFrom);

  inline bool IsBullet() const { return GetItemGroup() == ItemGroup::GROUP_BULLET; }
  inline bool IsBelt() const { return GetItemGroup() == ItemGroup::GROUP_BELT; }
  inline bool IsWeapon() const { return GetItemGroup() == ItemGroup::GROUP_WEAPON; }
  inline bool IsAccessory() const { return GetItemGroup() == ItemGroup::GROUP_ACCESSORY; }
  inline bool IsItemCard() const { return GetItemGroup() == ItemGroup::GROUP_ITEMCARD; }
  inline bool IsSummonCard() const { return GetItemGroup() == ItemGroup::GROUP_SUMMONCARD; }
  inline bool IsSkillCard() const { return GetItemGroup() == ItemGroup::GROUP_SKILLCARD; }
  inline bool IsSpellCard() const { return GetItemGroup() == ItemGroup::GROUP_SPELLCARD; }
  inline bool IsStrikeCube() const { return GetItemGroup() == ItemGroup::GROUP_STRIKE_CUBE; }
  inline bool IsDefenceCube() const { return GetItemGroup() == ItemGroup::GROUP_DEFENCE_CUBE; }
  inline bool IsSkillCube() const { return GetItemGroup() == ItemGroup::GROUP_SKILL_CUBE; }
  inline bool IsArtifact() const { return GetItemGroup() == ItemGroup::GROUP_ARTIFACT; }

  inline bool IsTwoHandItem() const { return GetItemBase()->wear_type == ItemWearType::WEAR_TWOHAND; }

  bool IsInInventory() const;
  bool IsInStorage() const;

  void DBUpdate();
  void DBInsert();
  void SetCurrentEndurance(int n);
  int GetMaxEndurance() const;
  void SetBindTarget(Unit *pUnit);

  ItemWearType GetWearType();
  int GetLevelLimit();
  int GetItemRank() const;
  bool IsBow();
  bool IsCrossBow();
  bool IsCashItem();

  bool IsItem() const override { return true; }

  void SetOwnerInfo(uint, int, int);
  void SetPickupOrder(const ItemPickupOrder &order);

  // private:
  ItemInstance m_Instance{};
  ItemTemplate *m_pItemBase{};
  Summon *m_pSummon{nullptr};
  uint m_nHandle{0};
  int m_nAccountID{0};
  int m_nItemID{0};
  uint m_hBindedTarget{0};
  uint m_unInventoryIndex{0};
  uint m_nDropTime{0};
  bool m_bIsEventDrop{0};
  bool m_bIsVirtualItem{0};
  bool m_bIsNeedUpdateToDB{false};
  ItemPickupOrder m_pPickupOrder{};
};