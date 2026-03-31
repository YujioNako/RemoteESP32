#include <Wire.h>
#include <U8g2lib.h>
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include <IRrecv.h>
#include <IRutils.h>
#include <Preferences.h>
#include <IRac.h>

#define PIN_OLED_SDA 21
#define PIN_OLED_SCL 22
#define PIN_ENC_CLK 32
#define PIN_ENC_DT 33
#define PIN_ENC_SW 25
#define PIN_IR_TX 26
#define PIN_IR_RX 27

U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, PIN_OLED_SCL, PIN_OLED_SDA);
IRsend irsend(PIN_IR_TX);
IRrecv irrecv(PIN_IR_RX, 1024, 15, true);
decode_results results;
Preferences prefs;
IRac ac(PIN_IR_TX);

// ================= 状态机与菜单引擎 =================
enum AppState { BOOT, MAIN_MENU, TV_MENU, TV_BRAND, AC_MENU, LEARN_GRP, LEARN_ACT, LEARN_WAIT, CUSTOM_GRP, CUSTOM_ACT, SETTINGS_MENU, GRP_MANAGE, RENAME_SEL, RENAME_EDIT, GRP_INSERT_SEL, GRP_DELETE_SEL, SIGNAL_ANALYSIS };
AppState currentState = BOOT;

int cursorIndex = 0;
int scrollOffset = 0;
int currentMenuSize = 0;
bool needsRedraw = true;
bool isEditing = false;
int sysBrightness = 128;

// ================= 📡 分析仪专属变量 =================
String analyzedProtocol = "等待信号...";
String analyzedValue = "---";
String analyzedDesc = "---";
int scrollX_val = 0;   
int scrollX_desc = 0;  
unsigned long lastAnimTime = 0;

// ================= 📺 电视字典 =================
int currentTVBrand = 0;
const int TV_BRAND_COUNT = 13;
const int TV_ACTION_COUNT = 15;
const char* tvMenuItems[] = { "[切换品牌]", "电源 (Power)", "音量 +", "音量 -", "频道 +", "频道 -", "静音 (Mute)", "上 (Up)", "下 (Down)", "左 (Left)", "右 (Right)", "确认 (OK)", "返回 (Back)", "主页 (Home)", "菜单 (Menu)", "信号源 (Source)" };
struct TVDict {
  const char* brandName;
  decode_type_t protocol;
  uint16_t bits;
  uint64_t codes[TV_ACTION_COUNT];
};
const TVDict tvDatabase[TV_BRAND_COUNT] = {
  { "Sony (索尼)", SONY, 12, { 0xA90, 0x490, 0xC90, 0x090, 0x890, 0x290, 0x2F0, 0xAF0, 0x2D0, 0xCD0, 0xA70, 0x670, 0x070, 0xE70, 0xA50 } },
  { "Samsung (三星)", SAMSUNG, 32, { 0xE0E040BF, 0xE0E0E01F, 0xE0E0D02F, 0xE0E048B7, 0xE0E008F7, 0xE0E0F00F, 0xE0E006F9, 0xE0E08679, 0xE0E0A659, 0xE0E046B9, 0xE0E016E9, 0xE0E01AE5, 0x00, 0xE0E058A7, 0xE0E0807F } },
  { "LG", NEC, 32, { 0x20DF10EF, 0x20DF40BF, 0x20DFC03F, 0x20DF00FF, 0x20DF807F, 0x20DF906F, 0x20DF02FD, 0x20DF827D, 0x20DFE01F, 0x20DF609F, 0x20DF22DD, 0x20DF14EB, 0x20DF08F7, 0x20DFC23D, 0x20DFD02F } },
  { "Panasonic (松下)", PANASONIC, 48, { 0x40040100BCBD, 0x400401000405, 0x400401008485, 0x400401002C2D, 0x40040100ACAD, 0x400401004C4D, 0x400401005253, 0x40040100D2D3, 0x400401007273, 0x40040100F2F3, 0x400401009293, 0x40040100CBAA, 0x00, 0x400401004A4B, 0x40040100A0A1 } },
  { "Philips (飞利浦RC5)", RC5, 12, { 0x80C, 0x810, 0x811, 0x820, 0x821, 0x80D, 0x858, 0x859, 0x85A, 0x85B, 0x85C, 0x80A, 0x00, 0x852, 0x838 } },
  { "Philips (飞利浦RC6)", RC6, 20, { 0x1000C, 0x10010, 0x10011, 0x10020, 0x10021, 0x1000D, 0x10058, 0x10059, 0x1005A, 0x1005B, 0x1005C, 0x1000A, 0x10054, 0x10043, 0x10038 } },
  { "Xiaomi (小米)", NEC, 32, { 0x18E718E7, 0x18E710EF, 0x18E7906F, 0x18E7807F, 0x18E700FF, 0x18E7C03F, 0x18E730CF, 0x18E7B04F, 0x18E7708F, 0x18E7F00F, 0x18E7A05F, 0x18E720DF, 0x18E7E01F, 0x18E7609F, 0x18E750AF } },
  { "Hisense (海信)", NEC, 32, { 0x04FBD02F, 0x04FB04FB, 0x04FB847B, 0x04FBA05F, 0x04FB20DF, 0x04FB906F, 0x04FB48B7, 0x04FBC837, 0x04FBE817, 0x04FB6897, 0x04FB28D7, 0x04FBF00F, 0x04FB12ED, 0x04FBC03F, 0x04FB30CF } },
  { "TCL", NEC, 32, { 0x04FBD02F, 0x04FB04FB, 0x04FB847B, 0x04FBA05F, 0x04FB20DF, 0x04FB906F, 0x04FB48B7, 0x04FBC837, 0x04FBE817, 0x04FB6897, 0x04FB28D7, 0x04FBF00F, 0x04FB12ED, 0x04FBC03F, 0x04FB30CF } },
  { "Skyworth (创维)", NEC, 32, { 0x00FF02FD, 0x00FFA857, 0x00FF6897, 0x00FFE817, 0x00FF18E7, 0x00FF28D7, 0x00FF9867, 0x00FF58A7, 0x00FFD827, 0x00FF38C7, 0x00FFB847, 0x00FF7887, 0x00, 0x00FF50AF, 0x00FF10EF } },
  { "Sharp (夏普)", SHARP, 15, { 0x41A2, 0x41A8, 0x41A9, 0x4190, 0x4191, 0x41AA, 0x4198, 0x4199, 0x419A, 0x419B, 0x419C, 0x419D, 0x00, 0x4192, 0x41B0 } },
  { "Toshiba (东芝)", NEC, 32, { 0x40BF12ED, 0x40BF1AE5, 0x40BF1EE1, 0x40BF1CE3, 0x40BF1DE2, 0x40BF10EF, 0x40BF5AA5, 0x40BF52AD, 0x40BF58A7, 0x40BF5EA1, 0x40BF5CA3, 0x40BF54AB, 0x00, 0x40BF56A9, 0x40BF32CD } },
  { "Apple TV", NEC, 32, { 0x77E1502F, 0x00, 0x00, 0x00, 0x00, 0x00, 0x77E1D02F, 0x77E1B02F, 0x77E1102F, 0x77E1E02F, 0x77E1BA45, 0x77E1402F, 0x00, 0x77E1402F, 0x00 } }
};

