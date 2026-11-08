#include "ui.h"

#include "bridge_uart.h"
#include "config.h"
#include "deck_link.h"
#include "diag.h"
#include "encoder.h"
#include "icons.h"
#include "lcd.h"
#include "touch.h"

// ---------------------------------------------------------------------------
// Layout: 28 px status bar + 5x3 grid of 84 px keys with 10 px gaps (480x320)
// ---------------------------------------------------------------------------
#define BAR_H     28
#define KEY_SIZE  84
#define KEY_GAP   10
#define KEY_R     12
#define COLS      5
#define ROWS      3
#define NUM_KEYS  (COLS * ROWS)

#define COL_BG       rgb565(10, 10, 14)
#define COL_BAR      rgb565(26, 26, 34)
#define COL_EMPTY    rgb565(22, 22, 28)
#define COL_TEXT_DIM rgb565(140, 140, 150)

enum class Act : uint8_t {
  None,
  Hotkey,       // momentary action, would be sent to the ESP32
  Toggle,       // on/off state
  Scene,        // radio group, arg = scene number
  Counter,
  Page,         // arg = page index
  Back,
  BrightDown,
  BrightShow,
  BrightUp,
  BeepToggle,
  DimToggle,
  Calibrate,
  DisplayTest,
  Info,
};

struct Key {
  const char *label;
  Icon icon;
  uint16_t color;
  Act act;
  uint8_t arg;
  // toggled state appearance
  const char *onLabel;
  Icon onIcon;
  uint16_t onColor;
};

struct Page {
  const char *name;
  Key keys[NUM_KEYS];
};

constexpr Key EMPTY = {nullptr, Icon::None, 0, Act::None, 0, nullptr, Icon::None, 0};

constexpr Key K(const char *label, Icon icon, uint16_t color, Act act, uint8_t arg = 0)
{
  return {label, icon, color, act, arg, nullptr, Icon::None, 0};
}

constexpr Key T(const char *label, Icon icon, uint16_t color, const char *onLabel, Icon onIcon, uint16_t onColor,
                Act act = Act::Toggle)
{
  return {label, icon, color, act, 0, onLabel, onIcon, onColor};
}

constexpr uint16_t RED_ON = rgb565(200, 35, 35);
constexpr uint16_t SLATE = rgb565(52, 60, 80);
constexpr uint16_t NAV = rgb565(58, 58, 70);

static const Page PAGES[] = {
  {"Main", {
    T("Mic", Icon::Mic, rgb565(40, 120, 70), "Muted", Icon::MicOff, RED_ON),
    T("Camera", Icon::Camera, rgb565(40, 90, 160), "No Cam", Icon::CameraOff, rgb565(80, 80, 90)),
    T("Record", Icon::Record, rgb565(95, 40, 40), "REC", Icon::Record, RED_ON),
    T("Go Live", Icon::Live, rgb565(100, 50, 150), "LIVE", Icon::Live, RED_ON),
    K("Snap", Icon::Screenshot, rgb565(30, 115, 125), Act::Hotkey),

    K("Main", Icon::Scene, SLATE, Act::Scene, 0),
    K("Game", Icon::Scene, SLATE, Act::Scene, 1),
    K("Chat", Icon::Scene, SLATE, Act::Scene, 2),
    K("BRB", Icon::Coffee, rgb565(115, 75, 40), Act::Scene, 3),
    K("Marker", Icon::Marker, rgb565(185, 105, 20), Act::Hotkey),

    K("Media", Icon::Folder, NAV, Act::Page, 1),
    K("Lock", Icon::Lock, rgb565(70, 70, 82), Act::Hotkey),
    K("Count", Icon::None, rgb565(35, 95, 115), Act::Counter),
    EMPTY,
    K("Setup", Icon::Gear, NAV, Act::Page, 2),
  }},
  {"Media", {
    K("Back", Icon::Back, NAV, Act::Back),
    K("Prev", Icon::Prev, rgb565(45, 70, 120), Act::Hotkey),
    T("Play", Icon::Play, rgb565(40, 120, 70), "Pause", Icon::Pause, rgb565(185, 105, 20)),
    K("Next", Icon::Next, rgb565(45, 70, 120), Act::Hotkey),
    K("Stop", Icon::Stop, rgb565(95, 40, 40), Act::Hotkey),

    K("Vol -", Icon::VolDown, SLATE, Act::Hotkey),
    T("Sound", Icon::Speaker, SLATE, "Muted", Icon::Mute, RED_ON),
    K("Vol +", Icon::VolUp, SLATE, Act::Hotkey),
    EMPTY, EMPTY,
    EMPTY, EMPTY, EMPTY, EMPTY, EMPTY,
  }},
  {"Settings", {
    K("Back", Icon::Back, NAV, Act::Back),
    K("Dimmer", Icon::SunSmall, rgb565(90, 80, 30), Act::BrightDown),
    K(nullptr, Icon::None, rgb565(50, 50, 40), Act::BrightShow),
    K("Bright", Icon::Sun, rgb565(150, 125, 30), Act::BrightUp),
    T("Beep", Icon::Bell, rgb565(40, 100, 110), "Silent", Icon::BellOff, rgb565(80, 80, 90), Act::BeepToggle),

    K("Touch", Icon::Target, rgb565(110, 60, 120), Act::Calibrate),
    K("Colors", Icon::Palette, rgb565(40, 40, 48), Act::DisplayTest),
    K("Info", Icon::Info, rgb565(45, 70, 120), Act::Info),
    T("AutoDim", Icon::SunSmall, rgb565(60, 90, 60), "No dim", Icon::Sun, rgb565(80, 80, 90), Act::DimToggle),
    EMPTY,
    EMPTY, EMPTY, EMPTY, EMPTY, EMPTY,
  }},
};
#define NUM_PAGES (sizeof(PAGES) / sizeof(PAGES[0]))

