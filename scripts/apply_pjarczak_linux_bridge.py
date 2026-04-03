#!/usr/bin/env python3
from pathlib import Path
import sys

def read(p: Path) -> str:
    return p.read_text(encoding='utf-8')

def write(p: Path, s: str) -> None:
    p.write_text(s, encoding='utf-8')

def replace_once(s: str, old: str, new: str, path: Path) -> str:
    if old not in s:
        raise RuntimeError(f"missing expected snippet in {path}: {old[:80]!r}")
    return s.replace(old, new, 1)

def replace_function_body(s: str, signature: str, new_body: str, path: Path) -> str:
    idx = s.find(signature)
    if idx < 0:
        raise RuntimeError(f"signature not found in {path}: {signature}")
    brace = s.find('{', idx)
    if brace < 0:
        raise RuntimeError(f"opening brace not found in {path}: {signature}")
    depth = 0
    end = brace
    while end < len(s):
        ch = s[end]
        if ch == '{':
            depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0:
                end += 1
                break
        end += 1
    if depth != 0:
        raise RuntimeError(f"unterminated function body in {path}: {signature}")
    return s[:brace] + new_body + s[end:]

NETWORK_AGENT_INIT = r'''{
    std::string library;
    std::string data_dir_str = data_dir();
    boost::filesystem::path data_dir_path(data_dir_str);
    auto plugin_folder = data_dir_path / "plugins";

    if (using_backup)
        plugin_folder = plugin_folder / "backup";

    const bool pj_bridge = Slic3r::PJarczakLinuxBridge::enabled();
    if (pj_bridge)
        validate_cert = false;

    std::optional<SignerSummary> self_cert_summary, module_cert_summary;
    if (validate_cert)
        self_cert_summary = SummarizeSelf();

#if defined(_MSC_VER) || defined(_WIN32)
    if (pj_bridge) {
        library = Slic3r::PJarczakLinuxBridge::bridge_network_library_path(plugin_folder);
        wchar_t lib_wstr[512];
        memset(lib_wstr, 0, sizeof(lib_wstr));
        ::MultiByteToWideChar(CP_UTF8, 0, library.c_str(), int(library.size()) + 1, lib_wstr, int(sizeof(lib_wstr) / sizeof(lib_wstr[0])));
        networking_module = LoadLibrary(lib_wstr);
    } else {
        library = plugin_folder.string() + "\\" + std::string(BAMBU_NETWORK_LIBRARY) + ".dll";
        wchar_t lib_wstr[128];
        memset(lib_wstr, 0, sizeof(lib_wstr));
        ::MultiByteToWideChar(CP_UTF8, NULL, library.c_str(), strlen(library.c_str()) + 1, lib_wstr, sizeof(lib_wstr) / sizeof(lib_wstr[0]));
        if (self_cert_summary) {
            module_cert_summary = SummarizeModule(library);
            if (module_cert_summary && IsSamePublisher(*self_cert_summary, *module_cert_summary))
                networking_module = LoadLibrary(lib_wstr);
        } else {
            networking_module = LoadLibrary(lib_wstr);
        }
    }
#else
    if (pj_bridge) {
        library = Slic3r::PJarczakLinuxBridge::bridge_network_library_path(plugin_folder);
        networking_module = dlopen(library.c_str(), RTLD_LAZY);
    } else {
    #if defined(__WXMAC__)
        library = plugin_folder.string() + "/" + std::string("lib") + std::string(BAMBU_NETWORK_LIBRARY) + ".dylib";
    #else
        library = plugin_folder.string() + "/" + std::string("lib") + std::string(BAMBU_NETWORK_LIBRARY) + ".so";
    #endif
        if (self_cert_summary) {
            module_cert_summary = SummarizeModule(library);
            if (module_cert_summary && IsSamePublisher(*self_cert_summary, *module_cert_summary))
                networking_module = dlopen(library.c_str(), RTLD_LAZY);
        } else {
            networking_module = dlopen(library.c_str(), RTLD_LAZY);
        }
    }
#endif

    if (!networking_module)
        return -1;

    InitFTModule(networking_module);

    check_debug_consistent_ptr        =  reinterpret_cast<func_check_debug_consistent>(get_network_function("bambu_network_check_debug_consistent"));
    get_version_ptr                   =  reinterpret_cast<func_get_version>(get_network_function("bambu_network_get_version"));
    create_agent_ptr                  =  reinterpret_cast<func_create_agent>(get_network_function("bambu_network_create_agent"));
    destroy_agent_ptr                 =  reinterpret_cast<func_destroy_agent>(get_network_function("bambu_network_destroy_agent"));
    init_log_ptr                      =  reinterpret_cast<func_init_log>(get_network_function("bambu_network_init_log"));
    set_config_dir_ptr                =  reinterpret_cast<func_set_config_dir>(get_network_function("bambu_network_set_config_dir"));
    set_cert_file_ptr                 =  reinterpret_cast<func_set_cert_file>(get_network_function("bambu_network_set_cert_file"));
    set_country_code_ptr              =  reinterpret_cast<func_set_country_code>(get_network_function("bambu_network_set_country_code"));
    start_ptr                         =  reinterpret_cast<func_start>(get_network_function("bambu_network_start"));
    set_on_ssdp_msg_fn_ptr            =  reinterpret_cast<func_set_on_ssdp_msg_fn>(get_network_function("bambu_network_set_on_ssdp_msg_fn"));
    set_on_user_login_fn_ptr          =  reinterpret_cast<func_set_on_user_login_fn>(get_network_function("bambu_network_set_on_user_login_fn"));
    set_on_printer_connected_fn_ptr   =  reinterpret_cast<func_set_on_printer_connected_fn>(get_network_function("bambu_network_set_on_printer_connected_fn"));
    set_on_server_connected_fn_ptr    =  reinterpret_cast<func_set_on_server_connected_fn>(get_network_function("bambu_network_set_on_server_connected_fn"));
    set_on_http_error_fn_ptr          =  reinterpret_cast<func_set_on_http_error_fn>(get_network_function("bambu_network_set_on_http_error_fn"));
    set_get_country_code_fn_ptr       =  reinterpret_cast<func_set_get_country_code_fn>(get_network_function("bambu_network_set_get_country_code_fn"));
    set_on_subscribe_failure_fn_ptr   =  reinterpret_cast<func_set_on_subscribe_failure_fn>(get_network_function("bambu_network_set_on_subscribe_failure_fn"));
    set_on_message_fn_ptr             =  reinterpret_cast<func_set_on_message_fn>(get_network_function("bambu_network_set_on_message_fn"));
    set_on_user_message_fn_ptr        =  reinterpret_cast<func_set_on_user_message_fn>(get_network_function("bambu_network_set_on_user_message_fn"));
    set_on_local_connect_fn_ptr       =  reinterpret_cast<func_set_on_local_connect_fn>(get_network_function("bambu_network_set_on_local_connect_fn"));
    set_on_local_message_fn_ptr       =  reinterpret_cast<func_set_on_local_message_fn>(get_network_function("bambu_network_set_on_local_message_fn"));
    set_queue_on_main_fn_ptr          = reinterpret_cast<func_set_queue_on_main_fn>(get_network_function("bambu_network_set_queue_on_main_fn"));
    connect_server_ptr                =  reinterpret_cast<func_connect_server>(get_network_function("bambu_network_connect_server"));
    is_server_connected_ptr           =  reinterpret_cast<func_is_server_connected>(get_network_function("bambu_network_is_server_connected"));
    refresh_connection_ptr            =  reinterpret_cast<func_refresh_connection>(get_network_function("bambu_network_refresh_connection"));
    start_subscribe_ptr               =  reinterpret_cast<func_start_subscribe>(get_network_function("bambu_network_start_subscribe"));
    stop_subscribe_ptr                =  reinterpret_cast<func_stop_subscribe>(get_network_function("bambu_network_stop_subscribe"));
    add_subscribe_ptr                 =  reinterpret_cast<func_add_subscribe>(get_network_function("bambu_network_add_subscribe"));
    del_subscribe_ptr                 =  reinterpret_cast<func_del_subscribe>(get_network_function("bambu_network_del_subscribe"));
    enable_multi_machine_ptr          =  reinterpret_cast<func_enable_multi_machine>(get_network_function("bambu_network_enable_multi_machine"));
    send_message_ptr                  =  reinterpret_cast<func_send_message>(get_network_function("bambu_network_send_message"));
    connect_printer_ptr               =  reinterpret_cast<func_connect_printer>(get_network_function("bambu_network_connect_printer"));
    disconnect_printer_ptr            =  reinterpret_cast<func_disconnect_printer>(get_network_function("bambu_network_disconnect_printer"));
    send_message_to_printer_ptr       =  reinterpret_cast<func_send_message_to_printer>(get_network_function("bambu_network_send_message_to_printer"));
    check_cert_ptr                    =  reinterpret_cast<func_check_cert>(get_network_function("bambu_network_update_cert"));
    install_device_cert_ptr           =  reinterpret_cast<func_install_device_cert>(get_network_function("bambu_network_install_device_cert"));
    start_discovery_ptr               =  reinterpret_cast<func_start_discovery>(get_network_function("bambu_network_start_discovery"));
    change_user_ptr                   =  reinterpret_cast<func_change_user>(get_network_function("bambu_network_change_user"));
    is_user_login_ptr                 =  reinterpret_cast<func_is_user_login>(get_network_function("bambu_network_is_user_login"));
    user_logout_ptr                   =  reinterpret_cast<func_user_logout>(get_network_function("bambu_network_user_logout"));
    get_user_id_ptr                   =  reinterpret_cast<func_get_user_id>(get_network_function("bambu_network_get_user_id"));
    get_user_name_ptr                 =  reinterpret_cast<func_get_user_name>(get_network_function("bambu_network_get_user_name"));
    get_user_avatar_ptr               =  reinterpret_cast<func_get_user_avatar>(get_network_function("bambu_network_get_user_avatar"));
    get_user_nickanme_ptr             =  reinterpret_cast<func_get_user_nickanme>(get_network_function("bambu_network_get_user_nickanme"));
    build_login_cmd_ptr               =  reinterpret_cast<func_build_login_cmd>(get_network_function("bambu_network_build_login_cmd"));
    build_logout_cmd_ptr              =  reinterpret_cast<func_build_logout_cmd>(get_network_function("bambu_network_build_logout_cmd"));
    build_login_info_ptr              =  reinterpret_cast<func_build_login_info>(get_network_function("bambu_network_build_login_info"));
    ping_bind_ptr                     =  reinterpret_cast<func_ping_bind>(get_network_function("bambu_network_ping_bind"));
    bind_detect_ptr                   =  reinterpret_cast<func_bind_detect>(get_network_function("bambu_network_bind_detect"));
    report_consent_ptr                =  reinterpret_cast<func_report_consent>(get_network_function("bambu_network_report_consent"));
    set_server_callback_ptr           =  reinterpret_cast<func_set_server_callback>(get_network_function("bambu_network_set_server_callback"));
    bind_ptr                          =  reinterpret_cast<func_bind>(get_network_function("bambu_network_bind"));
    unbind_ptr                        =  reinterpret_cast<func_unbind>(get_network_function("bambu_network_unbind"));
    get_bambulab_host_ptr             =  reinterpret_cast<func_get_bambulab_host>(get_network_function("bambu_network_get_bambulab_host"));
    get_user_selected_machine_ptr     =  reinterpret_cast<func_get_user_selected_machine>(get_network_function("bambu_network_get_user_selected_machine"));
    set_user_selected_machine_ptr     =  reinterpret_cast<func_set_user_selected_machine>(get_network_function("bambu_network_set_user_selected_machine"));
    start_print_ptr                   =  reinterpret_cast<func_start_print>(get_network_function("bambu_network_start_print"));
    start_local_print_with_record_ptr =  reinterpret_cast<func_start_local_print_with_record>(get_network_function("bambu_network_start_local_print_with_record"));
    start_send_gcode_to_sdcard_ptr    =  reinterpret_cast<func_start_send_gcode_to_sdcard>(get_network_function("bambu_network_start_send_gcode_to_sdcard"));
    start_local_print_ptr             =  reinterpret_cast<func_start_local_print>(get_network_function("bambu_network_start_local_print"));
    start_sdcard_print_ptr            =  reinterpret_cast<func_start_sdcard_print>(get_network_function("bambu_network_start_sdcard_print"));
    get_user_presets_ptr              =  reinterpret_cast<func_get_user_presets>(get_network_function("bambu_network_get_user_presets"));
    request_setting_id_ptr            =  reinterpret_cast<func_request_setting_id>(get_network_function("bambu_network_request_setting_id"));
    put_setting_ptr                   =  reinterpret_cast<func_put_setting>(get_network_function("bambu_network_put_setting"));
    get_setting_list_ptr              = reinterpret_cast<func_get_setting_list>(get_network_function("bambu_network_get_setting_list"));
    get_setting_list2_ptr             = reinterpret_cast<func_get_setting_list2>(get_network_function("bambu_network_get_setting_list2"));
    delete_setting_ptr                =  reinterpret_cast<func_delete_setting>(get_network_function("bambu_network_delete_setting"));
    get_studio_info_url_ptr           =  reinterpret_cast<func_get_studio_info_url>(get_network_function("bambu_network_get_studio_info_url"));
    set_extra_http_header_ptr         =  reinterpret_cast<func_set_extra_http_header>(get_network_function("bambu_network_set_extra_http_header"));
    get_my_message_ptr                =  reinterpret_cast<func_get_my_message>(get_network_function("bambu_network_get_my_message"));
    check_user_task_report_ptr        =  reinterpret_cast<func_check_user_task_report>(get_network_function("bambu_network_check_user_task_report"));
    get_user_print_info_ptr           =  reinterpret_cast<func_get_user_print_info>(get_network_function("bambu_network_get_user_print_info"));
    get_user_tasks_ptr                =  reinterpret_cast<func_get_user_tasks>(get_network_function("bambu_network_get_user_tasks"));
    get_printer_firmware_ptr          =  reinterpret_cast<func_get_printer_firmware>(get_network_function("bambu_network_get_printer_firmware"));
    get_task_plate_index_ptr          =  reinterpret_cast<func_get_task_plate_index>(get_network_function("bambu_network_get_task_plate_index"));
    get_user_info_ptr                 =  reinterpret_cast<func_get_user_info>(get_network_function("bambu_network_get_user_info"));
    request_bind_ticket_ptr           =  reinterpret_cast<func_request_bind_ticket>(get_network_function("bambu_network_request_bind_ticket"));
    get_subtask_info_ptr              =  reinterpret_cast<func_get_subtask_info>(get_network_function("bambu_network_get_subtask_info"));
    get_slice_info_ptr                =  reinterpret_cast<func_get_slice_info>(get_network_function("bambu_network_get_slice_info"));
    query_bind_status_ptr             =  reinterpret_cast<func_query_bind_status>(get_network_function("bambu_network_query_bind_status"));
    modify_printer_name_ptr           =  reinterpret_cast<func_modify_printer_name>(get_network_function("bambu_network_modify_printer_name"));
    get_camera_url_ptr                =  reinterpret_cast<func_get_camera_url>(get_network_function("bambu_network_get_camera_url"));
    get_camera_url_for_golive_ptr     =  reinterpret_cast<func_get_camera_url_for_golive>(get_network_function("bambu_network_get_camera_url_for_golive"));
    get_design_staffpick_ptr          =  reinterpret_cast<func_get_design_staffpick>(get_network_function("bambu_network_get_design_staffpick"));
    start_publish_ptr                 =  reinterpret_cast<func_start_pubilsh>(get_network_function("bambu_network_start_publish"));
    get_model_publish_url_ptr         =  reinterpret_cast<func_get_model_publish_url>(get_network_function("bambu_network_get_model_publish_url"));
    get_subtask_ptr                   =  reinterpret_cast<func_get_subtask>(get_network_function("bambu_network_get_subtask"));
    get_model_mall_home_url_ptr       =  reinterpret_cast<func_get_model_mall_home_url>(get_network_function("bambu_network_get_model_mall_home_url"));
    get_model_mall_detail_url_ptr     =  reinterpret_cast<func_get_model_mall_detail_url>(get_network_function("bambu_network_get_model_mall_detail_url"));
    get_my_profile_ptr                =  reinterpret_cast<func_get_my_profile>(get_network_function("bambu_network_get_my_profile"));
    get_my_token_ptr                  =  reinterpret_cast<func_get_my_profile>(get_network_function("bambu_network_get_my_token"));
    track_enable_ptr                  =  reinterpret_cast<func_track_enable>(get_network_function("bambu_network_track_enable"));
    track_remove_files_ptr            =  reinterpret_cast<func_track_remove_files>(get_network_function("bambu_network_track_remove_files"));
    track_event_ptr                   =  reinterpret_cast<func_track_event>(get_network_function("bambu_network_track_event"));
    track_header_ptr                  =  reinterpret_cast<func_track_header>(get_network_function("bambu_network_track_header"));
    track_update_property_ptr         =  reinterpret_cast<func_track_update_property>(get_network_function("bambu_network_track_update_property"));
    track_get_property_ptr            =  reinterpret_cast<func_track_get_property>(get_network_function("bambu_network_track_get_property"));
    put_model_mall_rating_url_ptr     =  reinterpret_cast<func_put_model_mall_rating_url>(get_network_function("bambu_network_put_model_mall_rating"));
    get_oss_config_ptr                =  reinterpret_cast<func_get_oss_config>(get_network_function("bambu_network_get_oss_config"));
    put_rating_picture_oss_ptr        =  reinterpret_cast<func_put_rating_picture_oss>(get_network_function("bambu_network_put_rating_picture_oss"));
    get_model_mall_rating_result_ptr  =  reinterpret_cast<func_get_model_mall_rating_result>(get_network_function("bambu_network_get_model_mall_rating"));
    get_mw_user_preference_ptr        =  reinterpret_cast<func_get_mw_user_preference>(get_network_function("bambu_network_get_mw_user_preference"));
    get_mw_user_4ulist_ptr            =  reinterpret_cast<func_get_mw_user_4ulist>(get_network_function("bambu_network_get_mw_user_4ulist"));
    get_hms_snapshot_ptr              =  reinterpret_cast<func_get_hms_snapshot>(get_network_function("bambu_network_get_hms_snapshot"));
    return 0;
}'''