// ================= ❄️ 空调字典 =================
int acBrandIndex = 0;
const decode_type_t acProtocols[] = { AIRTON, AIRWELL, AIWA_RC_T501, AMCOR, ARGO, ARRIS, BLUESTARHEAVY, BOSCH144, BOSE, CARRIER_AC, CARRIER_AC40, CARRIER_AC64, CARRIER_AC84, CARRIER_AC128, CLIMABUTLER, COOLIX, COOLIX48, CORONA_AC, DAIKIN, DAIKIN2, DAIKIN64, DAIKIN128, DAIKIN152, DAIKIN160, DAIKIN176, DAIKIN200, DAIKIN216, DAIKIN312, DELONGHI_AC, DENON, DISH, DOSHISHA, ECOCLIM, ELECTRA_AC, ELITESCREENS, EPSON, EUROM, FUJITSU_AC, GICABLE, GOODWEATHER, GORENJE, GREE, HAIER_AC, HAIER_AC160, HAIER_AC176, HAIER_AC_YRW02, HITACHI_AC, HITACHI_AC1, HITACHI_AC2, HITACHI_AC3, HITACHI_AC424, HITACHI_AC264, HITACHI_AC296, HITACHI_AC344, INAX, JVC, KELON, KELON168, KELVINATOR, LASERTAG, LEGOPF, LG, LG2, LUTRON, MAGIQUEST, METZ, MIDEA, MIDEA24, MILESTAG2, MIRAGE, MITSUBISHI, MITSUBISHI2, MITSUBISHI112, MITSUBISHI136, MITSUBISHI_AC, MITSUBISHI_HEAVY_88, MITSUBISHI_HEAVY_152, MULTIBRACKETS, MWM, NEC, NEC_LIKE, NEOCLIMA, NIKAI, PANASONIC, PANASONIC_AC, PANASONIC_AC32, PIONEER, RC5, RC5X, RC6, RCMM, RHOSS, SAMSUNG, SAMSUNG36, SAMSUNG_AC, SANYO, SANYO_AC, SANYO_AC88, SANYO_AC152, SANYO_LC7461, SHARP, SHARP_AC, SHERWOOD, SONY, SONY_38K, SYMPHONY, TCL96AC, TCL112AC, TECHNIBEL_AC, TECO, TEKNOPOINT, TOSHIBA_AC, TOTO, TRANSCOLD, TROTEC, TROTEC_3550, TRUMA, VESTEL_AC, VOLTAS, WHIRLPOOL_AC, WHYNTER, WOWWEE, XMP, YORK, ZEPEAL, kLastDecodeType };
const int AC_BRAND_COUNT = sizeof(acProtocols) / sizeof(acProtocols[0]);

// 空调控制参数
bool acPower = false;
int acMode = 0;
const char* acModeNames[] = { "Cool(冷)", "Heat(热)", "Fan(风)", "Dry(湿)", "Auto(自)" };
int acTemp = 26;
int acFan = 0;
const char* acFanNames[] = { "Auto", "Low", "Mid", "High" };
bool acSwing = false;
bool acSleep = false;
bool acTurbo = false;
bool acLight = true;
bool acBeep = true;
bool acQuiet = false;

// ================= 🛠️ 动态增删与词典引擎 =================
const int MAX_CUSTOM_GROUPS = 10;
int customGroupCount = 4;
const int CUSTOM_ACT_COUNT = 15;
const char* customActions[] = { "电源", "音量 +", "音量 -", "频道 / 上", "频道 / 下", "静音", "上", "下", "左", "右", "确认", "返回", "菜单", "主页", "信号源" };
const int LOC_COUNT = 21;
const char* locNames[] = { "", "客厅", "主卧", "次卧", "客房", "儿童房", "书房", "厨房", "餐厅", "卫生间", "浴室", "阳台", "花园", "车库", "玄关", "走廊", "影音室", "地下室", "阁楼", "办公室", "会议室" };
const int DEV_COUNT = 28;
const char* devNames[] = { "机顶盒", "电视", "投影仪", "音响", "功放", "风扇", "顶灯", "氛围灯", "落地灯", "射灯", "灯带", "空调", "净化器", "加湿器", "扫地机", "蓝光机", "DV机", "晾衣架", "电动窗帘", "投影幕布", "取暖器", "暖风机", "驱蚊器", "洗碗机", "智能插座", "机柜", "展示柜", "杂项设备" };
const int NUM_COUNT = 15;
const char* numNames[] = { "", "1", "2", "3", "4", "5", "6", "7", "8", "A", "B", "C", "L(左)", "R(右)" };

String customGroupStrs[MAX_CUSTOM_GROUPS];
const char* customGroups[MAX_CUSTOM_GROUPS];

int targetGroup = 0;
int targetAction = 0;
int renameTargetGrp = 0;
int tempLoc = 0;
int tempDev = 0;
int tempNum = 0;

const char* mainMenuItems[] = { "1. TV 控制", "2. AC 控制", "3. 学习录制", "4. 自定义组", "5. 系统设置" };
const char* grpManageItems[] = { "1. 重命名当前分组", "2. 精准插入新分组 (+)", "3. 删除指定分组 (-)" };

int lastClk = HIGH;
unsigned long lastButtonPress = 0;
bool isLongPress = false;

