#include "FieldProp.h"
#include "Object.h"
#include "World.h"

FieldProp *FieldProp::Create(FieldPropDeleteHandler *propDeleteHandler, FieldPropRespawnInfo pPropInfo, uint lifeTime)
{
    auto fp = new FieldProp{propDeleteHandler, pPropInfo};
    fp->nLifeTime = lifeTime;
    fp->SetCurrentXY(pPropInfo.x, pPropInfo.y);
    fp->m_PropInfo.layer = pPropInfo.layer;
    sWorld->AddObjectToWorld(fp);
    return fp;
}

bool FieldProp::IsUsable(Player *pPlayer) const
{
    return false;
}

bool FieldProp::Cast() const
{
    return false;
}

bool FieldProp::UseProp(Player * pPlayer) const
{
    return false;
}

uint FieldProp::GetCastingDelay() const
{
    return 0;
}

FieldProp::FieldProp(FieldPropDeleteHandler *propDeleteHandler, FieldPropRespawnInfo pPropInfo) : WorldObject(true)
{
    _mainType = MT_StaticObject;
    _subType  = ST_FieldProp;
    _objType  = OBJ_STATIC;

    m_pDeleteHandler = propDeleteHandler;
}
