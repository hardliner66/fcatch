/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
#include <new>
#include <base/math.h>
#include <engine/shared/config.h>
#include <engine/map.h>
#include <engine/console.h>
#include "gamecontext.h"
#include <game/version.h>
#include <game/collision.h>
#include <game/server/entities/loltext.h>
#include <game/gamecore.h>
#include "gamemodes/dm.h"
#include "gamemodes/tdm.h"
#include "gamemodes/ctf.h"
#include "gamemodes/mod.h"
#include "gamemodes/grenade.h"
#include "gamemodes/ifreeze.h"

#include "bot.h"
#include <algorithm>

enum
{
	RESET,
	NO_RESET
};

void CGameContext::Construct(int Resetting)
{
	m_Resetting = 0;
	m_pServer = 0;

	m_BotDifficulty = DIFFICULTY_MEDIUM;

	for(int i = 0; i < MAX_CLIENTS; i++)
		m_apPlayers[i] = 0;

	m_pController = 0;
	m_VoteCloseTime = 0;
	m_pVoteOptionFirst = 0;
	m_pVoteOptionLast = 0;
	m_NumVoteOptions = 0;
	m_LockTeams = 0;

	if(Resetting==NO_RESET)
		m_pVoteOptionHeap = new CHeap();

	m_SpecMuted = false;
	m_pBotEngine = new CBotEngine(this);
}

CGameContext::CGameContext(int Resetting)
{
	Construct(Resetting);
}

CGameContext::CGameContext()
{
	Construct(NO_RESET);
}

CGameContext::~CGameContext()
{
	for(int i = 0; i < MAX_CLIENTS; i++)
		delete m_apPlayers[i];
	if(!m_Resetting)
		delete m_pVoteOptionHeap;
	delete m_pBotEngine;
}

void CGameContext::Clear()
{
	CHeap *pVoteOptionHeap = m_pVoteOptionHeap;
	CVoteOptionServer *pVoteOptionFirst = m_pVoteOptionFirst;
	CVoteOptionServer *pVoteOptionLast = m_pVoteOptionLast;
	int NumVoteOptions = m_NumVoteOptions;
	CTuningParams Tuning = m_Tuning;

	m_Resetting = true;
	this->~CGameContext();
	mem_zero(this, sizeof(*this));
	new (this) CGameContext(RESET);

	m_pVoteOptionHeap = pVoteOptionHeap;
	m_pVoteOptionFirst = pVoteOptionFirst;
	m_pVoteOptionLast = pVoteOptionLast;
	m_NumVoteOptions = NumVoteOptions;
	m_Tuning = Tuning;
}


class CCharacter *CGameContext::GetPlayerChar(int ClientID)
{
	if(ClientID < 0 || ClientID >= MAX_CLIENTS || !m_apPlayers[ClientID])
		return 0;
	return m_apPlayers[ClientID]->GetCharacter();
}

void CGameContext::CreateDamageInd(vec2 Pos, float Angle, int Amount)
{
	float a = 3 * 3.14159f / 2 + Angle;
	//float a = get_angle(dir);
	float s = a-pi/3;
	float e = a+pi/3;
	for(int i = 0; i < Amount; i++)
	{
		float f = mix(s, e, float(i+1)/float(Amount+2));
		CNetEvent_DamageInd *pEvent = (CNetEvent_DamageInd *)m_Events.Create(NETEVENTTYPE_DAMAGEIND, sizeof(CNetEvent_DamageInd));
		if(pEvent)
		{
			pEvent->m_X = (int)Pos.x;
			pEvent->m_Y = (int)Pos.y;
			pEvent->m_Angle = (int)(f*256.0f);
		}
	}
}

void CGameContext::CreateHammerHit(vec2 Pos)
{
	// create the event
	CNetEvent_HammerHit *pEvent = (CNetEvent_HammerHit *)m_Events.Create(NETEVENTTYPE_HAMMERHIT, sizeof(CNetEvent_HammerHit));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}
}


void CGameContext::CreateExplosion(vec2 Pos, int Owner, int Weapon, bool NoDamage)
{
	// create the event
	CNetEvent_Explosion *pEvent = (CNetEvent_Explosion *)m_Events.Create(NETEVENTTYPE_EXPLOSION, sizeof(CNetEvent_Explosion));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
	}

	if (!NoDamage)
	{
		// deal damage
		CCharacter *apEnts[MAX_CLIENTS];
		float Radius = 135.0f;
		float InnerRadius = 48.0f;
		int Num = m_World.FindEntities(Pos, Radius, (CEntity**)apEnts, MAX_CLIENTS, CGameWorld::ENTTYPE_CHARACTER);
		for(int i = 0; i < Num; i++)
		{
			vec2 Diff = apEnts[i]->m_Pos - Pos;
			vec2 ForceDir(0,1);
			float l = length(Diff);
			if(l)
				ForceDir = normalize(Diff);
			l = 1-clamp((l-InnerRadius)/(Radius-InnerRadius), 0.0f, 1.0f);
			float Dmg = 6 * l;
			if((int)Dmg)
				apEnts[i]->TakeDamage(ForceDir*Dmg*2, (int)Dmg, Owner, Weapon);
		}
	}
}

/*
void create_smoke(vec2 Pos)
{
	// create the event
	EV_EXPLOSION *pEvent = (EV_EXPLOSION *)events.create(EVENT_SMOKE, sizeof(EV_EXPLOSION));
	if(pEvent)
	{
		pEvent->x = (int)Pos.x;
		pEvent->y = (int)Pos.y;
	}
}*/

void CGameContext::CreatePlayerSpawn(vec2 Pos)
{
	// create the event
	CNetEvent_Spawn *ev = (CNetEvent_Spawn *)m_Events.Create(NETEVENTTYPE_SPAWN, sizeof(CNetEvent_Spawn));
	if(ev)
	{
		ev->m_X = (int)Pos.x;
		ev->m_Y = (int)Pos.y;
	}
}

void CGameContext::CreateDeath(vec2 Pos, int ClientID)
{
	// create the event
	CNetEvent_Death *pEvent = (CNetEvent_Death *)m_Events.Create(NETEVENTTYPE_DEATH, sizeof(CNetEvent_Death));
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_ClientID = ClientID;
	}
}

void CGameContext::CreateSound(vec2 Pos, int Sound, int Mask)
{
	if (Sound < 0)
		return;

	// create a sound
	CNetEvent_SoundWorld *pEvent = (CNetEvent_SoundWorld *)m_Events.Create(NETEVENTTYPE_SOUNDWORLD, sizeof(CNetEvent_SoundWorld), Mask);
	if(pEvent)
	{
		pEvent->m_X = (int)Pos.x;
		pEvent->m_Y = (int)Pos.y;
		pEvent->m_SoundID = Sound;
	}
}

void CGameContext::CreateSoundGlobal(int Sound, int Target)
{
	if (Sound < 0)
		return;

	CNetMsg_Sv_SoundGlobal Msg;
	Msg.m_SoundID = Sound;
	if(Target == -2)
		Server()->SendPackMsg(&Msg, MSGFLAG_NOSEND, -1);
	else
	{
		int Flag = MSGFLAG_VITAL;
		if(Target != -1)
			Flag |= MSGFLAG_NORECORD;
		Server()->SendPackMsg(&Msg, Flag, Target);
	}
}


void CGameContext::SendChatTarget(int To, const char *pText)
{
	CNetMsg_Sv_Chat Msg;
	Msg.m_Team = 0;
	Msg.m_ClientID = -1;
	Msg.m_pMessage = pText;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, To);
}


void CGameContext::SendChat(int ChatterClientID, int Team, const char *pText)
{
	char aBuf[256];
	if(ChatterClientID >= 0 && ChatterClientID < MAX_CLIENTS)
		str_format(aBuf, sizeof(aBuf), "%d:%d:%s: %s", ChatterClientID, Team, Server()->ClientName(ChatterClientID), pText);
	else
		str_format(aBuf, sizeof(aBuf), "*** %s", pText);
	Console()->Print(IConsole::OUTPUT_LEVEL_ADDINFO, Team!=CHAT_ALL?"teamchat":"chat", aBuf);

	if(Team == CHAT_ALL)
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 0;
		Msg.m_ClientID = ChatterClientID;
		Msg.m_pMessage = pText;
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
	}
	else
	{
		CNetMsg_Sv_Chat Msg;
		Msg.m_Team = 1;
		Msg.m_ClientID = ChatterClientID;
		Msg.m_pMessage = pText;

		// pack one for the recording only
		Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NOSEND, -1);

		// send to the clients
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() == Team)
				Server()->SendPackMsg(&Msg, MSGFLAG_VITAL|MSGFLAG_NORECORD, i);
		}
	}
}

void CGameContext::SendEmoticon(int ClientID, int Emoticon)
{
	CNetMsg_Sv_Emoticon Msg;
	Msg.m_ClientID = ClientID;
	Msg.m_Emoticon = Emoticon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, -1);
}

void CGameContext::SendWeaponPickup(int ClientID, int Weapon)
{
	CNetMsg_Sv_WeaponPickup Msg;
	Msg.m_Weapon = Weapon;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}


void CGameContext::SendBroadcast(const char *pText, int ClientID)
{
	CNetMsg_Sv_Broadcast Msg;
	Msg.m_pMessage = pText;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

//
bool CGameContext::CanStartVote(CPlayer *pPlayer)
{
	if(g_Config.m_SvSpamprotection && pPlayer->m_LastVoteTry && pPlayer->m_LastVoteTry+Server()->TickSpeed()*3 > Server()->Tick())
		return false;

	int64 Now = Server()->Tick();
	pPlayer->m_LastVoteTry = Now;
	if(pPlayer->GetTeam() == TEAM_SPECTATORS)
	{
		SendChatTarget(pPlayer->GetCID(), "Spectators aren't allowed to start a vote.");
		return false;
	}

	if(m_VoteCloseTime)
	{
		SendChatTarget(pPlayer->GetCID(), "Wait for current vote to end before calling a new one.");
		return false;
	}

	int Timeleft = pPlayer->m_LastVoteCall + Server()->TickSpeed()*60 - Now;
	if(pPlayer->m_LastVoteCall && Timeleft > 0)
	{
		char aChatmsg[512] = {0};
		str_format(aChatmsg, sizeof(aChatmsg), "You must wait %d seconds before making another vote", (Timeleft/Server()->TickSpeed())+1);
		SendChatTarget(pPlayer->GetCID(), aChatmsg);
		return false;
	}
	return true;
}

void CGameContext::StartVote(const char *pDesc, const char *pCommand, const char *pReason)
{
	// check if a vote is already running
	if(m_VoteCloseTime)
		return;

	// reset votes
	m_VoteEnforce = VOTE_ENFORCE_UNKNOWN;
	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			m_apPlayers[i]->m_Vote = 0;
			m_apPlayers[i]->m_VotePos = 0;
		}
	}

	// start vote
	m_VoteCloseTime = time_get() + time_freq()*25;
	str_copy(m_aVoteDescription, pDesc, sizeof(m_aVoteDescription));
	str_copy(m_aVoteCommand, pCommand, sizeof(m_aVoteCommand));
	str_copy(m_aVoteReason, pReason, sizeof(m_aVoteReason));
	SendVoteSet(-1);
	m_VoteUpdate = true;
}

void CGameContext::StartVoteAs(const char *pDesc, const char *pCommand, const char *pReason, CPlayer *pPlayer)
{
	StartVote(pDesc, pCommand, pReason);
	pPlayer->m_Vote = 1;
	pPlayer->m_VotePos = m_VotePos = 1;
	m_VoteCreator = pPlayer->GetCID();
	pPlayer->m_LastVoteCall = Server()->Tick();
}

void CGameContext::EndVote()
{
	m_VoteCloseTime = 0;
	SendVoteSet(-1);
}

void CGameContext::SendVoteSet(int ClientID)
{
	CNetMsg_Sv_VoteSet Msg;
	if(m_VoteCloseTime)
	{
		Msg.m_Timeout = (m_VoteCloseTime-time_get())/time_freq();
		Msg.m_pDescription = m_aVoteDescription;
		Msg.m_pReason = m_aVoteReason;
	}
	else
	{
		Msg.m_Timeout = 0;
		Msg.m_pDescription = "";
		Msg.m_pReason = "";
	}
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendVoteStatus(int ClientID, int Total, int Yes, int No)
{
	CNetMsg_Sv_VoteStatus Msg = {0};
	Msg.m_Total = Total;
	Msg.m_Yes = Yes;
	Msg.m_No = No;
	Msg.m_Pass = Total - (Yes+No);

	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);

}