// ================= 数据平移引擎 =================
// 覆盖原函数
void copyGroupData(int srcGrp, int dstGrp) {
  prefs.putInt(("g_l_" + String(dstGrp)).c_str(), prefs.getInt(("g_l_" + String(srcGrp)).c_str(), 0));
  prefs.putInt(("g_d_" + String(dstGrp)).c_str(), prefs.getInt(("g_d_" + String(srcGrp)).c_str(), 0));
  prefs.putInt(("g_n_" + String(dstGrp)).c_str(), prefs.getInt(("g_n_" + String(srcGrp)).c_str(), 0));
  for (int act = 0; act < CUSTOM_ACT_COUNT; act++) {
    char sT[15], sH[15], sL[15], sB[15], sR[15], sRL[15], dT[15], dH[15], dL[15], dB[15], dR[15], dRL[15];
    sprintf(sT, "t_%d_%d", srcGrp, act); sprintf(dT, "t_%d_%d", dstGrp, act);
    sprintf(sH, "h_%d_%d", srcGrp, act); sprintf(dH, "h_%d_%d", dstGrp, act);
    sprintf(sL, "l_%d_%d", srcGrp, act); sprintf(dL, "l_%d_%d", dstGrp, act);
    sprintf(sB, "b_%d_%d", srcGrp, act); sprintf(dB, "b_%d_%d", dstGrp, act);
    sprintf(sR, "r_%d_%d", srcGrp, act); sprintf(dR, "r_%d_%d", dstGrp, act);
    sprintf(sRL, "rl_%d_%d", srcGrp, act); sprintf(dRL, "rl_%d_%d", dstGrp, act);
    
    if (prefs.isKey(sT)) {
      prefs.putInt(dT, prefs.getInt(sT));
      if (prefs.isKey(sH)) prefs.putUInt(dH, prefs.getUInt(sH)); else prefs.remove(dH);
      if (prefs.isKey(sL)) prefs.putUInt(dL, prefs.getUInt(sL)); else prefs.remove(dL);
      if (prefs.isKey(sB)) prefs.putInt(dB, prefs.getInt(sB)); else prefs.remove(dB);
      if (prefs.isKey(sRL)) {
        int rLen = prefs.getInt(sRL);
        prefs.putInt(dRL, rLen);
        uint16_t tmp[rLen];
        prefs.getBytes(sR, tmp, rLen * sizeof(uint16_t));
        prefs.putBytes(dR, tmp, rLen * sizeof(uint16_t));
      } else {
        prefs.remove(dR);
        prefs.remove(dRL);
      }
    } else {
      prefs.remove(dT); prefs.remove(dH); prefs.remove(dL); 
      prefs.remove(dB); prefs.remove(dR); prefs.remove(dRL);
    }
  }
}

// 覆盖原函数
void clearGroupData(int grp) {
  prefs.remove(("g_l_" + String(grp)).c_str());
  prefs.remove(("g_d_" + String(grp)).c_str());
  prefs.remove(("g_n_" + String(grp)).c_str());
  for (int act = 0; act < CUSTOM_ACT_COUNT; act++) {
    char T[15], H[15], L[15], B[15], R[15], RL[15];
    sprintf(T, "t_%d_%d", grp, act);
    sprintf(H, "h_%d_%d", grp, act);
    sprintf(L, "l_%d_%d", grp, act);
    sprintf(B, "b_%d_%d", grp, act);
    sprintf(R, "r_%d_%d", grp, act);
    sprintf(RL, "rl_%d_%d", grp, act);
    prefs.remove(T);
    prefs.remove(H);
    prefs.remove(L);
    prefs.remove(B);
    prefs.remove(R);
    prefs.remove(RL);
  }
}
void insertGroupAt(int index) {
  if (customGroupCount >= MAX_CUSTOM_GROUPS) return;
  for (int i = customGroupCount - 1; i >= index; i--) { copyGroupData(i, i + 1); }
  clearGroupData(index);
  customGroupCount++;
  prefs.putInt("grpCount", customGroupCount);
}
void deleteGroupAt(int index) {
  if (customGroupCount <= 1) return;
  for (int i = index; i < customGroupCount - 1; i++) { copyGroupData(i + 1, i); }
  clearGroupData(customGroupCount - 1);
  customGroupCount--;
  prefs.putInt("grpCount", customGroupCount);
}
void buildGroupName(int grp) {
  int l = prefs.getInt(("g_l_" + String(grp)).c_str(), 0);
  int d = prefs.getInt(("g_d_" + String(grp)).c_str(), 0);
  int n = prefs.getInt(("g_n_" + String(grp)).c_str(), 0);
  if (!prefs.isKey(("g_d_" + String(grp)).c_str())) {
    if (grp == 0 && customGroupCount <= 4) d = 0;
    else if (grp == 1 && customGroupCount <= 4) d = 2;
    else if (grp == 2 && customGroupCount <= 4) d = 3;
    else d = 27;
  }
  String res = "";
  if (l > 0) res += String(locNames[l]) + " | ";
  res += String(devNames[d]);
  if (n > 0) res += " " + String(numNames[n]);
  customGroupStrs[grp] = String(grp + 1) + ". " + res;
  customGroups[grp] = customGroupStrs[grp].c_str();
}
void initGroupNames() {
  for (int i = 0; i < customGroupCount; i++) buildGroupName(i);
}

// ================= 生命周期 =================
void setup() {
  Serial.begin(115200);
  pinMode(PIN_ENC_CLK, INPUT_PULLUP);
  pinMode(PIN_ENC_DT, INPUT_PULLUP);
  pinMode(PIN_ENC_SW, INPUT_PULLUP);
  prefs.begin("remoteData", false);
  
  sysBrightness = prefs.getInt("brightness", 128);
  currentTVBrand = prefs.getInt("tvBrand", 0);
  customGroupCount = prefs.getInt("grpCount", 4);
  if (customGroupCount > MAX_CUSTOM_GROUPS) customGroupCount = MAX_CUSTOM_GROUPS;
  if (customGroupCount < 1) customGroupCount = 1;

  // 读取所有空调数据，确保断电保存
  acBrandIndex = prefs.getInt("acBrand", 0);
  acPower = prefs.getBool("acPower", false);
  acMode = prefs.getInt("acMode", 0);
  acTemp = prefs.getInt("acTemp", 26);
  acFan = prefs.getInt("acFan", 0);
  acSwing = prefs.getBool("acSwing", false);
  acSleep = prefs.getBool("acSleep", false);
  acTurbo = prefs.getBool("acTurbo", false);
  acLight = prefs.getBool("acLight", true);
  acBeep = prefs.getBool("acBeep", true);
  acQuiet = prefs.getBool("acQuiet", false);

  initGroupNames();

  u8g2.begin();
  u8g2.enableUTF8Print();
  u8g2.setContrast(sysBrightness);
  u8g2.setBusClock(400000);

  irsend.begin();
  irrecv.enableIRIn();
  runBootSequence();
  changeMenu(MAIN_MENU, 5);
}

