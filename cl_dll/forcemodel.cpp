#include <algorithm>
#include <cstring>
#include <vector>

#include "hud.h"
#include "cl_util.h"
#include "com_model.h"
#include "r_studioint.h"
#include "forcemodel.h"
#include "steam_id.h"

extern engine_studio_api_t IEngineStudio;
extern hud_player_info_t   g_PlayerInfoList [MAX_PLAYERS + 1];
extern extra_player_info_t g_PlayerExtraInfo[MAX_PLAYERS + 1];

namespace force_model
{
	namespace
	{
		struct model_override
		{
			char* who;
			model_t* model;

			model_override(const char* who_, model_t* model)
				: model(model)
			{
				auto len = strlen(who_);

				who = new char[len + 1];
				std::memcpy(who, who_, len + 1);
			}

			~model_override()
			{
				delete[] who;
			}
		};

		// Overrides by the team name.
		std::vector<model_override> team_model_overrides;

		// Cache for fast lookup.
		model_t* team_model_overrides_cache[MAX_PLAYERS];

		// Overrides by the SteamID.
		std::vector<model_override> steam_id_model_overrides;

		// Cache for fast lookup.
		model_t* steam_id_model_overrides_cache[MAX_PLAYERS];

		void callback_cl_forceteammodel()
		{
			if (gEngfuncs.Cmd_Argc() != 3) {
				gEngfuncs.Con_Printf("cl_forceteammodel <team name> <model name>\n");
				return;
			}

			auto model_name = gEngfuncs.Cmd_Argv(2);

			char model_path[4096];
			std::snprintf(model_path, ARRAYSIZE(model_path), "models/player/%s/%s.mdl", model_name, model_name);

			auto model = IEngineStudio.Mod_ForName(model_path, 0);
			if (!model) {
				gEngfuncs.Con_Printf("This model could not be loaded.\n");
				return;
			}

			auto team_name = gEngfuncs.Cmd_Argv(1);

			auto it = std::find_if(
				team_model_overrides.begin(),
				team_model_overrides.end(),
				[=](const model_override& entry) {
					return !strcmp(entry.who, team_name);
				});

			if (it != team_model_overrides.end())
				it->model = model;
			else
				team_model_overrides.emplace_back(team_name, model);

			update_player_teams();
		}

		void callback_cl_forceteammodel_list()
		{
			if (team_model_overrides.size() == 0) {
				gEngfuncs.Con_Printf("There are no team model overrides.\n");
				return;
			}

			for (unsigned i = 0; i < team_model_overrides.size(); ++i) {
				const auto& entry = team_model_overrides[i];
				gEngfuncs.Con_Printf("%u. %s -> %s\n", i + 1, entry.who, entry.model->name);
			}
		}

		void callback_cl_forceteammodel_remove()
		{
			if (gEngfuncs.Cmd_Argc() != 2) {
				gEngfuncs.Con_Printf("cl_forceteammodel_remove <team name>\n");
				return;
			}

			auto it = std::find_if(
				team_model_overrides.begin(),
				team_model_overrides.end(),
				[=](const model_override& entry) {
					return !strcmp(entry.who, gEngfuncs.Cmd_Argv(1));
				});

			if (it != team_model_overrides.end()) {
				team_model_overrides.erase(it);

				update_player_teams();
			}
		}

