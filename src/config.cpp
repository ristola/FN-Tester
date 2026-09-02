#include "config.h"

#include <Preferences.h>

namespace
{
    const char *kNamespace = "fntester";
}

bool AppConfig::findSavedPassword(const String &ssid, String &passwordOut) const
{
    for (int i = 0; i < saved_network_count; i++)
    {
        if (saved_ssids[i] == ssid)
        {
            passwordOut = saved_passwords[i];
            return true;
        }
    }
    return false;
}

void AppConfig::rememberNetwork(const String &ssid, const String &password)
{
    for (int i = 0; i < saved_network_count; i++)
    {
        if (saved_ssids[i] == ssid)
        {
            saved_passwords[i] = password;
            return;
        }
    }

    if (saved_network_count < kMaxSavedNetworks)
    {
        saved_ssids[saved_network_count] = ssid;
        saved_passwords[saved_network_count] = password;
        saved_network_count++;
        return;
    }

    // Table full and this is a new SSID: evict the oldest (index 0).
    for (int i = 1; i < kMaxSavedNetworks; i++)
    {
        saved_ssids[i - 1] = saved_ssids[i];
        saved_passwords[i - 1] = saved_passwords[i];
    }
    saved_ssids[kMaxSavedNetworks - 1] = ssid;
    saved_passwords[kMaxSavedNetworks - 1] = password;
}

void config_load(AppConfig &out)
{
    Preferences prefs;
    prefs.begin(kNamespace, /*readOnly=*/true);
    out.wifi_ssid = prefs.getString("wifi_ssid", "");
    out.wifi_password = prefs.getString("wifi_pass", "");
    out.wifi_enabled = prefs.getBool("wifi_on", true);
    out.screensaver_enabled = prefs.getBool("ss_on", true);
    out.screensaver_timeout_sec = prefs.getInt("ss_sec", 60);
    out.espnow_friendly_name = prefs.getString("esn_name", "FN Tester - 01");
    out.fn_output_model = static_cast<uint8_t>(prefs.getUChar("fn_model", 1));

    out.saved_network_count = prefs.getInt("net_count", 0);
    if (out.saved_network_count < 0)
        out.saved_network_count = 0;
    if (out.saved_network_count > AppConfig::kMaxSavedNetworks)
        out.saved_network_count = AppConfig::kMaxSavedNetworks;
    for (int i = 0; i < out.saved_network_count; i++)
    {
        out.saved_ssids[i] = prefs.getString(("net" + String(i) + "s").c_str(), "");
        out.saved_passwords[i] = prefs.getString(("net" + String(i) + "p").c_str(), "");
    }
    prefs.end();
}

void config_save(const AppConfig &in)
{
    Preferences prefs;
    prefs.begin(kNamespace, /*readOnly=*/false);
    prefs.putString("wifi_ssid", in.wifi_ssid);
    prefs.putString("wifi_pass", in.wifi_password);
    prefs.putBool("wifi_on", in.wifi_enabled);
    prefs.putBool("ss_on", in.screensaver_enabled);
    prefs.putInt("ss_sec", in.screensaver_timeout_sec);
    prefs.putString("esn_name", in.espnow_friendly_name);
    prefs.putUChar("fn_model", in.fn_output_model);

    prefs.putInt("net_count", in.saved_network_count);
    for (int i = 0; i < in.saved_network_count; i++)
    {
        prefs.putString(("net" + String(i) + "s").c_str(), in.saved_ssids[i]);
        prefs.putString(("net" + String(i) + "p").c_str(), in.saved_passwords[i]);
    }
    prefs.end();
}

void config_clear()
{
    Preferences prefs;
    prefs.begin(kNamespace, /*readOnly=*/false);
    prefs.clear();
    prefs.end();
}