void loop() {
  readEncoder();

  // 学习捕获
  if (currentState == LEARN_WAIT) {
    if (irrecv.decode(&results)) {
      // 核心修改：如果是 UNKNOWN，只要脉冲长度大于 20，我们就认为是有效的非标准协议信号
      if ((results.decode_type == UNKNOWN && results.rawlen < 20) || (results.decode_type != UNKNOWN && results.bits < 8)) {
        showToast("Error! 信号太弱");
      } else {
        saveIRCode(targetGroup, targetAction, &results);
        if (results.decode_type == UNKNOWN) {
          showToast("RAW Saved!"); // 提示保存了生数据
        } else {
          showToast("Saved OK!");
        }
        changeMenu(LEARN_ACT, CUSTOM_ACT_COUNT);
      }
      irrecv.resume();
    }
  }

  if (currentState == SIGNAL_ANALYSIS) {
    if (irrecv.decode(&results)) {
      analyzedProtocol = typeToString(results.decode_type);
      analyzedValue = resultToHexidecimal(&results);
      analyzedDesc = IRAcUtils::resultAcToString(&results);

      if (analyzedDesc.length() > 2) {
        analyzedDesc.replace("\n", " | ");
        analyzedDesc.replace("\r", "");
        if (analyzedDesc.endsWith(" | ")) {
          analyzedDesc = analyzedDesc.substring(0, analyzedDesc.length() - 3);
        }
      } else {
        analyzedDesc = "基本指令位宽: " + String(results.bits) + " Bits";
      }

      scrollX_val = 0;
      scrollX_desc = 0;
      irrecv.resume();
      needsRedraw = true;
    }

    if (millis() - lastAnimTime > 40) {
      lastAnimTime = millis();
      needsRedraw = true;
    }
  }

  updateDisplay();
}

// ================= UI 切换核心 =================
void changeMenu(AppState newState, int itemsCount) {
  currentState = newState;
  cursorIndex = 0;
  scrollOffset = 0;
  currentMenuSize = itemsCount;
  isEditing = false;
  needsRedraw = true;
}

// ---------------- 旋钮与互动引擎 ----------------
void readEncoder() {
  int currentClk = digitalRead(PIN_ENC_CLK);
  if (currentClk != lastClk && currentClk == LOW) {
    int direction = (digitalRead(PIN_ENC_DT) != currentClk) ? 1 : -1;

    if (isEditing) {
      if (currentState == SETTINGS_MENU && cursorIndex == 0) {
        sysBrightness += direction * 15;
        if (sysBrightness > 255) sysBrightness = 255;
        if (sysBrightness < 0) sysBrightness = 0;
        u8g2.setContrast(sysBrightness);
      } else if (currentState == AC_MENU) {
        if (cursorIndex == 0) {
          acBrandIndex += direction;
          if (acBrandIndex >= AC_BRAND_COUNT) acBrandIndex = 0;
          if (acBrandIndex < 0) acBrandIndex = AC_BRAND_COUNT - 1;
        } else if (cursorIndex == 1) {
          acPower = !acPower;
        } else if (cursorIndex == 2) {
          acMode += direction;
          if (acMode > 4) acMode = 0;
          if (acMode < 0) acMode = 4;
        } else if (cursorIndex == 3) {
          acTemp += direction;
          if (acTemp > 30) acTemp = 30;
          if (acTemp < 16) acTemp = 16;
        } else if (cursorIndex == 4) {
          acFan += direction;
          if (acFan > 3) acFan = 0;
          if (acFan < 0) acFan = 3;
        } else if (cursorIndex == 5) {
          acSwing = !acSwing;
        } else if (cursorIndex == 6) {
          acSleep = !acSleep;
        } else if (cursorIndex == 7) {
          acTurbo = !acTurbo;
        } else if (cursorIndex == 8) {
          acLight = !acLight;
        } else if (cursorIndex == 9) {
          acBeep = !acBeep;
        } else if (cursorIndex == 10) {
          acQuiet = !acQuiet;
        }
      } else if (currentState == RENAME_EDIT) {
        if (cursorIndex == 0) {
          tempLoc += direction;
          if (tempLoc >= LOC_COUNT) tempLoc = 0;
          if (tempLoc < 0) tempLoc = LOC_COUNT - 1;
        } else if (cursorIndex == 1) {
          tempDev += direction;
          if (tempDev >= DEV_COUNT) tempDev = 0;
          if (tempDev < 0) tempDev = DEV_COUNT - 1;
        } else if (cursorIndex == 2) {
          tempNum += direction;
          if (tempNum >= NUM_COUNT) tempNum = 0;
          if (tempNum < 0) tempNum = NUM_COUNT - 1;
        }
      }
    } else {
      cursorIndex += direction;
      if (cursorIndex >= currentMenuSize) cursorIndex = 0;
      if (cursorIndex < 0) cursorIndex = currentMenuSize - 1;
      if (cursorIndex < scrollOffset) scrollOffset = cursorIndex;
      if (cursorIndex >= scrollOffset + 2) scrollOffset = cursorIndex - 2;
    }
    needsRedraw = true;
  }
  lastClk = currentClk;

  if (digitalRead(PIN_ENC_SW) == LOW) {
    if (millis() - lastButtonPress > 50) {
      unsigned long pressTime = millis();
      while (digitalRead(PIN_ENC_SW) == LOW) {
        if (millis() - pressTime > 600) isLongPress = true;
      }
      if (isLongPress) handleLongPress();
      else handleShortPress();
      isLongPress = false;
      lastButtonPress = millis();
      needsRedraw = true;
    }
  }
}

