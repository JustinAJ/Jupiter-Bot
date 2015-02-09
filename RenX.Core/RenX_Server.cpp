/**
 * Copyright (C) 2014-2015 Justin James.
 *
 * This license must be preserved.
 * Any applications, libraries, or code which make any use of any
 * component of this program must not be commercial, unless explicit
 * permission is granted from the original author. The use of this
 * program for non-profit purposes is permitted.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * In the event that this license restricts you from making desired use of this program, contact the original author.
 * Written by Justin James <justin.aj@hotmail.com>
 */

#include <ctime>
#include "Jupiter/INIFile.h"
#include "Jupiter/String.h"
#include "ServerManager.h"
#include "IRC_Bot.h"
#include "RenX_Server.h"
#include "RenX_PlayerInfo.h"
#include "RenX_GameCommand.h"
#include "RenX_Functions.h"
#include "RenX_Plugin.h"
#include "RenX_BanDatabase.h"

int RenX::Server::think()
{
	if (RenX::Server::connected == false)
	{
		if (time(0) >= RenX::Server::lastAttempt + RenX::Server::delay)
		{
			if (RenX::Server::connect())
				RenX::Server::sendLogChan(IRCCOLOR "03[RenX]" IRCCOLOR " Socket successfully reconnected to Renegade-X server.");
			else RenX::Server::sendLogChan(IRCCOLOR "04[Error]" IRCCOLOR " Failed to reconnect to Renegade-X server.");
		}
	}
	else
	{
		if (RenX::Server::sock.recv() > 0)
		{
			Jupiter::ReferenceString buffer = RenX::Server::sock.getBuffer();
			unsigned int totalLines = buffer.tokenCount('\n');

			if (totalLines != 0)
			{
				RenX::Server::lastLine.concat(buffer.getToken(0, '\n'));
				if (totalLines != 1) // if there's only one token, there is no newline.
				{
					RenX::Server::processLine(RenX::Server::lastLine);
					RenX::Server::lastLine = buffer.getToken(totalLines - 1, '\n');

					for (unsigned int currentLine = 1; currentLine != totalLines - 1; currentLine++)
						RenX::Server::processLine(buffer.getToken(currentLine, '\n'));
				}
			}
		}
		else if (Jupiter::Socket::getLastError() != 10035) // This is a serious error
		{
			RenX::Server::wipeData();
			RenX::Server::sendLogChan(IRCCOLOR "07[Warning]" IRCCOLOR " Connection to Renegade-X server lost. Reconnection attempt in progress.");
			if (RenX::Server::reconnect())
				RenX::Server::sendLogChan(IRCCOLOR "06[Progress]" IRCCOLOR " Connection to Renegade-X server reestablished. Initializing Renegade-X RCON protocol...");
			else
				RenX::Server::sendLogChan(IRCCOLOR "04[Error]" IRCCOLOR " Connection to Renegade-X server lost. Reconnection attempt failed.");
		}
	}
	return 0;
}

int RenX::Server::OnRehash()
{
	Jupiter::StringS oldHostname = RenX::Server::hostname;
	unsigned short oldPort = RenX::Server::port;
	Jupiter::StringS oldClientHostname = RenX::Server::clientHostname;
	Jupiter::StringS oldPass = RenX::Server::pass;
	unsigned int oldUUIDMode = RenX::Server::uuidMode;
	int oldSteamFormat = RenX::Server::steamFormat;
	RenX::Server::commands.emptyAndDelete();
	RenX::Server::init();
	if (oldHostname.equalsi(RenX::Server::hostname) && oldPort == RenX::Server::port && oldClientHostname.equalsi(RenX::Server::clientHostname) && oldPass.equalsi(RenX::Server::pass))
	{
		if ((oldUUIDMode != RenX::Server::uuidMode || (RenX::Server::uuidMode == 0 && oldSteamFormat != RenX::Server::steamFormat)) && RenX::Server::players.size() != 0)
		{
			RenX::PlayerInfo *player;
			for (Jupiter::DLList<RenX::PlayerInfo>::Node *n = RenX::Server::players.getNode(0); n != nullptr; n = n->next)
			{
				player = n->data;
				switch (this->uuidMode)
				{
				default:
				case 0:
					if (player->steamid != 0)
						player->uuid = this->formatSteamID(player);
					else
						player->uuid = Jupiter::ReferenceString::empty;
					break;
				case 1:
					player->uuid = player->name;
					break;
				}
			}
		}
	}
	else
		RenX::Server::reconnect();
	return 0;
}

bool RenX::Server::OnBadRehash(bool removed)
{
	return removed;
}

bool RenX::Server::isConnected() const
{
	return RenX::Server::connected;
}

bool RenX::Server::isFirstGame() const
{
	return RenX::Server::firstGame;
}

bool RenX::Server::isFirstKill() const
{
	return RenX::Server::firstKill;
}

bool RenX::Server::isFirstDeath() const
{
	return RenX::Server::firstDeath;
}

bool RenX::Server::isPublicLogChanType(int type) const
{
	return RenX::Server::logChanType == type;
}

bool RenX::Server::isAdminLogChanType(int type) const
{
	return RenX::Server::adminLogChanType == type;
}

bool RenX::Server::isLogChanType(int type) const
{
	return RenX::Server::isPublicLogChanType(type) || RenX::Server::isAdminLogChanType(type);
}

int RenX::Server::send(const Jupiter::ReadableString &command)
{
	char *t = new char[command.size() + 2];
	*t = 'c';
	for (size_t i = 0; i != command.size(); i++) t[i + 1] = command.get(i);
	t[command.size() + 1] = '\n';
	int r;
	if (RenX::Server::profile->mustSanitize)
		RenX::sanitizeString(t);
	r = RenX::Server::sock.send(t, command.size() + 2);
	delete[] t;
	return r;
}

int RenX::Server::sendMessage(const Jupiter::ReadableString &message)
{
	if (RenX::Server::neverSay)
	{
		int r = 0;
		if (RenX::Server::profile->privateMessages && RenX::Server::players.size() != 0)
			for (Jupiter::DLList<RenX::PlayerInfo>::Node *node = RenX::Server::players.getNode(0); node != nullptr; node = node->next)
				if (node->data->isBot == false)
				{
					if (RenX::Server::rconVersion <= 2)
						r += RenX::Server::sock.send(Jupiter::StringS::Format("cevaprivatesay pid%d %.*s\n", node->data->id, message.size(), message.ptr()));
					else
						r += RenX::Server::sock.send(Jupiter::StringS::Format("chostprivatesay pid%d %.*s\n", node->data->id, message.size(), message.ptr()));
				}
		return r;
	}
	else
	{
		Jupiter::StringS cmd = STRING_LITERAL_AS_REFERENCE("chostsay ");
		if (RenX::Server::rconVersion <= 2)
			cmd = STRING_LITERAL_AS_REFERENCE("csay ");
		cmd += message;
		cmd += '\n';
		if (RenX::Server::profile->mustSanitize)
			RenX::sanitizeString(cmd);
		return RenX::Server::sock.send(cmd);
	}
}

int RenX::Server::sendMessage(const RenX::PlayerInfo *player, const Jupiter::ReadableString &message)
{
	if (RenX::Server::profile->privateMessages == false)
		return RenX::Server::sendMessage(message);

	if (RenX::Server::rconVersion <= 2)
		return RenX::Server::sock.send(Jupiter::StringS::Format("cevaprivatesay pid%d %.*s\n", player->id, message.size(), message.ptr()));
	return RenX::Server::sock.send(Jupiter::StringS::Format("chostprivatesay pid%d %.*s\n", player->id, message.size(), message.ptr()));
}

int RenX::Server::sendData(const Jupiter::ReadableString &data)
{
	return RenX::Server::sock.send(data);
}

RenX::PlayerInfo *RenX::Server::getPlayer(int id) const
{
	if (RenX::Server::players.size() == 0) return nullptr;
	for (Jupiter::DLList<RenX::PlayerInfo>::Node *node = RenX::Server::players.getNode(0); node != nullptr; node = node->next) if (node->data->id == id) return node->data;
	return nullptr;
}

RenX::PlayerInfo *RenX::Server::getPlayerByName(const Jupiter::ReadableString &name) const
{
	if (RenX::Server::players.size() == 0) return nullptr;

	for (Jupiter::DLList<RenX::PlayerInfo>::Node *node = RenX::Server::players.getNode(0); node != nullptr; node = node->next)
		if (node->data->name == name)
			return node->data;

	Jupiter::ReferenceString idToken = name;
	if (name.matchi("Player?*"))
		idToken.shiftRight(6);
	else if (name.matchi("pid?*"))
		idToken.shiftRight(3);
	else return nullptr;
	int id = idToken.asInt(10);

	for (Jupiter::DLList<RenX::PlayerInfo>::Node *node = RenX::Server::players.getNode(0); node != nullptr; node = node->next)
		if (node->data->id == id)
			return node->data;

	return nullptr;
}

RenX::PlayerInfo *RenX::Server::getPlayerByPartName(const Jupiter::ReadableString &partName) const
{
	if (RenX::Server::players.size() == 0) return nullptr;
	RenX::PlayerInfo *r = RenX::Server::getPlayerByName(partName);
	if (r != nullptr) return r;
	return RenX::Server::getPlayerByPartNameFast(partName);
}

RenX::PlayerInfo *RenX::Server::getPlayerByPartNameFast(const Jupiter::ReadableString &partName) const
{
	if (RenX::Server::players.size() == 0) return nullptr;
	for (Jupiter::DLList<RenX::PlayerInfo>::Node *node = RenX::Server::players.getNode(0); node != nullptr; node = node->next)
		if (node->data->name.findi(partName) != Jupiter::INVALID_INDEX)
			return node->data;
	return nullptr;
}

Jupiter::StringS RenX::Server::formatSteamID(const RenX::PlayerInfo *player) const
{
	return RenX::Server::formatSteamID(player->steamid);
}

Jupiter::StringS RenX::Server::formatSteamID(uint64_t id) const
{
	if (id == 0)
		return Jupiter::ReferenceString::empty;

	switch (RenX::Server::steamFormat)
	{
	default:
	case 16:
		return Jupiter::StringS::Format("0x%.16llX", id);
	case 10:
		return Jupiter::StringS::Format("%llu", id);
	case 8:
		return Jupiter::StringS::Format("0%llo", id);
	case -2:
		id -= 0x0110000100000000ULL;
		if (id % 2 == 1)
			return Jupiter::StringS::Format("STEAM_1:1:%llu", id / 2ULL);
		else
			return Jupiter::StringS::Format("STEAM_1:0:%llu", id / 2ULL);
	case -3:
		id -= 0x0110000100000000ULL;
		return Jupiter::StringS::Format("[U:1:%llu]", id);
	}
}