NETWORK_AGENT_SOURCE = r'''{
    if (source_module || !networking_module)
        return source_module;

    if (Slic3r::PJarczakLinuxBridge::enabled() && Slic3r::PJarczakLinuxBridge::source_module_is_network_module()) {
        source_module = networking_module;
        return source_module;
    }

    std::string library;
    std::string data_dir_str = data_dir();
    boost::filesystem::path data_dir_path(data_dir_str);
    auto plugin_folder = data_dir_path / "plugins";
#if defined(_MSC_VER) || defined(_WIN32)
    wchar_t lib_wstr[128];
    library = plugin_folder.string() + "/" + std::string(BAMBU_SOURCE_LIBRARY) + ".dll";
    memset(lib_wstr, 0, sizeof(lib_wstr));
    ::MultiByteToWideChar(CP_UTF8, NULL, library.c_str(), strlen(library.c_str())+1, lib_wstr, sizeof(lib_wstr) / sizeof(lib_wstr[0]));
    source_module = LoadLibrary(lib_wstr);
#else
#if defined(__WXMAC__)
    library = plugin_folder.string() + "/" + std::string("lib") + std::string(BAMBU_SOURCE_LIBRARY) + ".dylib";
#else
    library = plugin_folder.string() + "/" + std::string("lib") + std::string(BAMBU_SOURCE_LIBRARY) + ".so";
#endif
    source_module = dlopen(library.c_str(), RTLD_LAZY);
#endif
    return source_module;
}'''