extern "C" volatile uint8_t clock_source;  // clock.c

namespace {

Print *logOut = nullptr;

uint8_t page = 0;
uint8_t parentPage = 0;
bool toggled[NUM_PAGES][NUM_KEYS];
uint8_t activeScene = 0;
uint16_t counter = 0;

uint8_t brightness = BACKLIGHT_DEFAULT;
bool beepOn = true;
bool autoDim = true;
bool dimmed = false;
uint32_t lastActivity = 0;

char toast[32] = "";
uint32_t toastUntil = 0;

// Host mode: while the ESP32 reports a host app attached, the grid shows keys
// set by the host (DECK_CMD_SET_KEY / DECK_CMD_KEY_IMAGE) instead of PAGES,
// and presses are only reported, not acted on.
#define HOST_IMG DECK_KEY_IMG  // host key images are 80x80, drawn 2 px inside the key
struct HostKey {
  uint16_t bg, fg;
  char label[24];
  bool image;
};
HostKey hostKeys[NUM_KEYS];
bool hostMode = false;
bool linkOnline = false;
uint8_t linkFlags = 0;

uint8_t eventPage();

// touch tracking
int8_t pressedKey = -1;
bool touchActive = false;
bool ignoreUntilRelease = false;
uint8_t missCount = 0;
uint32_t lastPoll = 0;

// Touch debug readout in the status bar (UI_DEBUG in config.h)
uint32_t dbgPolls = 0, dbgFails = 0;
#if UI_DEBUG
uint32_t dbgDrawn = 0;
#endif
bool dbgPen = false;
int16_t dbgX = -1, dbgY = -1;

int16_t keyX(uint8_t i) { return KEY_GAP + (i % COLS) * (KEY_SIZE + KEY_GAP); }
int16_t keyY(uint8_t i) { return BAR_H + KEY_GAP + (i / COLS) * (KEY_SIZE + KEY_GAP); }

int8_t keyAt(int16_t x, int16_t y)
{
  if (y < BAR_H) return -1;
  int16_t col = x * COLS / LCD_WIDTH;
  int16_t row = (y - BAR_H) * ROWS / (LCD_HEIGHT - BAR_H);
  if (col < 0 || col >= COLS || row < 0 || row >= ROWS) return -1;
  return (int8_t)(row * COLS + col);
}

uint16_t lighten(uint16_t c, uint8_t amount)  // amount 0..255 toward white
{
  uint8_t r = (c >> 11) << 3, g = ((c >> 5) & 0x3F) << 2, b = (c & 0x1F) << 3;
  r += ((255 - r) * amount) >> 8;
  g += ((255 - g) * amount) >> 8;
  b += ((255 - b) * amount) >> 8;
  return rgb565(r, g, b);
}

bool isOn(uint8_t i)
{
  const Key &k = PAGES[page].keys[i];
  switch (k.act) {
    case Act::Scene: return k.arg == activeScene;
    case Act::BeepToggle: return !beepOn;
    case Act::DimToggle: return !autoDim;
    default: return k.onLabel && toggled[page][i];
  }
}

void click()
{
  if (beepOn) tone(BUZZER_PIN, 2400, 12);
}

void drawHostKey(uint8_t i, bool pressed)
{
  const HostKey &h = hostKeys[i];
  int16_t x = keyX(i), y = keyY(i);

  if (h.image) {  // image fills the key; pressing only draws a rounded frame round it
    Lcd::strokeRoundRect(x, y, KEY_SIZE, KEY_SIZE, KEY_R, pressed ? Color::White : COL_BG,
                         (KEY_SIZE - HOST_IMG) / 2);
    return;
  }

  uint16_t bg = pressed ? lighten(h.bg, 90) : h.bg;
  // A previous image filled the whole square: clear the corners the rounded key won't cover
  Lcd::fillRect(x, y, KEY_R, KEY_R, COL_BG);
  Lcd::fillRect(x + KEY_SIZE - KEY_R, y, KEY_R, KEY_R, COL_BG);
  Lcd::fillRect(x, y + KEY_SIZE - KEY_R, KEY_R, KEY_R, COL_BG);
  Lcd::fillRect(x + KEY_SIZE - KEY_R, y + KEY_SIZE - KEY_R, KEY_R, KEY_R, COL_BG);
  if (pressed) Lcd::drawRoundRect(x, y, KEY_SIZE, KEY_SIZE, KEY_R, Color::White, bg, 2);
  else Lcd::fillRoundRect(x, y, KEY_SIZE, KEY_SIZE, KEY_R, bg);

  // label: up to two lines, split at '\n'
  char lines[2][24] = {"", ""};
  uint8_t n = 0, len = 0;
  for (const char *c = h.label; *c && n < 2; c++) {
    if (*c == '\n') {
      n++;
      len = 0;
    } else if (len < sizeof(lines[0]) - 1) {
      lines[n][len++] = *c;
      lines[n][len] = '\0';
    }
  }
  uint8_t count = lines[1][0] ? 2 : (lines[0][0] ? 1 : 0);
  uint8_t scale = 2;
  for (uint8_t l = 0; l < count; l++)
    if (Lcd::textWidth(lines[l], 2) > KEY_SIZE - 8) scale = 1;
  int16_t lineH = 8 * scale + 4;
  int16_t top = y + (KEY_SIZE - count * lineH + 4) / 2;
  for (uint8_t l = 0; l < count; l++)
    Lcd::drawTextCentered(x + KEY_SIZE / 2, top + l * lineH, lines[l], scale, h.fg, bg);
}

void drawKey(uint8_t i, bool pressed)
{
  if (hostMode) {
    drawHostKey(i, pressed);
    return;
  }
  const Key &k = PAGES[page].keys[i];
  int16_t x = keyX(i), y = keyY(i);

  if (k.act == Act::None && !k.label) {
    Lcd::fillRoundRect(x, y, KEY_SIZE, KEY_SIZE, KEY_R, COL_EMPTY);
    return;
  }

  bool on = isOn(i);
  const char *label = (on && k.onLabel) ? k.onLabel : k.label;
  Icon icon = (on && k.onLabel) ? k.onIcon : k.icon;
  uint16_t color = (on && k.onLabel) ? k.onColor : k.color;
  if (k.act == Act::Scene && on) color = rgb565(30, 140, 90);
  if (pressed) color = lighten(color, 90);

  if (k.act == Act::Scene && on) {
    Lcd::drawRoundRect(x, y, KEY_SIZE, KEY_SIZE, KEY_R, Color::White, color, 3);
  } else if (pressed) {
    Lcd::drawRoundRect(x, y, KEY_SIZE, KEY_SIZE, KEY_R, Color::White, color, 2);
  } else {
    Lcd::fillRoundRect(x, y, KEY_SIZE, KEY_SIZE, KEY_R, color);
  }

  int16_t cx = x + KEY_SIZE / 2;
  char buf[12];

  if (k.act == Act::Counter) {
    snprintf(buf, sizeof(buf), "%u", counter);
    Lcd::drawTextCentered(cx, y + 18, buf, counter < 1000 ? 4 : 3, Color::White, color);
  } else if (k.act == Act::BrightShow) {
    snprintf(buf, sizeof(buf), "%u%%", brightness);
    Lcd::drawTextCentered(cx, y + 24, buf, 3, Color::White, color);
    label = "Level";
  } else {
    drawIcon(icon, cx, y + 32, Color::White, color);
  }

  if (label) {
    uint8_t scale = Lcd::textWidth(label, 2) <= KEY_SIZE - 10 ? 2 : 1;
    Lcd::drawTextCentered(cx, scale == 2 ? y + 62 : y + 66, label, scale, Color::White, color);
  }
}

void drawStatusBar()
{
  Lcd::fillRect(0, 0, LCD_WIDTH, BAR_H, COL_BAR);
  Lcd::drawText(10, 7, hostMode ? "Host" : PAGES[page].name, 2, Color::White, COL_BAR);

  if (toast[0]) {
    Lcd::drawTextCentered(LCD_WIDTH / 2 + 10, 7, toast, 2, Color::Yellow, COL_BAR);
  } else if (!hostMode) {
    for (uint8_t p = 0; p < NUM_PAGES; p++) {
      int16_t dx = LCD_WIDTH / 2 + (p - (int)(NUM_PAGES - 1) / 2.0f) * 18;
      if (p == page) Lcd::fillCircle(dx, BAR_H / 2, 5, Color::White);
      else Lcd::drawRing(dx, BAR_H / 2, 5, 1, COL_TEXT_DIM, COL_BAR);
    }
  }

#if !UI_DEBUG
  const char *text;
  uint16_t dot;
  if (!linkOnline) {
    text = "ESP offline";
    dot = rgb565(180, 60, 40);
  } else if (!(linkFlags & DECK_STATUS_USB)) {
    text = "No USB";
    dot = rgb565(200, 140, 30);
  } else if (linkFlags & DECK_STATUS_HOST_APP) {
    text = "Host app";
    dot = rgb565(60, 130, 220);
  } else {
    text = "USB";
    dot = rgb565(50, 170, 80);
  }
  Lcd::fillCircle(LCD_WIDTH - 78, BAR_H / 2, 4, dot);
  Lcd::drawText(LCD_WIDTH - 70, 10, text, 1, COL_TEXT_DIM, COL_BAR);
  if (linkFlags & DECK_STATUS_CAPSLOCK) Lcd::drawText(LCD_WIDTH - 112, 10, "CAPS", 1, Color::Yellow, COL_BAR);
#endif
}

void drawPage()
{
  Lcd::fillScreen(COL_BG);
  drawStatusBar();
  for (uint8_t i = 0; i < NUM_KEYS; i++) drawKey(i, false);
}

void showToast(const char *msg)
{
  strncpy(toast, msg, sizeof(toast) - 1);
  toast[sizeof(toast) - 1] = '\0';
  toastUntil = millis() + 2000;
  drawStatusBar();
}

void waitForTap()
{
  while (Touch::isPressed()) diagDelay(5);
  diagDelay(50);
  while (!Touch::isPressed()) diagDelay(5);
  click();
  while (Touch::isPressed()) diagDelay(5);
  diagDelay(50);
}

void displayTest()
{
  static const uint16_t bars[8] = {Color::White, Color::Yellow, Color::Cyan, Color::Green,
                                   Color::Magenta, Color::Red, Color::Blue, Color::Black};
  static const char *names[8] = {"WHT", "YEL", "CYN", "GRN", "MAG", "RED", "BLU", "BLK"};
  for (uint8_t i = 0; i < 8; i++) {
    Lcd::fillRect(i * 60, 0, 60, 150, bars[i]);
  }
  Lcd::fillRect(0, 150, LCD_WIDTH, 22, Color::Black);
  for (uint8_t i = 0; i < 8; i++) Lcd::drawTextCentered(i * 60 + 30, 154, names[i], 2, Color::White, Color::Black);

  for (int16_t x = 0; x < LCD_WIDTH; x++) {
    uint8_t v = x * 255 / (LCD_WIDTH - 1);
    Lcd::fillRect(x, 175, 1, 30, rgb565(v, 0, 0));
    Lcd::fillRect(x, 205, 1, 30, rgb565(0, v, 0));
    Lcd::fillRect(x, 235, 1, 30, rgb565(0, 0, v));
    Lcd::fillRect(x, 265, 1, 30, rgb565(v, v, v));
  }
  Lcd::fillRect(0, 295, LCD_WIDTH, 25, Color::Black);
  Lcd::drawTextCentered(LCD_WIDTH / 2, 300, "Tap to exit", 2, COL_TEXT_DIM, Color::Black);
  waitForTap();
}

void infoScreen()
{
  Lcd::fillScreen(COL_BG);
  Lcd::drawText(16, 14, "TFT35 Deck - test build", 2, Color::White, COL_BG);
  char line[80];
  int16_t y = 50;
  auto row = [&](const char *s) { Lcd::drawText(16, y, s, 2, COL_TEXT_DIM, COL_BG); y += 24; };

  snprintf(line, sizeof(line), "Built  %s %s", __DATE__, __TIME__); row(line);
  static const char *clk[3] = {"crystal", "internal osc", "internal, no PLL"};
  snprintf(line, sizeof(line), "MCU    STM32F207VC %lu MHz (%s)", (unsigned long)(SystemCoreClock / 1000000),
           clk[clock_source < 3 ? clock_source : 2]);
  row(line);
  snprintf(line, sizeof(line), "LCD    %s  id %06lX", Lcd::controllerName(Lcd::controller()), (unsigned long)Lcd::rawId());
  row(line);
  uint32_t s = millis() / 1000;
  snprintf(line, sizeof(line), "Uptime %luh %02lum %02lus", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60)); row(line);
  snprintf(line, sizeof(line), "Light  %u%%  beep %s  dim %s", brightness, beepOn ? "on" : "off", autoDim ? "on" : "off"); row(line);
  row("Touch calibration:");
  const int32_t *c = Touch::calibration();
  snprintf(line, sizeof(line), "{ %ld, %ld, %ld, %ld, %ld, %ld }", (long)c[0], (long)c[1], (long)c[2], (long)c[3], (long)c[4], (long)c[5]);
  Lcd::drawText(16, y, line, 1, Color::Yellow, COL_BG);
  Lcd::drawTextCentered(LCD_WIDTH / 2, 296, "Tap to exit", 2, COL_TEXT_DIM, COL_BG);
  waitForTap();
}