// ---------------- 按键逻辑 ----------------
void handleShortPress() {
  if (isEditing) {
    isEditing = false;
    if (currentState == SETTINGS_MENU && cursorIndex == 0) {
      prefs.putInt("brightness", sysBrightness);
    }
    if (currentState == AC_MENU && cursorIndex < 11) {
      // 退出编辑时，保存当前所有空调参数到 NVS，实现断电保存
      prefs.putInt("acBrand", acBrandIndex);
      prefs.putBool("acPower", acPower);
      prefs.putInt("acMode", acMode);
      prefs.putInt("acTemp", acTemp);
      prefs.putInt("acFan", acFan);
      prefs.putBool("acSwing", acSwing);
      prefs.putBool("acSleep", acSleep);
      prefs.putBool("acTurbo", acTurbo);
      prefs.putBool("acLight", acLight);
      prefs.putBool("acBeep", acBeep);
      prefs.putBool("acQuiet", acQuiet);

      if (cursorIndex == 0) { // 如果修改了品牌，发送一条测试指令(自动忽略电源，只测协议)
        bool tempPower = acPower;
        acPower = false;
        sendACCommand();
        acPower = tempPower;
        showToast("Test Sent!");
      }
    }
    needsRedraw = true;
    return;
  }

  if (currentState == LEARN_WAIT) {
    changeMenu(LEARN_ACT, CUSTOM_ACT_COUNT);
    return;
  }
  if (currentState == SIGNAL_ANALYSIS) {
    changeMenu(SETTINGS_MENU, 6);
    return;
  }

  switch (currentState) {
    case MAIN_MENU:
      if (cursorIndex == 0) changeMenu(TV_MENU, TV_ACTION_COUNT + 1);
      else if (cursorIndex == 1) changeMenu(AC_MENU, 12); // 空调菜单更新为12项
      else if (cursorIndex == 2) changeMenu(LEARN_GRP, customGroupCount);
      else if (cursorIndex == 3) changeMenu(CUSTOM_GRP, customGroupCount);
      else if (cursorIndex == 4) changeMenu(SETTINGS_MENU, 6);
      break;

    case TV_MENU:
      if (cursorIndex == 0) changeMenu(TV_BRAND, TV_BRAND_COUNT);
      else sendTVCommand(currentTVBrand, cursorIndex - 1);
      break;
    case TV_BRAND:
      currentTVBrand = cursorIndex;
      prefs.putInt("tvBrand", currentTVBrand);
      sendTVCommand(currentTVBrand, 0);
      changeMenu(TV_MENU, TV_ACTION_COUNT + 1);
      break;
    case AC_MENU:
      if (cursorIndex < 11) isEditing = true;
      else if (cursorIndex == 11) sendACCommand(); // 第12项是发送按键
      break;

    case LEARN_GRP:
      targetGroup = cursorIndex;
      changeMenu(LEARN_ACT, CUSTOM_ACT_COUNT);
      break;
    case LEARN_ACT:
      targetAction = cursorIndex;
      changeMenu(LEARN_WAIT, 1);
      while (irrecv.decode(&results)) { irrecv.resume(); }
      break;

    case CUSTOM_GRP:
      targetGroup = cursorIndex;
      changeMenu(CUSTOM_ACT, CUSTOM_ACT_COUNT);
      break;
    case CUSTOM_ACT: sendCustomCommand(targetGroup, cursorIndex); break;

    case SETTINGS_MENU:
      if (cursorIndex == 0) isEditing = true;
      else if (cursorIndex == 1) {
        analyzedProtocol = "等待接收...";
        analyzedValue = "---";
        analyzedDesc = "---";
        scrollX_val = 0;
        scrollX_desc = 0;
        changeMenu(SIGNAL_ANALYSIS, 1);
        while (irrecv.decode(&results)) { irrecv.resume(); }  
      } else if (cursorIndex == 2) changeMenu(GRP_MANAGE, 3);
      else if (cursorIndex == 3) {
        runBootSequence();
        changeMenu(MAIN_MENU, 5);
      } else if (cursorIndex == 4) {
        prefs.clear();
        prefs.putInt("brightness", sysBrightness);
        prefs.putInt("tvBrand", currentTVBrand);
        customGroupCount = 4;
        prefs.putInt("grpCount", customGroupCount);
        initGroupNames();
        showToast("系统已重置!");
      } else if (cursorIndex == 5) showToast("Ver 12.0 | Ultimate");
      break;

    case GRP_MANAGE:
      if (cursorIndex == 0) changeMenu(RENAME_SEL, customGroupCount);
      else if (cursorIndex == 1) changeMenu(GRP_INSERT_SEL, customGroupCount + 1);
      else if (cursorIndex == 2) changeMenu(GRP_DELETE_SEL, customGroupCount);
      break;
    case GRP_INSERT_SEL:
      if (customGroupCount >= MAX_CUSTOM_GROUPS) {
        showToast("已达最大上限(10)");
        changeMenu(GRP_MANAGE, 3);
        break;
      }
      insertGroupAt(cursorIndex);
      initGroupNames();
      showToast("分组已插入!");
      changeMenu(GRP_MANAGE, 3);
      break;
    case GRP_DELETE_SEL:
      if (customGroupCount <= 1) {
        showToast("至少保留1个组!");
        changeMenu(GRP_MANAGE, 3);
        break;
      }
      deleteGroupAt(cursorIndex);
      initGroupNames();
      showToast("分组已删除!");
      changeMenu(GRP_MANAGE, 3);
      break;
    case RENAME_SEL:
      renameTargetGrp = cursorIndex;
      tempLoc = prefs.getInt(("g_l_" + String(renameTargetGrp)).c_str(), 0);
      tempDev = prefs.getInt(("g_d_" + String(renameTargetGrp)).c_str(), 0);
      tempNum = prefs.getInt(("g_n_" + String(renameTargetGrp)).c_str(), 0);
      changeMenu(RENAME_EDIT, 4);
      break;
    case RENAME_EDIT:
      if (cursorIndex < 3) isEditing = true;
      else if (cursorIndex == 3) {
        prefs.putInt(("g_l_" + String(renameTargetGrp)).c_str(), tempLoc);
        prefs.putInt(("g_d_" + String(renameTargetGrp)).c_str(), tempDev);
        prefs.putInt(("g_n_" + String(renameTargetGrp)).c_str(), tempNum);
        buildGroupName(renameTargetGrp);
        showToast("组名已更新!");
        changeMenu(RENAME_SEL, customGroupCount);
      }
      break;
  }
}