GUI_DOWNLOAD = r'''{
    int result = 0;
    json j;
    std::string err_msg;

    AppConfig* app_config = wxGetApp().app_config;
    if (!app_config)
        return -1;

    m_networking_cancel_update = false;

    fs::path target_file_path = (fs::temp_directory_path() / package_name);
    fs::path tmp_path = target_file_path;
    tmp_path += format(".%1%%2%", get_current_pid(), ".tmp");

    const bool pj_force_linux_payload = Slic3r::PJarczakLinuxBridge::should_force_linux_plugin_payload(name);
    std::map<std::string, std::string> saved_headers = Slic3r::Http::get_extra_headers();
    bool changed_headers = false;

    auto restore_headers = [&]() {
        if (changed_headers) {
            Slic3r::Http::set_extra_headers(saved_headers);
            changed_headers = false;
        }
    };

    if (pj_force_linux_payload) {
        auto headers = saved_headers;
        headers["X-BBL-OS-Type"] = Slic3r::PJarczakLinuxBridge::forced_download_os_type();
        Slic3r::Http::set_extra_headers(headers);
        changed_headers = true;
    }

    std::string  url = get_plugin_url(name, app_config->get_country_code());
    std::string download_url;
    Slic3r::Http http_url = Slic3r::Http::get(url);
    http_url.timeout_connect(TIMEOUT_CONNECT)
        .timeout_max(TIMEOUT_RESPONSE)
        .on_complete(
        [&download_url](std::string body, unsigned status) {
            try {
                json j = json::parse(body);
                if (j["message"].get<std::string>() == "success") {
                    json resource = j.at("resources");
                    if (resource.is_array()) {
                        for (auto iter = resource.begin(); iter != resource.end(); iter++) {
                            for (auto sub_iter = iter.value().begin(); sub_iter != iter.value().end(); sub_iter++) {
                                if (boost::iequals(sub_iter.key(), "url"))
                                    download_url = sub_iter.value();
                            }
                        }
                    }
                }
            } catch (...) {}
        }).on_error(
            [&result, &err_msg](std::string body, std::string error, unsigned int status) {
                err_msg += "[download_plugin 1] on_error: " + error + ", body = " + body;
                result = -1;
        }).perform_sync();

    restore_headers();

    bool cancel = false;
    if (result < 0) {
        if (pro_fn) pro_fn(InstallStatusDownloadFailed, 0, cancel);
        return result;
    }

    if (download_url.empty()) {
        if (pro_fn) pro_fn(InstallStatusDownloadFailed, 0, cancel);
        return -1;
    } else if (pro_fn) {
        pro_fn(InstallStatusNormal, 5, cancel);
    }

    if (m_networking_cancel_update || cancel)
        return -1;

    Slic3r::Http http = Slic3r::Http::get(download_url);
    int reported_percent = 0;
    http.on_progress(
        [this, &pro_fn, cancel_fn, &result, &reported_percent, &err_msg](Slic3r::Http::Progress progress, bool& cancel) {
            int percent = 0;
            if (progress.dltotal != 0)
                percent = progress.dlnow * 50 / progress.dltotal;
            bool was_cancel = false;
            if (pro_fn && ((percent - reported_percent) >= 10)) {
                pro_fn(InstallStatusNormal, percent, was_cancel);
                reported_percent = percent;
            }
            cancel = m_networking_cancel_update || was_cancel;
            if (cancel_fn && cancel_fn())
                cancel = true;

            if (cancel) {
                err_msg += "[download_plugin] cancel";
                result = -1;
            }
        })
        .on_complete([&pro_fn, tmp_path, target_file_path](std::string body, unsigned status) {
            bool cancel = false;
            fs::fstream file(tmp_path, std::ios::out | std::ios::binary | std::ios::trunc);
            file.write(body.c_str(), body.size());
            file.close();
            fs::rename(tmp_path, target_file_path);
            if (pro_fn) pro_fn(InstallStatusDownloadCompleted, 80, cancel);
            })
        .on_error([&pro_fn, &result, &err_msg](std::string body, std::string error, unsigned int status) {
            bool cancel = false;
            if (pro_fn) pro_fn(InstallStatusDownloadFailed, 0, cancel);
            err_msg += "[download_plugin 2] on_error: " + error + ", body = " + body;
            result = -1;
        });
    http.perform_sync();

    return result;
}'''

