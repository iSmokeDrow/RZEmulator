//
// Created by xijezu on 17.12.17.
//

#ifndef PROJECT_ITEMFIELDS_H
#define PROJECT_ITEMFIELDS_H

static const long MAX_GOLD_FOR_INVENTORY = 100000000000;

enum FlagBits : uint {
    FB_Card          = 0x01,
    FB_Full          = 0x02,
    FB_Inserted      = 0x04,
    FB_Failed        = 0x08,
    FB_Event         = 0x10,
    FB_ContainsPet   = 0x20,
    FB_Taming        = 0x20000000,
    FB_NonChaosStone = 0x40000000,
    FB_Summon        = 0x80000000,
};

enum ItemFlag : int {
    FLAG_CASHITEM   = 0,
    FLAG_WEAR       = 1,
    FLAG_USE        = 2,
    FLAG_TARGET_USE = 3,
    FLAG_DUPLICATE  = 4,
    FLAG_DROP       = 5,
    FLAG_TRADE      = 6,
    FLAG_SELL       = 7,
    FLAG_STORAGE    = 8,
    FLAG_OVERWEIGHT = 9,
    FLAG_RIDING     = 10,
    FLAG_MOVE       = 11,
    FLAG_SIT        = 12,
    FLAG_ENHANCE    = 13,
    FLAG_QUEST      = 14,
    FLAG_RAID       = 15,
    FLAG_SECROUTE   = 16,
    FLAG_EVENTMAP   = 17,
    FLAG_HUNTAHOLIC = 18
};

enum GenerateCode : int {
    ByMonster        = 0,
    ByMarket         = 1,
    ByQuest          = 2,
    ByScript         = 3,
    ByMix            = 4,
    ByGm             = 5,
    ByBasic          = 6,
    ByTrade          = 7,
    ByDivide         = 8,
    ByItem           = 10,
    ByFieldProp      = 11,
    ByAuction        = 12,
    ByShoveling      = 13,
    ByHuntaholic     = 14,
    ByDonationReward = 15,
    ByUnknown        = 126
};

enum ItemFlag2 : int {
    FlagNone          = -1,
    FlagCard          = 0,
    FlagFull          = 1,
    FlagInserted      = 2,
    FlagFailed        = 3,
    FlagEvent         = 4,
    FlagContainsPet   = 5,
    FlagTaming        = 29,
    FlagNonChaosStone = 30,
    FlagSummon        = 31,
};

enum ItemWearType : int16_t {
    WearCantWear      = -1,
    WearNone          = -1,
    WearWeapon        = 0,
    WearShield        = 1,
    WearArmor         = 2,
    WearHelm          = 3,
    WearGlove         = 4,
    WearBoots         = 5,
    WearBelt          = 6,
    WearMantle        = 7,
    WearArmulet       = 8,
    WearRing          = 9,
    WearSecondRing    = 10,
    WearEarring       = 11,
    WearFace          = 12,
    WearHair          = 13,
    WearDecoWeapon    = 14,
    WearDecoShield    = 15,
    WearDecoArmor     = 16,
    WearDecoHelm      = 17,
    WearDecoGloves    = 18,
    WearDecoBoots     = 19,
    WearDecoMantle    = 20,
    WearDecoShoulder  = 21,
    WearRideItem      = 22,
    WearBagSlot       = 23,
    WearTwoFingerRing = 94,
    WearTwoHand       = 99,
    WearSkill         = 100,
    WearRightHand     = 0,
    WearLeftHand      = 1,
    WearBullet        = 1,
};

enum ItemClass : int {
    ClassEtc              = 0,
    ClassDoubleAxe        = 95,
    ClassDoubleSword      = 96,
    ClassDoubleDagger     = 98,
    ClassEveryWeapon      = 99,
    ClassEtcWeapon        = 100,
    ClassOneHandSword     = 101,
    ClassTwoHandSword     = 102,
    ClassDagger           = 103,
    ClassSpear            = 104,
    ClassTwoHandAxe       = 105,
    ClassOneHandMace      = 106,
    ClassTwoHandMace      = 107,
    ClassHeavyBow         = 108,
    ClassLightBow         = 109,
    ClassCrossBow         = 110,
    ClassOneHandStaff     = 111,
    ClassTwoHandStaff     = 112,
    ClassOneHandAxe       = 113,
    ClassArmor            = 200,
    ClassFighterArmor     = 201,
    ClassHunterArmor      = 202,
    ClassMagicianArmor    = 203,
    ClassSummonerArmor    = 204,
    ClassShield           = 210,
    ClassHelm             = 220,
    ClassBoots            = 230,
    ClassGloves           = 240,
    ClassBelt             = 250,
    ClassMantle           = 260,
    ClassEtcAccessory     = 300,
    ClassRing             = 301,
    ClassEarring          = 302,
    ClassArmulet          = 303,
    ClassEyeglass         = 304,
    ClassMask             = 305,
    ClassCube             = 306,
    ClassBoostChip        = 400,
    ClassSoulStone        = 401,
    ClassDecoShield       = 601,
    ClassDecoArmor        = 602,
    ClassDecoHelm         = 603,
    ClassDecoGloves       = 604,
    ClassDecoBoots        = 605,
    ClassDecoMantle       = 606,
    ClassDecoShoulder     = 607,
    ClassDecoHair         = 608,
    ClassDecoOneHandSword = 609,
    ClassDecoTwoHandSword = 610,
    ClassDecoDagger       = 611,
    ClassDecoTwoHandSpear = 612,
    ClassDecoTwoHandAxe   = 613,
    ClassDecoOneHandMace  = 614,
    ClassDecoTwoHandMace  = 615,
    ClassDecoHeavyBow     = 616,
    ClassDecoLightBow     = 617,
    ClassDecoCrossBow     = 618,
    ClassDecoOneHandStaff = 619,
    ClassDecoTwoHandStaff = 620,
    ClassDecoOneHandAxe   = 621,
};

enum ItemType : int {
    TypeEtc       = 0,
    TypeArmor     = 1,
    TypeCard      = 2,
    TypeSupply    = 3,
    TypeCube      = 4,
    TypeCharm     = 5,
    TypeUse       = 6,
    TypeSoulStone = 7,
    TypeUseCard   = 8,
};

enum ItemGroup : int {
    Etc                     = 0,
    Weapon                  = 1,
    Armor                   = 2,
    Shield                  = 3,
    Helm                    = 4,
    Glove                   = 5,
    Boots                   = 6,
    Belt                    = 7,
    Mantle                  = 8,
    Accessory               = 9,
    SkillCard               = 10,
    ItemCard                = 11,
    SpellCard               = 12,
    SummonCard              = 13,
    Face                    = 15,
    Underwear               = 16,
    Bag                     = 17,
    PetCage                 = 18,
    StrikeCube              = 21,
    DefenseCube             = 22,
    SkillCube               = 23,
    RestorationCube         = 24,
    SoulStone               = 93,
    Bullet                  = 98,
    Consumable              = 99,
    NpcFace                 = 100,
    Deco                    = 110,
    Riding                  = 120,
    ElementaEffector        = 130,
    ElementalEffectEnhancer = 140,
};

#endif //PROJECT_ITEMFIELDS_H
