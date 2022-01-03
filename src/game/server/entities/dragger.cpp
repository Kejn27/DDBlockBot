/* (c) Shereef Marzouk. See "licence DDRace.txt" and the readme.txt in the root of the distribution for more information. */
#include "dragger.h"
#include <engine/config.h>
#include <engine/server.h>
#include <engine/shared/config.h>
#include <game/generated/protocol.h>
#include <game/server/gamecontext.h>
#include <game/server/gamemodes/DDRace.h>
#include <game/server/player.h>
#include <game/server/teams.h>
#include <game/version.h>

#include "character.h"

CDragger::CDragger(CGameWorld *pGameWorld, vec2 Pos, float Strength, bool NW,
	int CaughtTeam, int Layer, int Number) :
	CEntity(pGameWorld, CGameWorld::ENTTYPE_LASER)
{
	m_Target = 0;
	m_Layer = Layer;
	m_Number = Number;
	m_Pos = Pos;
	m_Strength = Strength;
	m_EvalTick = Server()->Tick();
	m_NW = NW;
	m_CaughtTeam = CaughtTeam;
	GameWorld()->InsertEntity(this);

	for(int &SoloID : m_SoloIDs)
	{
		SoloID = -1;
	}
}

void CDragger::Move()
{
	if(m_Target && (!m_Target->IsAlive() || (m_Target->IsAlive() && (m_Target->m_Super || m_Target->IsPaused() || (m_Layer == LAYER_SWITCH && m_Number && !GameServer()->Collision()->m_pSwitchers[m_Number].m_Status[m_Target->Team()])))))
		m_Target = 0;

	mem_zero(m_SoloEnts, sizeof(m_SoloEnts));
	CCharacter *TempEnts[MAX_CLIENTS];

	int Num = GameServer()->m_World.FindEntities(m_Pos, g_Config.m_SvDraggerRange,
		(CEntity **)m_SoloEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
	mem_copy(TempEnts, m_SoloEnts, sizeof(TempEnts));

	int Id = -1;
	int MinLen = 0;
	CCharacter *Temp;
	for(int i = 0; i < Num; i++)
	{
		Temp = m_SoloEnts[i];
		if(Temp->Team() != m_CaughtTeam)
		{
			m_SoloEnts[i] = 0;
			continue;
		}
		if(m_Layer == LAYER_SWITCH && m_Number && !GameServer()->Collision()->m_pSwitchers[m_Number].m_Status[Temp->Team()])
		{
			m_SoloEnts[i] = 0;
			continue;
		}
		int Res =
			m_NW ?
				GameServer()->Collision()->IntersectNoLaserNW(m_Pos, Temp->m_Pos, 0, 0) :
				GameServer()->Collision()->IntersectNoLaser(m_Pos, Temp->m_Pos, 0, 0);

		if(Res == 0)
		{
			int Len = length(Temp->m_Pos - m_Pos);
			if(MinLen == 0 || MinLen > Len)
			{
				MinLen = Len;
				Id = i;
			}

			if(!Temp->Teams()->m_Core.GetSolo(Temp->GetPlayer()->GetCID()))
				m_SoloEnts[i] = 0;
		}
		else
		{
			m_SoloEnts[i] = 0;
		}
	}

	if(!m_Target)
		m_Target = Id != -1 ? TempEnts[Id] : 0;

	if(m_Target)
	{
		for(auto &SoloEnt : m_SoloEnts)
		{
			if(SoloEnt == m_Target)
				SoloEnt = 0;
		}
	}
}

void CDragger::Drag()
{
	if(!m_Target)
		return;

	CCharacter *Target = m_Target;

	for(int i = -1; i < MAX_CLIENTS; i++)
	{
		if(i >= 0)
			Target = m_SoloEnts[i];

		if(!Target)
			continue;

		int Res = 0;
		if(!m_NW)
			Res = GameServer()->Collision()->IntersectNoLaser(m_Pos,
				Target->m_Pos, 0, 0);
		else
			Res = GameServer()->Collision()->IntersectNoLaserNW(m_Pos,
				Target->m_Pos, 0, 0);
		if(Res || length(m_Pos - Target->m_Pos) > g_Config.m_SvDraggerRange)
		{
			Target = 0;
			if(i == -1)
				m_Target = 0;
			else
				m_SoloEnts[i] = 0;
		}
		else if(length(m_Pos - Target->m_Pos) > 28)
		{
			vec2 Temp = Target->Core()->m_Vel + (normalize(m_Pos - Target->m_Pos) * m_Strength);
			Target->Core()->m_Vel = ClampVel(Target->m_MoveRestrictions, Temp);
		}
	}
}

void CDragger::Reset()
{
	m_MarkedForDestroy = true;
}

void CDragger::Tick()
{
	if(((CGameControllerDDRace *)GameServer()->m_pController)->m_Teams.GetTeamState(m_CaughtTeam) == CGameTeams::TEAMSTATE_EMPTY)
		return;
	if(Server()->Tick() % int(Server()->TickSpeed() * 0.15f) == 0)
	{
		int Flags;
		m_EvalTick = Server()->Tick();
		int index = GameServer()->Collision()->IsMover(m_Pos.x, m_Pos.y,
			&Flags);
		if(index)
		{
			m_Core = GameServer()->Collision()->CpSpeed(index, Flags);
		}
		m_Pos += m_Core;
		Move();
	}
	Drag();
	return;
}

void CDragger::Snap(int SnappingClient)
{
	if(((CGameControllerDDRace *)GameServer()->m_pController)->m_Teams.GetTeamState(m_CaughtTeam) == CGameTeams::TEAMSTATE_EMPTY)
		return;

	int SnappingClientVersion = SnappingClient >= 0 ? GameServer()->GetClientVersion(SnappingClient) : CLIENT_VERSIONNR;

	CCharacter *Target = m_Target;

	for(int &SoloID : m_SoloIDs)
	{
		if(SoloID == -1)
			break;

		Server()->SnapFreeID(SoloID);
		SoloID = -1;
	}

	int pos = 0;

	for(int i = -1; i < MAX_CLIENTS; i++)
	{
		if(i >= 0)
		{
			Target = m_SoloEnts[i];

			if(!Target)
				continue;
		}

		if(Target)
		{
			if(NetworkClipped(SnappingClient, m_Pos) && NetworkClipped(SnappingClient, Target->m_Pos))
				continue;
		}
		else if(NetworkClipped(SnappingClient, m_Pos))
			continue;

		CCharacter *Char = GameServer()->GetPlayerChar(SnappingClient);

		if(SnappingClient > -1 && (GameServer()->m_apPlayers[SnappingClient]->GetTeam() == -1 || GameServer()->m_apPlayers[SnappingClient]->IsPaused()) && GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID != SPEC_FREEVIEW)
			Char = GameServer()->GetPlayerChar(GameServer()->m_apPlayers[SnappingClient]->m_SpectatorID);

		if(i != -1 || SnappingClientVersion < VERSION_DDNET_SWITCH)
		{
			int Tick = (Server()->Tick() % Server()->TickSpeed()) % 11;
			if(Char && Char->IsAlive() && (m_Layer == LAYER_SWITCH && m_Number && !GameServer()->Collision()->m_pSwitchers[m_Number].m_Status[Char->Team()] && (!Tick)))
				continue;
		}

		if(Char && Char->IsAlive())
		{
			if(Char->Team() != m_CaughtTeam)
				continue;
		}
		else
		{
			// send to spectators only active draggers and some inactive from team 0
			if(!((Target && Target->IsAlive()) || m_CaughtTeam == 0))
				continue;
		}

		if(Char && Char->IsAlive() && Target && Target->IsAlive() && Target->GetPlayer()->GetCID() != Char->GetPlayer()->GetCID() && ((Char->GetPlayer()->m_ShowOthers == 0 && (Char->Teams()->m_Core.GetSolo(SnappingClient) || Char->Teams()->m_Core.GetSolo(Target->GetPlayer()->GetCID()))) || (Char->GetPlayer()->m_ShowOthers == 2 && !Target->SameTeam(SnappingClient))))
		{
			continue;
		}

		CNetObj_Laser *obj;

		if(i == -1)
		{
			obj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem(
				NETOBJTYPE_LASER, GetID(), sizeof(CNetObj_Laser)));

			CNetObj_EntityEx *pEntData = static_cast<CNetObj_EntityEx *>(Server()->SnapNewItem(NETOBJTYPE_ENTITYEX, GetID(), sizeof(CNetObj_EntityEx)));
			if(!pEntData)
				return;

			pEntData->m_SwitchNumber = m_Number;
			pEntData->m_Layer = m_Layer;
			pEntData->m_EntityClass = clamp(ENTITYCLASS_DRAGGER_WEAK + round_to_int(m_Strength) - 1, (int)ENTITYCLASS_DRAGGER_WEAK, (int)ENTITYCLASS_DRAGGER_STRONG);
		}
		else
		{
			m_SoloIDs[pos] = Server()->SnapNewID();
			obj = static_cast<CNetObj_Laser *>(Server()->SnapNewItem( // TODO: Have to free IDs again?
				NETOBJTYPE_LASER, m_SoloIDs[pos], sizeof(CNetObj_Laser)));
			pos++;
		}

		if(!obj)
			continue;
		obj->m_X = (int)m_Pos.x;
		obj->m_Y = (int)m_Pos.y;
		if(Target)
		{
			obj->m_FromX = (int)Target->m_Pos.x;
			obj->m_FromY = (int)Target->m_Pos.y;
		}
		else
		{
			obj->m_FromX = (int)m_Pos.x;
			obj->m_FromY = (int)m_Pos.y;
		}

		if(i != -1 || SnappingClientVersion < VERSION_DDNET_SWITCH)
		{
			int StartTick = m_EvalTick;
			if(StartTick < Server()->Tick() - 4)
				StartTick = Server()->Tick() - 4;
			else if(StartTick > Server()->Tick())
				StartTick = Server()->Tick();
			obj->m_StartTick = StartTick;
		}
		else
		{
			obj->m_StartTick = 0;
		}
	}
}

CDraggerTeam::CDraggerTeam(CGameWorld *pGameWorld, vec2 Pos, float Strength,
	bool NW, int Layer, int Number)
{
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		m_Draggers[i] = new CDragger(pGameWorld, Pos, Strength, NW, i, Layer, Number);
	}
}

//CDraggerTeam::~CDraggerTeam()
//{
//	for (int i = 0; i < MAX_CLIENTS; ++i)
//	{
//		delete m_Draggers[i];
//	}
//}