void RenX::Server::kickPlayer(int id)
{
	RenX::Server::sock.send(Jupiter::StringS::Format("cadminkick pid%d\n", id));
}

void RenX::Server::kickPlayer(const RenX::PlayerInfo *player)
{
	if (this->profile->pidbug)
	{
		if (player->isBot)
			RenX::Server::sock.send(Jupiter::StringS::Format("cadminkick %.*s\n", player->name.size(), player->name.ptr()));
		else if (player->id < 1000)
			RenX::Server::kickPlayer(player->id);
		else if (player->name.contains('|') == false)
			RenX::Server::sock.send(Jupiter::StringS::Format("cadminkick %.*s\n", player->name.size(), player->name.ptr()));
		else
			RenX::Server::kickPlayer(player->id);
	}
	else
		RenX::Server::kickPlayer(player->id);
}

void RenX::Server::banPlayer(int id)
{
	if (RenX::Server::rconBan)
		RenX::Server::sock.send(Jupiter::StringS::Format("cadminkickban pid%d\n", id));
	else
	{
		RenX::PlayerInfo *player = RenX::Server::getPlayer(id);
		if (player != nullptr)
			RenX::Server::banPlayer(player);
	}
}

void RenX::Server::banPlayer(const RenX::PlayerInfo *player, time_t length)
{
	if (RenX::Server::rconBan && length == 0)
	{
		if (this->profile->pidbug)
		{
			if (player->isBot)
				RenX::Server::sock.send(Jupiter::StringS::Format("cadminkickban %.*s\n", player->name.size(), player->name.ptr()));
			else if (player->id < 1000)
				RenX::Server::banPlayer(player->id);
			else if (player->name.contains('|') == false)
				RenX::Server::sock.send(Jupiter::StringS::Format("cadminkickban %.*s\n", player->name.size(), player->name.ptr()));
			else
				RenX::Server::banPlayer(player->id);
		}
		else
			RenX::Server::sock.send(Jupiter::StringS::Format("cadminkickban pid%d\n", player->id));
	}
	else
		RenX::Server::kickPlayer(player);
	if (RenX::Server::localBan)
		RenX::banDatabase->add(this, player, length);
}

bool RenX::Server::removePlayer(int id)
{
	if (RenX::Server::players.size() == 0) return false;
	for (Jupiter::DLList<RenX::PlayerInfo>::Node *node = RenX::Server::players.getNode(0); node != nullptr; node = node->next)
	{
		if (node->data->id == id)
		{
			RenX::PlayerInfo *p = RenX::Server::players.remove(node);
			Jupiter::ArrayList<RenX::Plugin> &xPlugins = *RenX::getCore()->getPlugins();
			for (size_t i = 0; i < xPlugins.size(); i++)
				xPlugins.get(i)->RenX_OnPlayerDelete(this, p);
			delete p;
			return true;
		}
	}
	return false;
}

bool RenX::Server::removePlayer(RenX::PlayerInfo *player)
{
	return RenX::Server::removePlayer(player->id);
}

bool RenX::Server::updateClientList()
{
	if (RenX::Server::xRconVersion != 0)
		return RenX::Server::sock.send(STRING_LITERAL_AS_REFERENCE("_x\x01\n")) > 0;
	if (RenX::Server::rconVersion <= 2)
		return RenX::Server::sock.send(STRING_LITERAL_AS_REFERENCE("clogclientlist\n")) > 0;
	return RenX::Server::sock.send(STRING_LITERAL_AS_REFERENCE("cclientlist\n")) > 0;
}

bool RenX::Server::giveCredits(int id, double credits)
{
	return RenX::Server::xRconVersion != 0 && RenX::Server::sock.send(Jupiter::StringS::Format("_x\x04%d%c%.4f\n", id, RenX::DelimC, credits)) > 0;
}

bool RenX::Server::giveCredits(RenX::PlayerInfo *player, double credits)
{
	return RenX::Server::giveCredits(player->id, credits);
}

bool RenX::Server::changeTeam(int id, unsigned char options)
{
	return RenX::Server::xRconVersion != 0 && RenX::Server::sock.send(Jupiter::StringS::Format("_x\x07%d%c%c\n", id, RenX::DelimC, options)) > 0;
}

bool RenX::Server::changeTeam(RenX::PlayerInfo *player, unsigned char options)
{
	return RenX::Server::changeTeam(player->id, options);
}

bool RenX::Server::setTeam(int id, int team, unsigned char options)
{
	return RenX::Server::xRconVersion != 0 && RenX::Server::sock.send(Jupiter::StringS::Format("_x\x07%d%c%c%c%d\n", id, RenX::DelimC, options, RenX::DelimC, team)) > 0;
}

bool RenX::Server::setTeam(RenX::PlayerInfo *player, int team, unsigned char options)
{
	return RenX::Server::setTeam(player->id, team, options);
}

const Jupiter::ReadableString &RenX::Server::getPrefix() const
{
	return RenX::Server::IRCPrefix;
}

void RenX::Server::setPrefix(const Jupiter::ReadableString &prefix)
{
	RenX::Server::IRCPrefix = prefix;
}

const Jupiter::ReadableString &RenX::Server::getCommandPrefix() const
{
	return RenX::Server::CommandPrefix;
}

void RenX::Server::setCommandPrefix(const Jupiter::ReadableString &prefix)
{
	RenX::Server::CommandPrefix = prefix;
}

const Jupiter::ReadableString &RenX::Server::getRules() const
{
	return RenX::Server::rules;
}

void RenX::Server::setRules(const Jupiter::ReadableString &rules)
{
	RenX::Server::rules = rules;
	Jupiter::IRC::Client::Config->set(RenX::Server::configSection, STRING_LITERAL_AS_REFERENCE("Rules"), rules);
	RenX::Server::sendMessage(Jupiter::StringS::Format("NOTICE: The rules have been modified! Rules: %.*s", rules.size(), rules.ptr()));
}

const Jupiter::ReadableString &RenX::Server::getHostname() const
{
	return RenX::Server::hostname;
}

unsigned short RenX::Server::getPort() const
{
	return RenX::Server::port;
}

time_t RenX::Server::getLastAttempt() const
{
	return RenX::Server::lastAttempt;
}

time_t RenX::Server::getDelay() const
{
	return RenX::Server::delay;
}

const Jupiter::ReadableString &RenX::Server::getPassword() const
{
	return RenX::Server::pass;
}

const Jupiter::ReadableString &RenX::Server::getUser() const
{
	return RenX::Server::rconUser;
}

const Jupiter::ReadableString &RenX::Server::getName() const
{
	return RenX::Server::serverName;
}

const Jupiter::ReadableString &RenX::Server::getMap() const
{
	return RenX::Server::map;
}

RenX::GameCommand *RenX::Server::getCommand(unsigned int index) const
{
	return RenX::Server::commands.get(index);
}

RenX::GameCommand *RenX::Server::getCommand(const Jupiter::ReadableString &trigger) const
{
	for (size_t i = 0; i != RenX::Server::commands.size(); i++)
	{
		RenX::GameCommand *cmd = RenX::Server::commands.get(i);
		if (cmd->matches(trigger))
			return cmd;
	}
	return nullptr;
}

unsigned int RenX::Server::getCommandCount() const
{
	return RenX::Server::commands.size();
}

unsigned int RenX::Server::triggerCommand(const Jupiter::ReadableString &trigger, RenX::PlayerInfo *player, const Jupiter::ReadableString &parameters)
{
	unsigned int r = 0;
	RenX::GameCommand *cmd;
	for (size_t i = 0; i < RenX::Server::commands.size(); i++)
	{
		cmd = RenX::Server::commands.get(i);
		if (cmd->matches(trigger))
		{
			if (player->access >= cmd->getAccessLevel())
				cmd->trigger(this, player, parameters);
			else
				RenX::Server::sendMessage(player, STRING_LITERAL_AS_REFERENCE("Access Denied."));
			r++;
		}
	}
	return r;
}

void RenX::Server::addCommand(RenX::GameCommand *command)
{
	RenX::Server::commands.add(command);
	if (RenX::Server::commandAccessLevels != nullptr)
	{
		const Jupiter::ReadableString &accessLevel = RenX::Server::commandAccessLevels->get(command->getTrigger());
		if (accessLevel.isEmpty() == false)
			command->setAccessLevel(accessLevel.asInt());
	}
	if (RenX::Server::commandAliases != nullptr)
	{
		const Jupiter::ReadableString &aliasList = RenX::Server::commandAliases->get(command->getTrigger());
		unsigned int j = aliasList.wordCount(WHITESPACE);
		while (j != 0)
			command->addTrigger(Jupiter::ReferenceString::getWord(aliasList, --j, WHITESPACE));
	}
}

bool RenX::Server::removeCommand(RenX::GameCommand *command)
{
	for (size_t i = 0; i != RenX::Server::commands.size(); i++)
	{
		if (RenX::Server::commands.get(i) == command)
		{
			delete RenX::Server::commands.remove(i);
			return true;
		}
	}
	return false;
}

bool RenX::Server::removeCommand(const Jupiter::ReadableString &trigger)
{
	for (size_t i = 0; i != RenX::Server::commands.size(); i++)
	{
		if (RenX::Server::commands.get(i)->matches(trigger))
		{
			delete RenX::Server::commands.remove(i);
			return true;
		}
	}
	return false;
}

void RenX::Server::sendPubChan(const char *fmt, ...) const
{
	va_list args;
	va_start(args, fmt);
	Jupiter::StringL msg;
	const Jupiter::ReadableString &serverPrefix = RenX::Server::IRCPrefix;
	if (serverPrefix.isEmpty() == false)
	{
		msg += serverPrefix;
		msg += ' ';
		msg.avformat(fmt, args);
	}
	else msg.vformat(fmt, args);
	va_end(args);
	RenX::Server::sendPubChan(msg);
}

void RenX::Server::sendPubChan(const Jupiter::ReadableString &msg) const
{
	for (size_t i = 0; i != serverManager->size(); i++)
		serverManager->getServer(i)->messageChannels(RenX::Server::logChanType, msg);
}