void logAction(const Key &k, uint8_t i, const char *what)
{
  if (!logOut) return;
  logOut->print("[deck] page=");
  logOut->print(PAGES[page].name);
  logOut->print(" key=");
  logOut->print(i);
  logOut->print(" '");
  logOut->print(k.label ? k.label : "");
  logOut->print("' -> ");
  logOut->println(what);
}

void trigger(uint8_t i)
{
  const Key &k = PAGES[page].keys[i];
  char msg[32];

  switch (k.act) {
    case Act::None: return;

    case Act::Hotkey:
      snprintf(msg, sizeof(msg), "Sent: %s", k.label);
      logAction(k, i, "hotkey");
      showToast(msg);
      break;

    case Act::Toggle:
      toggled[page][i] = !toggled[page][i];
      snprintf(msg, sizeof(msg), "%s: %s", k.label, toggled[page][i] ? k.onLabel : "off");
      logAction(k, i, toggled[page][i] ? "toggle on" : "toggle off");
      showToast(msg);
      break;

    case Act::Scene: {
      uint8_t prev = activeScene;
      activeScene = k.arg;
      snprintf(msg, sizeof(msg), "Scene: %s", k.label);
      logAction(k, i, "scene");
      showToast(msg);
      for (uint8_t j = 0; j < NUM_KEYS; j++) {
        const Key &o = PAGES[page].keys[j];
        if (o.act == Act::Scene && o.arg == prev && j != i) drawKey(j, false);
      }
      break;
    }

    case Act::Counter:
      counter++;
      logAction(k, i, "counter");
      break;

    case Act::Page:
      parentPage = page;
      page = k.arg;
      logAction(k, i, "open page");
      toast[0] = '\0';
      drawPage();
      return;

    case Act::Back:
      page = parentPage;
      parentPage = 0;
      logAction(k, i, "back");
      toast[0] = '\0';
      drawPage();
      return;

    case Act::BrightDown:
    case Act::BrightUp:
      brightness = constrain((int)brightness + (k.act == Act::BrightUp ? 10 : -10), 10, 100);
      Ui::setBacklight(brightness);
      for (uint8_t j = 0; j < NUM_KEYS; j++)
        if (PAGES[page].keys[j].act == Act::BrightShow) drawKey(j, false);
      break;

    case Act::BrightShow: break;

    case Act::BeepToggle:
      beepOn = !beepOn;
      showToast(beepOn ? "Beep on" : "Beep off");
      break;

    case Act::DimToggle:
      autoDim = !autoDim;
      showToast(autoDim ? "Auto-dim on" : "Auto-dim off");
      break;

    case Act::Calibrate:
      Touch::runCalibration();
      drawPage();
      return;

    case Act::DisplayTest:
      displayTest();
      drawPage();
      return;

    case Act::Info:
      infoScreen();
      drawPage();
      return;
  }
  drawKey(i, false);
}