void handleLongPress() {
  if (currentState == MAIN_MENU) return;
  AppState prevState = MAIN_MENU;
  int prevItemCount = 5;
  int prevCursor = 0;
  if (currentState == TV_MENU || currentState == AC_MENU || currentState == LEARN_GRP || currentState == CUSTOM_GRP || currentState == SETTINGS_MENU) {
    prevState = MAIN_MENU;
    prevItemCount = 5;
    if (currentState == TV_MENU) prevCursor = 0;
    else if (currentState == AC_MENU) prevCursor = 1;
    else if (currentState == LEARN_GRP) prevCursor = 2;
    else if (currentState == CUSTOM_GRP) prevCursor = 3;
    else prevCursor = 4;
  } else if (currentState == TV_BRAND) {
    prevState = TV_MENU;
    prevItemCount = TV_ACTION_COUNT + 1;
    prevCursor = 0;
  } else if (currentState == LEARN_ACT) {
    prevState = LEARN_GRP;
    prevItemCount = customGroupCount;
    prevCursor = targetGroup;
  } else if (currentState == LEARN_WAIT) {
    prevState = LEARN_ACT;
    prevItemCount = CUSTOM_ACT_COUNT;
    prevCursor = targetAction;
  } else if (currentState == CUSTOM_ACT) {
    prevState = CUSTOM_GRP;
    prevItemCount = customGroupCount;
    prevCursor = targetGroup;
  } else if (currentState == GRP_MANAGE) {
    prevState = SETTINGS_MENU;
    prevItemCount = 6;
    prevCursor = 2;
  } else if (currentState == SIGNAL_ANALYSIS) {
    prevState = SETTINGS_MENU;
    prevItemCount = 6;
    prevCursor = 1;
  } else if (currentState == RENAME_SEL) {
    prevState = GRP_MANAGE;
    prevItemCount = 3;
    prevCursor = 0;
  } else if (currentState == GRP_INSERT_SEL) {
    prevState = GRP_MANAGE;
    prevItemCount = 3;
    prevCursor = 1;
  } else if (currentState == GRP_DELETE_SEL) {
    prevState = GRP_MANAGE;
    prevItemCount = 3;
    prevCursor = 2;
  } else if (currentState == RENAME_EDIT) {
    prevState = RENAME_SEL;
    prevItemCount = customGroupCount;
    prevCursor = renameTargetGrp;
  }

  changeMenu(prevState, prevItemCount);
  cursorIndex = prevCursor;
  scrollOffset = cursorIndex - 1;
  if (scrollOffset < 0) scrollOffset = 0;
  if (scrollOffset > currentMenuSize - 3) scrollOffset = currentMenuSize - 3 > 0 ? currentMenuSize - 3 : 0;
  needsRedraw = true;
}

// ================= 发射操作 =================
void sendTVCommand(int brandIdx, int actionIdx) {
  uint64_t code = tvDatabase[brandIdx].codes[actionIdx];
  if (code == 0x00) {
    showToast("Key N/A");
    return;
  }
  irsend.send(tvDatabase[brandIdx].protocol, code, tvDatabase[brandIdx].bits);
  showToast("TV Sent!");
}

void sendACCommand() {
  ac.next.protocol = acProtocols[acBrandIndex];
  ac.next.power = acPower;
  ac.next.degrees = acTemp;
  if (acMode == 0) ac.next.mode = stdAc::opmode_t::kCool;
  else if (acMode == 1) ac.next.mode = stdAc::opmode_t::kHeat;
  else if (acMode == 2) ac.next.mode = stdAc::opmode_t::kFan;
  else if (acMode == 3) ac.next.mode = stdAc::opmode_t::kDry;
  else ac.next.mode = stdAc::opmode_t::kAuto;
  if (acFan == 0) ac.next.fanspeed = stdAc::fanspeed_t::kAuto;
  else if (acFan == 1) ac.next.fanspeed = stdAc::fanspeed_t::kLow;
  else if (acFan == 2) ac.next.fanspeed = stdAc::fanspeed_t::kMedium;
  else ac.next.fanspeed = stdAc::fanspeed_t::kHigh;
  
  ac.next.swingv = acSwing ? stdAc::swingv_t::kAuto : stdAc::swingv_t::kOff;
  
  // 注入新拓展的高级控制参数
  ac.next.sleep = acSleep;
  ac.next.turbo = acTurbo;
  ac.next.light = acLight;
  ac.next.beep = acBeep;
  ac.next.quiet = acQuiet;

  ac.sendAc();
}

void saveIRCode(int grp, int act, decode_results* res) {
  char keyT[15], keyH[15], keyL[15], keyB[15], keyR[15], keyRL[15];
  sprintf(keyT, "t_%d_%d", grp, act);
  sprintf(keyH, "h_%d_%d", grp, act);
  sprintf(keyL, "l_%d_%d", grp, act);
  sprintf(keyB, "b_%d_%d", grp, act);
  sprintf(keyR, "r_%d_%d", grp, act);    // 专门用于存放 raw 数组的 key
  sprintf(keyRL, "rl_%d_%d", grp, act);  // 专门用于存放 raw 数组长度的 key

  prefs.putInt(keyT, res->decode_type);

  if (res->decode_type != UNKNOWN) {
    // 标准协议，正常存储十六进制数据
    prefs.putUInt(keyH, (uint32_t)(res->value >> 32));
    prefs.putUInt(keyL, (uint32_t)(res->value & 0xFFFFFFFF));
    prefs.putInt(keyB, res->bits);
    // 清除可能残留的 RAW 数据
    prefs.remove(keyR);
    prefs.remove(keyRL);
  } else {
    // 未知协议，提取生脉冲数据 (抛弃第0位的起始间隔)
    uint16_t rawLen = res->rawlen - 1;
    uint16_t rawArray[rawLen];
    for (int i = 1; i <= rawLen; i++) {
      rawArray[i - 1] = res->rawbuf[i] * kRawTick;
    }
    // 将整个数组以 byte 块的形式存入 NVS
    prefs.putBytes(keyR, rawArray, rawLen * sizeof(uint16_t));
    prefs.putInt(keyRL, rawLen);
    // 清除可能残留的标准协议数据
    prefs.remove(keyH);
    prefs.remove(keyL);
    prefs.remove(keyB);
  }
}

