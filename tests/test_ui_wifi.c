/* tests/test_ui_wifi.c: pure parsers for the Wi-Fi reconnect (platform/wifi.c).
 * All fixtures use fake SSIDs ("TestNet") and fake MACs; real ones never enter git. */
#include "test.h"
#include "platform/wifi.h"

/* `iw dev wlan0 link`: only the first line matters, and only connected/not. */
static void test_iw_link(void) {
  CHECK(pp_wifi_iw_link_connected("Connected to aa:bb:cc:dd:ee:ff (on wlan0)\n"
                                  "SSID: TestNet\nfreq: 2412\n") == 1);
  CHECK(pp_wifi_iw_link_connected("Connected to aa:bb:cc:dd:ee:ff (on wlan0)") == 1);
  CHECK(pp_wifi_iw_link_connected("Not connected.\n") == 0);
  CHECK(pp_wifi_iw_link_connected("Not connected.") == 0);
  CHECK(pp_wifi_iw_link_connected("") == -1);
  CHECK(pp_wifi_iw_link_connected(NULL) == -1);
  CHECK(pp_wifi_iw_link_connected("command failed: No such device (-19)\n") == -1);
  /* junk after the first line must not flip the verdict */
  CHECK(pp_wifi_iw_link_connected("Not connected.\nConnected to aa:bb:cc:dd:ee:ff\n") == 0);
}

/* `connmanctl services`: favorite = a flag column containing '*'; the service id is
 * the last token starting with wifi_ and must be strictly hex-shaped. */
static void test_favorite_service(void) {
  char out[128];
  const char *list =
      "*AO TestNet              wifi_aabbccddeeff_546573744e6574_managed_psk\n"
      "    OtherNet             wifi_aabbccddeeff_4f746865724e6574_managed_psk\n"
      "    Cafe Wifi            wifi_001122334455_436166652057696669_managed_psk\n";
  CHECK(pp_wifi_favorite_service(list, out, sizeof out) == 0);
  CHECK_STR(out, "wifi_aabbccddeeff_546573744e6574_managed_psk");

  /* favorite not first: autoconnect+online but no '*' is not enough */
  const char *fav_second =
      " AO TestNet              wifi_aabbccddeeff_546573744e6574_managed_psk\n"
      "*A  HomeNet              wifi_001122334455_486f6d654e6574_managed_psk\n";
  CHECK(pp_wifi_favorite_service(fav_second, out, sizeof out) == 0);
  CHECK_STR(out, "wifi_001122334455_486f6d654e6574_managed_psk");

  /* no favorite at all */
  const char *none =
      "    TestNet              wifi_aabbccddeeff_546573744e6574_managed_psk\n"
      " AO OtherNet             wifi_aabbccddeeff_4f746865724e6574_managed_psk\n";
  CHECK(pp_wifi_favorite_service(none, out, sizeof out) == -1);

  /* a favorite ethernet service is ignored; the wifi favorite still wins */
  const char *wired_fav =
      "*AO Wired                ethernet_aabbccddeeff_cable\n"
      "*A  TestNet              wifi_aabbccddeeff_546573744e6574_managed_psk\n";
  CHECK(pp_wifi_favorite_service(wired_fav, out, sizeof out) == 0);
  CHECK_STR(out, "wifi_aabbccddeeff_546573744e6574_managed_psk");

  /* open and wpa3 suffixes are fine */
  CHECK(pp_wifi_favorite_service("*AO OpenNet  wifi_aabbccddeeff_4f70656e4e6574_managed_none\n",
                                 out, sizeof out) == 0);
  CHECK_STR(out, "wifi_aabbccddeeff_4f70656e4e6574_managed_none");
  CHECK(pp_wifi_favorite_service("*AO SAE  wifi_aabbccddeeff_534145_managed_sae\n",
                                 out, sizeof out) == 0);

  /* rejections: never hand a bad token to execv */
  CHECK(pp_wifi_favorite_service("*AO UPPER  wifi_AABBCCDDEEFF_546573744e6574_managed_psk\n",
                                 out, sizeof out) == -1); /* uppercase hex */
  CHECK(pp_wifi_favorite_service("*AO Bad  wifi_aabbccddeeff_546573744e6574_psk\n",
                                 out, sizeof out) == -1); /* missing _managed_ */
  CHECK(pp_wifi_favorite_service("*AO Inject  wifi_aabbccddeeff_546573744e6574_managed_psk;rm\n",
                                 out, sizeof out) == -1); /* shell metachar */
  CHECK(pp_wifi_favorite_service("*AO Short  wifi__546573744e6574_managed_psk\n",
                                 out, sizeof out) == -1); /* empty mac */
  CHECK(pp_wifi_favorite_service("*AO Trail  wifi_aabbccddeeff_546573744e6574_managed_\n",
                                 out, sizeof out) == -1); /* empty security */
  CHECK(pp_wifi_favorite_service("*AO Hidden  wifi_aabbccddeeff_hidden_managed_psk\n",
                                 out, sizeof out) == -1); /* non-hex ssid part */
  CHECK(pp_wifi_favorite_service("", out, sizeof out) == -1);
  CHECK(pp_wifi_favorite_service(NULL, out, sizeof out) == -1);

  /* tiny buffer: fail instead of truncating into a different service id */
  char small[16];
  CHECK(pp_wifi_favorite_service(list, small, sizeof small) == -1);
}