void wake()
{
  lastActivity = millis();
  if (dimmed) {
    dimmed = false;
    Ui::setBacklight(brightness);
  }
}

// Touch state machine.
//  - A touch is active while valid readings arrive, and is released after
//    RELEASE_POLLS polls in a row without one (PENIRQ alone is not trusted).
//  - The first readings of a resistive touch are skewed while pressure builds,
//    so the key is chosen only once SETTLE_POLLS readings agree within
//    SETTLE_PX. Sliding off a key for CANCEL_POLLS readings cancels the press.
#define RELEASE_POLLS 4  // x 8 ms
#define SETTLE_POLLS  3
#define SETTLE_PX     30
#define CANCEL_POLLS  3

uint8_t settleCount = 0, offCount = 0;
int16_t settleX = 0, settleY = 0;

void release()
{
  touchActive = false;
  missCount = 0;
  int8_t k = pressedKey;
  pressedKey = -1;
  bool ignore = ignoreUntilRelease;
  ignoreUntilRelease = false;
  if (k < 0) return;
  Link::sendKey(eventPage(), k, ignore ? DECK_KEY_CANCELLED : DECK_KEY_RELEASED);
  if (hostMode) drawKey(k, false);
  else if (!ignore) trigger(k);
}

void pollTouch()
{
  int16_t x = 0, y = 0;
  bool pen = Touch::isPressed();
  bool valid = pen && Touch::read(x, y);
  dbgPolls++;
  dbgPen = pen;

  if (!valid) {
    if (pen) dbgFails++;
    if (touchActive && ++missCount >= RELEASE_POLLS) release();
    return;
  }

  missCount = 0;
  dbgX = x;
  dbgY = y;
  lastActivity = millis();

  if (!touchActive) {  // new touch: start settling
    touchActive = true;
    settleCount = 1;
    offCount = 0;
    settleX = x;
    settleY = y;
    if (dimmed) {  // first touch only wakes the screen
      ignoreUntilRelease = true;
    }
    wake();
    return;
  }

  if (ignoreUntilRelease) return;

  if (pressedKey < 0) {  // still settling
    if (abs(x - settleX) <= SETTLE_PX && abs(y - settleY) <= SETTLE_PX) settleCount++;
    else settleCount = 1;
    settleX = x;
    settleY = y;
    if (settleCount < SETTLE_POLLS) return;

    int8_t k = keyAt(x, y);
    if (logOut) {
      logOut->print("[touch] press x=");
      logOut->print(x);
      logOut->print(" y=");
      logOut->print(y);
      logOut->print(" key=");
      logOut->println(k);
    }
    if (k >= 0 && (hostMode || PAGES[page].keys[k].act != Act::None)) {
      pressedKey = k;
      Link::sendKey(eventPage(), k, DECK_KEY_PRESSED);
      click();
      drawKey(k, true);
    } else {
      settleCount = 0;  // status bar / empty slot: keep looking while held
    }
    return;
  }

  if (keyAt(x, y) != pressedKey) {  // slid off: cancel after a few readings
    if (++offCount >= CANCEL_POLLS) {
      Link::sendKey(eventPage(), pressedKey, DECK_KEY_CANCELLED);
      drawKey(pressedKey, false);
      pressedKey = -1;
      ignoreUntilRelease = true;
    }
  } else {
    offCount = 0;
  }
}