void CGameContext::AbortVoteKickOnDisconnect(int ClientID)
{
	if(m_VoteCloseTime && ((!str_comp_num(m_aVoteCommand, "kick ", 5) && str_toint(&m_aVoteCommand[5]) == ClientID) ||
		(!str_comp_num(m_aVoteCommand, "set_team ", 9) && str_toint(&m_aVoteCommand[9]) == ClientID)))
		m_VoteCloseTime = -1;
}


void CGameContext::CheckPureTuning()
{
	// might not be created yet during start up
	if(!m_pController)
		return;

	if(	str_comp(m_pController->m_pGameType, "DM")==0 ||
		str_comp(m_pController->m_pGameType, "TDM")==0 ||
		str_comp(m_pController->m_pGameType, "CTF")==0)
	{
		CTuningParams p;
		if(mem_comp(&p, &m_Tuning, sizeof(p)) != 0)
		{
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "resetting tuning due to pure server");
			m_Tuning = p;
		}
	}
}

void CGameContext::SendTuningParams(int ClientID)
{
	CheckPureTuning();

	CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
	int *pParams = (int *)&m_Tuning;
	for(unsigned i = 0; i < sizeof(m_Tuning)/sizeof(int); i++)
		Msg.AddInt(pParams[i]);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SendFreezeTuningParams(int ClientID)
{
	static CTuningParams FakeTuning;

	FakeTuning.m_GroundControlSpeed = 0;
	FakeTuning.m_GroundJumpImpulse = 0;
	FakeTuning.m_GroundControlAccel = 0;
	FakeTuning.m_AirControlSpeed = 0;
	FakeTuning.m_AirJumpImpulse = 0;
	FakeTuning.m_AirControlAccel = 0;
	FakeTuning.m_HookDragSpeed = 0;
	FakeTuning.m_HookDragAccel = 0;
	FakeTuning.m_HookFireSpeed = 0;
	FakeTuning.m_HookLength = 0;

	CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
	int *pParams = (int *)&FakeTuning;
	for(unsigned i = 0; i < sizeof(FakeTuning)/sizeof(int); i++)
		Msg.AddInt(pParams[i]);
	Server()->SendMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::SwapTeams()
{
	if(!m_pController->IsTeamplay())
		return;

	SendChat(-1, CGameContext::CHAT_ALL, "Teams were swapped");

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
			m_apPlayers[i]->SetTeam(m_apPlayers[i]->GetTeam()^1, false);
	}

	(void)m_pController->CheckTeamBalance();
}

void CGameContext::OnTick()
{
	// check tuning
	CheckPureTuning();

	// Check bot number
	CheckBotNumber();

	// Test basic move for bots
	for(int i = 0; i < MAX_CLIENTS ; i++)
	{
		if(!m_apPlayers[i] || !m_apPlayers[i]->m_IsBot)
			continue;
		CNetObj_PlayerInput Input = m_apPlayers[i]->m_pBot->GetLastInputData();
		m_apPlayers[i]->OnPredictedInput(&Input);
	}

	// copy tuning
	m_World.m_Core.m_Tuning = m_Tuning;
	m_World.Tick();

	//if(world.paused) // make sure that the game object always updates
	m_pController->Tick();

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
		{
			m_apPlayers[i]->Tick();
			m_apPlayers[i]->PostTick();
		}
	}

	if(g_Config.m_SvChatMessage[0] && Server()->Tick() % (Server()->TickSpeed()*g_Config.m_SvChatMessageInterval*60) == 0)
	{
		str_sanitize_cc(g_Config.m_SvChatMessage);
		SendChat(-1, CHAT_ALL, g_Config.m_SvChatMessage);
	}

	// update voting
	if(m_VoteCloseTime)
	{
		// abort the kick-vote on player-leave
		if(m_VoteCloseTime == -1)
		{
			SendChat(-1, CGameContext::CHAT_ALL, "Vote aborted");
			EndVote();
		}
		else
		{
			int Total = 0, Yes = 0, No = 0;
			if(m_VoteUpdate)
			{
				// count votes
				char aaBuf[MAX_CLIENTS][NETADDR_MAXSTRSIZE] = {{0}};
				for(int i = 0; i < MAX_CLIENTS; i++)
					if(m_apPlayers[i])
						Server()->GetClientAddr(i, aaBuf[i], NETADDR_MAXSTRSIZE);
				bool aVoteChecked[MAX_CLIENTS] = {0};
				for(int i = 0; i < MAX_CLIENTS; i++)
				{
					if(!m_apPlayers[i] || m_apPlayers[i]->GetTeam() == TEAM_SPECTATORS || aVoteChecked[i] || m_apPlayers[i]->m_IsBot)	// don't count in votes by spectators
						continue;

					int ActVote = m_apPlayers[i]->m_Vote;
					int ActVotePos = m_apPlayers[i]->m_VotePos;

					// check for more players with the same ip (only use the vote of the one who voted first)
					for(int j = i+1; j < MAX_CLIENTS; ++j)
					{
						if(!m_apPlayers[j] || aVoteChecked[j] || str_comp(aaBuf[j], aaBuf[i]))
							continue;

						aVoteChecked[j] = true;
						if(m_apPlayers[j]->m_Vote && (!ActVote || ActVotePos > m_apPlayers[j]->m_VotePos))
						{
							ActVote = m_apPlayers[j]->m_Vote;
							ActVotePos = m_apPlayers[j]->m_VotePos;
						}
					}

					Total++;
					if(ActVote > 0)
						Yes++;
					else if(ActVote < 0)
						No++;
				}

				if(Yes >= Total/2+1)
					m_VoteEnforce = VOTE_ENFORCE_YES;
				else if(No >= (Total+1)/2)
					m_VoteEnforce = VOTE_ENFORCE_NO;
			}

			if(m_VoteEnforce == VOTE_ENFORCE_YES)
			{
				Server()->SetRconCID(IServer::RCON_CID_VOTE);
				Console()->ExecuteLine(m_aVoteCommand);
				Server()->SetRconCID(IServer::RCON_CID_SERV);
				EndVote();
				SendChat(-1, CGameContext::CHAT_ALL, "Vote passed");

				if(m_apPlayers[m_VoteCreator])
					m_apPlayers[m_VoteCreator]->m_LastVoteCall = 0;
			}
			else if(m_VoteEnforce == VOTE_ENFORCE_NO || time_get() > m_VoteCloseTime)
			{
				EndVote();
				SendChat(-1, CGameContext::CHAT_ALL, "Vote failed");
			}
			else if(m_VoteUpdate)
			{
				m_VoteUpdate = false;
				SendVoteStatus(-1, Total, Yes, No);
			}
		}
	}

	// Test basic move for bots
	for(int i = 0; i < MAX_CLIENTS ; i++)
	{
		if(!m_apPlayers[i] || !m_apPlayers[i]->m_IsBot)
			continue;
		CNetObj_PlayerInput Input = m_apPlayers[i]->m_pBot->GetInputData();
		m_apPlayers[i]->OnDirectInput(&Input);
	}
#ifdef CONF_DEBUG
	if(g_Config.m_DbgDummies)
	{
		for(int i = 0; i < g_Config.m_DbgDummies ; i++)
		{
			CNetObj_PlayerInput Input = {0};
			Input.m_Direction = (i&1)?-1:1;
			m_apPlayers[MAX_CLIENTS-i-1]->OnPredictedInput(&Input);
		}
	}
#endif
}

// Server hooks
void CGameContext::OnClientDirectInput(int ClientID, void *pInput)
{
	if(!m_World.m_Paused)
		m_apPlayers[ClientID]->OnDirectInput((CNetObj_PlayerInput *)pInput);

	int Flags = ((CNetObj_PlayerInput *)pInput)->m_PlayerFlags;
	if((Flags & 128) || (Flags & 256) || (Flags & 512))
	{
		char addr[NETADDR_MAXSTRSIZE] = {0};
		Server()->GetClientAddr(ClientID, addr, NETADDR_MAXSTRSIZE);
		auto ClientName = Server()->ClientName(ClientID);

		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%s@%s::%d", ClientName, addr, Flags);

		auto id = std::string(aBuf);

		auto p = std::find(m_BotDetects.begin(), m_BotDetects.end(), id);
		if (p == m_BotDetects.end()) {
			m_BotDetects.push_back(id);
			char bBuf[256];
			str_format(bBuf, sizeof(bBuf), "%s using flags %d (bot!)", id.c_str(), Flags);
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "botdetect", bBuf);
		}
	}
}

void CGameContext::OnClientPredictedInput(int ClientID, void *pInput)
{
	if(!m_World.m_Paused)
		m_apPlayers[ClientID]->OnPredictedInput((CNetObj_PlayerInput *)pInput);
}

void CGameContext::OnClientEnter(int ClientID)
{
	//world.insert_entity(&players[client_id]);
	m_apPlayers[ClientID]->Respawn();
	char aBuf[512];
	str_format(aBuf, sizeof(aBuf), "'%s' entered and joined the %s", Server()->ClientName(ClientID), m_pController->GetTeamName(m_apPlayers[ClientID]->GetTeam()));
	SendChat(-1, CGameContext::CHAT_ALL, aBuf);

	str_format(aBuf, sizeof(aBuf), "team_join player='%d:%s' team=%d", ClientID, Server()->ClientName(ClientID), m_apPlayers[ClientID]->GetTeam());
	Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "game", aBuf);

	m_VoteUpdate = true;
	int Pl = 0;
	for(int i = 0; i < MAX_CLIENTS; i++)
		if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
			Pl++;
	if(Pl > 2 && m_pController->IsIFreeze() && g_Config.m_SvIFreezeJoinFrozen)
		m_apPlayers[ClientID]->m_FreezeOnSpawn = true;
}

void CGameContext::OnClientConnected(int ClientID)
{
	//Check if the slot is used by a bot
	if(m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_IsBot)
	{
		delete m_apPlayers[ClientID];
		m_apPlayers[ClientID] = 0;
	}
	// Check which team the player should be on
	int StartTeam = g_Config.m_SvTournamentMode ? TEAM_SPECTATORS : m_pController->GetAutoTeam(ClientID);

	if (g_Config.m_SvBotsEnabled && g_Config.m_SvBotVsHuman && (StartTeam != TEAM_SPECTATORS)) {
		if (m_pController->m_PlayerTeamRed) {
			StartTeam = TEAM_RED;
		} else {
			StartTeam = TEAM_BLUE;
		}
	}

	m_apPlayers[ClientID] = new(ClientID) CPlayer(this, ClientID, StartTeam);
	//players[client_id].init(client_id);
	//players[client_id].client_id = client_id;

	(void)m_pController->CheckTeamBalance();

	CheckBotNumber();

#ifdef CONF_DEBUG
	if(g_Config.m_DbgDummies)
	{
		if(ClientID >= MAX_CLIENTS-g_Config.m_DbgDummies)
			return;
	}
#endif

	// send active vote
	if(m_VoteCloseTime)
		SendVoteSet(ClientID);

	// send motd
	CNetMsg_Sv_Motd Msg;
	Msg.m_pMessage = g_Config.m_SvMotd;
	Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, ClientID);
}

void CGameContext::OnClientDrop(int ClientID, const char *pReason)
{
	AbortVoteKickOnDisconnect(ClientID);
	m_apPlayers[ClientID]->OnDisconnect(pReason);
	delete m_apPlayers[ClientID];
	m_apPlayers[ClientID] = 0;

	(void)m_pController->CheckTeamBalance();
	m_VoteUpdate = true;

	// update spectator modes and last pm-id and count how many players are still ingame
	int PlIngame = 0;
	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(!m_apPlayers[i])
			continue;
		PlIngame++;
		if(m_apPlayers[i]->m_SpectatorID == ClientID)
			m_apPlayers[i]->m_SpectatorID = SPEC_FREEVIEW;
		if(m_apPlayers[i]->m_LastPMReceivedFrom == ClientID)
			m_apPlayers[i]->m_LastPMReceivedFrom = -2;
	}

	if(PlIngame == 0)
		m_World.m_Paused = false;
}

