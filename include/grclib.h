#ifndef GRCLIB_H
#define GRCLIB_H
#ifdef __cplusplus
extern "C" {
#endif
#ifdef _WIN32
#ifdef GRCLIB_EXPORTS
#define GRCLIB_API __declspec(dllexport)
#else
#define GRCLIB_API __declspec(dllimport)
#endif
#else
#define GRCLIB_API
#endif
typedef void* RCHandle;
typedef struct {
    char* name;
    char* ip;
    int port;
    int players;
    char* language;
    char* description;
} RCServer;
typedef struct {
    char* account;
    int id;
    char* nick;
    char* level;
} RCPlayer;
typedef struct {
    char* name;
    char* image;
    char* script;
} RCWeapon;
typedef struct {
    char* name;
    char* script;
} RCClass;
typedef struct {
    int id;
    char* name;
    char* type;
    char* image;
    char* script;
} RCNPC;
typedef struct {
    char* name;
    char* type;
} RCLevel;
typedef struct {
    char* rights;
    char* pattern;
} RCFileBrowserFolder;
typedef struct {
    char* path;
    char* rights;
    int size;
    int modified;
    int is_directory;
} RCFileBrowserEntry;
typedef void (*RC_OnConnected)(void* user_data);
typedef void (*RC_OnDisconnected)(const char* reason, void* user_data);
typedef void (*RC_OnPlayerJoined)(const char* account, int player_id, void* user_data);
typedef void (*RC_OnPlayerLeft)(const char* account, int player_id, void* user_data);
typedef void (*RC_OnMessage)(const char* message, void* user_data);
typedef void (*RC_OnPrivateMessage)(int player_id, const char* account, const char* nick, const char* message, void* user_data);
typedef void (*RC_OnFileReceived)(const char* path, const void* content, int length, void* user_data);
typedef void (*RC_OnWeaponAdded)(const char* name, void* user_data);
typedef void (*RC_OnWeaponDeleted)(const char* name, void* user_data);
typedef void (*RC_OnClassAdded)(const char* name, void* user_data);
typedef void (*RC_OnClassDeleted)(const char* name, void* user_data);
typedef void (*RC_OnNPCAdded)(int id, const char* name, void* user_data);
typedef void (*RC_OnNPCDeleted)(int id, void* user_data);
typedef void (*RC_OnNPCAttributes)(int npc_id, const char* attributes, void* user_data);
typedef void (*RC_OnPlayerPropChanged)(int player_id, const char* prop, const char* value, void* user_data);
typedef void (*RC_OnWorldTime)(int world_time, void* user_data);
typedef void (*RC_OnMaxUploadFileSize)(long long max_size, void* user_data);
typedef void (*RC_OnCommandResponse)(const char* response, void* user_data);
typedef void (*RC_OnRawPacket)(int packet_id, const char* data, int length, void* user_data);
typedef void (*RC_OnPMServersUpdated)(int count, void* user_data);
typedef void (*RC_OnNPCFlags)(int npc_id, const char* flags, void* user_data);
typedef void (*RC_OnPMServerPlayers)(const char* server_name, const char* player_data, void* user_data);
typedef void (*RC_OnFileBrowserFolders)(int count, void* user_data);
typedef void (*RC_OnFileBrowserFiles)(const char* folder, int count, void* user_data);
typedef void (*RC_OnFileBrowserMessage)(const char* message, void* user_data);
typedef void (*RC_OnScriptReceived)(const char* script_type, const char* name, int id, const char* script, void* user_data);
typedef void (*RC_OnServerData)(const char* data_type, const char* content, void* user_data);
typedef void (*RC_OnPlayerRights)(const char* account, int rights, const char* ip_range, const char* folder_access, void* user_data);
typedef void (*RC_OnPlayerTextData)(const char* data_type, const char* account, const char* content, void* user_data);
typedef void (*RC_OnPlayerAttributes)(const char* account, const char* properties_json, const char* editor_text, void* user_data);
typedef void (*RC_OnLocalNPCs)(const char* level, const char* content, void* user_data);
typedef void (*RC_OnIrcMessage)(const char* channel, const char* line, void* user_data);
typedef void (*RC_OnBanData)(const char* account, const char* computer_id, const char* details, void* user_data);
typedef void (*RC_OnBanListData)(const char* data_type, const char* account, const char* content, void* user_data);
typedef void (*RC_OnAccountList)(const char* accounts, void* user_data);
GRCLIB_API RCHandle rc_connect(const char* listserver_host, int listserver_port, const char* account, const char* password);
GRCLIB_API int rc_get_servers(RCHandle handle, RCServer** servers_out);
GRCLIB_API int rc_connect_to_server(RCHandle handle, int server_index);
GRCLIB_API int rc_connect_to_nc_server(RCHandle handle);
GRCLIB_API int rc_is_connected(RCHandle handle);
GRCLIB_API int rc_is_authenticated(RCHandle handle);
GRCLIB_API int rc_is_nc_connected(RCHandle handle);
GRCLIB_API int rc_is_nc_authenticated(RCHandle handle);
GRCLIB_API void rc_on_connected(RCHandle handle, RC_OnConnected callback, void* user_data);
GRCLIB_API void rc_on_disconnected(RCHandle handle, RC_OnDisconnected callback, void* user_data);
GRCLIB_API void rc_on_player_joined(RCHandle handle, RC_OnPlayerJoined callback, void* user_data);
GRCLIB_API void rc_on_player_left(RCHandle handle, RC_OnPlayerLeft callback, void* user_data);
GRCLIB_API void rc_on_message(RCHandle handle, RC_OnMessage callback, void* user_data);
GRCLIB_API void rc_on_private_message(RCHandle handle, RC_OnPrivateMessage callback, void* user_data);
GRCLIB_API void rc_on_file_received(RCHandle handle, RC_OnFileReceived callback, void* user_data);
GRCLIB_API void rc_on_weapon_added(RCHandle handle, RC_OnWeaponAdded callback, void* user_data);
GRCLIB_API void rc_on_weapon_deleted(RCHandle handle, RC_OnWeaponDeleted callback, void* user_data);
GRCLIB_API void rc_on_class_added(RCHandle handle, RC_OnClassAdded callback, void* user_data);
GRCLIB_API void rc_on_class_deleted(RCHandle handle, RC_OnClassDeleted callback, void* user_data);
GRCLIB_API void rc_on_npc_added(RCHandle handle, RC_OnNPCAdded callback, void* user_data);
GRCLIB_API void rc_on_npc_deleted(RCHandle handle, RC_OnNPCDeleted callback, void* user_data);
GRCLIB_API void rc_on_npc_attributes(RCHandle handle, RC_OnNPCAttributes callback, void* user_data);
GRCLIB_API void rc_on_player_prop_changed(RCHandle handle, RC_OnPlayerPropChanged callback, void* user_data);
GRCLIB_API void rc_on_world_time(RCHandle handle, RC_OnWorldTime callback, void* user_data);
GRCLIB_API void rc_on_max_upload_file_size(RCHandle handle, RC_OnMaxUploadFileSize callback, void* user_data);
GRCLIB_API void rc_on_command_response(RCHandle handle, RC_OnCommandResponse callback, void* user_data);
GRCLIB_API void rc_on_raw_packet(RCHandle handle, RC_OnRawPacket callback, void* user_data);
GRCLIB_API void rc_on_pm_servers_updated(RCHandle handle, RC_OnPMServersUpdated callback, void* user_data);
GRCLIB_API void rc_on_npc_flags(RCHandle handle, RC_OnNPCFlags callback, void* user_data);
GRCLIB_API void rc_on_pm_server_players(RCHandle handle, RC_OnPMServerPlayers callback, void* user_data);
GRCLIB_API void rc_on_filebrowser_folders(RCHandle handle, RC_OnFileBrowserFolders callback, void* user_data);
GRCLIB_API void rc_on_filebrowser_files(RCHandle handle, RC_OnFileBrowserFiles callback, void* user_data);
GRCLIB_API void rc_on_filebrowser_message(RCHandle handle, RC_OnFileBrowserMessage callback, void* user_data);
GRCLIB_API void rc_on_script_received(RCHandle handle, RC_OnScriptReceived callback, void* user_data);
GRCLIB_API void rc_on_server_data(RCHandle handle, RC_OnServerData callback, void* user_data);
GRCLIB_API void rc_on_player_rights(RCHandle handle, RC_OnPlayerRights callback, void* user_data);
GRCLIB_API void rc_on_player_text_data(RCHandle handle, RC_OnPlayerTextData callback, void* user_data);
GRCLIB_API void rc_on_player_attributes(RCHandle handle, RC_OnPlayerAttributes callback, void* user_data);
GRCLIB_API void rc_on_local_npcs(RCHandle handle, RC_OnLocalNPCs callback, void* user_data);
GRCLIB_API void rc_on_irc_message(RCHandle handle, RC_OnIrcMessage callback, void* user_data);
GRCLIB_API void rc_on_ban_data(RCHandle handle, RC_OnBanData callback, void* user_data);
GRCLIB_API void rc_on_ban_list_data(RCHandle handle, RC_OnBanListData callback, void* user_data);
GRCLIB_API void rc_on_account_list(RCHandle handle, RC_OnAccountList callback, void* user_data);
GRCLIB_API int rc_get_players(RCHandle handle, RCPlayer** players_out);
GRCLIB_API int rc_get_weapons(RCHandle handle, RCWeapon** weapons_out);
GRCLIB_API int rc_get_classes(RCHandle handle, RCClass** classes_out);
GRCLIB_API int rc_get_npcs(RCHandle handle, RCNPC** npcs_out);
GRCLIB_API int rc_get_levels(RCHandle handle, RCLevel** levels_out);
GRCLIB_API int rc_get_pm_servers(RCHandle handle, const char*** servers_out);
GRCLIB_API char* rc_get_cached_npc_flags(RCHandle handle, int npc_id);
GRCLIB_API int rc_get_filebrowser_folders(RCHandle handle, RCFileBrowserFolder** folders_out);
GRCLIB_API int rc_get_filebrowser_files(RCHandle handle, RCFileBrowserEntry** entries_out);
GRCLIB_API int rc_copy_filebrowser_folders(RCHandle handle, RCFileBrowserFolder** folders_out);
GRCLIB_API int rc_copy_filebrowser_files(RCHandle handle, RCFileBrowserEntry** entries_out);
GRCLIB_API void rc_free_filebrowser_folders(RCFileBrowserFolder* folders, int count);
GRCLIB_API void rc_free_filebrowser_files(RCFileBrowserEntry* entries, int count);
GRCLIB_API char* rc_get_server_options(RCHandle handle);
GRCLIB_API char* rc_get_server_flags(RCHandle handle);
GRCLIB_API char* rc_get_folder_config(RCHandle handle);
GRCLIB_API long long rc_get_max_upload_file_size(RCHandle handle);
GRCLIB_API int rc_execute(RCHandle handle, const char* command);
GRCLIB_API int rc_upload_file(RCHandle handle, const char* path, const char* content, int length);
GRCLIB_API int rc_download_file(RCHandle handle, const char* path);
GRCLIB_API int rc_warp_player(RCHandle handle, int player_id, const char* level, float x, float y);
GRCLIB_API int rc_disconnect_player(RCHandle handle, int player_id, const char* reason);
GRCLIB_API int rc_add_weapon(RCHandle handle, const char* name, const char* image, const char* script);
GRCLIB_API int rc_delete_weapon(RCHandle handle, const char* name);
GRCLIB_API int rc_update_weapon(RCHandle handle, const char* name, const char* image, const char* script);
GRCLIB_API int rc_add_class(RCHandle handle, const char* name, const char* script);
GRCLIB_API int rc_delete_class(RCHandle handle, const char* name);
GRCLIB_API int rc_update_class(RCHandle handle, const char* name, const char* script);
GRCLIB_API int rc_delete_npc(RCHandle handle, int npc_id);
GRCLIB_API int rc_update_npc(RCHandle handle, int npc_id, const char* script);
GRCLIB_API int rc_create_npc_on_server(RCHandle handle, const char* name, int npc_id, const char* type, const char* scripter, const char* level, const char* x, const char* y);
GRCLIB_API int rc_disconnect_nc(RCHandle handle);
GRCLIB_API int rc_set_nickname(RCHandle handle, const char* nickname);
GRCLIB_API int rc_upload_level(RCHandle handle, const char* level_name, const char* content, int length);
GRCLIB_API int rc_download_level(RCHandle handle, const char* level_name);
GRCLIB_API int rc_request_server_list(RCHandle handle);
GRCLIB_API int rc_request_pm_server_list(RCHandle handle);
GRCLIB_API int rc_send_toall_message(RCHandle handle, const char* message);
GRCLIB_API int rc_request_npc_script(RCHandle handle, int npc_id);
GRCLIB_API int rc_request_npc_attributes(RCHandle handle, int npc_id);
GRCLIB_API int rc_request_class_script(RCHandle handle, const char* class_name);
GRCLIB_API int rc_request_weapon_script(RCHandle handle, const char* weapon_name);
GRCLIB_API int rc_reset_npc(RCHandle handle, int npc_id);
GRCLIB_API int rc_warp_npc(RCHandle handle, int npc_id, float x, float y, const char* level);
GRCLIB_API int rc_get_npc_flags(RCHandle handle, int npc_id);
GRCLIB_API int rc_set_npc_flags(RCHandle handle, int npc_id, const char* flags);
GRCLIB_API int rc_send_nc_packet(RCHandle handle, int packet_id, const char* data, int length);
GRCLIB_API int rc_request_player_rights(RCHandle handle, const char* account);
GRCLIB_API int rc_request_player_attrs(RCHandle handle, const char* account);
GRCLIB_API int rc_request_player_account(RCHandle handle, const char* account);
GRCLIB_API int rc_request_account_list(RCHandle handle, const char* account_filter, const char* conditions);
GRCLIB_API int rc_request_player_comments(RCHandle handle, const char* account);
GRCLIB_API int rc_request_player_profile(RCHandle handle, const char* account);
GRCLIB_API int rc_send_private_message(RCHandle handle, int player_id, const char* message);
GRCLIB_API int rc_set_player_rights(RCHandle handle, const char* account, int rights_value, const char* ip_range, const char* folder_access);
GRCLIB_API int rc_set_player_comments(RCHandle handle, const char* account, const char* comments);
GRCLIB_API int rc_set_player_attributes(RCHandle handle, const char* account, const char* properties_json);
GRCLIB_API int rc_set_player_account(RCHandle handle, const char* account, const char* account_text);
GRCLIB_API int rc_add_player_account(RCHandle handle, const char* account_text);
GRCLIB_API int rc_set_player_profile(RCHandle handle, const char* account, const char* profile_text);
GRCLIB_API int rc_send_mass_pm(RCHandle handle, const int* player_ids, int count, const char* message);
GRCLIB_API char* rc_format_player_rights_text(int rights_value, const char* ip_range, const char* folder_access);
GRCLIB_API char* rc_format_player_account_text(const char* account_data);
GRCLIB_API char* rc_format_player_attributes_text(const char* properties_json);
GRCLIB_API char* rc_parse_player_rights_text(const char* text);
GRCLIB_API char* rc_parse_player_account_text(const char* text);
GRCLIB_API char* rc_parse_player_attributes_text(const char* text);
GRCLIB_API int rc_is_new_protocol(RCHandle handle);
GRCLIB_API void rc_set_new_protocol(RCHandle handle, int enabled);
GRCLIB_API void rc_set_login_pcid_list(RCHandle handle, const char* pcid_list);
GRCLIB_API int rc_request_local_npcs(RCHandle handle, const char* level);
GRCLIB_API int rc_send_irc_text(RCHandle handle, const char* command, const char* param1, const char* param2, const char* param3);
GRCLIB_API int rc_irc_login(RCHandle handle);
GRCLIB_API int rc_irc_join(RCHandle handle, const char* channel);
GRCLIB_API int rc_irc_part(RCHandle handle, const char* channel);
GRCLIB_API int rc_request_player_ban(RCHandle handle, const char* account, int player_id);
GRCLIB_API int rc_request_player_ban_by_account(RCHandle handle, const char* account);
GRCLIB_API int rc_request_ban_types(RCHandle handle);
GRCLIB_API int rc_request_ban_history(RCHandle handle, const char* account);
GRCLIB_API int rc_request_staff_activity(RCHandle handle, const char* account);
GRCLIB_API int rc_set_ban(RCHandle handle, const char* target, const char* world, int banned, const char* ban_type, const char* release_time, const char* reason);
GRCLIB_API int rc_set_legacy_player_ban(RCHandle handle, const char* account, int banned, const char* reason);
GRCLIB_API int rc_request_server_options(RCHandle handle);
GRCLIB_API int rc_upload_server_options(RCHandle handle, const char* content);
GRCLIB_API int rc_request_server_flags(RCHandle handle);
GRCLIB_API int rc_upload_server_flags(RCHandle handle, const char* content);
GRCLIB_API int rc_request_folder_config(RCHandle handle);
GRCLIB_API int rc_upload_folder_config(RCHandle handle, const char* content);
GRCLIB_API int rc_send_raw_packet(RCHandle handle, int packet_id, const char* data, int length);
GRCLIB_API int rc_send_admin_message(RCHandle handle, int player_id, const char* message);
GRCLIB_API int rc_send_admin_message_all(RCHandle handle, const char* message);
GRCLIB_API int rc_reset_player(RCHandle handle, const char* account);
GRCLIB_API char* rc_gtokenize(const char* text);
GRCLIB_API char* rc_gtokenize_reverse(const char* content);
GRCLIB_API char* rc_get_rights_names();
GRCLIB_API char* rc_get_color_names();
GRCLIB_API char* rc_get_packet_names(int nc, int direction);
GRCLIB_API char* rc_get_1plus_text_net_string(const char* text);
GRCLIB_API int rc_read_gbyte(const char* data, int length, int offset, int* value_out, int* offset_out);
GRCLIB_API int rc_read_gshort(const char* data, int length, int offset, int* value_out, int* offset_out);
GRCLIB_API int rc_read_gint5(const char* data, int length, int offset, int* value_out, int* offset_out);
GRCLIB_API char* rc_read_length_string(const char* data, int length, int offset, int* offset_out);
GRCLIB_API char* rc_read_comma_text(const char* data, int length, int offset, int read_length);
GRCLIB_API int rc_filebrowser_start(RCHandle handle);
GRCLIB_API int rc_filebrowser_cd(RCHandle handle, const char* folder_path);
GRCLIB_API int rc_filebrowser_download(RCHandle handle, const char* file_path);
GRCLIB_API int rc_filebrowser_delete(RCHandle handle, const char* file_path);
GRCLIB_API int rc_filebrowser_rename(RCHandle handle, const char* old_path, const char* new_path);
GRCLIB_API int rc_filebrowser_move(RCHandle handle, const char* destination_folder, const char* file_path);
GRCLIB_API int rc_request_pm_server_players(RCHandle handle, const char* server_name);
GRCLIB_API const char* rc_last_error(RCHandle handle);
GRCLIB_API void rc_free(void* ptr);
GRCLIB_API void rc_disconnect(RCHandle handle);
GRCLIB_API void rc_process_events(RCHandle handle);
#ifdef __cplusplus
}
#endif
#endif
