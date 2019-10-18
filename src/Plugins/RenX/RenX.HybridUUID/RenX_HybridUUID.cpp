/**
 * Copyright (C) 2015-2017 Jessica James.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Written by Jessica James <jessica.aj@outlook.com>
 */

#include "Jupiter/String.hpp"
#include "RenX_Core.h"
#include "RenX_Server.h"
#include "RenX_PlayerInfo.h"
#include "RenX_Functions.h"
#include "RenX_HybridUUID.h"

using namespace Jupiter::literals;

Jupiter::StringS calc_uuid(RenX::PlayerInfo &player)
{
	if (player.steamid != 0U)
		return Jupiter::StringS::Format("S%.16llX", player.steamid);
	return "N"_jrs + player.name;
}

RenX_HybridUUIDPlugin::RenX_HybridUUIDPlugin()
{
	RenX::Core &core = *RenX::getCore();
	size_t index = core.getServerCount();
	while (index != 0)
		core.getServer(--index)->setUUIDFunction(calc_uuid);
}

RenX_HybridUUIDPlugin::~RenX_HybridUUIDPlugin()
{
	RenX::Core &core = *RenX::getCore();
	size_t index = core.getServerCount();
	while (index != 0)
		core.getServer(--index)->setUUIDFunction(RenX::default_uuid_func);
}

void RenX_HybridUUIDPlugin::RenX_OnServerCreate(RenX::Server &server)
{
	server.setUUIDFunction(calc_uuid);
}

// Plugin instantiation and entry point.
RenX_HybridUUIDPlugin pluginInstance;

extern "C" JUPITER_EXPORT Jupiter::Plugin *getPlugin()
{
	return &pluginInstance;
}