void CGameContext::OnMessage(int MsgID, CUnpacker *pUnpacker, int ClientID)
{
	void *pRawMsg = m_NetObjHandler.SecureUnpackMsg(MsgID, pUnpacker);
	CPlayer *pPlayer = m_apPlayers[ClientID];

	if (pPlayer) {
		int Version = pUnpacker->GetInt();
		int cid = pPlayer->GetCID();

		if (MsgID == (NETMSGTYPE_CL_CALLVOTE + 1)) {
			char buf[128] = { 0 };
			int botcl = (Version < 100 || Version == 12073 ||
						Version == 405 || Version == 502 ||
						Version == 602 || Version == 605 ||
						Version == 1 ||   Version == 708);
			if (botcl) {
				char addr[NETADDR_MAXSTRSIZE] = {0};
				Server()->GetClientAddr(ClientID, addr, NETADDR_MAXSTRSIZE);
				auto ClientName = Server()->ClientName(ClientID);

				char id[MAX_NAME_LENGTH+NETADDR_MAXSTRSIZE];
				str_format(id, sizeof(id), "%s@%s", ClientName, addr);
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "%s using version %d (bot!)", id, Version);
				Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "botdetect", aBuf);
				return;
			}

			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "%s using version %d", Server()->ClientName(cid), Version);
			Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "botdetect", aBuf);
			return;
		}
	}

	if(!pRawMsg)
	{
		if(g_Config.m_Debug)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "dropped weird message '%s' (%d), failed on '%s'", m_NetObjHandler.GetMsgName(MsgID), MsgID, m_NetObjHandler.FailedMsgOn());
			Console()->Print(IConsole::OUTPUT_LEVEL_DEBUG, "server", aBuf);
		}
		return;
	}

	if(Server()->ClientIngame(ClientID))
	{
		if(MsgID == NETMSGTYPE_CL_SAY)
		{
			if(g_Config.m_SvSpamprotection && pPlayer->m_LastChat && pPlayer->m_LastChat+Server()->TickSpeed() > Server()->Tick())
				return;

			CNetMsg_Cl_Say *pMsg = (CNetMsg_Cl_Say *)pRawMsg;
			int Team = pMsg->m_Team ? pPlayer->GetTeam() : CGameContext::CHAT_ALL;

			// trim right and set maximum length to 128 utf8-characters
			int Length = 0;
			const char *p = pMsg->m_pMessage;
			const char *pEnd = 0;
			while(*p)
 			{
				const char *pStrOld = p;
				int Code = str_utf8_decode(&p);

				// check if unicode is not empty
				if(Code > 0x20 && Code != 0xA0 && Code != 0x034F && (Code < 0x2000 || Code > 0x200F) && (Code < 0x2028 || Code > 0x202F) &&
					(Code < 0x205F || Code > 0x2064) && (Code < 0x206A || Code > 0x206F) && (Code < 0xFE00 || Code > 0xFE0F) &&
					Code != 0xFEFF && (Code < 0xFFF9 || Code > 0xFFFC))
				{
					pEnd = 0;
				}
				else if(pEnd == 0)
					pEnd = pStrOld;

				if(++Length >= 511)
				{
					*(const_cast<char *>(p)) = 0;
					break;
				}
 			}
			if(pEnd != 0)
				*(const_cast<char *>(pEnd)) = 0;

			// drop empty and autocreated spam messages (more than 16 characters per second)
			if(Length == 0 || (g_Config.m_SvSpamprotection && pPlayer->m_LastChat && pPlayer->m_LastChat+Server()->TickSpeed()*((15+Length)/16) > Server()->Tick()))
				return;

			pPlayer->m_LastChat = Server()->Tick();

			//Check if the player is muted
			CMute::CMuteEntry *pMute = m_Mute.Muted(ClientID);
			if(pMute)
			{
				char aBuf[128];
				int Expires = (pMute->m_Expires - Server()->Tick())/Server()->TickSpeed();
				str_format(aBuf, sizeof(aBuf), "You are muted for %d minutes and %d seconds.", Expires/60, Expires%60);
				SendChatTarget(ClientID, aBuf);
				return;
			}
			//mute the player if he's spamming
			else if(g_Config.m_SvMuteDuration && ((pPlayer->m_ChatTicks += g_Config.m_SvChatValue) > g_Config.m_SvChatThreshold) && Team == CGameContext::CHAT_ALL)
			{
				m_Mute.AddMute(ClientID, g_Config.m_SvMuteDuration);
				pPlayer->m_ChatTicks = 0;
				return;
			}

			if(str_length(pMsg->m_pMessage) >= 512)
			{
				SendChatTarget(ClientID, "Your Message is too long.");
				return;
			}

			// check for invalid chars
			unsigned char *pMessage = (unsigned char *)pMsg->m_pMessage;
			while (*pMessage)
			{
				if(*pMessage < 32)
					*pMessage = ' ';
				pMessage++;
			}

			if(ShowCommand(ClientID, pPlayer, pMsg->m_pMessage, &Team))
			{
				if(g_Config.m_SvChatMaxDuplicates != -1)
				{
					if(str_comp_num(pPlayer->m_aOldChatMsg, pMsg->m_pMessage, sizeof(pPlayer->m_aOldChatMsg)) == 0)
					{
						if(pPlayer->m_OldChatMsgCount++ >= g_Config.m_SvChatMaxDuplicates)
						{
							SendChatTarget(ClientID, "You are trying to send too many identical messages.");
							return;
						}
					}
					else
					{
						pPlayer->m_OldChatMsgCount = 0;
						str_copy(pPlayer->m_aOldChatMsg, pMsg->m_pMessage, sizeof(pPlayer->m_aOldChatMsg));
					}
				}

				// force redirecting of messages
				if(m_SpecMuted && pPlayer->GetTeam() == TEAM_SPECTATORS)
					Team = CGameContext::CHAT_SPEC;

				//Lowercase a string (Only support for latin letters)
				if(g_Config.m_SvAntiCapslock && CheckForCapslock(pMsg->m_pMessage))
				{
					int Char, CurrentPos = 0;
					char aNewMsg[512] = {0};
					while(*pMsg->m_pMessage)
					{
						Char = str_utf8_decode(&pMsg->m_pMessage);
						CurrentPos += str_utf8_encode(&aNewMsg[CurrentPos], str_utf8_tolower(Char));
					}
					SendChat(ClientID, Team, aNewMsg);
				}
				else
					SendChat(ClientID, Team, pMsg->m_pMessage);
			}
		}
		else if(MsgID == NETMSGTYPE_CL_CALLVOTE)
		{
			if(!CanStartVote(pPlayer))
				return;

			char aChatmsg[512] = {0};
			char aDesc[VOTE_DESC_LENGTH] = {0};
			char aCmd[VOTE_CMD_LENGTH] = {0};
			CNetMsg_Cl_CallVote *pMsg = (CNetMsg_Cl_CallVote *)pRawMsg;
			const char *pReason = pMsg->m_Reason[0] ? pMsg->m_Reason : "No reason given";

			if(g_Config.m_SvVoteForceReason && !pMsg->m_Reason[0] && str_comp_nocase(pMsg->m_Type, "option") != 0)
			{
				SendChatTarget(ClientID, "You must give a reason for your vote.");
				return;
			}

			if(str_comp_nocase(pMsg->m_Type, "option") == 0)
			{
				CVoteOptionServer *pOption = m_pVoteOptionFirst;
				while(pOption)
				{
					if(str_comp_nocase(pMsg->m_Value, pOption->m_aDescription) == 0)
					{
						str_format(aChatmsg, sizeof(aChatmsg), "'%s' called vote to change server option '%s' (%s)", Server()->ClientName(ClientID),
									pOption->m_aDescription, pReason);
						str_format(aDesc, sizeof(aDesc), "%s", pOption->m_aDescription);
						str_format(aCmd, sizeof(aCmd), "%s", pOption->m_aCommand);
						break;
					}

					pOption = pOption->m_pNext;
				}

				if(!pOption)
				{
					str_format(aChatmsg, sizeof(aChatmsg), "'%s' isn't an option on this server", pMsg->m_Value);
					SendChatTarget(ClientID, aChatmsg);
					return;
				}
			}
			else if(str_comp_nocase(pMsg->m_Type, "kick") == 0)
			{
				if(!g_Config.m_SvVoteKick)
				{
					SendChatTarget(ClientID, "Server does not allow voting to kick players");
					return;
				}

				if(g_Config.m_SvVoteKickMin)
				{
					int PlayerNum = 0;
					for(int i = 0; i < MAX_CLIENTS; ++i)
						if(m_apPlayers[i] && m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
							++PlayerNum;

					if(PlayerNum < g_Config.m_SvVoteKickMin)
					{
						str_format(aChatmsg, sizeof(aChatmsg), "Kick voting requires %d players on the server", g_Config.m_SvVoteKickMin);
						SendChatTarget(ClientID, aChatmsg);
						return;
					}
				}

				int KickID = str_toint(pMsg->m_Value);
				if(KickID < 0 || KickID >= MAX_CLIENTS || !m_apPlayers[KickID])
				{
					SendChatTarget(ClientID, "Invalid client id to kick");
					return;
				}
				if(m_apPlayers[KickID]->m_IsBot)
				{
					SendChatTarget(ClientID, "You can't kick server bots");
					return;
				}
				if(KickID == ClientID)
				{
					SendChatTarget(ClientID, "You can't kick yourself");
					return;
				}
				if(Server()->IsAuthed(KickID))
				{
					SendChatTarget(ClientID, "You can't kick admins");
					char aBufKick[256];
					str_format(aBufKick, sizeof(aBufKick), "'%s' called for vote to kick you (Reason: %s)", Server()->ClientName(ClientID), pReason);
					SendChatTarget(KickID, aBufKick);
					return;
				}

				str_format(aChatmsg, sizeof(aChatmsg), "'%s' called for vote to kick '%s' (%s)", Server()->ClientName(ClientID), Server()->ClientName(KickID), pReason);
				str_format(aDesc, sizeof(aDesc), "Kick '%s'", Server()->ClientName(KickID));
				if (!g_Config.m_SvVoteKickBantime)
					str_format(aCmd, sizeof(aCmd), "kick %d Kicked by vote", KickID);
				else
				{
					char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
					Server()->GetClientAddr(KickID, aAddrStr, sizeof(aAddrStr));
					str_format(aCmd, sizeof(aCmd), "ban %s %d Banned by vote", aAddrStr, g_Config.m_SvVoteKickBantime);
				}
			}
			else if(str_comp_nocase(pMsg->m_Type, "spectate") == 0)
			{
				if(!g_Config.m_SvVoteSpectate)
				{
					SendChatTarget(ClientID, "Server does not allow voting to move players to spectators");
					return;
				}

				int SpectateID = str_toint(pMsg->m_Value);
				if(SpectateID < 0 || SpectateID >= MAX_CLIENTS || !m_apPlayers[SpectateID] || m_apPlayers[SpectateID]->GetTeam() == TEAM_SPECTATORS)
				{
					SendChatTarget(ClientID, "Invalid client id to move");
					return;
				}
				if(m_apPlayers[SpectateID]->m_IsBot)
				{
					SendChatTarget(ClientID, "You can't move server bots");
					return;
				}
				if(SpectateID == ClientID)
				{
					SendChatTarget(ClientID, "You can't move yourself");
					return;
				}

				str_format(aChatmsg, sizeof(aChatmsg), "'%s' called for vote to move '%s' to spectators (%s)", Server()->ClientName(ClientID), Server()->ClientName(SpectateID), pReason);
				str_format(aDesc, sizeof(aDesc), "move '%s' to spectators", Server()->ClientName(SpectateID));
				str_format(aCmd, sizeof(aCmd), "set_team %d -1 %d", SpectateID, g_Config.m_SvVoteSpectateRejoindelay);
			}
			else if(str_comp_nocase(pMsg->m_Type, "mute") == 0)
			{
				if(!g_Config.m_SvVoteMute)
				{
					SendChatTarget(ClientID, "Server does not allow voting to mute players");
					return;
				}

				int MuteID = str_toint(pMsg->m_Value);
				if(!IsValidCID(MuteID))
				{
					SendChatTarget(ClientID, "Invalid client id to mute");
					return;
				}
				if(MuteID == ClientID)
				{
					SendChatTarget(ClientID, "You can't mute yourself");
					return;
				}
				if(Server()->IsAuthed(MuteID))
				{
					SendChatTarget(ClientID, "You can't mute admins");
					return;
				}

				str_format(aChatmsg, sizeof(aChatmsg), "'%s' called for vote to mute '%s' (%s)", Server()->ClientName(ClientID), Server()->ClientName(MuteID), pReason);
				str_format(aDesc, sizeof(aDesc), "mute '%s'", Server()->ClientName(MuteID));
				str_format(aCmd, sizeof(aCmd), "mute %d %d", MuteID, g_Config.m_SvVoteMuteDuration);
			}

			if(aCmd[0])
			{
				SendChat(-1, CGameContext::CHAT_ALL, aChatmsg);
				StartVoteAs(aDesc, aCmd, "", pPlayer);
			}
		}
		else if(MsgID == NETMSGTYPE_CL_VOTE)
		{
			if(!m_VoteCloseTime)
				return;

			if(pPlayer->m_Vote == 0)
			{
				CNetMsg_Cl_Vote *pMsg = (CNetMsg_Cl_Vote *)pRawMsg;
				if(!pMsg->m_Vote)
					return;

				pPlayer->m_Vote = pMsg->m_Vote;
				pPlayer->m_VotePos = ++m_VotePos;
				m_VoteUpdate = true;
			}
		}
		else if (MsgID == NETMSGTYPE_CL_SETTEAM && !m_World.m_Paused)
		{
			CNetMsg_Cl_SetTeam *pMsg = (CNetMsg_Cl_SetTeam *)pRawMsg;

			if(pPlayer->GetTeam() == pMsg->m_Team || (g_Config.m_SvSpamprotection && pPlayer->m_LastSetTeam && pPlayer->m_LastSetTeam+Server()->TickSpeed()*3 > Server()->Tick()))
				return;

			if(pMsg->m_Team != TEAM_SPECTATORS && m_LockTeams)
			{
				pPlayer->m_LastSetTeam = Server()->Tick();
				SendBroadcast("Teams are locked", ClientID);
				return;
			}

			if(pPlayer->GetCharacter() && pPlayer->GetCharacter()->Frozen())
			{
				SendChatTarget(ClientID, "You can not change team while you're frozen");
				return;
			}

			if(pPlayer->m_TeamChangeTick > Server()->Tick())
			{
				pPlayer->m_LastSetTeam = Server()->Tick();
				int TimeLeft = (pPlayer->m_TeamChangeTick - Server()->Tick())/Server()->TickSpeed();
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "Time to wait before changing team: %02d:%02d", TimeLeft/60, TimeLeft%60);
				SendBroadcast(aBuf, ClientID);
				return;
			}

			// Switch team on given client and kill/respawn him
			if(m_pController->CanJoinTeam(pMsg->m_Team, ClientID))
			{
				const int team = pPlayer->GetTeam();
				if(m_pController->CanChangeTeam(pPlayer, pMsg->m_Team))
				{
					if (g_Config.m_SvBotsEnabled && g_Config.m_SvBotVsHuman) {
						bool players_active = false;
						for (int i = 0; i < MAX_CLIENTS ; i++) {
							if (m_apPlayers[i] && !m_apPlayers[i]->m_IsBot && (m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)) {
								players_active = true;
								break;
							}
						}

						if (pMsg->m_Team != TEAM_SPECTATORS) {
							if (!players_active) {
								m_pController->m_PlayerTeamRed = !m_pController->m_PlayerTeamRed;
							} else {
								if (pMsg->m_Team == TEAM_RED) {
									pMsg->m_Team = TEAM_BLUE;
								}
								if (pMsg->m_Team == TEAM_BLUE) {
									pMsg->m_Team = TEAM_RED;
								}
								SendBroadcast("Cannot join bot team. Use /switch if you want to change sides.", ClientID);
							}
						}
					}

					pPlayer->m_LastSetTeam = Server()->Tick();
					if(team == TEAM_SPECTATORS || pMsg->m_Team == TEAM_SPECTATORS)
						m_VoteUpdate = true;
					pPlayer->SetTeam(pMsg->m_Team);
					(void)m_pController->CheckTeamBalance();
					pPlayer->m_TeamChangeTick = Server()->Tick();
				}
				else if (team != pMsg->m_Team) {
					if (g_Config.m_SvBotsEnabled && g_Config.m_SvBotVsHuman) {
						char aDesc[VOTE_DESC_LENGTH] = "switch";
						char aCmd[VOTE_CMD_LENGTH] = "switch";

						StartVoteAs(aDesc, aCmd, "", pPlayer);
					} else {
						SendBroadcast("Teams must be balanced, please join other team", ClientID);
					}
				}
			}
			else
			{
				char aBuf[128];
				str_format(aBuf, sizeof(aBuf), "Only %d active players are allowed", Server()->MaxClients()-g_Config.m_SvSpectatorSlots);
				SendBroadcast(aBuf, ClientID);
			}
		}
		else if (MsgID == NETMSGTYPE_CL_SETSPECTATORMODE && !m_World.m_Paused)
		{
			CNetMsg_Cl_SetSpectatorMode *pMsg = (CNetMsg_Cl_SetSpectatorMode *)pRawMsg;

			if(pPlayer->GetTeam() != TEAM_SPECTATORS || pPlayer->m_SpectatorID == pMsg->m_SpectatorID || ClientID == pMsg->m_SpectatorID ||
				(g_Config.m_SvSpamprotection && pPlayer->m_LastSetSpectatorMode && pPlayer->m_LastSetSpectatorMode+Server()->TickSpeed()*3 > Server()->Tick()))
				return;

			pPlayer->m_LastSetSpectatorMode = Server()->Tick();
			if(pMsg->m_SpectatorID != SPEC_FREEVIEW && (!m_apPlayers[pMsg->m_SpectatorID] || m_apPlayers[pMsg->m_SpectatorID]->GetTeam() == TEAM_SPECTATORS))
				SendChatTarget(ClientID, "Invalid spectator id used");
			else
				pPlayer->m_SpectatorID = pMsg->m_SpectatorID;
		}
		else if (MsgID == NETMSGTYPE_CL_CHANGEINFO)
		{
			if(g_Config.m_SvSpamprotection && pPlayer->m_LastChangeInfo && pPlayer->m_LastChangeInfo+Server()->TickSpeed()*5 > Server()->Tick())
				return;

			CNetMsg_Cl_ChangeInfo *pMsg = (CNetMsg_Cl_ChangeInfo *)pRawMsg;
			pPlayer->m_LastChangeInfo = Server()->Tick();

			//Skip leading [F] if set by player
			if(m_pController->IsIFreeze() && str_comp_num(pMsg->m_pName, "[F]", 3) == 0)
				pMsg->m_pName += 3;

			// set infos
			char aOldName[MAX_NAME_LENGTH];
			str_copy(aOldName, Server()->ClientName(ClientID), sizeof(aOldName));
			Server()->SetClientName(ClientID, pMsg->m_pName);
			if(str_comp(aOldName, Server()->ClientName(ClientID)) != 0)
			{
				char aChatText[256];
				str_format(aChatText, sizeof(aChatText), "'%s' changed name to '%s'", aOldName, Server()->ClientName(ClientID));
				SendChat(-1, CGameContext::CHAT_ALL, aChatText);
			}
			Server()->SetClientClan(ClientID, pMsg->m_pClan);
			Server()->SetClientCountry(ClientID, pMsg->m_Country);
			str_copy(pPlayer->m_TeeInfos.m_SkinName, pMsg->m_pSkin, sizeof(pPlayer->m_TeeInfos.m_SkinName));
			pPlayer->m_TeeInfos.m_UseCustomColor = pMsg->m_UseCustomColor;
			pPlayer->m_TeeInfos.m_ColorBody = pMsg->m_ColorBody;
			pPlayer->m_TeeInfos.m_ColorFeet = pMsg->m_ColorFeet;
			m_pController->OnPlayerInfoChange(pPlayer);
		}
		else if (MsgID == NETMSGTYPE_CL_EMOTICON && !m_World.m_Paused)
		{
			CNetMsg_Cl_Emoticon *pMsg = (CNetMsg_Cl_Emoticon *)pRawMsg;

			if(g_Config.m_SvSpamprotection && pPlayer->m_LastEmote && pPlayer->m_LastEmote+Server()->TickSpeed()*3 > Server()->Tick())
				return;

			pPlayer->m_LastEmote = Server()->Tick();

			SendEmoticon(ClientID, pMsg->m_Emoticon);
			//Greyfox emotional tees
			CCharacter* pChr = pPlayer->GetCharacter();

			if(pChr && g_Config.m_SvEmotionalTees)
			{
				int Emote = EMOTE_NORMAL;
				switch(pMsg->m_Emoticon)
				{
					case EMOTICON_EXCLAMATION:
					case EMOTICON_GHOST:
					case EMOTICON_QUESTION:
					case EMOTICON_WTF:
						Emote = EMOTE_SURPRISE;
						break;
					case EMOTICON_DOTDOT:
					case EMOTICON_DROP:
					case EMOTICON_ZZZ:
						Emote = EMOTE_BLINK;
						break;
					case EMOTICON_EYES:
					case EMOTICON_HEARTS:
					case EMOTICON_MUSIC:
						Emote = EMOTE_HAPPY;
						break;
					case EMOTICON_OOP:
					case EMOTICON_SORRY:
						Emote = EMOTE_PAIN;
						break;
					case EMOTICON_DEVILTEE:
					case EMOTICON_SPLATTEE:
					case EMOTICON_ZOMG:
					case EMOTICON_SUSHI:
						Emote = EMOTE_ANGRY;
						break;
				}
				pChr->SetEmote(Emote, Server()->Tick() + 2 * Server()->TickSpeed());
			}
		}
		else if (MsgID == NETMSGTYPE_CL_KILL && !m_World.m_Paused)
		{
			if(pPlayer->m_LastKill && pPlayer->m_LastKill+Server()->TickSpeed()*3 > Server()->Tick())
				return;

			if(pPlayer->GetCharacter() && pPlayer->GetCharacter()->Frozen())
			{
				SendChatTarget(ClientID, "You can not commit suicide while you're frozen");
				pPlayer->m_LastKill = Server()->Tick();
				return;
			}

			pPlayer->m_LastKill = Server()->Tick();
			pPlayer->KillCharacter(WEAPON_SELF);
		}
	}
	else
	{
		if(MsgID == NETMSGTYPE_CL_STARTINFO)
		{
			if(pPlayer->m_IsReady)
				return;

			CNetMsg_Cl_StartInfo *pMsg = (CNetMsg_Cl_StartInfo *)pRawMsg;
			pPlayer->m_LastChangeInfo = Server()->Tick();

			// set start infos
			Server()->SetClientName(ClientID, pMsg->m_pName);
			Server()->SetClientClan(ClientID, pMsg->m_pClan);
			Server()->SetClientCountry(ClientID, pMsg->m_Country);
			str_copy(pPlayer->m_TeeInfos.m_SkinName, pMsg->m_pSkin, sizeof(pPlayer->m_TeeInfos.m_SkinName));
			pPlayer->m_TeeInfos.m_UseCustomColor = pMsg->m_UseCustomColor;
			pPlayer->m_TeeInfos.m_ColorBody = pMsg->m_ColorBody;
			pPlayer->m_TeeInfos.m_ColorFeet = pMsg->m_ColorFeet;
			m_pController->OnPlayerInfoChange(pPlayer);

			// send vote options
			CNetMsg_Sv_VoteClearOptions ClearMsg;
			Server()->SendPackMsg(&ClearMsg, MSGFLAG_VITAL, ClientID);

			CNetMsg_Sv_VoteOptionListAdd OptionMsg;
			int NumOptions = 0;
			OptionMsg.m_pDescription0 = "";
			OptionMsg.m_pDescription1 = "";
			OptionMsg.m_pDescription2 = "";
			OptionMsg.m_pDescription3 = "";
			OptionMsg.m_pDescription4 = "";
			OptionMsg.m_pDescription5 = "";
			OptionMsg.m_pDescription6 = "";
			OptionMsg.m_pDescription7 = "";
			OptionMsg.m_pDescription8 = "";
			OptionMsg.m_pDescription9 = "";
			OptionMsg.m_pDescription10 = "";
			OptionMsg.m_pDescription11 = "";
			OptionMsg.m_pDescription12 = "";
			OptionMsg.m_pDescription13 = "";
			OptionMsg.m_pDescription14 = "";
			CVoteOptionServer *pCurrent = m_pVoteOptionFirst;
			while(pCurrent)
			{
				switch(NumOptions++)
				{
				case 0: OptionMsg.m_pDescription0 = pCurrent->m_aDescription; break;
				case 1: OptionMsg.m_pDescription1 = pCurrent->m_aDescription; break;
				case 2: OptionMsg.m_pDescription2 = pCurrent->m_aDescription; break;
				case 3: OptionMsg.m_pDescription3 = pCurrent->m_aDescription; break;
				case 4: OptionMsg.m_pDescription4 = pCurrent->m_aDescription; break;
				case 5: OptionMsg.m_pDescription5 = pCurrent->m_aDescription; break;
				case 6: OptionMsg.m_pDescription6 = pCurrent->m_aDescription; break;
				case 7: OptionMsg.m_pDescription7 = pCurrent->m_aDescription; break;
				case 8: OptionMsg.m_pDescription8 = pCurrent->m_aDescription; break;
				case 9: OptionMsg.m_pDescription9 = pCurrent->m_aDescription; break;
				case 10: OptionMsg.m_pDescription10 = pCurrent->m_aDescription; break;
				case 11: OptionMsg.m_pDescription11 = pCurrent->m_aDescription; break;
				case 12: OptionMsg.m_pDescription12 = pCurrent->m_aDescription; break;
				case 13: OptionMsg.m_pDescription13 = pCurrent->m_aDescription; break;
				case 14:
					{
						OptionMsg.m_pDescription14 = pCurrent->m_aDescription;
						OptionMsg.m_NumOptions = NumOptions;
						Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, ClientID);
						OptionMsg = CNetMsg_Sv_VoteOptionListAdd();
						NumOptions = 0;
						OptionMsg.m_pDescription1 = "";
						OptionMsg.m_pDescription2 = "";
						OptionMsg.m_pDescription3 = "";
						OptionMsg.m_pDescription4 = "";
						OptionMsg.m_pDescription5 = "";
						OptionMsg.m_pDescription6 = "";
						OptionMsg.m_pDescription7 = "";
						OptionMsg.m_pDescription8 = "";
						OptionMsg.m_pDescription9 = "";
						OptionMsg.m_pDescription10 = "";
						OptionMsg.m_pDescription11 = "";
						OptionMsg.m_pDescription12 = "";
						OptionMsg.m_pDescription13 = "";
						OptionMsg.m_pDescription14 = "";
					}
				}
				pCurrent = pCurrent->m_pNext;
			}
			if(NumOptions > 0)
			{
				OptionMsg.m_NumOptions = NumOptions;
				Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, ClientID);
			}

			// send tuning parameters to client
			SendTuningParams(ClientID);

			// client is ready to enter
			pPlayer->m_IsReady = true;
			CNetMsg_Sv_ReadyToEnter m;
			Server()->SendPackMsg(&m, MSGFLAG_VITAL|MSGFLAG_FLUSH, ClientID);
		}
	}
}