		void callback_cl_forcemodel()
		{
			if (gEngfuncs.Cmd_Argc() != 3) {
				gEngfuncs.Con_Printf("cl_forcemodel <name / SteamID / #UID> <model name>\n");
				return;
			}

			auto model_name = gEngfuncs.Cmd_Argv(2);

			char model_path[4096];
			std::snprintf(model_path, ARRAYSIZE(model_path), "models/player/%s/%s.mdl", model_name, model_name);

			auto model = IEngineStudio.Mod_ForName(model_path, 0);
			if (!model) {
				gEngfuncs.Con_Printf("This model could not be loaded.\n");
				return;
			}

			auto name = gEngfuncs.Cmd_Argv(1);
			bool name_contains_color_tags = contains_color_tags(name);

			int matched_player_index = -1; // -1 means didn't match anyone.

			// Matching by the name might return several players.
			std::vector<int> matched_player_indices;

			// Try getting the user ID to match against.
			int uid = -1; // -1 means an invalid user ID.
			if (!name_contains_color_tags)
				sscanf(name, "#%d", &uid);

			for (int i = 0; i < MAX_PLAYERS; ++i) {
				// Make sure the information is up to date.
				gEngfuncs.pfnGetPlayerInfo(i + 1, &g_PlayerInfoList[i + 1]);

				// This player slot is empty.
				if (g_PlayerInfoList[i + 1].name == nullptr)
					continue;

				if (!name_contains_color_tags) {
					// Try to match by the user ID.
					if (uid != -1 && IEngineStudio.PlayerInfo(i)->userid == uid) {
						// Got a match by the user ID.
						matched_player_index = i;
						break;
					}

					// Try to match by the SteamID.
					if (!strcmp(steam_id::get_steam_id(i).c_str(), name)) {
						// Got a match by the SteamID.
						matched_player_index = i;
						break;
					}
				}

				// Try to match by the name.
				const char* name_to_match_against;
				if (name_contains_color_tags)
					name_to_match_against = g_PlayerInfoList[i + 1].name;
				else
					name_to_match_against = strip_color_tags_thread_unsafe(g_PlayerInfoList[i + 1].name);

				if (strstr(name_to_match_against, name) != nullptr) {
					// Got a match by the name. Add it to the match list.
					matched_player_indices.push_back(i);
				}
			}

			if (matched_player_index == -1) {
				if (matched_player_indices.size() > 1) {
					// The name matched several players.
					gEngfuncs.Con_Printf("Ambiguous player name, can be one of:\n");

					for (auto index : matched_player_indices) {
						const char* name_to_print;
						if (name_contains_color_tags)
							name_to_print = g_PlayerInfoList[index + 1].name;
						else
							name_to_print = strip_color_tags_thread_unsafe(g_PlayerInfoList[index + 1].name);

						gEngfuncs.Con_Printf(
							"- %s (#%d; %s)\n",
							name_to_print,
							IEngineStudio.PlayerInfo(index)->userid,
							steam_id::get_steam_id(index).c_str()
						);
					}

					gEngfuncs.Con_Printf("Please specify who you meant! You can also use the SteamID or UID.\n");
				} else if (matched_player_indices.size() == 1) {
					matched_player_index = matched_player_indices[0];
				}
			}

			if (matched_player_index != -1) {
				// Matched someone.
				const char* matched_steam_id = steam_id::get_steam_id(matched_player_index).c_str();

				auto it = std::find_if(
					steam_id_model_overrides.begin(),
					steam_id_model_overrides.end(),
					[=](const model_override& entry) {
						return !strcmp(entry.who, matched_steam_id);
					});

				if (it != steam_id_model_overrides.end())
					it->model = model;
				else
					steam_id_model_overrides.emplace_back(matched_steam_id, model);

				update_player_steam_ids();
			} else if (matched_player_indices.size() == 0) {
				// Didn't match anyone.
				gEngfuncs.Con_Printf("There is no such player.\n");
			}
		}

		void callback_cl_forcemodel_list()
		{
			if (steam_id_model_overrides.size() == 0) {
				gEngfuncs.Con_Printf("There are no model overrides.\n");
				return;
			}

			for (unsigned i = 0; i < steam_id_model_overrides.size(); ++i) {
				const auto& entry = steam_id_model_overrides[i];

				const char* name;
				int uid = -1;
				for (size_t j = 0; j < MAX_PLAYERS; ++j) {
					if (!strcmp(steam_id::get_steam_id(j).c_str(), entry.who)) {
						// Make sure the information is up to date.
						gEngfuncs.pfnGetPlayerInfo(j + 1, &g_PlayerInfoList[j + 1]);

						// If the player is still connected:
						if (g_PlayerInfoList[j + 1].name != nullptr) {
							name = g_PlayerInfoList[j + 1].name;
							uid = IEngineStudio.PlayerInfo(j)->userid;
							break;
						}
					}
				}

				if (uid == -1) {
					gEngfuncs.Con_Printf("%u. %s -> %s\n", i + 1, entry.who, entry.model->name);
				} else {
					gEngfuncs.Con_Printf(
						"%u. %s (#%d; `%s`) -> %s\n",
						i + 1,
						entry.who,
						uid,
						name,
						entry.model->name
					);
				}
			}
		}