void sendCustomCommand(int grp, int act) {
  char keyT[15], keyH[15], keyL[15], keyB[15], keyR[15], keyRL[15];
  sprintf(keyT, "t_%d_%d", grp, act);
  sprintf(keyH, "h_%d_%d", grp, act);
  sprintf(keyL, "l_%d_%d", grp, act);
  sprintf(keyB, "b_%d_%d", grp, act);
  sprintf(keyR, "r_%d_%d", grp, act);
  sprintf(keyRL, "rl_%d_%d", grp, act);

  if (!prefs.isKey(keyT)) {
    showToast("未录入!");
    return;
  }

  decode_type_t type = (decode_type_t)prefs.getInt(keyT);

  if (type != UNKNOWN) {
    // 发送标准协议
    uint32_t high = prefs.getUInt(keyH);
    uint32_t low = prefs.getUInt(keyL);
    uint64_t val = ((uint64_t)high << 32) | low;
    uint16_t bits = prefs.getInt(keyB);
    irsend.send(type, val, bits);
  } else {
    // 发送生数据 (Raw Mode)
    int rawLen = prefs.getInt(keyRL, 0);
    if (rawLen > 0) {
      uint16_t rawArray[rawLen];
      prefs.getBytes(keyR, rawArray, rawLen * sizeof(uint16_t));
      // 大多数非标准家用遥控器调制频率为 38kHz
      irsend.sendRaw(rawArray, rawLen, 38); 
    }
  }
  showToast("Custom Sent!");
}

// ---------------- 动态 UI 绘制引擎 ----------------
void updateDisplay() {
  if (!needsRedraw) return;
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312a);

  switch (currentState) {
    case MAIN_MENU: drawScrollMenu("主菜单", mainMenuItems); break;
    case TV_MENU:
      {
        String title = "TV (" + String(tvDatabase[currentTVBrand].brandName) + ")";
        drawScrollMenu(title.c_str(), tvMenuItems);
        break;
      }
    case TV_BRAND:
      {
        const char* tempBrands[TV_BRAND_COUNT];
        for (int i = 0; i < TV_BRAND_COUNT; i++) tempBrands[i] = tvDatabase[i].brandName;
        drawScrollMenu("选择电视品牌", tempBrands);
        break;
      }
    case AC_MENU: drawACDashboard(); break;
    case LEARN_GRP: drawScrollMenu("选择录制分组", customGroups); break;
    case CUSTOM_GRP: drawScrollMenu("选择控制分组", customGroups); break;
    case GRP_MANAGE: drawScrollMenu("自定义组管理", grpManageItems); break;
    case GRP_INSERT_SEL:
      {
        String insStrs[MAX_CUSTOM_GROUPS + 1];
        const char* insPtrs[MAX_CUSTOM_GROUPS + 1];
        for (int i = 0; i < customGroupCount; i++) {
          insStrs[i] = "插在第 " + String(i + 1) + " 组前面";
          insPtrs[i] = insStrs[i].c_str();
        }
        insStrs[customGroupCount] = "[ 追加到末尾 ]";
        insPtrs[customGroupCount] = insStrs[customGroupCount].c_str();
        drawScrollMenu("选择插入位置", insPtrs);
        break;
      }
    case GRP_DELETE_SEL: drawScrollMenu("选择要删除的组", customGroups); break;
    case RENAME_SEL: drawScrollMenu("选择要重命名的组", customGroups); break;
    case RENAME_EDIT: drawRenameEdit(); break;
    case LEARN_ACT:
    case CUSTOM_ACT:
      {
        String actNames[CUSTOM_ACT_COUNT];
        const char* actCStrs[CUSTOM_ACT_COUNT];
        for (int i = 0; i < CUSTOM_ACT_COUNT; i++) {
          char keyT[15];
          sprintf(keyT, "t_%d_%d", targetGroup, i);
          if (prefs.isKey(keyT)) actNames[i] = "[√] " + String(customActions[i]);
          else actNames[i] = "[ ] " + String(customActions[i]);
          actCStrs[i] = actNames[i].c_str();
        }
        String title = (currentState == LEARN_ACT) ? "录入到哪个键?" : "发射哪个键?";
        drawScrollMenu(title.c_str(), actCStrs);
        break;
      }
    case LEARN_WAIT:
      u8g2.drawFrame(5, 10, 118, 44);
      u8g2.setCursor(20, 25);
      u8g2.print("进入捕获模式");
      u8g2.setCursor(15, 43);
      u8g2.print("请对准接收头按下");
      u8g2.setCursor(25, 61);
      u8g2.print("[短按取消]");
      break;

    case SIGNAL_ANALYSIS:
      {
        u8g2.drawFrame(0, 0, 128, 64);
        u8g2.setCursor(5, 12);
        u8g2.print("📡 红外信号分析仪");
        u8g2.drawLine(0, 15, 128, 15);
        u8g2.setCursor(5, 28);
        u8g2.print("协议: " + analyzedProtocol);

        int valW = u8g2.getUTF8Width(analyzedValue.c_str());
        int descW = u8g2.getUTF8Width(analyzedDesc.c_str());

        if (valW > 90) {
          scrollX_val -= 2;
          if (scrollX_val < -(valW + 10)) scrollX_val = 90;
        } else {
          scrollX_val = 0;
        }

        if (descW > 90) {
          scrollX_desc -= 2;
          if (scrollX_desc < -(descW + 10)) scrollX_desc = 90;
        } else {
          scrollX_desc = 0;
        }

        u8g2.setCursor(5, 43);
        u8g2.print("键值: ");
        u8g2.setClipWindow(35, 30, 126, 45); 
        u8g2.setCursor(35 + scrollX_val, 43);
        u8g2.print(analyzedValue);
        u8g2.setMaxClipWindow(); 

        u8g2.setCursor(5, 58);
        u8g2.print("解析: ");
        u8g2.setClipWindow(35, 46, 126, 60);
        u8g2.setCursor(35 + scrollX_desc, 58);
        u8g2.print(analyzedDesc);
        u8g2.setMaxClipWindow();

        break;
      }

    case SETTINGS_MENU:
      {
        String dynSet[6] = { "亮度: " + String(sysBrightness), "📡 红外信号分析", "自定义组管理", "重新自检", "清空系统数据", "关于本机" };
        const char* setPtrs[6];
        for (int i = 0; i < 6; i++) setPtrs[i] = dynSet[i].c_str();
        drawScrollMenu("系统设置", setPtrs);
        break;
      }
    default:
      u8g2.setCursor(10, 30);
      u8g2.print("模块开发中...");
      break;
  }
  u8g2.sendBuffer();
  needsRedraw = false;
}