void CGameContext::ConTuneParam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pParamName = pResult->GetString(0);
	float NewValue = pResult->GetFloat(1);

	if(pResult->NumArguments() == 1 && pSelf->Tuning()->Get(pParamName, &NewValue))
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%s is %.2f", pParamName, NewValue);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
	}
	else if(pSelf->Tuning()->Set(pParamName, NewValue))
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "%s changed to %.2f", pParamName, NewValue);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
		pSelf->SendTuningParams(-1);
	}
	else
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "No such tuning parameter");
}

void CGameContext::ConTuneReset(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	CTuningParams TuningParams;
	*pSelf->Tuning() = TuningParams;
	pSelf->SendTuningParams(-1);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", "Tuning reset");
}

void CGameContext::ConTuneDump(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	char aBuf[256];
	for(int i = 0; i < pSelf->Tuning()->Num(); i++)
	{
		float v;
		pSelf->Tuning()->Get(i, &v);
		str_format(aBuf, sizeof(aBuf), "%s %.2f", pSelf->Tuning()->m_apNames[i], v);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "tuning", aBuf);
	}
}

void CGameContext::ConPause(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	if(pSelf->m_pController->IsGameOver())
		return;

	pSelf->m_World.m_Paused ^= 1;
}

void CGameContext::ConChangeMap(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_pController->ChangeMap(pResult->NumArguments() ? pResult->GetString(0) : "");
}