// Knob (rotary encoder), per page like a Stream Deck+ dial:
//   Main / Media: turn = volume, press = mute (Main) / play-pause (Media)
//   Settings:     turn = brightness, press = back
//   any page:     long press = home
#define PAGE_MAIN       0
#define PAGE_MEDIA      1
#define PAGE_SETTINGS   2
#define MEDIA_PLAY_KEY  2
#define MEDIA_SOUND_KEY 6

uint8_t volume = 50;  // virtual level until the host reports it back

void setToggle(uint8_t pg, uint8_t key, bool on)
{
  toggled[pg][key] = on;
  if (page == pg) drawKey(key, false);
}

void logDial(const char *what)
{
  if (!logOut) return;
  logOut->print("[dial] ");
  logOut->println(what);
}

void pollDial()
{
  int16_t steps = Encoder::takeSteps();
  bool pressed = Encoder::takeClick();
  bool held = Encoder::takeLongPress();
  if (!steps && !pressed && !held) return;
  wake();

  uint8_t pg = eventPage();
  if (steps) Link::sendDial(pg, (int8_t)constrain(steps, -127, 127));
  if (pressed) Link::sendDialButton(pg, DECK_DIAL_CLICK);
  if (held) Link::sendDialButton(pg, DECK_DIAL_LONG);
  if (hostMode) {  // the host decides what the knob does
    if (pressed || held) click();
    return;
  }

  char msg[32];
  if (held) {
    if (page != PAGE_MAIN) {
      page = PAGE_MAIN;
      parentPage = 0;
      toast[0] = '\0';
      drawPage();
    }
    click();
    logDial("home");
    showToast("Home");
    return;
  }

  if (steps) {
    if (page == PAGE_SETTINGS) {
      brightness = constrain((int)brightness + steps * 5, 10, 100);
      Ui::setBacklight(brightness);
      for (uint8_t j = 0; j < NUM_KEYS; j++)
        if (PAGES[page].keys[j].act == Act::BrightShow) drawKey(j, false);
      snprintf(msg, sizeof(msg), "Brightness %u%%", brightness);
    } else {
      volume = constrain((int)volume + steps * 2, 0, 100);
      if (toggled[PAGE_MEDIA][MEDIA_SOUND_KEY]) setToggle(PAGE_MEDIA, MEDIA_SOUND_KEY, false);  // turning unmutes
      snprintf(msg, sizeof(msg), "Volume %u%%", volume);
      char what[24];
      snprintf(what, sizeof(what), "volume %+d -> %u", steps * 2, volume);
      logDial(what);
    }
    showToast(msg);
  }

  if (pressed) {
    click();
    if (page == PAGE_SETTINGS) {
      page = parentPage;
      parentPage = 0;
      toast[0] = '\0';
      drawPage();
      logDial("back");
    } else if (page == PAGE_MEDIA) {
      bool playing = !toggled[PAGE_MEDIA][MEDIA_PLAY_KEY];
      setToggle(PAGE_MEDIA, MEDIA_PLAY_KEY, playing);
      logDial(playing ? "play" : "pause");
      showToast(playing ? "Playing" : "Paused");
    } else {
      bool muted = !toggled[PAGE_MEDIA][MEDIA_SOUND_KEY];
      setToggle(PAGE_MEDIA, MEDIA_SOUND_KEY, muted);
      logDial(muted ? "mute" : "unmute");
      showToast(muted ? "Muted" : "Unmuted");
    }
  }
}