		void callback_cl_forcemodel_remove()
		{
			if (gEngfuncs.Cmd_Argc() != 2) {
				gEngfuncs.Con_Printf("cl_forcemodel_remove <name / SteamID / #UID>\n");
				return;
			}

			auto name = gEngfuncs.Cmd_Argv(1);
			bool name_contains_color_tags = contains_color_tags(name);

			auto matched_element = steam_id_model_overrides.cend();

			// Matching by the name might return several players.
			std::vector<int> matched_player_indices;
			std::vector<std::vector<model_override>::const_iterator> matched_elements;

			// Try getting the user ID to match against.
			int uid = -1; // -1 means an invalid user ID.
			if (!name_contains_color_tags)
				sscanf(name, "#%d", &uid);

			for (auto it = steam_id_model_overrides.cbegin(); it != steam_id_model_overrides.cend(); ++it) {
				// Try to match by Steam ID.
				if (!strcmp(it->who, name)) {
					matched_element = it;
					break;
				}

				for (size_t j = 0; j < MAX_PLAYERS; ++j) {
					if (!strcmp(steam_id::get_steam_id(j).c_str(), it->who)) {
						// Make sure the information is up to date.
						gEngfuncs.pfnGetPlayerInfo(j + 1, &g_PlayerInfoList[j + 1]);

						// If the player is still connected:
						if (g_PlayerInfoList[j + 1].name != nullptr) {
							if (!name_contains_color_tags) {
								// Try to match by the user ID.
								if (uid != -1 && IEngineStudio.PlayerInfo(j)->userid == uid) {
									// Got a match by the user ID.
									matched_element = it;
									break;
								}
							}

							// Try to match by the name.
							const char* name_to_match_against;
							if (name_contains_color_tags)
								name_to_match_against = g_PlayerInfoList[j + 1].name;
							else
								name_to_match_against = strip_color_tags_thread_unsafe(g_PlayerInfoList[j + 1].name);

							if (strstr(name_to_match_against, name) != nullptr) {
								// Got a match by the name. Add it to the match list.
								matched_player_indices.push_back(j);
								matched_elements.push_back(it);
							}

							break;
						}
					}
				}

				// Found something definite.
				if (matched_element != steam_id_model_overrides.cend())
					break;
			}

			if (matched_element == steam_id_model_overrides.cend()) {
				if (matched_player_indices.size() > 1) {
					// The name matched several players.
					gEngfuncs.Con_Printf("Ambiguous player name, can be one of:\n");

					for (auto index : matched_player_indices) {
						const char* name_to_print;
						if (name_contains_color_tags)
							name_to_print = g_PlayerInfoList[index + 1].name;
						else
							name_to_print = strip_color_tags_thread_unsafe(g_PlayerInfoList[index + 1].name);

						gEngfuncs.Con_Printf(
							"- %s (#%d; %s)\n",
							name_to_print,
							IEngineStudio.PlayerInfo(index)->userid,
							steam_id::get_steam_id(index).c_str()
						);
					}

					gEngfuncs.Con_Printf("Please specify who you meant! You can also use the SteamID or UID.\n");
				} else if (matched_player_indices.size() == 1) {
					// Got a single match.
					matched_element = matched_elements[0];
				}
			}

			if (matched_element != steam_id_model_overrides.cend()) {
				// Matched someone.
				steam_id_model_overrides.erase(matched_element);

				update_player_steam_ids();
			} else if (matched_player_indices.size() == 0) {
				// Didn't match anyone.
				gEngfuncs.Con_Printf("There is no such override.\n");
			}
		}
	}

	void hook_commands()
	{
		gEngfuncs.pfnAddCommand("cl_forceteammodel", callback_cl_forceteammodel);
		gEngfuncs.pfnAddCommand("cl_forceteammodel_list", callback_cl_forceteammodel_list);
		gEngfuncs.pfnAddCommand("cl_forceteammodel_remove", callback_cl_forceteammodel_remove);

		gEngfuncs.pfnAddCommand("cl_forcemodel", callback_cl_forcemodel);
		gEngfuncs.pfnAddCommand("cl_forcemodel_list", callback_cl_forcemodel_list);
		gEngfuncs.pfnAddCommand("cl_forcemodel_remove", callback_cl_forcemodel_remove);
	}

	void update_player_team(int player_index)
	{
		auto team_name = g_PlayerExtraInfo[player_index + 1].teamname;

		auto it = std::find_if(
			team_model_overrides.cbegin(),
			team_model_overrides.cend(),
			[=](const model_override& entry) {
				return !strcmp(entry.who, team_name);
			});

		if (it != team_model_overrides.cend())
			team_model_overrides_cache[player_index] = it->model;
		else
			team_model_overrides_cache[player_index] = nullptr;
	}

	void update_player_teams()
	{
		for (size_t i = 0; i < MAX_PLAYERS; ++i)
			update_player_team(i);
	}

	void update_player_steam_id(int player_index)
	{
		auto it = std::find_if(
			steam_id_model_overrides.cbegin(),
			steam_id_model_overrides.cend(),
			[=](const model_override& entry) {
				return !strcmp(entry.who, steam_id::get_steam_id(player_index).c_str());
			});

		if (it != steam_id_model_overrides.cend())
			steam_id_model_overrides_cache[player_index] = it->model;
		else
			steam_id_model_overrides_cache[player_index] = nullptr;
	}

	void update_player_steam_ids()
	{
		for (size_t i = 0; i < MAX_PLAYERS; ++i)
			update_player_steam_id(i);
	}

	model_t* get_model_override(int player_index)
	{
		auto model = steam_id_model_overrides_cache[player_index];
		if (model)
			return model;

		return team_model_overrides_cache[player_index];
	}
}