GUI_COPY = r'''{
    if (app_config->get("update_network_plugin") != "true")
        return;

    std::string data_dir_str = data_dir();
    fs::path data_dir_path(data_dir_str);
    auto plugin_folder = data_dir_path / "plugins";
    auto cache_folder = data_dir_path / "ota" / "plugins";

    if (!boost::filesystem::exists(plugin_folder))
        boost::filesystem::create_directory(plugin_folder);

    if (!boost::filesystem::exists(cache_folder)) {
        app_config->set("update_network_plugin", "false");
        return;
    }

    try {
        std::string error_message;
        for (auto& dir_entry : boost::filesystem::directory_iterator(cache_folder))
        {
            const auto& path = dir_entry.path();
            std::string file_name = path.filename().string();
            std::string file_path = path.string();

            if (Slic3r::PJarczakLinuxBridge::enabled()) {
                if (file_name == Slic3r::PJarczakLinuxBridge::linux_payload_manifest_file_name()) {
                    std::string dest_path = (plugin_folder / file_name).string();
                    CopyFileResult cfr = copy_file(file_path, dest_path, error_message, false);
                    if (cfr != CopyFileResult::SUCCESS)
                        return;
                    continue;
                }
                if (!Slic3r::PJarczakLinuxBridge::is_linux_payload_filename(file_name))
                    continue;
                std::string validate_reason;
                if (!Slic3r::PJarczakLinuxBridge::validate_linux_payload_file(file_path, &validate_reason))
                    continue;
            }

            std::string dest_path = (plugin_folder / file_name).string();
            CopyFileResult cfr = copy_file(file_path, dest_path, error_message, false);
            if (cfr != CopyFileResult::SUCCESS)
                return;

            static constexpr const auto perms = fs::owner_read | fs::owner_write | fs::group_read | fs::others_read;
            fs::permissions(dest_path, perms);
        }

        if (boost::filesystem::exists(cache_folder))
            fs::remove_all(cache_folder);
    } catch (...) {}

    app_config->set("update_network_plugin", "false");
}'''