uint8_t eventPage() { return hostMode ? DECK_PAGE_HOST : page; }

void clearHostKeys()
{
  for (HostKey &h : hostKeys) {
    h.bg = COL_EMPTY;
    h.fg = Color::White;
    h.label[0] = '\0';
    h.image = false;
  }
}

void cancelTouch()  // page content changed under the finger
{
  if (pressedKey >= 0) Link::sendKey(eventPage(), pressedKey, DECK_KEY_CANCELLED);
  pressedKey = -1;
  if (touchActive) ignoreUntilRelease = true;
}

}  // namespace

namespace Ui {

void setBacklight(uint8_t percent)
{
  analogWrite(LCD_BACKLIGHT_PIN, (uint32_t)percent * 255 / 100);
}

void onLinkStatus(bool online, uint8_t flags)
{
  bool host = online && (flags & DECK_STATUS_HOST_APP);
  bool redrawBar = online != linkOnline || flags != linkFlags;
  linkOnline = online;
  linkFlags = flags;

  if (host != hostMode) {
    cancelTouch();
    hostMode = host;
    if (hostMode) {
      clearHostKeys();        // a new host session sends its own keys
      Link::requestRefresh();  // ... so ask for all of them now
    }
    toast[0] = '\0';
    drawPage();
    if (logOut) logOut->println(hostMode ? "[deck] host mode" : "[deck] local mode");
  } else if (redrawBar) {
    drawStatusBar();
  }
}

void onSound(uint8_t level)
{
  bool on = level != 0;
  if (on == beepOn) return;  // the host resends it on every attach: stay quiet then
  beepOn = on;
  showToast(beepOn ? "Beep on" : "Beep off");
  if (!hostMode && page == 2)
    for (uint8_t j = 0; j < NUM_KEYS; j++)
      if (PAGES[page].keys[j].act == Act::BeepToggle) drawKey(j, false);
}

void onBrightness(uint8_t percent)
{
  brightness = constrain(percent, 5, 100);
  if (!dimmed) setBacklight(brightness);
  if (!hostMode && page == 2)
    for (uint8_t j = 0; j < NUM_KEYS; j++)
      if (PAGES[page].keys[j].act == Act::BrightShow) drawKey(j, false);
}

void onSetKey(uint8_t key, uint16_t bg, uint16_t fg, const char *label, uint16_t len)
{
  if (key >= NUM_KEYS) return;
  HostKey &h = hostKeys[key];
  h.bg = bg;
  h.fg = fg;
  h.image = false;
  len = min<uint16_t>(len, sizeof(h.label) - 1);
  memcpy(h.label, label, len);
  h.label[len] = '\0';
  if (hostMode) drawKey(key, pressedKey == key);
}

void onClearKeys()
{
  clearHostKeys();
  if (hostMode)
    for (uint8_t i = 0; i < NUM_KEYS; i++) drawKey(i, false);
}

// Key images are assembled in RAM and drawn in one go once the last pixel
// arrives, so a key never shows a half-received image. A frame that doesn't
// continue where the previous one ended (lost data) drops the image; the key
// keeps what it showed before.
namespace {

uint16_t imgBuf[HOST_IMG * HOST_IMG];
int8_t imgKey = -1;     // key being assembled, -1 = none
uint16_t imgNext = 0;   // next expected pixel offset

// Returns false if this frame doesn't belong to the image being assembled.
bool imgFrame(uint8_t key, uint16_t offset)
{
  if (key >= NUM_KEYS || !hostMode) return false;
  if (offset == 0) {
    imgKey = key;
    imgNext = 0;
  }
  if (imgKey != key || offset != imgNext) {
    imgKey = -1;
    return false;
  }
  return true;
}

void imgFinish()
{
  if (imgNext < HOST_IMG * HOST_IMG) return;
  uint8_t key = imgKey;
  imgKey = -1;
  hostKeys[key].image = true;

  // Round the image's corners like the keys: the image sits 2 px inside the
  // key, so its corner radius is the key's minus 2.
  const int16_t margin = (KEY_SIZE - HOST_IMG) / 2, r = KEY_R - margin;
  for (int16_t row = 0; row < HOST_IMG; row++) {
    int16_t inset = Lcd::roundInset(row, HOST_IMG, r);
    for (int16_t i = 0; i < inset; i++) {
      imgBuf[row * HOST_IMG + i] = COL_BG;
      imgBuf[row * HOST_IMG + HOST_IMG - 1 - i] = COL_BG;
    }
  }

  int16_t x = keyX(key), y = keyY(key);
  Lcd::drawRect(x, y, KEY_SIZE, KEY_SIZE, COL_BG, margin);
  Lcd::drawImage(x + margin, y + margin, HOST_IMG, HOST_IMG, imgBuf);
  if (pressedKey == key) Lcd::strokeRoundRect(x, y, KEY_SIZE, KEY_SIZE, KEY_R, Color::White, margin);
}

}  // namespace

void onKeyImage(uint8_t key, uint16_t offset, const uint8_t *pixels, uint16_t count)
{
  if (!imgFrame(key, offset)) return;
  for (uint16_t i = 0; i < count && imgNext < HOST_IMG * HOST_IMG; i++, pixels += 2)
    imgBuf[imgNext++] = (uint16_t)(pixels[0] | (pixels[1] << 8));
  imgFinish();
}

void onKeyImageRle(uint8_t key, uint16_t offset, const uint8_t *runs, uint16_t len)
{
  if (!imgFrame(key, offset)) return;
  for (uint16_t i = 0; i + 3 <= len; i += 3) {
    uint8_t n = runs[i];
    uint16_t color = (uint16_t)(runs[i + 1] | (runs[i + 2] << 8));
    while (n-- && imgNext < HOST_IMG * HOST_IMG) imgBuf[imgNext++] = color;
  }
  imgFinish();
}

void begin(Print &out)
{
  logOut = &out;
  memset(toggled, 0, sizeof(toggled));
  clearHostKeys();
  setBacklight(brightness);
  lastActivity = millis();
  drawPage();
  logOut->println("[deck] ui ready");
}

void loop()
{
  uint32_t now = millis();
  if (now - lastPoll >= 8) {
    lastPoll = now;
    pollTouch();
  }
  pollDial();

#if UI_DEBUG
  if (now - dbgDrawn >= 250) {
    dbgDrawn = now;
    char a[28], b[28];
    const Touch::ReadStats &rs = Touch::lastRead();
    snprintf(a, sizeof(a), "#%03lu P%d A%d K%d s%u I%d", (unsigned long)(dbgPolls % 1000), dbgPen ? 1 : 0,
             touchActive ? 1 : 0, pressedKey, settleCount, ignoreUntilRelease ? 1 : 0);
    snprintf(b, sizeof(b), "%03d,%03d E%u z%u n%u F%lu", dbgX, dbgY, rs.error, rs.z, rs.spread,
             (unsigned long)(dbgFails % 100));
    Lcd::fillRect(LCD_WIDTH - 152, 0, 152, BAR_H, COL_BAR);
    Lcd::drawText(LCD_WIDTH - 150, 4, a, 1, dbgPen ? Color::Green : COL_TEXT_DIM, COL_BAR);
    Lcd::drawText(LCD_WIDTH - 150, 16, b, 1, COL_TEXT_DIM, COL_BAR);
  }
#endif

#if !UI_DEBUG
  // Link diagnostic while offline: bytes received on the bridge UART, UART
  // errors (FE framing, NE noise, OR overrun), "lo" if the RX pin sits low.
  static uint32_t linkDiagDrawn = 0;
  if (!linkOnline && now - linkDiagDrawn >= 500) {
    linkDiagDrawn = now;
    uint8_t e = Bridge.errors();
    char s[24];
    snprintf(s, sizeof(s), "rx%lu%s%s%s%s", (unsigned long)(Bridge.rxBytes() % 100000), (e & BridgeUart::DIAG_FE) ? " FE" : "",
             (e & BridgeUart::DIAG_NE) ? " NE" : "", (e & BridgeUart::DIAG_ORE) ? " OR" : "",
             Bridge.rxPinHigh() ? "" : " lo");
    Lcd::fillRect(276, 0, 120, BAR_H, COL_BAR);
    Lcd::drawText(280, 10, s, 1, Color::Yellow, COL_BAR);
  }
#endif

  if (toast[0] && (int32_t)(now - toastUntil) >= 0) {
    toast[0] = '\0';
    drawStatusBar();
  }

  if (autoDim && !dimmed && (int32_t)(millis() - lastActivity) > DIM_AFTER_MS) {
    dimmed = true;
    setBacklight(BACKLIGHT_DIM);
  }
}

}  // namespace Ui
