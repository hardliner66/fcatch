/* (c) Magnus Auvinen. See licence.txt in the root of the distribution for more information. */
/* If you are missing that file, acquire a complete release at teeworlds.com.                */
/* fCatch by hardliner66, based on zcatch by erd and Teetime                                 */

#ifndef GAME_SERVER_GAMEMODES_FCATCH_H
#define GAME_SERVER_GAMEMODES_FCATCH_H

#include <game/server/gamecontroller.h>

class CGameController_fCatch: public IGameController
{
	int m_PlayerCount;
	int m_ActivePlayerCount;
	bool m_fCatch_enabled;

	bool ShouldShowCapturesInsteadOfKills();

public:
	CGameController_fCatch(class CGameContext *pGameServer);
	virtual void Tick();
	virtual void DoWincheck();

	virtual void StartRound();
	virtual void OnCharacterSpawn(class CCharacter *pChr);
	virtual void OnPlayerInfoChange(class CPlayer *pP);
	virtual int OnCharacterDeath(class CCharacter *pVictim, class CPlayer *pKiller, int WeaponID);
	virtual bool OnEntity(int Index, vec2 Pos);
	virtual bool CanChangeTeam(CPlayer *pPlayer, int JoinTeam);
	virtual void EndRound();

	void CalcPlayerColor();
};

#endif