void CGameContext::ConRestart(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pSelf->m_pController->IsWarmup())
	{
		pSelf->m_pController->DoWarmup(0);
		pSelf->m_pController->m_FakeWarmup = 0;
	}
	else
		pSelf->m_pController->DoWarmup(g_Config.m_SvGoTime);
}

void CGameContext::ConBroadcast(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SendBroadcast(pResult->GetString(0), -1);
}

void CGameContext::ConSay(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->SendChat(-1, CGameContext::CHAT_ALL, pResult->GetString(0));
}

void CGameContext::ConSetTeam(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = clamp(pResult->GetInteger(0), 0, (int)MAX_CLIENTS-1);
	int Team = clamp(pResult->GetInteger(1), -1, 1);
	int Delay = pResult->NumArguments()>2 ? pResult->GetInteger(2) : 0;
	if(!pSelf->m_apPlayers[ClientID])
		return;

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "moved client %d to team %d", ClientID, Team);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	pSelf->m_apPlayers[ClientID]->m_TeamChangeTick = pSelf->Server()->Tick()+pSelf->Server()->TickSpeed()*Delay*60;
	pSelf->m_apPlayers[ClientID]->SetTeam(Team);
	(void)pSelf->m_pController->CheckTeamBalance();
}

void CGameContext::ConSetTeamAll(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Team = clamp(pResult->GetInteger(0), -1, 1);

	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "All players were moved to the %s", pSelf->m_pController->GetTeamName(Team));
	pSelf->SendChat(-1, CGameContext::CHAT_ALL, aBuf);

	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(pSelf->m_apPlayers[i])
			pSelf->m_apPlayers[i]->SetTeam(Team, false);

	(void)pSelf->m_pController->CheckTeamBalance();
}

void CGameContext::ConSwapTeams(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if (g_Config.m_SvBotsEnabled && g_Config.m_SvBotVsHuman) {
		pSelf->SendChat(-1, CGameContext::CHAT_ALL, "Cannot swap in bot vs human mode. Use /switch instead.");
		return;
	}

	pSelf->SwapTeams();
}

void CGameContext::ConShuffleTeams(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(!pSelf->m_pController->IsTeamplay())
		return;

	if (g_Config.m_SvBotsEnabled && g_Config.m_SvBotVsHuman) {
		pSelf->SendChat(-1, CGameContext::CHAT_ALL, "Cannot shuffle in bot vs human mode");
		return;
	}

	int CounterRed = 0;
	int CounterBlue = 0;
	int PlayerTeam = 0;
	for(int i = 0; i < MAX_CLIENTS; ++i)
		if(pSelf->m_apPlayers[i] && pSelf->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
			++PlayerTeam;
	PlayerTeam = (PlayerTeam+1)/2;

	pSelf->SendChat(-1, CGameContext::CHAT_ALL, "Teams were shuffled");

	for(int i = 0; i < MAX_CLIENTS; ++i)
	{
		if(pSelf->m_apPlayers[i] && pSelf->m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
		{
			if(CounterRed == PlayerTeam)
				pSelf->m_apPlayers[i]->SetTeam(TEAM_BLUE, false);
			else if(CounterBlue == PlayerTeam)
				pSelf->m_apPlayers[i]->SetTeam(TEAM_RED, false);
			else
			{
				if(rand() % 2)
				{
					pSelf->m_apPlayers[i]->SetTeam(TEAM_BLUE, false);
					++CounterBlue;
				}
				else
				{
					pSelf->m_apPlayers[i]->SetTeam(TEAM_RED, false);
					++CounterRed;
				}
			}
		}
	}

	(void)pSelf->m_pController->CheckTeamBalance();
}

void CGameContext::ConLockTeams(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_LockTeams ^= 1;
	if(pSelf->m_LockTeams)
		pSelf->SendChat(-1, CGameContext::CHAT_ALL, "Teams were locked");
	else
		pSelf->SendChat(-1, CGameContext::CHAT_ALL, "Teams were unlocked");
}

void CGameContext::ConAddVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);
	const char *pCommand = pResult->GetString(1);

	if(pSelf->m_NumVoteOptions == MAX_VOTE_OPTIONS)
	{
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "maximum number of vote options reached");
		return;
	}

	// check for valid option
	if(!pSelf->Console()->LineIsValid(pCommand) || str_length(pCommand) >= VOTE_CMD_LENGTH)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid command '%s'", pCommand);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}
	while(*pDescription && *pDescription == ' ')
		pDescription++;
	if(str_length(pDescription) >= VOTE_DESC_LENGTH || *pDescription == 0)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "skipped invalid option '%s'", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	// check for duplicate entry
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
		{
			char aBuf[256];
			str_format(aBuf, sizeof(aBuf), "option '%s' already exists", pDescription);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return;
		}
		pOption = pOption->m_pNext;
	}

	// add the option
	++pSelf->m_NumVoteOptions;
	int Len = str_length(pCommand);

	pOption = (CVoteOptionServer *)pSelf->m_pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
	pOption->m_pNext = 0;
	pOption->m_pPrev = pSelf->m_pVoteOptionLast;
	if(pOption->m_pPrev)
		pOption->m_pPrev->m_pNext = pOption;
	pSelf->m_pVoteOptionLast = pOption;
	if(!pSelf->m_pVoteOptionFirst)
		pSelf->m_pVoteOptionFirst = pOption;

	str_copy(pOption->m_aDescription, pDescription, sizeof(pOption->m_aDescription));
	mem_copy(pOption->m_aCommand, pCommand, Len+1);
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "added option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	// inform clients about added option
	CNetMsg_Sv_VoteOptionAdd OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);
}

void CGameContext::ConRemoveVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pDescription = pResult->GetString(0);

	// check for valid option
	CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
	while(pOption)
	{
		if(str_comp_nocase(pDescription, pOption->m_aDescription) == 0)
			break;
		pOption = pOption->m_pNext;
	}
	if(!pOption)
	{
		char aBuf[256];
		str_format(aBuf, sizeof(aBuf), "option '%s' does not exist", pDescription);
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
		return;
	}

	// inform clients about removed option
	CNetMsg_Sv_VoteOptionRemove OptionMsg;
	OptionMsg.m_pDescription = pOption->m_aDescription;
	pSelf->Server()->SendPackMsg(&OptionMsg, MSGFLAG_VITAL, -1);

	// TODO: improve this
	// remove the option
	--pSelf->m_NumVoteOptions;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "removed option '%s' '%s'", pOption->m_aDescription, pOption->m_aCommand);
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);

	CHeap *pVoteOptionHeap = new CHeap();
	CVoteOptionServer *pVoteOptionFirst = 0;
	CVoteOptionServer *pVoteOptionLast = 0;
	int NumVoteOptions = pSelf->m_NumVoteOptions;
	for(CVoteOptionServer *pSrc = pSelf->m_pVoteOptionFirst; pSrc; pSrc = pSrc->m_pNext)
	{
		if(pSrc == pOption)
			continue;

		// copy option
		int Len = str_length(pSrc->m_aCommand);
		CVoteOptionServer *pDst = (CVoteOptionServer *)pVoteOptionHeap->Allocate(sizeof(CVoteOptionServer) + Len);
		pDst->m_pNext = 0;
		pDst->m_pPrev = pVoteOptionLast;
		if(pDst->m_pPrev)
			pDst->m_pPrev->m_pNext = pDst;
		pVoteOptionLast = pDst;
		if(!pVoteOptionFirst)
			pVoteOptionFirst = pDst;

		str_copy(pDst->m_aDescription, pSrc->m_aDescription, sizeof(pDst->m_aDescription));
		mem_copy(pDst->m_aCommand, pSrc->m_aCommand, Len+1);
	}

	// clean up
	delete pSelf->m_pVoteOptionHeap;
	pSelf->m_pVoteOptionHeap = pVoteOptionHeap;
	pSelf->m_pVoteOptionFirst = pVoteOptionFirst;
	pSelf->m_pVoteOptionLast = pVoteOptionLast;
	pSelf->m_NumVoteOptions = NumVoteOptions;
}