void RenX::Server::sendAdmChan(const char *fmt, ...) const
{
	va_list args;
	va_start(args, fmt);
	Jupiter::StringL msg;
	const Jupiter::ReadableString &serverPrefix = RenX::Server::getPrefix();
	if (serverPrefix.isEmpty() == false)
	{
		msg += serverPrefix;
		msg += ' ';
		msg.avformat(fmt, args);
	}
	else msg.vformat(fmt, args);
	va_end(args);
	RenX::Server::sendAdmChan(msg);
}

void RenX::Server::sendAdmChan(const Jupiter::ReadableString &msg) const
{
	for (size_t i = 0; i != serverManager->size(); i++)
		serverManager->getServer(i)->messageChannels(RenX::Server::adminLogChanType, msg);
}

void RenX::Server::sendLogChan(const char *fmt, ...) const
{
	va_list args;
	va_start(args, fmt);
	Jupiter::StringL msg;
	const Jupiter::ReadableString &serverPrefix = RenX::Server::getPrefix();
	if (serverPrefix.isEmpty() == false)
	{
		msg += serverPrefix;
		msg += ' ';
		msg.avformat(fmt, args);
	}
	else msg.vformat(fmt, args);
	va_end(args);
	RenX::Server::sendLogChan(msg);
}

void RenX::Server::sendLogChan(const Jupiter::ReadableString &msg) const
{
	for (size_t i = 0; i != serverManager->size(); i++)
	{
		IRC_Bot *server = serverManager->getServer(i);
		server->messageChannels(RenX::Server::logChanType, msg);
		server->messageChannels(RenX::Server::adminLogChanType, msg);
	}
}

#define PARSE_PLAYER_DATA_P(DATA) \
	Jupiter::ReferenceString name; \
	TeamType team; \
	int id; \
	bool isBot; \
	parsePlayerData(DATA, name, team, id, isBot);