void drawScrollMenu(const char* title, const char** items) {
  u8g2.setCursor(0, 12);
  u8g2.print(title);
  u8g2.drawLine(0, 15, 128, 15);
  for (int i = 0; i < 3; i++) {
    int itemIndex = scrollOffset + i;
    if (itemIndex >= currentMenuSize) break;
    int yPos = 28 + (i * 14);
    if (itemIndex == cursorIndex) {
      if (isEditing) u8g2.drawFrame(0, yPos - 11, 128, 14);
      else {
        u8g2.setDrawColor(1);
        u8g2.drawBox(0, yPos - 11, 128, 14);
        u8g2.setDrawColor(0);
      }
      u8g2.setCursor(4, yPos);
      u8g2.print(items[itemIndex]);
      u8g2.setDrawColor(1);
    } else {
      u8g2.setCursor(4, yPos);
      u8g2.print(items[itemIndex]);
    }
  }
  int scrollBarHeight = 48 / (currentMenuSize > 0 ? currentMenuSize : 1);
  if (scrollBarHeight < 5) scrollBarHeight = 5;
  int scrollBarY = 16 + (48 - scrollBarHeight) * cursorIndex / (currentMenuSize - 1 > 0 ? currentMenuSize - 1 : 1);
  u8g2.drawBox(126, scrollBarY, 2, scrollBarHeight);
}

void drawRenameEdit() {
  u8g2.setCursor(0, 12);
  u8g2.print("📝 组名编辑");
  u8g2.drawLine(0, 15, 128, 15);
  String items[4] = { "位置: " + String(tempLoc == 0 ? "(无)" : locNames[tempLoc]), "设备: " + String(devNames[tempDev]), "编号: " + String(tempNum == 0 ? "(无)" : numNames[tempNum]), "[ 保存退出 ]" };
  for (int i = 0; i < 3; i++) {
    int itemIdx = scrollOffset + i;
    if (itemIdx >= 4) break;
    int yPos = 28 + (i * 14);
    if (itemIdx == cursorIndex) {
      if (isEditing) u8g2.drawFrame(0, yPos - 11, 128, 14);
      else {
        u8g2.setDrawColor(1);
        u8g2.drawBox(0, yPos - 11, 128, 14);
        u8g2.setDrawColor(0);
      }
      u8g2.setCursor(4, yPos);
      u8g2.print(items[itemIdx].c_str());
      u8g2.setDrawColor(1);
    } else {
      u8g2.setCursor(4, yPos);
      u8g2.print(items[itemIdx].c_str());
    }
  }
  u8g2.drawBox(126, 16 + (48 - 12) * cursorIndex / 3, 2, 12);
}

void drawACDashboard() {
  u8g2.setCursor(0, 12);
  u8g2.print("❄️ 空调超级引擎");
  u8g2.drawLine(0, 15, 128, 15);
  
  // 扩展为 12 个项
  String acItems[12] = { 
    "协议: " + typeToString(acProtocols[acBrandIndex]), 
    "电源: " + String(acPower ? "ON" : "OFF"), 
    "模式: " + String(acModeNames[acMode]), 
    "温度: " + String(acTemp) + " C", 
    "风速: " + String(acFanNames[acFan]), 
    "扫风: " + String(acSwing ? "打开" : "关闭"), 
    "睡眠: " + String(acSleep ? "打开" : "关闭"),
    "强劲(Turbo): " + String(acTurbo ? "打开" : "关闭"),
    "灯光: " + String(acLight ? "打开" : "关闭"),
    "蜂鸣器: " + String(acBeep ? "打开" : "关闭"),
    "静音: " + String(acQuiet ? "打开" : "关闭"),
    "[ 发送状态指令 >>> ]" 
  };

  for (int i = 0; i < 3; i++) {
    int itemIdx = scrollOffset + i;
    if (itemIdx >= 12) break; // 保护边界
    int yPos = 28 + (i * 14);
    if (itemIdx == cursorIndex) {
      if (isEditing) u8g2.drawFrame(0, yPos - 11, 128, 14);
      else {
        u8g2.setDrawColor(1);
        u8g2.drawBox(0, yPos - 11, 128, 14);
        u8g2.setDrawColor(0);
      }
      u8g2.setCursor(4, yPos);
      u8g2.print(acItems[itemIdx].c_str());
      u8g2.setDrawColor(1);
    } else {
      u8g2.setCursor(4, yPos);
      u8g2.print(acItems[itemIdx].c_str());
    }
  }
  
  // 滚动条比例适配 12个项目
  int scrollBarHeight = 48 / 12; 
  if (scrollBarHeight < 4) scrollBarHeight = 4;
  u8g2.drawBox(126, 16 + (48 - scrollBarHeight) * cursorIndex / 11, 2, scrollBarHeight);
}

void showToast(const char* msg) {
  u8g2.clearBuffer();
  u8g2.drawRBox(10, 20, 108, 24, 4);
  u8g2.setDrawColor(0);
  u8g2.setCursor(20, 36);
  u8g2.print(msg);
  u8g2.setDrawColor(1);
  u8g2.sendBuffer();
  delay(800);
  needsRedraw = true;
}

void runBootSequence() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_wqy12_t_gb2312a);
  u8g2.setCursor(15, 20);
  u8g2.print("Universal Remote");
  u8g2.setCursor(10, 45);
  u8g2.print("OLED & NVS ... OK");
  u8g2.sendBuffer();
  delay(800);
  u8g2.clearBuffer();
  u8g2.setCursor(5, 20);
  u8g2.print("IR 自检启动...");
  u8g2.setCursor(5, 40);
  u8g2.print("请将手掌放在红外");
  u8g2.setCursor(5, 55);
  u8g2.print("模块上方进行反射");
  u8g2.sendBuffer();
  bool irPassed = false;
  unsigned long testStart = millis();
  while (millis() - testStart < 5000) {
    while (irrecv.decode(&results)) { irrecv.resume(); }
    irsend.sendNEC(0x87654321, 32);
    delay(30);
    if (irrecv.decode(&results)) {
      if (results.bits > 8) {
        irPassed = true;
        break;
      }
    }
    delay(150);
  }
  u8g2.clearBuffer();
  u8g2.setCursor(15, 30);
  if (irPassed) u8g2.print("红外链路 ... 正常");
  else {
    u8g2.print("红外链路 ... 失败!");
    u8g2.setCursor(15, 50);
    u8g2.print("请检查连线!");
  }
  u8g2.sendBuffer();
  delay(1500);
}