void CGameContext::ConForceVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	const char *pType = pResult->GetString(0);
	const char *pValue = pResult->GetString(1);
	const char *pReason = pResult->NumArguments() > 2 && pResult->GetString(2)[0] ? pResult->GetString(2) : "No reason given";
	char aBuf[128] = {0};

	if(str_comp_nocase(pType, "option") == 0)
	{
		CVoteOptionServer *pOption = pSelf->m_pVoteOptionFirst;
		while(pOption)
		{
			if(str_comp_nocase(pValue, pOption->m_aDescription) == 0)
			{
				str_format(aBuf, sizeof(aBuf), "admin forced server option '%s' (%s)", pValue, pReason);
				pSelf->SendChatTarget(-1, aBuf);
				pSelf->Console()->ExecuteLine(pOption->m_aCommand);
				break;
			}

			pOption = pOption->m_pNext;
		}

		if(!pOption)
		{
			str_format(aBuf, sizeof(aBuf), "'%s' isn't an option on this server", pValue);
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
			return;
		}
	}
	else if(str_comp_nocase(pType, "kick") == 0)
	{
		int KickID = str_toint(pValue);
		if(KickID < 0 || KickID >= MAX_CLIENTS || !pSelf->m_apPlayers[KickID])
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id to kick");
			return;
		}

		if (!g_Config.m_SvVoteKickBantime)
		{
			str_format(aBuf, sizeof(aBuf), "kick %d %s", KickID, pReason);
			pSelf->Console()->ExecuteLine(aBuf);
		}
		else
		{
			char aAddrStr[NETADDR_MAXSTRSIZE] = {0};
			pSelf->Server()->GetClientAddr(KickID, aAddrStr, sizeof(aAddrStr));
			str_format(aBuf, sizeof(aBuf), "ban %s %d %s", aAddrStr, g_Config.m_SvVoteKickBantime, pReason);
			pSelf->Console()->ExecuteLine(aBuf);
		}
	}
	else if(str_comp_nocase(pType, "spectate") == 0)
	{
		int SpectateID = str_toint(pValue);
		if(SpectateID < 0 || SpectateID >= MAX_CLIENTS || !pSelf->m_apPlayers[SpectateID] || pSelf->m_apPlayers[SpectateID]->GetTeam() == TEAM_SPECTATORS)
		{
			pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "Invalid client id to move");
			return;
		}

		str_format(aBuf, sizeof(aBuf), "admin moved '%s' to spectator (%s)", pSelf->Server()->ClientName(SpectateID), pReason);
		pSelf->SendChatTarget(-1, aBuf);
		str_format(aBuf, sizeof(aBuf), "set_team %d -1 %d", SpectateID, g_Config.m_SvVoteSpectateRejoindelay);
		pSelf->Console()->ExecuteLine(aBuf);
	}
}

void CGameContext::ConClearVotes(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", "cleared votes");
	CNetMsg_Sv_VoteClearOptions VoteClearOptionsMsg;
	pSelf->Server()->SendPackMsg(&VoteClearOptionsMsg, MSGFLAG_VITAL, -1);
	pSelf->m_pVoteOptionHeap->Reset();
	pSelf->m_pVoteOptionFirst = 0;
	pSelf->m_pVoteOptionLast = 0;
	pSelf->m_NumVoteOptions = 0;
}

void CGameContext::ConVote(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;

	// check if there is a vote running
	if(!pSelf->m_VoteCloseTime)
		return;

	if(str_comp_nocase(pResult->GetString(0), "yes") == 0)
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_YES;
	else if(str_comp_nocase(pResult->GetString(0), "no") == 0)
		pSelf->m_VoteEnforce = CGameContext::VOTE_ENFORCE_NO;
	char aBuf[256];
	str_format(aBuf, sizeof(aBuf), "admin forced vote %s", pResult->GetString(0));
	pSelf->SendChatTarget(-1, aBuf);
	str_format(aBuf, sizeof(aBuf), "forcing vote %s", pResult->GetString(0));
	pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "server", aBuf);
}

int CGameContext::CreateLolText(CEntity *pParent, bool Follow, vec2 Pos, vec2 Vel, int Lifespan, const char *pText)
{
	return CLoltext::Create(&m_World, pParent, Pos, Vel, Lifespan, pText, true, Follow);
}

int CGameContext::CreateLolText(CEntity *pParent, const char *pText)
{
	return CLoltext::Create(&m_World, pParent, vec2(0, 0), vec2(0, -0.5f), g_Config.m_SvLoltextLifespan, pText, true, false);
}

void CGameContext::DestroyLolText(int TextID)
{
	CLoltext::Destroy(&m_World, TextID);
}

void CGameContext::ShowStats(int ClientID, int ReceiverID)
{
	char aBuf[32] = { 0 };
	CPlayer *pP = m_apPlayers[ClientID];
	if(ReceiverID != -1)
	{
		pP = m_apPlayers[ReceiverID];
		str_format(aBuf, sizeof(aBuf), "(%s) ", Server()->ClientName(ReceiverID));
	}

	char aaBuf[5][128];
	str_format(aaBuf[0], sizeof(aaBuf[0]), "--- Statistics %s---", aBuf);
	str_format(aaBuf[1], sizeof(aaBuf[1]), "Total Shots: %d", pP->m_Stats.m_TotalShots);
	str_format(aaBuf[2], sizeof(aaBuf[2]), "Kills: %d", pP->m_Stats.m_Kills);
	str_format(aaBuf[3], sizeof(aaBuf[3]), "Deaths: %d", pP->m_Stats.m_Deaths);
	str_format(aaBuf[4], sizeof(aaBuf[4]), "Ratio: %.2f", (pP->m_Stats.m_Deaths > 0) ? ((float) pP->m_Stats.m_Kills / (float) pP->m_Stats.m_Deaths) : 0);

	for (int i = 0; i < 5; i++)
		SendChatTarget(ClientID, aaBuf[i]);
}

bool CGameContext::CheckForCapslock(const char *pStr)
{
	int Lower = 0, Upper = 0, None = 0;
	if(!str_utf8_check(pStr))
		return false;

	while(*pStr)
	{
		int Char = str_utf8_decode(&pStr);

		if(str_islower(Char))
			Lower++;
		else if(str_isupper(Char))
			Upper++;
		else
			None++;
	}

	int Length = Lower + Upper + None;
	if((Length > g_Config.m_SvAntiCapslockMinimum) && ((double)(Lower+None)/Length < g_Config.m_SvAntiCapslockTolerance/10.f))
		return true;

	return false;
}

void CGameContext::ConchainSpecialMotdupdate(IConsole::IResult *pResult, void *pUserData, IConsole::FCommandCallback pfnCallback, void *pCallbackUserData)
{
	pfnCallback(pResult, pCallbackUserData);
	if(pResult->NumArguments())
	{
		CNetMsg_Sv_Motd Msg;
		Msg.m_pMessage = g_Config.m_SvMotd;
		CGameContext *pSelf = (CGameContext *)pUserData;
		for(int i = 0; i < MAX_CLIENTS; ++i)
			if(pSelf->m_apPlayers[i])
				pSelf->Server()->SendPackMsg(&Msg, MSGFLAG_VITAL, i);
	}
}

void CGameContext::ConFreeze(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetInteger(0);

	if(!pSelf->IsValidCID(ClientID) && !pSelf->GetPlayerChar(ClientID))
		return;

	pSelf->GetPlayerChar(ClientID)->Freeze(pResult->GetInteger(1));

	if(pResult->GetInteger(1) == -1)
		pSelf->SendBroadcast("You have been deep-freezed", ClientID);
	else
		pSelf->SendBroadcast("You have been frozen", ClientID);
}

void CGameContext::ConUnFreeze(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetInteger(0);
	if(!pSelf->IsValidCID(ClientID) && !pSelf->GetPlayerChar(ClientID))
			return;
	pSelf->GetPlayerChar(ClientID)->Melt(-1);
}

void CGameContext::ConSwitch(IConsole::IResult *pResult, void *pUserData) {
	if (g_Config.m_SvBotsEnabled && !g_Config.m_SvBotVsHuman) {
		return;
	}
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_pController->DoWarmup(g_Config.m_SvBotSwitchTime);
	pSelf->m_pController->m_PlayerTeamRed = !pSelf->m_pController->m_PlayerTeamRed;
	char aBuf[128];

	str_format(aBuf, sizeof(aBuf), "Switching sides in %d seconds!", g_Config.m_SvBotSwitchTime);
	pSelf->SendChat(-1, CHAT_ALL, aBuf);
}

void CGameContext::ConSetDifficulty(IConsole::IResult *pResult, void *pUserData) {
	if (!g_Config.m_SvBotsEnabled) {
		return;
	}
	CGameContext *pSelf = (CGameContext *)pUserData;
	int difficulty = pResult->GetInteger(0);

	if (!ValidDifficulty(difficulty)) {
		return;
	}

	pSelf->m_BotDifficulty = difficulty;

	switch (difficulty) {
		case DIFFICULTY_PEACEFUL_STATIONARY: {
			g_Config.m_SvBotAccuracy = 0;
			g_Config.m_SvBotAllowMove = 0;
			g_Config.m_SvBotAllowHook = 0;
			g_Config.m_SvBotAllowFire = 0;
			g_Config.m_SvBotReach = 100;
			g_Config.m_SvBotHookReach = 100;
		}break;
		case DIFFICULTY_PEACEFUL_NO_HOOK: {
			g_Config.m_SvBotAccuracy = 0;
			g_Config.m_SvBotAllowMove = 1;
			g_Config.m_SvBotAllowHook = 0;
			g_Config.m_SvBotAllowFire = 0;
			g_Config.m_SvBotReach = 100;
			g_Config.m_SvBotHookReach = 100;
		}break;
		case DIFFICULTY_PEACEFUL: {
			g_Config.m_SvBotAccuracy = 0;
			g_Config.m_SvBotAllowMove = 1;
			g_Config.m_SvBotAllowHook = 1;
			g_Config.m_SvBotAllowFire = 0;
			g_Config.m_SvBotReach = 100;
			g_Config.m_SvBotHookReach = 100;
		}break;
		case DIFFICULTY_EASIEST: {
			g_Config.m_SvBotAccuracy = 4;
			g_Config.m_SvBotAllowMove = 1;
			g_Config.m_SvBotAllowHook = 0;
			g_Config.m_SvBotAllowFire = 1;
			g_Config.m_SvBotReach = 50;
			g_Config.m_SvBotHookReach = 50;
		}break;
		case DIFFICULTY_VERY_EASY: {
			g_Config.m_SvBotAccuracy = 20;
			g_Config.m_SvBotAllowMove = 1;
			g_Config.m_SvBotAllowHook = 0;
			g_Config.m_SvBotAllowFire = 1;
			g_Config.m_SvBotReach = 60;
			g_Config.m_SvBotHookReach = 60;
		}break;
		case DIFFICULTY_EASY: {
			g_Config.m_SvBotAccuracy = 40;
			g_Config.m_SvBotAllowMove = 1;
			g_Config.m_SvBotAllowHook = 1;
			g_Config.m_SvBotAllowFire = 1;
			g_Config.m_SvBotReach = 70;
			g_Config.m_SvBotHookReach = 70;
		}break;
		case DIFFICULTY_MEDIUM: {
			g_Config.m_SvBotAccuracy = 60;
			g_Config.m_SvBotAllowMove = 1;
			g_Config.m_SvBotAllowHook = 1;
			g_Config.m_SvBotAllowFire = 1;
			g_Config.m_SvBotReach = 80;
			g_Config.m_SvBotHookReach = 80;
		}break;
		case DIFFICULTY_HARD: {
			g_Config.m_SvBotAccuracy = 80;
			g_Config.m_SvBotAllowMove = 1;
			g_Config.m_SvBotAllowHook = 1;
			g_Config.m_SvBotAllowFire = 1;
			g_Config.m_SvBotReach = 90;
			g_Config.m_SvBotHookReach = 90;
		}break;
		case DIFFICULTY_VERY_HARD: {
			g_Config.m_SvBotAccuracy = 95;
			g_Config.m_SvBotAllowMove = 1;
			g_Config.m_SvBotAllowHook = 1;
			g_Config.m_SvBotAllowFire = 1;
			g_Config.m_SvBotReach = 95;
			g_Config.m_SvBotHookReach = 95;
		}break;
		case DIFFICULTY_GODLIKE: {
			g_Config.m_SvBotAccuracy = 100;
			g_Config.m_SvBotAllowMove = 1;
			g_Config.m_SvBotAllowHook = 1;
			g_Config.m_SvBotAllowFire = 1;
			g_Config.m_SvBotReach = 100;
			g_Config.m_SvBotHookReach = 100;
		}break;
	}
}