def patch_cmake(repo: Path):
    path = repo / "src/slic3r/CMakeLists.txt"
    s = read(path)
    s = replace_once(
        s,
        '    Utils/CertificateVerify.hpp\n    Utils/CertificateVerify.cpp\n)\n',
        '    Utils/CertificateVerify.hpp\n    Utils/CertificateVerify.cpp\n    Utils/PJarczakLinuxBridge/PJarczakLinuxBridgeConfig.hpp\n    Utils/PJarczakLinuxBridge/PJarczakLinuxBridgeConfig.cpp\n)\n',
        path
    )
    s = replace_once(
        s,
        'add_subdirectory(GUI/DeviceCore)\nadd_subdirectory(GUI/DeviceTab)\n',
        'add_subdirectory(GUI/DeviceCore)\nadd_subdirectory(GUI/DeviceTab)\nadd_subdirectory(Utils/PJarczakLinuxBridge)\n',
        path
    )
    write(path, s)

def patch_network_agent(repo: Path):
    path = repo / "src/slic3r/Utils/NetworkAgent.cpp"
    s = read(path)
    s = replace_once(
        s,
        '#include "slic3r/Utils/CertificateVerify.hpp"\n',
        '#include "slic3r/Utils/CertificateVerify.hpp"\n#include "PJarczakLinuxBridge/PJarczakLinuxBridgeConfig.hpp"\n',
        path
    )
    s = replace_function_body(s, 'int NetworkAgent::initialize_network_module(bool using_backup, bool validate_cert)\n', NETWORK_AGENT_INIT, path)
    s = replace_function_body(s, '#if defined(_MSC_VER) || defined(_WIN32)\nHMODULE NetworkAgent::get_bambu_source_entry()\n#else\nvoid* NetworkAgent::get_bambu_source_entry()\n#endif\n', NETWORK_AGENT_SOURCE, path)
    write(path, s)

def patch_gui_app(repo: Path):
    path = repo / "src/slic3r/GUI/GUI_App.cpp"
    s = read(path)
    s = replace_once(
        s,
        '#include "BBLUtil.hpp"\n',
        '#include "BBLUtil.hpp"\n#include "../Utils/PJarczakLinuxBridge/PJarczakLinuxBridgeConfig.hpp"\n',
        path
    )
    s = replace_function_body(s, 'int GUI_App::download_plugin(std::string name, std::string package_name, InstallProgressFn pro_fn, WasCancelledFn cancel_fn)\n', GUI_DOWNLOAD, path)
    s = replace_function_body(s, 'void GUI_App::copy_network_if_available()\n', GUI_COPY, path)
    write(path, s)

def main():
    if len(sys.argv) != 2:
        print("usage: apply_pjarczak_linux_bridge.py /path/to/BambuStudio")
        raise SystemExit(2)

    repo = Path(sys.argv[1]).resolve()
    patch_cmake(repo)
    patch_network_agent(repo)
    patch_gui_app(repo)
    print("patched:", repo)

if __name__ == "__main__":
    main()