/* `rfkill list`: soft/hard flags of the Wireless LAN block only. */
static void test_rfkill(void) {
  int soft = -9, hard = -9;
  const char *both_no =
      "0: phy0: Wireless LAN\n"
      "\tSoft blocked: no\n"
      "\tHard blocked: no\n"
      "1: hci0: Bluetooth\n"
      "\tSoft blocked: yes\n"
      "\tHard blocked: no\n";
  CHECK(pp_wifi_rfkill_blocked(both_no, &soft, &hard) == 0);
  CHECK(soft == 0 && hard == 0);

  const char *soft_yes =
      "0: phy0: Wireless LAN\n"
      "\tSoft blocked: yes\n"
      "\tHard blocked: no\n";
  CHECK(pp_wifi_rfkill_blocked(soft_yes, &soft, &hard) == 0);
  CHECK(soft == 1 && hard == 0);

  const char *hard_yes =
      "0: phy0: Wireless LAN\n"
      "\tSoft blocked: no\n"
      "\tHard blocked: yes\n";
  CHECK(pp_wifi_rfkill_blocked(hard_yes, &soft, &hard) == 0);
  CHECK(soft == 0 && hard == 1);

  /* bluetooth's flags must not leak into the wifi result */
  const char *bt_blocked =
      "0: hci0: Bluetooth\n"
      "\tSoft blocked: yes\n"
      "\tHard blocked: yes\n"
      "1: phy0: Wireless LAN\n"
      "\tSoft blocked: no\n"
      "\tHard blocked: no\n";
  CHECK(pp_wifi_rfkill_blocked(bt_blocked, &soft, &hard) == 0);
  CHECK(soft == 0 && hard == 0);

  CHECK(pp_wifi_rfkill_blocked("0: hci0: Bluetooth\n\tSoft blocked: no\n\tHard blocked: no\n",
                               &soft, &hard) == -1);
  CHECK(pp_wifi_rfkill_blocked("", &soft, &hard) == -1);
  CHECK(pp_wifi_rfkill_blocked(NULL, &soft, &hard) == -1);
}

/* `connmanctl technologies`: Powered/Connected of the wifi block. */
static void test_connman_state(void) {
  int powered = -9, connected = -9;
  const char *t =
      "/net/connman/technology/ethernet\n"
      "  Name = Wired\n"
      "  Type = ethernet\n"
      "  Powered = True\n"
      "  Connected = False\n"
      "/net/connman/technology/wifi\n"
      "  Name = Wi-Fi\n"
      "  Type = wifi\n"
      "  Powered = True\n"
      "  Connected = True\n"
      "  Tethering = False\n";
  CHECK(pp_wifi_connman_state(t, &powered, &connected) == 0);
  CHECK(powered == 1 && connected == 1);

  const char *down =
      "/net/connman/technology/wifi\n"
      "  Name = Wi-Fi\n"
      "  Type = wifi\n"
      "  Powered = True\n"
      "  Connected = False\n";
  CHECK(pp_wifi_connman_state(down, &powered, &connected) == 0);
  CHECK(powered == 1 && connected == 0);

  const char *off =
      "/net/connman/technology/wifi\n"
      "  Powered = False\n"
      "  Connected = False\n";
  CHECK(pp_wifi_connman_state(off, &powered, &connected) == 0);
  CHECK(powered == 0 && connected == 0);

  /* no wifi block */
  CHECK(pp_wifi_connman_state("/net/connman/technology/ethernet\n"
                              "  Powered = True\n  Connected = True\n",
                              &powered, &connected) == -1);
  CHECK(pp_wifi_connman_state("", &powered, &connected) == -1);
  CHECK(pp_wifi_connman_state(NULL, &powered, &connected) == -1);
}

int main(void) {
  RUN(test_iw_link);
  RUN(test_favorite_service);
  RUN(test_rfkill);
  RUN(test_connman_state);
  return TEST_RESULT();
}