void CGameContext::ConMuteSpec(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	if(pResult->NumArguments() == 0)
	{
		char aBuf[32];
		str_format(aBuf, sizeof(aBuf), "Spectators is %s", (pSelf->m_SpecMuted) ? "muted" : "unmuted");
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", aBuf);
		return;
	}

	bool IsMute = (bool)clamp(pResult->GetInteger(0), 0, 1);
	pSelf->m_SpecMuted = IsMute;
	if(IsMute)
		pSelf->SendChat(-1, CHAT_ALL, "Spectators has been muted");
	else
		pSelf->SendChat(-1, CHAT_ALL, "Spectators has been unmuted");
}

void CGameContext::ConStop(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_World.m_Paused = true;
}

void CGameContext::ConGo(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->m_pController->m_FakeWarmup = pSelf->Server()->TickSpeed() * g_Config.m_SvGoTime;
}

void CGameContext::ConXonX(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int Mode = pResult->GetInteger(0);
	g_Config.m_SvSpectatorSlots = MAX_CLIENTS - 2*Mode;
	pSelf->m_pController->DoWarmup(g_Config.m_SvWarTime);
	char aBuf[128];

	str_format(aBuf, sizeof(aBuf), "Upcoming %don%d! Please stay on spectator", Mode, Mode);
	pSelf->SendBroadcast(aBuf, -1);

	str_format(aBuf, sizeof(aBuf), "The %don%d will start in %d seconds!", Mode, Mode, g_Config.m_SvWarTime);
	pSelf->SendChat(-1, CHAT_ALL, aBuf);
}

void CGameContext::ConReset(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	g_Config.m_SvSpectatorSlots = 0;
	pSelf->SendChat(-1, CHAT_ALL, "Reset spectator slots");
}

void CGameContext::ConSetName(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->Server()->SetClientName(pResult->GetInteger(0), pResult->GetString(1));
}

void CGameContext::ConSetClan(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	pSelf->Server()->SetClientClan(pResult->GetInteger(0), pResult->GetString(1));
}

void CGameContext::ConKill(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetInteger(0);

	if(ClientID == -1)
	{
		for(int i = 0; i < MAX_CLIENTS; i++)
		{
			if(pSelf->GetPlayerChar(i))
				pSelf->GetPlayerChar(i)->Die(i, WEAPON_WORLD);
		}
		pSelf->SendBroadcast("All players killed by Admin", -1);
	}
	else if(pSelf->GetPlayerChar(ClientID))
		pSelf->GetPlayerChar(ClientID)->Die(ClientID, WEAPON_WORLD);
}

#ifdef USECHEATS
void CGameContext::ConGive(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int ClientID = pResult->GetInteger(0);
	if(!pSelf->IsValidCID(ClientID))
		return;

	//Single weapon
	int Weapon = pResult->GetInteger(1);
	if(Weapon >= 0 && Weapon < NUM_WEAPONS)
	{
		if(Weapon == WEAPON_NINJA)
		{
			if(pSelf->GetPlayerChar(ClientID))
				pSelf->GetPlayerChar(ClientID)->GiveNinja();
		}
		else
		{
			if(pResult->NumArguments() > 2)
				pSelf->m_apPlayers[ClientID]->m_KeepWeapon[Weapon] = (bool)clamp(pResult->GetInteger(2), 0, 1);
			if(pSelf->GetPlayerChar(ClientID))
				pSelf->GetPlayerChar(ClientID)->GiveWeapon(Weapon, -1);
		}
	}
	// all weapons
	else if(Weapon == -1)
	{
		for(int i = 0; i < NUM_WEAPONS-1; i++)
		{
			if(pResult->NumArguments() > 2)
				pSelf->m_apPlayers[ClientID]->m_KeepWeapon[i] = (bool)clamp(pResult->GetInteger(2), 0, 1);
			if(pSelf->GetPlayerChar(ClientID))
				pSelf->GetPlayerChar(ClientID)->GiveWeapon(i, -1);
		}
	}
	else if(Weapon == -2)
	{
		if(pSelf->GetPlayerChar(ClientID))
		{
			if(pResult->NumArguments() > 2)
				pSelf->m_apPlayers[ClientID]->m_KeepAward = (bool)clamp(pResult->GetInteger(2), 0, 1);

			pSelf->m_apPlayers[ClientID]->m_GotAward = true;
			char aBuf[128];
			str_format(aBuf, sizeof(aBuf), "Admin gave %s the killingspree award", pSelf->Server()->ClientName(ClientID));
			pSelf->SendChat(-1, CHAT_ALL, aBuf);
		}
	}
	else
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "invalid weapon id");
}

void CGameContext::ConTakeWeapon(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext*)pUserData;
	int ClientID = pResult->GetInteger(0);
	int Weapon = pResult->GetInteger(1);

	if(!pSelf->IsValidCID(ClientID))
		return;

	if(Weapon >= 0 && Weapon < NUM_WEAPONS-1)
	{
		if(pSelf->GetPlayerChar(ClientID))
			pSelf->GetPlayerChar(ClientID)->TakeWeapon(Weapon);
		pSelf->m_apPlayers[ClientID]->m_KeepWeapon[Weapon] = false;
	}
	else if(Weapon == -1)
		for(int i = 0; i < NUM_WEAPONS-1; i++)
		{
			if(pSelf->GetPlayerChar(ClientID))
				pSelf->GetPlayerChar(ClientID)->TakeWeapon(i);
			pSelf->m_apPlayers[ClientID]->m_KeepWeapon[i] = false;
		}
	else if(Weapon == -2)
	{
		if(pSelf->GetPlayerChar(ClientID))
			pSelf->m_apPlayers[ClientID]->m_GotAward = false;
		pSelf->m_apPlayers[ClientID]->m_KeepAward = false;
	}
	else
		pSelf->Console()->Print(IConsole::OUTPUT_LEVEL_STANDARD, "Server", "invalid weapon id");
}

void CGameContext::ConTeleport(IConsole::IResult *pResult, void *pUserData)
{
	CGameContext *pSelf = (CGameContext *)pUserData;
	int TeleFrom = pResult->GetInteger(0);
	int TeleTo = pResult->GetInteger(1);

	if(pSelf->IsValidCID(TeleFrom) && pSelf->IsValidCID(TeleTo))
	{
		CCharacter* pChr = pSelf->GetPlayerChar(TeleFrom);
		if(pChr)
			pChr->GetCore()->m_Pos = pSelf->m_apPlayers[TeleTo]->m_ViewPos;
		else
			pSelf->m_apPlayers[TeleFrom]->m_ViewPos = pSelf->m_apPlayers[TeleTo]->m_ViewPos;
	}
}
#endif

void CGameContext::OnConsoleInit()
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();

	Console()->Register("tune", "s?i", CFGFLAG_SERVER, ConTuneParam, this, "Tune variable to value");
	Console()->Register("tune_reset", "", CFGFLAG_SERVER, ConTuneReset, this, "Reset tuning");
	Console()->Register("tune_dump", "", CFGFLAG_SERVER, ConTuneDump, this, "Dump tuning");

	Console()->Register("pause", "", CFGFLAG_SERVER, ConPause, this, "Pause/unpause game");
	Console()->Register("change_map", "?r", CFGFLAG_SERVER|CFGFLAG_STORE, ConChangeMap, this, "Change map");
	Console()->Register("restart", "", CFGFLAG_SERVER|CFGFLAG_STORE, ConRestart, this, "Restart in x seconds (0 = abort)");
	Console()->Register("broadcast", "r", CFGFLAG_SERVER, ConBroadcast, this, "Broadcast message");
	Console()->Register("say", "r", CFGFLAG_SERVER, ConSay, this, "Say in chat");
	Console()->Register("set_team", "ii?i", CFGFLAG_SERVER, ConSetTeam, this, "Set team of player to team");
	Console()->Register("set_team_all", "i", CFGFLAG_SERVER, ConSetTeamAll, this, "Set team of all players to team");
	Console()->Register("swap_teams", "", CFGFLAG_SERVER, ConSwapTeams, this, "Swap the current teams");
	Console()->Register("shuffle_teams", "", CFGFLAG_SERVER, ConShuffleTeams, this, "Shuffle the current teams");
	Console()->Register("lock_teams", "", CFGFLAG_SERVER, ConLockTeams, this, "Lock/unlock teams");

	Console()->Register("add_vote", "sr", CFGFLAG_SERVER, ConAddVote, this, "Add a voting option");
	Console()->Register("remove_vote", "s", CFGFLAG_SERVER, ConRemoveVote, this, "remove a voting option");
	Console()->Register("force_vote", "ss?r", CFGFLAG_SERVER, ConForceVote, this, "Force a voting option");
	Console()->Register("clear_votes", "", CFGFLAG_SERVER, ConClearVotes, this, "Clears the voting options");
	Console()->Register("vote", "r", CFGFLAG_SERVER, ConVote, this, "Force a vote to yes/no");

	Console()->Chain("sv_motd", ConchainSpecialMotdupdate, this);

	Console()->Register("freeze", "ii", CFGFLAG_SERVER, ConFreeze, this, "Freeze a player for x seconds");
	Console()->Register("unfreeze", "i", CFGFLAG_SERVER, ConUnFreeze, this, "Unfreeze a player");
	Console()->Register("melt", "i", CFGFLAG_SERVER, ConUnFreeze, this, "Melt a player (same effect like unfreeze)");
	Console()->Register("mute_spec", "?i", CFGFLAG_SERVER, ConMuteSpec, this, "All messages written in spectators will be redirect to teamchat for this round");
	Console()->Register("stop", "", CFGFLAG_SERVER, ConStop, this, "Pause the game");
	Console()->Register("go", "", CFGFLAG_SERVER, ConGo, this, "Continue the game");
	Console()->Register("xonx", "i", CFGFLAG_SERVER, ConXonX, this, "Limit amount of active players");
	Console()->Register("reset", "", CFGFLAG_SERVER, ConReset, this, "Reset amount of active players");
	Console()->Register("set_name", "ir", CFGFLAG_SERVER, ConSetName, this, "Set the name of a player");
	Console()->Register("set_clan", "ir", CFGFLAG_SERVER, ConSetClan, this, "Set the clan of a player");
	Console()->Register("kill", "i", CFGFLAG_SERVER, ConKill, this, "Kill a player");
	Console()->Register("switch", "", CFGFLAG_SERVER, ConSwitch, this, "Switch teams");
	Console()->Register("difficulty", "i", CFGFLAG_SERVER, ConSetDifficulty, this, "Change Difficulty");

#ifdef USECHEATS
	Console()->Register("give", "ii?i", CFGFLAG_SERVER, ConGive, this, "Give a player the a weapon (-2=Award;-1=All weapons;0=Hammer;1=Gun;2=Shotgun;3=Grenade;4=Riffle,5=Ninja)");
	Console()->Register("takeweapon", "ii", CFGFLAG_SERVER, ConTakeWeapon, this, "Takes away a weapon of a player (-2=Award;-1=All weapons;0=Hammer;1=Gun;2=Shotgun;3=Grenade;4=Riffle");
	Console()->Register("tele", "ii", CFGFLAG_SERVER, ConTeleport, this, "Teleports a player to another");
	Console()->Register("teleport", "ii", CFGFLAG_SERVER, ConTeleport, this, "Teleports a player to another");
#endif
	m_Mute.OnConsoleInit(m_pConsole);
}