void RenX::Server::processLine(const Jupiter::ReadableString &line)
{
	Jupiter::ReferenceString buff = line;
	Jupiter::ArrayList<RenX::Plugin> &xPlugins = *RenX::getCore()->getPlugins();
	Jupiter::ReferenceString header = buff.getToken(0, RenX::DelimC);

	/** Local functions */
	auto onPreGameOver = [this](RenX::WinType winType, RenX::TeamType team, int gScore, int nScore)
	{
		RenX::PlayerInfo *player;

		if (this->players.size() != 0)
		{
			for (Jupiter::DLList<RenX::PlayerInfo>::Node *n = this->players.getNode(0); n != nullptr; n = n->next)
			{
				player = n->data;
				if (player != nullptr)
				{
					if (player->team == team)
						player->wins++;
					else player->loses++;
				}
			}
		}
	};
	auto onPostGameOver = [this](RenX::WinType winType, RenX::TeamType team, int gScore, int nScore)
	{
		this->firstGame = false;
		this->firstAction = false;
		this->firstKill = false;
		this->firstDeath = false;
		RenX::PlayerInfo *player;

		if (this->players.size() != 0)
		{
			for (Jupiter::DLList<RenX::PlayerInfo>::Node *n = this->players.getNode(0); n != nullptr; n = n->next)
			{
				player = n->data;
				if (player != nullptr)
				{
					player->kills = 0;
					player->deaths = 0;
					player->suicides = 0;
					player->headshots = 0;
					player->vehicleKills = 0;
					player->buildingKills = 0;
					player->defenceKills = 0;
				}
			}
		}
	};
	auto onChat = [this](RenX::PlayerInfo *player, const Jupiter::ReadableString &message)
	{
		const Jupiter::ReadableString &prefix = this->getCommandPrefix();
		if (message.find(prefix) == 0 && message.size() != prefix.size())
		{
			Jupiter::ReferenceString command;
			Jupiter::ReferenceString parameters;
			if (containsSymbol(WHITESPACE, message.get(prefix.size())))
			{
				command = Jupiter::ReferenceString::getWord(message, 1, WHITESPACE);
				parameters = Jupiter::ReferenceString::gotoWord(message, 2, WHITESPACE);
			}
			else
			{
				command = Jupiter::ReferenceString::getWord(message, 0, WHITESPACE);
				command.shiftRight(prefix.size());
				parameters = Jupiter::ReferenceString::gotoWord(message, 1, WHITESPACE);
			}
			this->triggerCommand(command, player, parameters);
		}
	};
	auto onAction = [this]()
	{
		if (this->firstAction == false)
		{
			this->firstAction = true;
			this->silenceJoins = false;
		}
	};
	auto parsePlayerData = [this](const Jupiter::ReadableString &data, Jupiter::ReferenceString &name, TeamType &team, int &id, bool &isBot)
	{
		Jupiter::ReferenceString idToken = Jupiter::ReferenceString::getToken(data, 1, ',');
		name = Jupiter::ReferenceString::gotoToken(data, 2, ',');
		if (data[0] == ',')
			team = TeamType::Other;
		else
			team = RenX::getTeam(data[0]);
		if (idToken.get(0) == 'b')
		{
			idToken.shiftRight(1);
			isBot = true;
		}
		else
			isBot = false;
		id = idToken.asInt(10);
	};
	auto getPlayerOrAdd = [&](const Jupiter::ReadableString &name, int id, RenX::TeamType team, bool isBot, uint64_t steamid, const Jupiter::ReadableString &ip)
	{
		bool checkBans = false;
		RenX::PlayerInfo *r = this->getPlayer(id);
		auto checkMissing = [&]()
		{
			if (r->ip32 == 0 && ip.isEmpty() == false)
			{
				r->ip = ip;
				r->ip32 = Jupiter::Socket::pton4(Jupiter::CStringS(r->ip).c_str());
				checkBans = true;
			}
			if (r->steamid == 0U && steamid != 0U)
			{
				r->steamid = steamid;
				if (this->uuidMode == 0)
					r->uuid = this->formatSteamID(r);
				checkBans = true;
			}
		};
		if (r == nullptr)
		{
			checkBans = true;
			r = new RenX::PlayerInfo();
			r->id = id;
			r->name = name;
			checkMissing();
			if (r->isBot = isBot)
				r->formatNamePrefix = IRCCOLOR "05[B]";
			r->joinTime = time(nullptr);
			if (id != 0)
				this->players.add(r);
			if (this->uuidMode == 1)
				r->uuid = r->name;

			for (size_t i = 0; i < xPlugins.size(); i++)
				xPlugins.get(i)->RenX_OnPlayerCreate(this, r);
		}
		else
		{
			checkMissing();
			if (r->name.size() == 0)
				r->name = name;
		}
		r->team = team;
		if (checkBans)
		{
			const Jupiter::ArrayList<RenX::BanDatabase::Entry> &entries = RenX::banDatabase->getEntries();
			RenX::BanDatabase::Entry *entry;
			for (size_t i = 0; i != entries.size(); i++)
			{
				entry = entries.get(i);
				if (entry->active)
				{
					if (entry->length != 0 && entry->timestamp + entry->length < time(0))
						banDatabase->deactivate(i);
					else if (this->localSteamBan && entry->steamid != 0 && entry->steamid == r->steamid)
						this->kickPlayer(r);
					else if (this->localIPBan && entry->ip != 0 && entry->ip == r->ip32)
						this->kickPlayer(r);
					else if (this->localNameBan && entry->name.isEmpty() == false && entry->name.equalsi(r->name))
						this->kickPlayer(r);
				}
			}
		}
		return r;
	};
	auto parseGetPlayerOrAdd = [&parsePlayerData, &getPlayerOrAdd](const Jupiter::ReadableString &token)
	{
		PARSE_PLAYER_DATA_P(token);
		return getPlayerOrAdd(name, id, team, isBot, 0U, Jupiter::ReferenceString::empty);
	};

	if (buff.size() != 0)
	{
		switch (header[0])
		{
		case 'r':
			if (this->lastCommand.equalsi("clientlist"))
			{
				// ID | IP | Steam ID | Admin Status | Team | Name
				header.shiftRight(1);
				if (header.isEmpty() == false)
				{
					bool isBot = false;
					int id;
					uint64_t steamid = 0;
					RenX::TeamType team = TeamType::Other;
					Jupiter::ReferenceString steamToken = buff.getToken(2, RenX::DelimC);
					Jupiter::ReferenceString adminToken = buff.getToken(3, RenX::DelimC);
					Jupiter::ReferenceString teamToken = buff.getToken(4, RenX::DelimC);
					if (header.get(0) == 'b')
					{
						isBot = true;
						header.shiftRight(1);
						id = header.asInt();
						header.shiftLeft(1);
					}
					else
						id = header.asInt();

					if (steamToken.equals("-----NO-STEAM-----") == false)
						steamid = steamToken.asUnsignedLongLong();

					if (teamToken.isEmpty() == false)
						team = RenX::getTeam(teamToken.get(0));

					if (adminToken.equalsi("None"))
						getPlayerOrAdd(buff.getToken(5, RenX::DelimC), id, team, isBot, steamid, buff.getToken(1, RenX::DelimC));
					else
						getPlayerOrAdd(buff.getToken(5, RenX::DelimC), id, team, isBot, steamid, buff.getToken(1, RenX::DelimC))->adminType = adminToken;
				}
				header.shiftLeft(1);
			}
			break;
		case 'l':
			if (RenX::Server::rconVersion >= 3)
			{
				header.shiftRight(1);
				Jupiter::ReferenceString subHeader = buff.getToken(1, RenX::DelimC);
				if (header.equals("GAME"))
				{
					if (subHeader.equals("Deployed;"))
					{
						// Beacon | "by" | Player
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(buff.getToken(4, RenX::DelimC));
						Jupiter::ReferenceString objectType = buff.getToken(2, RenX::DelimC);
						if (objectType.match("*Beacon"))
							player->beaconPlacements++;
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnDeploy(this, player, objectType);
						onAction();
					}
					else if (subHeader.equals("Disarmed;"))
					{
						// Beacon | "by" | Player
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(buff.getToken(4, RenX::DelimC));
						Jupiter::ReferenceString objectType = buff.getToken(2, RenX::DelimC);
						if (objectType.match("*Beacon"))
							player->beaconPlacements++;
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnDisarm(this, player, objectType);
						onAction();
					}
					/*else if (subHeader.equals("Exploded;"))
					{
						// Explosive | "at" | Location | "by" | Owner
						onAction();
					}*/
					else if (subHeader.equals("Captured;"))
					{
						// Team ',' Building | "by" | Player
						Jupiter::ReferenceString teamBuildingToken = buff.getToken(2, RenX::DelimC);
						Jupiter::ReferenceString teamToken = teamBuildingToken.getToken(0, ',');
						Jupiter::ReferenceString building = teamBuildingToken.getToken(1, ',');
						if (building.size() > 2) // temporary fix to remove unique identifier from building token
						{
							if (building.get(building.size() - 2) == '_')
								building.truncate(2);
							else if (building.size() > 3 && building.get(building.size() - 3) == '_')
								building.truncate(3);
						}
						TeamType oldTeam = teamToken.isEmpty() ? TeamType::None : RenX::getTeam(teamToken.get(0));
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(buff.getToken(4, RenX::DelimC));
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnCapture(this, player, building, oldTeam);
						onAction();
					}
					else if (subHeader.equals("Neutralized;"))
					{
						// Team ',' Building | "by" | Player
						Jupiter::ReferenceString teamBuildingToken = buff.getToken(2, RenX::DelimC);
						Jupiter::ReferenceString teamToken = teamBuildingToken.getToken(0, ',');
						Jupiter::ReferenceString building = teamBuildingToken.getToken(1, ',');
						if (building.size() > 2) // temporary fix to remove unique identifier from building token
						{
							if (building.get(building.size() - 2) == '_')
								building.truncate(2);
							else if (building.size() > 3 && building.get(building.size() - 3) == '_')
								building.truncate(3);
						}
						TeamType oldTeam = teamToken.isEmpty() ? TeamType::None : RenX::getTeam(teamToken.get(0));
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(buff.getToken(4, RenX::DelimC));
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnNeutralize(this, player, building, oldTeam);
						onAction();
					}
					else if (subHeader.equals("Death;"))
					{
						// "player" | Player | "by" | Killer Player | "with" | Damage Type
						// "player" | Player | "died by" | Damage Type
						// "player" | Player | "suicide by" | Damage Type
						//		NOTE: Filter these out when Player.isEmpty().
						Jupiter::ReferenceString playerToken = buff.getToken(3, RenX::DelimC);
						if (playerToken.isEmpty() == false)
						{
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(playerToken);
							Jupiter::ReferenceString type = buff.getToken(4, RenX::DelimC);
							Jupiter::ReferenceString damageType;
							if (type.equals("by"))
							{
								damageType = buff.getToken(7, RenX::DelimC);
								Jupiter::ReferenceString killerData = buff.getToken(5, RenX::DelimC);
								Jupiter::ReferenceString kName = killerData.getToken(2, ',');
								Jupiter::ReferenceString kIDToken = killerData.getToken(1, ',');
								Jupiter::ReferenceString kTeamToken = killerData.getToken(0, ',');
								RenX::TeamType vTeam = TeamType::Other;
								if (kTeamToken.isEmpty() == false)
									vTeam = RenX::getTeam(kTeamToken.get(0));
								if (kIDToken.equals("ai") || kIDToken.isEmpty())
									for (size_t i = 0; i < xPlugins.size(); i++)
										xPlugins.get(i)->RenX_OnKill(this, kName, vTeam, player, damageType);
								else
								{
									int kID = 0;
									bool kIsBot = false;
									if (kIDToken.get(0) == 'b')
									{
										kIsBot = true;
										kIDToken.shiftRight(1);
										kID = kIDToken.asInt();
										kIDToken.shiftLeft(1);
									}
									else
										kID = kIDToken.asInt();
									RenX::PlayerInfo *killer = getPlayerOrAdd(kName, kID, vTeam, kIsBot, 0, Jupiter::ReferenceString::empty);
									for (size_t i = 0; i < xPlugins.size(); i++)
										xPlugins.get(i)->RenX_OnKill(this, killer, player, damageType);
								}
							}
							else if (type.equals("died by"))
							{
								damageType = buff.getToken(5, RenX::DelimC);
								for (size_t i = 0; i < xPlugins.size(); i++)
									xPlugins.get(i)->RenX_OnDie(this, player, damageType);
							}
							else if (type.equals("suicide by"))
							{
								damageType = buff.getToken(5, RenX::DelimC);
								for (size_t i = 0; i < xPlugins.size(); i++)
									xPlugins.get(i)->RenX_OnSuicide(this, player, damageType);
							}
						}
						onAction();
					}
					else if (subHeader.equals("Stolen;"))
					{
						// Vehicle | "by" | Player
						// Vehicle | "bound to" | Bound Player | "by" | Player
						Jupiter::ReferenceString vehicle = buff.getToken(2, RenX::DelimC);
						Jupiter::ReferenceString byLine = buff.getToken(3, RenX::DelimC);
						if (byLine.equals("by"))
						{
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(buff.getToken(4, RenX::DelimC));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnSteal(this, player, vehicle);
						}
						else if (byLine.equals("bound to"))
						{
							RenX::PlayerInfo *victim = parseGetPlayerOrAdd(buff.getToken(4, RenX::DelimC));
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(buff.getToken(6, RenX::DelimC));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnSteal(this, player, vehicle, victim);
						}
						onAction();
					}
					else if (subHeader.equals("Destroyed;"))
					{
						// "vehicle" | Vehicle | "by" | Killer | "with" | Damage Type
						// "defence" | Defence | "by" | Killer | "with" | Damage Type
						// "emplacement" | Emplacement | "by" | Killer Player | "with" | Damage Type
						// "building" | Building | "by" | Killer | "with" | Damage Type
						Jupiter::ReferenceString typeToken = buff.getToken(2, RenX::DelimC);
						RenX::ObjectType type = ObjectType::None;
						if (typeToken.equals("vehicle"))
							type = ObjectType::Vehicle;
						else if (typeToken.equals("defence") || typeToken.equals("emplacement"))
							type = ObjectType::Defence;
						else if (typeToken.equals("building"))
							type = ObjectType::Building;

						if (type != ObjectType::None)
						{
							Jupiter::ReferenceString objectName = buff.getToken(3, RenX::DelimC);
							if (buff.getToken(4, RenX::DelimC).equals("by"))
							{
								Jupiter::ReferenceString killerToken = buff.getToken(5, RenX::DelimC);
								Jupiter::ReferenceString teamToken = killerToken.getToken(0, ',');
								Jupiter::ReferenceString idToken = killerToken.getToken(1, ',');
								Jupiter::ReferenceString name = killerToken.gotoToken(2, ',');
								Jupiter::ReferenceString damageType = buff.getToken(7, RenX::DelimC);

								RenX::TeamType team = TeamType::Other;
								if (teamToken.isEmpty() == false)
									team = RenX::getTeam(teamToken.get(0));

								if (idToken.equals("ai") || idToken.isEmpty())
								{
									for (size_t i = 0; i < xPlugins.size(); i++)
										xPlugins.get(i)->RenX_OnDestroy(this, name, team, objectName, RenX::getEnemy(team), damageType, type);
								}
								else
								{
									int id;
									bool isBot = false;
									if (idToken.get(0) == 'b')
									{
										isBot = true;
										idToken.shiftRight(1);
									}
									id = idToken.asInt();
									RenX::PlayerInfo *player = getPlayerOrAdd(name, id, team, isBot, 0, Jupiter::ReferenceString::empty);
									for (size_t i = 0; i < xPlugins.size(); i++)
										xPlugins.get(i)->RenX_OnDestroy(this, player, objectName, damageType, type);
								}
							}
						}
						onAction();
					}
					else if (subHeader.equals("MatchEnd;"))
					{
						// "winner" | Winner | Reason("TimeLimit" etc) | "GDI=" GDI Score | "Nod=" Nod Score
						// "tie" | Reason | "GDI=" GDI Score | "Nod=" Nod Score
						Jupiter::ReferenceString winTieToken = buff.getToken(2, RenX::DelimC);
						if (winTieToken.equals("winner"))
						{
							Jupiter::ReferenceString sWinType = buff.getToken(4, RenX::DelimC);
							WinType winType = WinType::Unknown;
							if (sWinType.equals("TimeLimit"))
								winType = WinType::Score;
							else if (sWinType.equals("Buildings"))
								winType = WinType::Base;
							else if (sWinType.equals("triggered"))
								winType = WinType::Shutdown;

							Jupiter::ReferenceString winTeam = buff.getToken(3, RenX::DelimC);
							TeamType team = TeamType::Other;
							if (winTeam.isEmpty() == false)
								team = RenX::getTeam(winTeam.get(0));

							int gScore = buff.getToken(5, RenX::DelimC).getToken(1, '=').asInt();
							int nScore = buff.getToken(6, RenX::DelimC).getToken(1, '=').asInt();

							onPreGameOver(winType, team, gScore, nScore);
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnGameOver(this, winType, team, gScore, nScore);
							onPostGameOver(winType, team, gScore, nScore);
						}
						else if (winTieToken.equals("tie"))
						{
							int gScore = buff.getToken(4, RenX::DelimC).getToken(1, '=').asInt();
							int nScore = buff.getToken(5, RenX::DelimC).getToken(1, '=').asInt();
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnGameOver(this, RenX::WinType::Tie, RenX::TeamType::None, gScore, nScore);
							onPostGameOver(WinType::Tie, RenX::TeamType::None, gScore, nScore);
						}
					}
					else
					{
						Jupiter::ReferenceString raw = buff.gotoToken(1, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnGame(this, raw);
					}
				}
				else if (header.equals("CHAT"))
				{
					if (subHeader.equals("Say;"))
					{
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(buff.getToken(2, RenX::DelimC));
						Jupiter::ReferenceString message = buff.getToken(4, RenX::DelimC);
						onChat(player, message);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnChat(this, player, message);
						onAction();
					}
					else if (subHeader.equals("TeamSay;"))
					{
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(buff.getToken(2, RenX::DelimC));
						Jupiter::ReferenceString message = buff.getToken(4, RenX::DelimC);
						onChat(player, message);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnTeamChat(this, player, message);
						onAction();
					}
					/*else if (subHeader.equals("AdminSay;"))
					{
						// Player | "said:" | Message
						onAction();
					}
					else if (subHeader.equals("ReportSay;"))
					{
						// Player | "said:" | Message
						onAction();
					}*/
					else
					{
						Jupiter::ReferenceString raw = buff.gotoToken(1, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnOtherChat(this, raw);
					}
				}
				else if (header.equals("PLAYER"))
				{
					if (subHeader.equals("Enter;"))
					{
						PARSE_PLAYER_DATA_P(buff.getToken(2, RenX::DelimC));
						uint64_t steamid = 0;
						if (buff.getToken(5, RenX::DelimC).equals("steamid"))
							steamid = buff.getToken(6, RenX::DelimC).asUnsignedLongLong();
						RenX::PlayerInfo *player = getPlayerOrAdd(name, id, team, isBot, steamid, buff.getToken(4, RenX::DelimC));
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnJoin(this, player);
					}
					else if (subHeader.equals("TeamJoin;"))
					{
						// Player | "joined" | Team
						// Player | "joined" | Team | "left" | Old Team
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(buff.getToken(2, RenX::DelimC));
						if (buff.tokenCount(RenX::DelimC) > 4)
						{
							Jupiter::ReferenceString oldTeamToken = buff.getToken(6, RenX::DelimC);
							RenX::TeamType oldTeam = TeamType::Other;
							if (oldTeamToken.isEmpty() == false)
								oldTeam = RenX::getTeam(oldTeamToken.get(0));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnTeamChange(this, player, oldTeam);
						}
					}
					else if (subHeader.equals("Exit;"))
					{
						// Player
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(buff.getToken(2, RenX::DelimC));
						if (this->silenceParts == false)
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnPart(this, player);
						this->removePlayer(player);
					}
					else if (subHeader.equals("NameChange;"))
					{
						// Player | "to:" | New Name
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(buff.getToken(2, RenX::DelimC));
						Jupiter::ReferenceString newName = buff.getToken(3, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnNameChange(this, player, newName);
						player->name = newName;
						onAction();
					}
					else
					{
						Jupiter::ReferenceString raw = buff.gotoToken(1, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnPlayer(this, raw);
					}
				}
				else if (header.equals("RCON"))
				{
					if (subHeader.equals("Command;"))
					{
						// User | "executed:" | Command
						Jupiter::ReferenceString user = buff.getToken(2, RenX::DelimC);
						if (buff.getToken(3, RenX::DelimC).equals("executed:"))
						{
							Jupiter::ReferenceString command = buff.gotoToken(4, RenX::DelimC);
							Jupiter::ReferenceString cmd = command.getWord(0, " ");
							if (cmd.equalsi("hostsay"))
							{
								Jupiter::ReferenceString message = command.gotoWord(1, " ");
								for (size_t i = 0; i < xPlugins.size(); i++)
									xPlugins.get(i)->RenX_OnHostChat(this, message);
							}
							else if (cmd.equalsi("hostprivatesay"))
							{
								RenX::PlayerInfo *player = this->getPlayerByName(command.getWord(1, " "));
								if (player != nullptr)
								{
									Jupiter::ReferenceString message = command.gotoWord(2, " ");
									for (size_t i = 0; i < xPlugins.size(); i++)
										xPlugins.get(i)->RenX_OnHostPage(this, player, message);
								}
								else
									for (size_t i = 0; i < xPlugins.size(); i++)
										xPlugins.get(i)->RenX_OnExecute(this, user, command);
							}
							else
								for (size_t i = 0; i < xPlugins.size(); i++)
									xPlugins.get(i)->RenX_OnExecute(this, user, command);
							if (this->rconUser.equals(user))
								this->lastCommand = cmd;
						}
					}
					else if (subHeader.equals("Subscribed;"))
					{
						// User
						Jupiter::ReferenceString user = buff.getToken(2, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnSubscribe(this, user);
					}
					else if (subHeader.equals("Unsubscribed;"))
					{
						// User
						Jupiter::ReferenceString user = buff.getToken(2, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnUnsubscribe(this, user);
					}
					else if (subHeader.equals("Blocked;"))
					{
						// User | Reason="(Denied by IP Policy)" / "(Not on Whitelist)"
						Jupiter::ReferenceString user = buff.getToken(2, RenX::DelimC);
						Jupiter::ReferenceString message = buff.gotoToken(3, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnBlock(this, user, message);
					}
					else if (subHeader.equals("Connected;"))
					{
						// User
						Jupiter::ReferenceString user = buff.getToken(2, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnConnect(this, user);
					}
					else if (subHeader.equals("Authenticated;"))
					{
						// User
						Jupiter::ReferenceString user = buff.getToken(2, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnAuthenticate(this, user);
					}
					else if (subHeader.equals("Banned;"))
					{
						// User | "reason" | Reason="(Too many password attempts)"
						Jupiter::ReferenceString user = buff.getToken(2, RenX::DelimC);
						Jupiter::ReferenceString message = buff.gotoToken(4, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnBan(this, user, message);
					}
					else if (subHeader.equals("InvalidPassword;"))
					{
						// User
						Jupiter::ReferenceString user = buff.getToken(2, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnInvalidPassword(this, user);
					}
					else if (subHeader.equals("Dropped;"))
					{
						// User | "reason" | Reason="(Auth Timeout)"
						Jupiter::ReferenceString user = buff.getToken(2, RenX::DelimC);
						Jupiter::ReferenceString message = buff.gotoToken(4, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnDrop(this, user, message);
					}
					else if (subHeader.equals("Disconnected;"))
					{
						// User
						Jupiter::ReferenceString user = buff.getToken(2, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnDisconnect(this, user);
					}
					else if (subHeader.equals("StoppedListen;"))
					{
						// Reason="(Reached Connection Limit)"
						Jupiter::ReferenceString message = buff.gotoToken(2, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnStopListen(this, message);
					}
					else if (subHeader.equals("ResumedListen;"))
					{
						// Reason="(No longer at Connection Limit)"
						Jupiter::ReferenceString message = buff.gotoToken(2, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnResumeListen(this, message);
					}
					else if (subHeader.equals("Warning;"))
					{
						// Warning="(Hit Max Attempt Records - You should investigate Rcon attempts and/or decrease prune time)"
						Jupiter::ReferenceString message = buff.gotoToken(2, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnWarning(this, message);
					}
					else
					{
						Jupiter::ReferenceString raw = buff.gotoToken(1, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnRCON(this, raw);
					}
				}
				else if (header.equals("ADMIN"))
				{
					if (subHeader.equals("Rcon;"))
					{
						// Player | "executed:" | Command
						if (buff.getToken(3, RenX::DelimC).equals("executed:"))
						{
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(buff.getToken(2, RenX::DelimC));
							Jupiter::ReferenceString cmd = buff.gotoToken(4, RenX::DelimC);
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnExecute(this, player, cmd);
						}
					}
					else if (subHeader.equals("Login;"))
					{
						// Player | "as" | Type="moderator" / "administrator"
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(buff.getToken(2, RenX::DelimC));
						player->adminType = buff.getToken(4, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnAdminLogin(this, player);
					}
					else if (subHeader.equals("Logout;"))
					{
						// Player | "as" | Type="moderator" / "administrator"
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(buff.getToken(2, RenX::DelimC));
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnAdminLogout(this, player);
						player->adminType = Jupiter::ReferenceString::empty;
					}
					else if (subHeader.equals("Granted;"))
					{
						// Player | "as" | Type="moderator" / "administrator"
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(buff.getToken(2, RenX::DelimC));
						player->adminType = buff.getToken(4, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnAdminGrant(this, player);
					}
					else
					{
						Jupiter::ReferenceString raw = buff.gotoToken(1, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnAdmin(this, raw);
					}
				}
				else if (header.equals("VOTE"))
				{
					if (subHeader.equals("Called;"))
					{
						// TeamType="Global" / "GDI" / "Nod" / "" | VoteType="Rx_VoteMenuChoice_"... | "parameters" | Parameters(Empty) | "by" | Player
						// TeamType="Global" / "GDI" / "Nod" / "" | VoteType="Rx_VoteMenuChoice_"... | "by" | Player
						Jupiter::ReferenceString voteType = buff.getToken(3, RenX::DelimC);
						Jupiter::ReferenceString teamToken = buff.getToken(2, RenX::DelimC);
						RenX::TeamType team;
						if (teamToken.equals("Global"))
							team = TeamType::None;
						else if (teamToken.equals("GDI"))
							team = TeamType::GDI;
						else if (teamToken.equals("Nod"))
							team = TeamType::Nod;
						else
							team = TeamType::Other;

						Jupiter::ReferenceString playerToken;
						Jupiter::ReferenceString parameters;
						if (buff.getToken(4, RenX::DelimC).equals("parameters"))
						{
							playerToken = buff.getToken(buff.tokenCount(RenX::DelimC) - 1, RenX::DelimC);
							parameters = buff.getToken(5, RenX::DelimC);
						}
						else
							playerToken = buff.getToken(5, RenX::DelimC);

						RenX::PlayerInfo *player = parseGetPlayerOrAdd(playerToken);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnVoteCall(this, team, voteType, player, parameters);
						onAction();
					}
					else if (subHeader.equals("Results;"))
					{
						// TeamType="Global" / "GDI" / "Nod" / "" | VoteType="Rx_VoteMenuChoice_"... | Success="pass" / "fail" | "Yes=" Yes votes | "No=" No votes
						Jupiter::ReferenceString voteType = buff.getToken(3, RenX::DelimC);
						Jupiter::ReferenceString teamToken = buff.getToken(2, RenX::DelimC);
						RenX::TeamType team;
						if (teamToken.equals("Global"))
							team = TeamType::None;
						else if (teamToken.equals("GDI"))
							team = TeamType::GDI;
						else if (teamToken.equals("Nod"))
							team = TeamType::Nod;
						else
							team = TeamType::Other;

						bool success = true;
						if (buff.getToken(4, RenX::DelimC).equals("fail"))
							success = false;

						int yesVotes = 0;
						Jupiter::ReferenceString yesVotesToken = buff.getToken(5, RenX::DelimC);
						if (yesVotesToken.size() > 4)
						{
							yesVotesToken.shiftRight(4);
							yesVotes = yesVotesToken.asInt();
						}

						int noVotes = 0;
						Jupiter::ReferenceString noVotesToken = buff.getToken(5, RenX::DelimC);
						if (yesVotesToken.size() > 3)
						{
							yesVotesToken.shiftRight(3);
							yesVotes = yesVotesToken.asInt();
						}

						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnVoteOver(this, team, voteType, success, yesVotes, noVotes);
					}
					else if (subHeader.equals("Cancelled;"))
					{
						// TeamType="Global" / "GDI" / "Nod" | VoteType="Rx_VoteMenuChoice_"...
						Jupiter::ReferenceString voteType = buff.getToken(3, RenX::DelimC);
						Jupiter::ReferenceString teamToken = buff.getToken(2, RenX::DelimC);
						RenX::TeamType team;
						if (teamToken.equals("Global"))
							team = TeamType::None;
						else if (teamToken.equals("GDI"))
							team = TeamType::GDI;
						else if (teamToken.equals("Nod"))
							team = TeamType::Nod;
						else
							team = TeamType::Other;

						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnVoteCancel(this, team, voteType);
					}
					else
					{
						Jupiter::ReferenceString raw = buff.gotoToken(1, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnVote(this, raw);
					}
				}
				else if (header.equals("MAP"))
				{
					if (subHeader.equals("Changing;"))
					{
						// Map | Mode="seamless" / "nonseamless"
						Jupiter::ReferenceString map = buff.getToken(2, RenX::DelimC);
						bool seamless = true;
						if (buff.getToken(3, RenX::DelimC).equals("nonseamless"))
						{
							this->silenceParts = true;
							seamless = false;
						}
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnMapChange(this, map, seamless);
						this->map = map;
					}
					else if (subHeader.equals("Loaded;"))
					{
						// Map
						Jupiter::ReferenceString map = buff.getToken(2, RenX::DelimC);
						this->map = map;
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnMapLoad(this, map);
					}
					else
					{
						Jupiter::ReferenceString raw = buff.gotoToken(1, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnMap(this, raw);
					}
				}
				else if (header.equals("DEMO"))
				{
					if (subHeader.equals("Record;"))
					{
						// "client request by" | Player
						// "admin command by" | Player
						// "rcon command"
						Jupiter::ReferenceString type = buff.getToken(2, RenX::DelimC);
						if (type.equals("client request by") || type.equals("admin command by"))
						{
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(buff.getToken(3, RenX::DelimC));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnDemoRecord(this, player);
						}
						else
						{
							Jupiter::ReferenceString user = buff.getToken(3, RenX::DelimC); // not actually used, but here for possible future usage
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnDemoRecord(this, user);
						}
					}
					else
					{
						Jupiter::ReferenceString raw = buff.gotoToken(1, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnDemo(this, raw);
					}
				}
				/*else if (header.equals("ERROR;")) // Decided to disable this entirely, since it's unreachable anyways.
				{
					// Should be under RCON.
					// "Could not open TCP Port" Port "- Rcon Disabled"
				}*/
				else
				{
					buff.shiftRight(1);
					for (size_t i = 0; i < xPlugins.size(); i++)
						xPlugins.get(i)->RenX_OnLog(this, buff);
					buff.shiftLeft(1);
				}
			}
			else // RCONv2
			{
				Jupiter::ReferenceString playerData = buff.getToken(1, RenX::DelimC);
				Jupiter::ReferenceString action = buff.getToken(2, RenX::DelimC);
				if (header.equals("lGAME:"))
				{
					if (action.equals("deployed"))
					{
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(playerData);
						Jupiter::ReferenceString objectType = buff.getToken(3, RenX::DelimC);
						if (objectType.match("*Beacon"))
							player->beaconPlacements++;
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnDeploy(this, player, objectType);
						onAction();
					}
					else if (action.equals("suicided by"))
					{
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(playerData);
						player->deaths++;
						player->suicides++;
						Jupiter::ReferenceString damageType = buff.getToken(3, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnSuicide(this, player, damageType);
						this->firstDeath = true;
						onAction();
					}
					else if (action.equals("killed"))
					{
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(playerData);
						Jupiter::ReferenceString victimData = buff.getToken(3, RenX::DelimC);
						Jupiter::ReferenceString vTeamToken = victimData.getToken(0, ',');
						Jupiter::ReferenceString vidToken = victimData.getToken(1, ',');
						if (vTeamToken.size() != 0 && vidToken.size() != 0)
						{
							Jupiter::ReferenceString vname = victimData.getToken(2, ',');
							int vid;
							bool visBot = false;
							if (vidToken[0] == 'b')
							{
								vidToken.shiftRight(1);
								visBot = true;
							}
							vid = vidToken.asInt(10);
							TeamType vteam = RenX::getTeam(vTeamToken.get(0));
							Jupiter::ReferenceString damageType = buff.getToken(5, RenX::DelimC);
							RenX::PlayerInfo *victim = getPlayerOrAdd(vname, vid, vteam, visBot, 0, Jupiter::ReferenceString::empty);
							player->kills++;
							if (damageType.equals("Rx_DmgType_Headshot")) player->headshots++;
							victim->deaths++;

							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnKill(this, player, victim, damageType);
						}

						if (this->needsCList)
						{
							this->updateClientList();
							this->needsCList = false;
						}

						this->firstKill = true;
						this->firstDeath = true;
						onAction();
					}
					else if (action.match("died by"))
					{
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(playerData);
						player->deaths++;
						Jupiter::ReferenceString damageType = buff.getToken(3, RenX::DelimC);
						if (damageType.equals("DamageType"))
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnTeamChange(this, player, RenX::getEnemy(player->team));
						else
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnDie(this, player, damageType);
						this->firstDeath = true;
						onAction();
					}
					else if (action.match("destroyed*"))
					{
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(playerData);
						Jupiter::ReferenceString victim = buff.getToken(3, RenX::DelimC);
						Jupiter::ReferenceString damageType = buff.getToken(5, RenX::DelimC);
						ObjectType type;
						if (action.equals("destroyed building"))
						{
							type = ObjectType::Building;
							player->buildingKills++;
						}
						else if (victim.match("Rx_Defence_*"))
						{
							type = ObjectType::Defence;
							player->defenceKills++;
						}
						else
						{
							type = ObjectType::Vehicle;
							player->vehicleKills++;
						}
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnDestroy(this, player, victim, damageType, type);
						onAction();
					}
					else if (playerData.match("??? wins (*)"))
					{
						TeamType team = RenX::getTeam(playerData[0]);
						int gScore = buff.getToken(2, RenX::DelimC).gotoToken(1, '=').asInt(10);
						int nScore = buff.getToken(3, RenX::DelimC).gotoToken(1, '=').asInt(10);
						Jupiter::ReferenceString winType = Jupiter::ReferenceString::substring(playerData, 10);
						winType.truncate(1);
						WinType iWinType = WinType::Unknown;
						if (gScore == nScore)
							iWinType = WinType::Tie;
						else if (winType.equals("TimeLimit"))
							iWinType = WinType::Score;
						else if (winType.equals("Buildings"))
							iWinType = WinType::Base;

						this->needsCList = true;
						if (this->profile->disconnectOnGameOver)
							this->silenceParts = true;

						onPreGameOver(iWinType, team, gScore, nScore);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnGameOver(this, iWinType, team, gScore, nScore);
						onPostGameOver(iWinType, team, gScore, nScore);
					}
					else if (playerData.equals("Tie"))
					{
						int gScore;
						switch (RenX::Server::profile->tieFormat)
						{
						default:
						case 1:
							gScore = buff.getToken(2, RenX::DelimC).gotoToken(1, '=').asInt(10);
							break;
						case 0:
							gScore = action.gotoToken(1, '=').asInt(10);
							break;
						}
						int nScore = buff.getToken(3, RenX::DelimC).gotoToken(1, '=').asInt(10);

						this->needsCList = true;
						if (this->profile->disconnectOnGameOver)
							this->silenceParts = true;

						if (gScore == nScore)
						{
							onPreGameOver(WinType::Tie, TeamType::Other, gScore, nScore);
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnGameOver(this, WinType::Tie, TeamType::Other, gScore, nScore);
							onPostGameOver(WinType::Tie, TeamType::Other, gScore, nScore);
						}
						else
						{
							TeamType winTeam = gScore > nScore ? RenX::getTeam('G') : RenX::getTeam('N');
							onPreGameOver(WinType::Shutdown, winTeam, gScore, nScore);
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnGameOver(this, WinType::Shutdown, winTeam, gScore, nScore);
							onPostGameOver(WinType::Shutdown, winTeam, gScore, nScore);
						}
					}
					else
					{
						Jupiter::ReferenceString raw = buff.gotoToken(1, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnGame(this, raw);
					}
				}
				else if (header.equals("lCHAT:"))
				{
					if (action.equals("teamsay:"))
					{
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(playerData);
						Jupiter::ReferenceString message = buff.getToken(3, RenX::DelimC);
						onChat(player, message);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnTeamChat(this, player, message);
					}
					else if (action.equals("say:"))
					{
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(playerData);
						Jupiter::ReferenceString message = buff.getToken(3, RenX::DelimC);
						onChat(player, message);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnChat(this, player, message);
					}
					onAction();
				}
				else if (header.equals("lPLAYER:"))
				{
					if (action.equals("disconnected"))
					{
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(playerData);
						if (this->silenceParts == false)
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnPart(this, player);
						this->removePlayer(player);
					}
					else if (action.equals("entered from"))
					{
						PARSE_PLAYER_DATA_P(playerData);
						RenX::PlayerInfo *player = getPlayerOrAdd(name, id, team, isBot, buff.getToken(4, RenX::DelimC).equals("steamid") ? buff.getToken(5, RenX::DelimC).asUnsignedLongLong() : 0, buff.getToken(3, RenX::DelimC));

						if (this->silenceJoins == false)
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnJoin(this, player);
					}
					else if (action.equals("changed name to:"))
					{
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(playerData);
						Jupiter::ReferenceString newName = buff.getToken(3, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnNameChange(this, player, newName);
						player->name = newName;
						if (RenX::Server::uuidMode == 1)
							player->uuid = player->name;
						onAction();
					}
				}
				else if (header.equals("lRCON:"))
				{
					if (action.equals("executed:"))
					{
						Jupiter::ReferenceString command = buff.getToken(3, RenX::DelimC);
						Jupiter::ReferenceString cmd = command.getWord(0, " ");
						if (cmd.equalsi("say"))
						{
							Jupiter::ReferenceString message = command.gotoWord(1, " ");
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnHostChat(this, message);
						}
						else if (cmd.equalsi("evaprivatesay"))
						{
							RenX::PlayerInfo *player = this->getPlayerByName(command.getWord(1, " "));
							if (player != nullptr)
							{
								Jupiter::ReferenceString message = command.gotoWord(2, " ");
								for (size_t i = 0; i < xPlugins.size(); i++)
									xPlugins.get(i)->RenX_OnHostPage(this, player, message);
							}
							else
								for (size_t i = 0; i < xPlugins.size(); i++)
									xPlugins.get(i)->RenX_OnExecute(this, playerData, command);
						}
						else
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnExecute(this, playerData, command);
					}
					else if (action.equals("subscribed"))
					{
						if (this->rconUser.isEmpty())
							this->rconUser = playerData;
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnSubscribe(this, playerData);
					}
					else
					{
						Jupiter::ReferenceString raw = buff.gotoToken(1, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnRCON(this, raw);
					}
				}
				else if (header.equals("lADMIN:"))
				{
					RenX::PlayerInfo *player = parseGetPlayerOrAdd(playerData);
					if (action.equals("logged in as"))
					{
						player->adminType = buff.getToken(3, RenX::DelimC);
						if (player->adminType.equalsi("moderator") && player->access < 1)
							player->access = 1;
						else if (player->adminType.equalsi("administrator") && player->access < 2)
							player->access = 2;
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnAdminLogin(this, player);
					}
					else if (action.equals("logged out of"))
					{
						Jupiter::ReferenceString type = buff.getToken(3, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnAdminLogout(this, player);
						player->adminType = "";
						player->access = 0;
					}
					else if (action.equals("granted"))
					{
						player->adminType = buff.getToken(3, RenX::DelimC);
						if (player->adminType.equalsi("moderator") && player->access < 1)
							player->access = 1;
						else if (player->adminType.equalsi("administrator") && player->access < 2)
							player->access = 2;
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnAdminGrant(this, player);
					}
					else
					{
						Jupiter::ReferenceString raw = buff.gotoToken(1, RenX::DelimC);
						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnAdmin(this, raw);
					}
				}
				else if (header.equals("lC-LIST:"))
				{
					// ID IP SteamID Team Name
					if (playerData.isEmpty())
						break;

					static const Jupiter::ReferenceString CListDelim = STRING_LITERAL_AS_REFERENCE("  ");
					int id;
					bool isBot = false;
					if (playerData.get(0) == 'b')
					{
						isBot = true;
						playerData.shiftRight(1);
						id = playerData.asInt(10);
						playerData.shiftLeft(1);
					}
					else id = playerData.asInt(10);
					Jupiter::ReferenceString ip = playerData.getToken(1, CListDelim);
					Jupiter::ReferenceString steamid = playerData.getToken(2, CListDelim);
					RenX::TeamType team;
					Jupiter::ReferenceString name;
					if (steamid.equals("-----NO")) // RCONv2-2a
					{
						steamid = "";
						Jupiter::ReferenceString &teamToken = playerData.getToken(4, CListDelim);
						if (teamToken.isEmpty())
							break;
						team = getTeam(teamToken.get(0));
						name = playerData.gotoToken(5, CListDelim);
					}
					else
					{
						if (steamid.equals("-----NO-STEAM-----")) // RCONv2-2.5a
							steamid = "";
						Jupiter::ReferenceString &teamToken = playerData.getToken(3, CListDelim);
						if (teamToken.isEmpty())
							break;
						team = getTeam(teamToken.get(0));
						name = playerData.gotoToken(4, CListDelim);
					}

					RenX::PlayerInfo *player = getPlayerOrAdd(name, id, team, isBot, steamid.asUnsignedLongLong(), ip);
				}
				else
				{
					buff.shiftRight(1);
					for (size_t i = 0; i < xPlugins.size(); i++)
						xPlugins.get(i)->RenX_OnLog(this, buff);
					buff.shiftLeft(1);
				}
			}
			break;

		case 'x':
			header.shiftRight(1);
			if (header.size() == 0)
			{
				header.shiftLeft(1);
				break;
			}
			if (header[0] == 'r') // Command response
			{
				if (header.size() == 1)
				{
					header.shiftLeft(1);
					break;
				}
				header.shiftRight(1);
				switch (header[0])
				{
				case 1: // Client list: Normal Player Data | IP | Steam ID | Start Time | Ping | Kills | Deaths | Score | Credits | Class
					header.shiftRight(1);
					{
						PARSE_PLAYER_DATA_P(header);
						PlayerInfo *player = getPlayerOrAdd(name, id, team, isBot, buff.getToken(1, RenX::DelimC).asUnsignedLongLong(0), buff.getToken(2, RenX::DelimC));
						player->ping = buff.getToken(4, RenX::DelimC).asUnsignedInt() * 4;
						player->kills = buff.getToken(5, RenX::DelimC).asUnsignedInt();
						player->deaths = buff.getToken(6, RenX::DelimC).asUnsignedInt();
						player->score = static_cast<float>(buff.getToken(7, RenX::DelimC).asDouble());
						player->credits = static_cast<float>(buff.getToken(8, RenX::DelimC).asDouble());
						player->character = RenX::getCharacter(buff.getToken(9, RenX::DelimC));
					}
					header.shiftLeft(1);
					break;
				case 2: // Ping, Score, Credits list: Normal Player Data | Ping | Score | Credits
					header.shiftRight(1);
					{
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(header);
						player->ping = buff.getToken(1, RenX::DelimC).asUnsignedInt();
						player->score = static_cast<float>(buff.getToken(2, RenX::DelimC).asDouble());
						player->credits = static_cast<float>(buff.getToken(3, RenX::DelimC).asDouble());
					}
					header.shiftLeft(1);
					break;
				case 3: // Echo: Data
					break;
				case 4: // Add Credits: Normal Player Data | Credits
					header.shiftRight(1);
					parseGetPlayerOrAdd(header)->credits = static_cast<float>(buff.getToken(1, RenX::DelimC).asDouble());
					header.shiftLeft(1);
					break;
				case 5: // Ping: {Average Ping}/{Normal Player Data | Ping}
					break;
				case 6: // Command 2 on Timer: Time interval
					break;
				case 7: // Team change: Normal Player Data | Options (00<Reset Kills><Reset Deaths><Force-Zero(Never returned)><Reset Score><Reset Credits><Kill>)
					header.shiftRight(1);
					{
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(header);
						unsigned char options = 0;
						Jupiter::ReferenceString optionsToken = buff.getToken(1, RenX::DelimC);
						if (optionsToken.isEmpty() == false)
							options = optionsToken.get(0);
						if (options & 0x02)
							player->credits = 0.0f;
						if (options & 0x04)
							player->score = 0.0f;
						// 0x08 unused.
						if (options & 0x10)
							player->deaths = 0;
						if (options & 0x20)
							player->kills = 0;

						for (size_t i = 0; i < xPlugins.size(); i++)
							xPlugins.get(i)->RenX_OnTeamChange(this, player, RenX::getEnemy(player->team));
					}
					header.shiftLeft(1);
					break;
				default:
					break;
				}
				header.shiftLeft(1);
			}
			else if (header.equals("version"))
			{
				Jupiter::ReferenceString xVersionToken = buff.getToken(1, RenX::DelimC);
				RenX::Server::xRconVersion = xVersionToken.asUnsignedInt(10);
				RenX::Server::xRconRevision = xVersionToken.getToken(1, '.').asUnsignedInt(10);
				if (this->rconUser.equals(buff.getToken(2, RenX::DelimC)) == false)
					this->rconUser = buff.getToken(2, RenX::DelimC);
				RenX::Server::serverName = buff.getToken(3, RenX::DelimC);
				RenX::Server::map = buff.getToken(4, RenX::DelimC);
				for (size_t i = 0; i < xPlugins.size(); i++)
					xPlugins.get(i)->RenX_XOnVersion(this, RenX::Server::xRconVersion);
				RenX::Server::updateClientList();
				RenX::Server::sock.send(STRING_LITERAL_AS_REFERENCE("_x\x06\n"));
			}
			else if (header.equals("grant_character"))
			{
				RenX::PlayerInfo *player = parseGetPlayerOrAdd(buff.getToken(1, RenX::DelimC));
				Jupiter::ReferenceString character = buff.getToken(2, RenX::DelimC);
				for (size_t i = 0; i < xPlugins.size(); i++)
					xPlugins.get(i)->RenX_OnGrantCharacter(this, player, character);
				player->character = RenX::getCharacter(character);
			}
			else if (header.equals("grant_weapon"))
			{
				RenX::PlayerInfo *player = parseGetPlayerOrAdd(buff.getToken(1, RenX::DelimC));
				for (size_t i = 0; i < xPlugins.size(); i++)
					xPlugins.get(i)->RenX_OnGrantWeapon(this, player, buff.getToken(2, RenX::DelimC));
			}
			else if (header.equals("spawn_vehicle"))
			{
				Jupiter::ReferenceString tok1 = buff.getToken(1, RenX::DelimC);
				if (tok1.equalsi("buy"))
				{
					RenX::PlayerInfo *player = parseGetPlayerOrAdd(buff.getToken(3, RenX::DelimC));
					for (size_t i = 0; i < xPlugins.size(); i++)
						xPlugins.get(i)->RenX_OnSpawnVehicle(this, player, buff.getToken(2, RenX::DelimC));
				}
				else
				{
					RenX::TeamType team;
					if (tok1.isEmpty())
						team = TeamType::Other;
					else
						team = RenX::getTeam(tok1.get(0));
					for (size_t i = 0; i < xPlugins.size(); i++)
						xPlugins.get(i)->RenX_OnSpawnVehicleNoOwner(this, team, buff.getToken(2, RenX::DelimC));
				}
			}
			else if (header.equals("mine_place"))
			{
				RenX::PlayerInfo *player = parseGetPlayerOrAdd(buff.getToken(1, RenX::DelimC));
				for (size_t i = 0; i < xPlugins.size(); i++)
					xPlugins.get(i)->RenX_OnMinePlace(this, player, buff.getToken(2, RenX::DelimC));
			}
			/*else if (header.equals("mlimit_inc"))
			{
			}*/
			else if (header.equals("kill"))
			{
				Jupiter::ReferenceString kData = buff.getToken(2, RenX::DelimC);
				Jupiter::ReferenceString vData = buff.getToken(3, RenX::DelimC);
				if (kData.isEmpty() == false && vData.isEmpty() == false) // Safety check
				{
					struct
					{
						uint8_t type; // 1 = Player, 2 = Non-Player, 3 = None
						Jupiter::ReferenceString data;
					} killerData, victimData;
					Jupiter::ReadableString &damageType = buff.getToken(1, RenX::DelimC);
					killerData.type = kData[0];
					killerData.data = kData.substring(1);
					victimData.type = vData[0];
					victimData.data = vData.substring(1);
					if (killerData.type == 1) // These are already handled in standard RCON logs; update models and move on.
					{
						RenX::PlayerInfo *player = parseGetPlayerOrAdd(killerData.data.gotoToken(1, ','));
						player->character = RenX::getCharacter(killerData.data.getToken(0, ','));
						if (victimData.type == 1)
						{
							if (this->xRconVersion > 1 || this->xRconRevision >= 1 || victimData.data.findi(STRING_LITERAL_AS_REFERENCE("None")) != 0)
							{
								RenX::PlayerInfo *victim = parseGetPlayerOrAdd(victimData.data.gotoToken(1, ','));
								victim->character = RenX::getCharacter(victimData.data.getToken(0, ','));
							}
						}
					}
					else if (killerData.type == 3) // No killer!
					{
						if (victimData.type == 2 && victimData.data.size() != 0)
						{
							TeamType victimTeam = RenX::getTeam(victimData.data.getToken(0, ',').get(0));
							victimData.data = victimData.data.gotoToken(1, ',');
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnDie(this, victimData.data, victimTeam, damageType);
						}
					}
					else if (killerData.data.size() != 0) // Non-player killer (log!)
					{
						TeamType killerTeam = RenX::getTeam(killerData.data.getToken(0, ',').get(0));
						killerData.data = killerData.data.gotoToken(1, ',');
						if (victimData.type == 1) // Non-player killed player
						{
							RenX::PlayerInfo *player = parseGetPlayerOrAdd(victimData.data.gotoToken(1, ','));
							player->character = RenX::getCharacter(victimData.data.getToken(0, ','));
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnKill(this, killerData.data, killerTeam, player, damageType);
						}
						else if (victimData.data.size() != 0) // Non-player destroyed non-player
						{
							TeamType victimTeam = RenX::getTeam(victimData.data.getToken(0, ',').get(0));
							victimData.data = victimData.data.gotoToken(1, ',');
							ObjectType type;
							if (victimData.data.match("Rx_Building_*"))
								type = ObjectType::Building;
							else if (victimData.data.match("Rx_Defence_*"))
								type = ObjectType::Defence;
							else
								type = ObjectType::Vehicle;
							for (size_t i = 0; i < xPlugins.size(); i++)
								xPlugins.get(i)->RenX_OnDestroy(this, killerData.data, killerTeam, victimData.data, victimTeam, damageType, type);
						}
					}
				}
			}
			else
			{
				buff.shiftRight(1);
				for (size_t i = 0; i < xPlugins.size(); i++)
					xPlugins.get(i)->RenX_XOnOther(this, buff);
				buff.shiftLeft(1);
			}
			header.shiftLeft(1);
			break;

		case 'c':
			buff.shiftRight(1);
			for (size_t i = 0; i < xPlugins.size(); i++)
				xPlugins.get(i)->RenX_OnCommand(this, buff);
			buff.shiftLeft(1);
			break;

		case 'e':
			buff.shiftRight(1);
			for (size_t i = 0; i < xPlugins.size(); i++)
				xPlugins.get(i)->RenX_OnError(this, buff);
			buff.shiftLeft(1);
			break;

		case 'v':
			buff.shiftRight(1);
			this->rconVersion = buff.asInt(10);
			this->gameVersion = buff.substring(3);

			if (this->rconVersion == 1)
				this->profile = RenX::openBeta1Profile;
			else if (gameVersion.equals("Open Beta 2"))
				this->profile = RenX::openBeta2Profile;
			else if (gameVersion.equals("Open Beta 3"))
				this->profile = RenX::openBeta3Profile;

			RenX::Server::updateClientList();

			if (this->profile->disconnectOnGameOver == false)
				this->firstGame = true;
			else if (this->firstGame == false)
			{
				this->firstAction = false;
				this->silenceJoins = true;
			}

			for (size_t i = 0; i < xPlugins.size(); i++)
				xPlugins.get(i)->RenX_OnVersion(this, buff);
			buff.shiftLeft(1);
			break;

		case 'a':
			buff.shiftRight(1);
			if (RenX::Server::rconVersion >= 3)
				RenX::Server::rconUser = buff;
			for (size_t i = 0; i < xPlugins.size(); i++)
				xPlugins.get(i)->RenX_OnAuthorized(this, buff);
			buff.shiftLeft(1);
			break;

		default:
			buff.shiftRight(1);
			for (size_t i = 0; i < xPlugins.size(); i++)
				xPlugins.get(i)->RenX_OnOther(this, header[0], buff);
			buff.shiftLeft(1);
			break;
		}
		for (size_t i = 0; i < xPlugins.size(); i++)
			xPlugins.get(i)->RenX_OnRaw(this, buff);
	}
}

void RenX::Server::disconnect()
{
	RenX::Server::sock.closeSocket();
	RenX::Server::wipeData();
	RenX::Server::connected = false;
}

bool RenX::Server::connect()
{
	RenX::Server::lastAttempt = time(0);
	if (RenX::Server::sock.connectToHost(RenX::Server::hostname.c_str(), RenX::Server::port, RenX::Server::clientHostname.isEmpty() ? nullptr : RenX::Server::clientHostname.c_str()))
	{
		RenX::Server::sock.setBlocking(false);
		RenX::Server::sock.send(Jupiter::StringS::Format("a%.*s\n", RenX::Server::pass.size(), RenX::Server::pass.ptr()));
		RenX::Server::sock.send(STRING_LITERAL_AS_REFERENCE("s\n"));
		RenX::Server::connected = true;
		RenX::Server::silenceParts = false;
		return true;
	}
	RenX::Server::connected = false;
	return false;
}

bool RenX::Server::reconnect()
{
	RenX::Server::disconnect();
	return RenX::Server::connect();
}

void RenX::Server::wipeData()
{
	RenX::Server::rconUser.truncate(RenX::Server::rconUser.size());
	while (RenX::Server::players.size() != 0)
		delete RenX::Server::players.remove(0U);
}

unsigned int RenX::Server::getVersion() const
{
	return RenX::Server::rconVersion;
}

unsigned int RenX::Server::getXVersion() const
{
	return RenX::Server::xRconVersion;
}

unsigned int RenX::Server::getXRevision() const
{
	return RenX::Server::xRconRevision;
}

const Jupiter::ReadableString &RenX::Server::getGameVersion() const
{
	return RenX::Server::gameVersion;
}

const Jupiter::ReadableString &RenX::Server::getRCONUsername() const
{
	return RenX::Server::rconUser;
}

RenX::Server::Server(const Jupiter::ReadableString &configurationSection)
{
	RenX::Server::configSection = configurationSection;
	init();
}

void RenX::Server::init()
{
	RenX::Server::hostname = Jupiter::IRC::Client::Config->get(RenX::Server::configSection, STRING_LITERAL_AS_REFERENCE("Hostname"), STRING_LITERAL_AS_REFERENCE("localhost"));
	RenX::Server::port = static_cast<unsigned short>(Jupiter::IRC::Client::Config->getInt(RenX::Server::configSection, STRING_LITERAL_AS_REFERENCE("Port"), 7777));
	RenX::Server::clientHostname = Jupiter::IRC::Client::Config->get(RenX::Server::configSection, STRING_LITERAL_AS_REFERENCE("ClientAddress"));
	RenX::Server::pass = Jupiter::IRC::Client::Config->get(RenX::Server::configSection, STRING_LITERAL_AS_REFERENCE("Password"), STRING_LITERAL_AS_REFERENCE("renx"));

	RenX::Server::logChanType = Jupiter::IRC::Client::Config->getShort(RenX::Server::configSection, STRING_LITERAL_AS_REFERENCE("ChanType"));
	RenX::Server::adminLogChanType = Jupiter::IRC::Client::Config->getShort(RenX::Server::configSection, STRING_LITERAL_AS_REFERENCE("AdminChanType"));

	RenX::Server::setCommandPrefix(Jupiter::IRC::Client::Config->get(RenX::Server::configSection, STRING_LITERAL_AS_REFERENCE("CommandPrefix")));
	RenX::Server::setPrefix(Jupiter::IRC::Client::Config->get(RenX::Server::configSection, STRING_LITERAL_AS_REFERENCE("IRCPrefix")));

	RenX::Server::rules = Jupiter::IRC::Client::Config->get(RenX::Server::configSection, STRING_LITERAL_AS_REFERENCE("Rules"), STRING_LITERAL_AS_REFERENCE("Anarchy!"));
	RenX::Server::delay = Jupiter::IRC::Client::Config->getInt(RenX::Server::configSection, STRING_LITERAL_AS_REFERENCE("ReconnectDelay"), 10);
	RenX::Server::uuidMode = Jupiter::IRC::Client::Config->getInt(RenX::Server::configSection, STRING_LITERAL_AS_REFERENCE("UUIDMode"), 0);
	RenX::Server::rconBan = Jupiter::IRC::Client::Config->getBool(RenX::Server::configSection, STRING_LITERAL_AS_REFERENCE("RCONBan"), false);
	RenX::Server::localSteamBan = Jupiter::IRC::Client::Config->getBool(RenX::Server::configSection, STRING_LITERAL_AS_REFERENCE("LocalSteamBan"), true);
	RenX::Server::localIPBan = Jupiter::IRC::Client::Config->getBool(RenX::Server::configSection, STRING_LITERAL_AS_REFERENCE("LocalIPBan"), true);
	RenX::Server::localNameBan = Jupiter::IRC::Client::Config->getBool(RenX::Server::configSection, STRING_LITERAL_AS_REFERENCE("LocalNameBan"), false);
	RenX::Server::localBan = RenX::Server::localIPBan || RenX::Server::localSteamBan || RenX::Server::localNameBan;
	RenX::Server::steamFormat = Jupiter::IRC::Client::Config->getInt(RenX::Server::configSection, STRING_LITERAL_AS_REFERENCE("SteamFormat"), 16);
	RenX::Server::neverSay = Jupiter::IRC::Client::Config->getBool(RenX::Server::configSection, STRING_LITERAL_AS_REFERENCE("NeverSay"), false);

	Jupiter::INIFile &commandsFile = RenX::getCore()->getCommandsFile();
	RenX::Server::commandAccessLevels = commandsFile.getSection(RenX::Server::configSection);
	RenX::Server::commandAliases = commandsFile.getSection(Jupiter::StringS::Format("%.*s.Aliases", RenX::Server::configSection.size(), RenX::Server::configSection.ptr()));

	for (size_t i = 0; i < RenX::GameMasterCommandList->size(); i++)
		RenX::Server::addCommand(RenX::GameMasterCommandList->get(i)->copy());
}

RenX::Server::~Server()
{
	sock.closeSocket();
	RenX::Server::wipeData();
	RenX::Server::commands.emptyAndDelete();
}