void CGameContext::SendDifficulties(int ClientID) {
	SendChatTarget(ClientID, "Valid difficulties are:");

	for (auto diff : difficulties) {
		char cBuf[32];
		str_format(cBuf, sizeof(cBuf), "%s %d", GetDifficultyName(diff), diff);
		SendChatTarget(ClientID, cBuf);
	}
}

void CGameContext::OnInit(/*class IKernel *pKernel*/)
{
	m_pServer = Kernel()->RequestInterface<IServer>();
	m_pConsole = Kernel()->RequestInterface<IConsole>();
	m_World.SetGameServer(this);
	m_Events.SetGameServer(this);
	m_Mute.Init(this);

	//if(!data) // only load once
		//data = load_data_from_memory(internal_data);

	for(int i = 0; i < NUM_NETOBJTYPES; i++)
		Server()->SnapSetStaticsize(i, m_NetObjHandler.GetObjSize(i));

	m_Layers.Init(Kernel());
	m_Collision.Init(&m_Layers);

	// reset everything here
	//world = new GAMEWORLD;
	//players = new CPlayer[MAX_CLIENTS];

	// select gametype
	if(str_comp_nocase(g_Config.m_SvGametype, "mod") == 0)
		m_pController = new CGameControllerMOD(this);
	else if(str_comp_nocase(g_Config.m_SvGametype, "ctf") == 0 || !str_comp_nocase(g_Config.m_SvGametype, "ctf+"))		// CTF
		m_pController = new CGameControllerCTF(this, IGameController::GAMETYPE_VANILLA);
	else if(str_comp_nocase(g_Config.m_SvGametype, "tdm") == 0 || !str_comp_nocase(g_Config.m_SvGametype, "tdm+"))		// TDM
		m_pController = new CGameControllerTDM(this, IGameController::GAMETYPE_VANILLA);
	else if(!str_comp_nocase(g_Config.m_SvGametype, "ictf") || !str_comp_nocase(g_Config.m_SvGametype, "ictf+"))		// iCTF
		m_pController = new CGameControllerCTF(this, IGameController::GAMETYPE_INSTAGIB);
	else if(!str_comp_nocase(g_Config.m_SvGametype, "idm") || !str_comp_nocase(g_Config.m_SvGametype, "idm+"))			// iDM
		m_pController = new CGameControllerDM(this, IGameController::GAMETYPE_INSTAGIB);
	else if(!str_comp_nocase(g_Config.m_SvGametype, "itdm") || !str_comp_nocase(g_Config.m_SvGametype, "itdm+"))		// iTDM
		m_pController = new CGameControllerTDM(this, IGameController::GAMETYPE_INSTAGIB);
	else if(!str_comp_nocase(g_Config.m_SvGametype, "gctf") || !str_comp_nocase(g_Config.m_SvGametype, "gctf+"))		// gCTF
		m_pController = new CGameControllerGCTF(this, IGameController::GAMETYPE_GCTF|IGameController::GAMETYPE_INSTAGIB);
	else if(!str_comp_nocase(g_Config.m_SvGametype, "gdm") || !str_comp_nocase(g_Config.m_SvGametype, "gdm+"))			// gDM
		m_pController = new CGameControllerGDM(this, IGameController::GAMETYPE_GCTF|IGameController::GAMETYPE_INSTAGIB);
	else if(!str_comp_nocase(g_Config.m_SvGametype, "gtdm") || !str_comp_nocase(g_Config.m_SvGametype, "gtdm+"))		// gTDM
		m_pController = new CGameControllerGTDM(this, IGameController::GAMETYPE_GCTF|IGameController::GAMETYPE_INSTAGIB);
	else if(!str_comp_nocase(g_Config.m_SvGametype, "ifreeze") || !str_comp_nocase(g_Config.m_SvGametype, "ifreeze+"))	// iFreeze
		m_pController = new CGameControllerIFreeze(this, IGameController::GAMETYPE_IFREEZE|IGameController::GAMETYPE_INSTAGIB);
	else
		m_pController = new CGameControllerDM(this, IGameController::GAMETYPE_VANILLA);

	// setup core world
	//for(int i = 0; i < MAX_CLIENTS; i++)
	//	game.players[i].core.world = &game.world.core;

	// create all entities from the game layer
	CMapItemLayerTilemap *pTileMap = m_Layers.GameLayer();
	CTile *pTiles = (CTile *)Kernel()->RequestInterface<IMap>()->GetData(pTileMap->m_Data);

	m_pBotEngine->Init(pTiles, pTileMap->m_Width, pTileMap->m_Height);



	/*
	num_spawn_points[0] = 0;
	num_spawn_points[1] = 0;
	num_spawn_points[2] = 0;
	*/

	for(int y = 0; y < pTileMap->m_Height; y++)
	{
		for(int x = 0; x < pTileMap->m_Width; x++)
		{
			int Index = pTiles[y*pTileMap->m_Width+x].m_Index;

			if(Index >= ENTITY_OFFSET)
			{
				vec2 Pos(x*32.0f+16.0f, y*32.0f+16.0f);
				m_pController->OnEntity(Index-ENTITY_OFFSET, Pos);
			}
		}
	}

	//game.world.insert_entity(game.Controller);

#ifdef CONF_DEBUG
	if(g_Config.m_DbgDummies)
	{
		for(int i = 0; i < g_Config.m_DbgDummies ; i++)
		{
			OnClientConnected(MAX_CLIENTS-i-1);
		}
	}
#endif

	CheckBotNumber();
}

void CGameContext::OnShutdown()
{
	CLoltext::Destroy(&m_World, -1);
	delete m_pController;
	m_pController = 0;
	Clear();
}

void CGameContext::OnSnap(int ClientID)
{
	// add tuning to demo
	CTuningParams StandardTuning;
	if(ClientID == -1 && Server()->DemoRecorder_IsRecording() && mem_comp(&StandardTuning, &m_Tuning, sizeof(CTuningParams)) != 0)
	{
		CMsgPacker Msg(NETMSGTYPE_SV_TUNEPARAMS);
		int *pParams = (int *)&m_Tuning;
		for(unsigned i = 0; i < sizeof(m_Tuning)/sizeof(int); i++)
			Msg.AddInt(pParams[i]);
		Server()->SendMsg(&Msg, MSGFLAG_RECORD|MSGFLAG_NOSEND, ClientID);
	}

	m_World.Snap(ClientID);
	m_pController->Snap(ClientID);
	m_Events.Snap(ClientID);

	// Snap bot debug info
	if(g_Config.m_SvBotEngineDrawGraph)
		m_pBotEngine->Snap(ClientID);
	for(int i = 0; i < MAX_CLIENTS; i++)
		if(m_apPlayers[i] && m_apPlayers[i]->IsBot() && g_Config.m_SvBotDrawTarget)
			m_apPlayers[i]->m_pBot->Snap(ClientID);

	for(int i = 0; i < MAX_CLIENTS; i++)
	{
		if(m_apPlayers[i])
			m_apPlayers[i]->Snap(ClientID);
	}
}
void CGameContext::OnPreSnap() {}
void CGameContext::OnPostSnap()
{
	m_Events.Clear();
}

bool CGameContext::IsClientReady(int ClientID)
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->m_IsReady ? true : false;
}

bool CGameContext::IsClientPlayer(int ClientID)
{
	return m_apPlayers[ClientID] && m_apPlayers[ClientID]->GetTeam() == TEAM_SPECTATORS ? false : true;
}

const char *CGameContext::GameType() { return m_pController && m_pController->m_pGameType ? m_pController->m_pGameType : ""; }
const char *CGameContext::Version() { return GAME_VERSION; }
const char *CGameContext::NetVersion() { return GAME_NETVERSION; }

IGameServer *CreateGameServer() { return new CGameContext; }

void CGameContext::DeleteBot(int i) {
	Server()->DelBot(i);
	if(m_apPlayers[i] && m_apPlayers[i]->m_IsBot) {
		m_apPlayers[i]->KillCharacter();
		dbg_msg("context","Delete bot at slot: %d", i);
		delete m_apPlayers[i];
		m_apPlayers[i] = 0;
	}
}

bool CGameContext::AddBot(int i, bool UseDropPlayer) {
	if (!g_Config.m_SvBotsEnabled) {
		return false;
	}
	int StartTeam = g_Config.m_SvTournamentMode ? TEAM_SPECTATORS : m_pController->GetAutoTeam(i);
	if(StartTeam == TEAM_SPECTATORS)
		return false;

	int TeamCount[2];
	TeamCount[0] = 0;
	TeamCount[1] = 0;

	if (g_Config.m_SvBotVsHuman) {
		for(int i = 0; i < MAX_CLIENTS; i++) {
			if (m_apPlayers[i] && !m_apPlayers[i]->IsBot()) {
				const int team = m_apPlayers[i]->GetTeam();
				if (team != TEAM_SPECTATORS) {
					TeamCount[team]++;
				}
			}
		}

		if (TeamCount[TEAM_BLUE] == 0) {
			StartTeam = TEAM_BLUE;
		}

		if (TeamCount[TEAM_RED] == 0) {
			StartTeam = TEAM_RED;
		}
	}

	if(Server()->NewBot(i) == 1)
		return false;
	dbg_msg("context","Add a bot at slot: %d", i);
	if(!UseDropPlayer || !m_apPlayers[i])
		m_apPlayers[i] = new(i) CPlayer(this, i, StartTeam);
	m_apPlayers[i]->m_IsBot = true;
	m_apPlayers[i]->m_pBot = new CBot(m_pBotEngine, m_apPlayers[i]);
	Server()->SetClientName(i, g_aBotName[i]);
	Server()->SetClientClan(i, g_BotClan);
	return true;
}

void CGameContext::SendLeaveMessage(int ClientID, const char* pReason) {
	m_apPlayers[ClientID]->SendLeaveMessage(pReason);
}

bool CGameContext::ReplacePlayerByBot(int ClientID) {
	if (!g_Config.m_SvBotsEnabled) {
		return false;
	}
	int BotNumber = 0;
	int PlayerCount = -1;
	for(int i = 0 ; i < MAX_CLIENTS ; ++i) {
		if(!m_apPlayers[i])
			continue;
		if(m_apPlayers[i]->m_IsBot)
			BotNumber++;
		else if (m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
			PlayerCount++;
	}
	if(!PlayerCount || BotNumber >= g_Config.m_SvBotSlots)
		return false;
	if(!g_Config.m_SvBotsEnabled || (g_Config.m_SvBotVsHuman && BotNumber > PlayerCount))
		return false;
	return AddBot(ClientID, true);
}

void CGameContext::CheckBotNumber() {
	int BotNumber = 0;
	int PlayerCount = 0;
	for(int i = 0 ; i < MAX_CLIENTS ; ++i) {
		if(!m_apPlayers[i])
			continue;
		if(m_apPlayers[i]->m_IsBot)
			BotNumber++;
		else if (m_apPlayers[i]->GetTeam() != TEAM_SPECTATORS)
			PlayerCount++;
	}
	if(!PlayerCount)
		BotNumber += g_Config.m_SvBotSlots;

	int MaxCount = g_Config.m_SvBotSlots;
	if (g_Config.m_SvBotVsHuman) {
		MaxCount = PlayerCount;
	}
	if (!g_Config.m_SvBotsEnabled) {
		MaxCount = 0;
	}

	// Remove bot excedent
	if(BotNumber-MaxCount > 0)	{
		int FirstBot = 0;
		for(int i = 0 ; i < BotNumber-MaxCount ; i++) {
			for(; FirstBot < MAX_CLIENTS ; FirstBot++)
				if(m_apPlayers[FirstBot] && m_apPlayers[FirstBot]->m_IsBot)
					break;
			if(FirstBot < MAX_CLIENTS)
				DeleteBot(FirstBot);
		}
	}

	// Add missing bot if possible
	if(MaxCount-BotNumber > 0) {
		int LastFreeSlot = Server()->MaxClients()-1;
		for(int i = 0 ; i < MaxCount-BotNumber ; i++) {
			for(; LastFreeSlot >= 0 ; LastFreeSlot--)
				if(!m_apPlayers[LastFreeSlot])
					break;
			if( LastFreeSlot >= 0)
				AddBot(LastFreeSlot);
		}
	}
}
