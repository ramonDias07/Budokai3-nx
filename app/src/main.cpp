#include "graphics.h"
#include "main_menu_hint.h"
#include "main_menu_legend_data.h"
#include "ptbr_overlay.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iterator>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <fmt/format.h>
#include <switch.h>
#include <rex/audio/switch/switch_audio_system.h>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/image_info.h>
#include <rex/input/filtro_mando.h>
#include <rex/logging.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system/achievement_manager.h>
#include <rex/system/kernel_state.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/overlay/achievement_notification.h>
#include <rex/ui/superposicion.h>
#include <rex/ui/window_switch.h>

extern const rex::PPCImageInfo PPCImageConfig;
REXCVAR_DECLARE(bool, switch_atajo_perfil_app);
REXCVAR_DECLARE(bool, switch_contador_fps);
REXCVAR_DECLARE(int32_t, b3_velocidade_jogo);
REXCVAR_DECLARE(int32_t, b3_limite_fps_luta);
REXCVAR_DECLARE(bool, b3_gpu_boost_ram1600);
REXCVAR_DECLARE(int32_t, b3_deko_resolucao_interna);
REXCVAR_DECLARE(int32_t, b3_deko_resolucao_saida);
REXCVAR_DECLARE(int32_t, b3_deko_ampliador);
REXCVAR_DECLARE(int32_t, audio_switch_tramas_en_cola);
REXCVAR_DECLARE(bool, audio_switch_ocultar_faltas);
REXCVAR_DEFINE_BOOL(b3_deko_musica_independente, true, "Budokai 3",
                    "Musica em tempo real. Desligue para comparar com a mistura da 0.1.32. Requer reiniciar.");
REXCVAR_DEFINE_INT32(b3_audio_atraso_ms, 0, "Budokai 3", "Atraso da mistura de efeitos em milissegundos.");

REXCVAR_DEFINE_INT32(b3_tempo_jogo_fps, 45, "Budokai 3",
                     "Tempo de jogo: 60 (original), 55, 50, 45, 40, 35 ou 30 FPS. 60 = b3_velocidade_jogo 0; outro = tempo real "
                     "com teto nesse FPS na luta. Ao vivo.");
REXCVAR_DEFINE_STRING(b3_perfil, "", "Budokai 3",
                      "Perfil ativo: default, performance ou nooverclock. Vazio (config antiga) = default.");
REXCVAR_DEFINE_STRING(b3_perfil_default, "", "Budokai 3",
                      "Ajustes salvos do perfil DEFAULT sobre os valores de fabrica: chave=valor;chave=valor.");
REXCVAR_DEFINE_STRING(b3_perfil_performance, "", "Budokai 3",
                      "Ajustes salvos do perfil PERFORMANCE sobre os valores de fabrica: chave=valor;chave=valor.");
REXCVAR_DEFINE_STRING(b3_perfil_nooverclock, "", "Budokai 3",
                      "Ajustes salvos do perfil NO OVERCLOCK sobre os valores de fabrica: chave=valor;chave=valor.");
REXCVAR_DEFINE_BOOL(b3_menu_legend, true, "Interface",
                    "Shows the quick menu shortcut ((+)+(-) : Quick Menu) under the game's own hint on the main menu.");
REXCVAR_DEFINE_BOOL(b3_traducao_ptbr, false, "Interface",
                    "Brazilian Portuguese fan translation (PS2), by TheRalf, read from traducao/ptbr.b3tr (added by "
                    "install.bat; see docs/traducao-ptbr.md). Live: applies from the next screen the game loads.");
REXCVAR_DEFINE_INT32(b3_traducao_ptbr_max_growth_mb, 0, "Interface",
                     "0 = use every translated entry. N > 0 = leave out (keep English) the entries that are more than N MB "
                     "bigger than the original entry; each load of one costs that much extra game memory for a moment. "
                     "Read at startup.");

// Build 0.1.51: the legend drawn under the game's "BACK : Help" hint on the main menu. The detection and the per-swap
// show/hide live in graphics.cpp; here the image is built and placed once (guest 1280x720 pixels, integer coordinates).
namespace b3 {
void SetMenuLegendLanguage(bool pt) {
  static std::mutex mutex;
  static rex::ui::IconoCompartido images[2];
  std::lock_guard lock(mutex);
  const int index = pt ? 1 : 0;
  if (!images[index]) {
    auto image = std::make_shared<rex::ui::IconoSuperposicion>();
    image->id = 0xB3E6E001u + uint32_t(index);
    image->ancho = pt ? legend::kPtWidth : legend::kEnWidth;
    image->alto = pt ? legend::kPtHeight : legend::kEnHeight;
    const uint8_t* data = pt ? legend::kPtRgba : legend::kEnRgba;
    image->rgba.assign(data, data + size_t(image->ancho) * image->alto * 4u);
    images[index] = std::move(image);
  }
  const float right = pt ? legend::kPtGuestRight : legend::kEnGuestRight;
  const float center_y = pt ? legend::kPtGuestCenterY : legend::kEnGuestCenterY;
  const auto& image = images[index];
  const float x = std::round(right - float(image->ancho));
  const float y = std::round(center_y - float(image->alto) / 2.0f);
  rex::ui::FijarCalcomania(image, x, y, float(image->ancho), float(image->alto));
  REXLOG_INFO("[B3 menu legend] image={} {}x{} at {},{}", pt ? "PT" : "EN", image->ancho, image->alto, x, y);
}
}  // namespace b3


namespace {
constexpr char kProfilesBind[] = "bind_b3_deko_profiles";
constexpr char kSettingsBind[] = "bind_b3_deko_settings";
constexpr int32_t kDelayStepMs = 5;

enum class Menu { kClosed, kQuick, kAdvanced, kAchievements };

struct MenuShortcut {
  uint64_t chord;
  Menu menu;
};

constexpr MenuShortcut kMenuShortcuts[] = {
    {HidNpadButton_Plus | HidNpadButton_Minus, Menu::kQuick},
    {HidNpadButton_L | HidNpadButton_R | HidNpadButton_Right, Menu::kAdvanced},
    {HidNpadButton_L | HidNpadButton_R | HidNpadButton_Left, Menu::kAchievements},
};

int NextIndex(int index, int size, int direction) {
  return (index + size + direction) % size;
}

std::string MenuText(std::string_view source, size_t max_columns) {
  std::string text(source);
  std::ranges::transform(text, text.begin(), [](char c) {
    return c >= 'a' && c <= 'z' ? char(c - 'a' + 'A') : c;
  });
  if (text.size() > max_columns) text.replace(max_columns - 3, std::string::npos, "...");
  return text;
}

// First row of a scrolling window of `visible` rows that keeps `row` inside it.
int WindowStart(int row, int count, int visible) {
  return std::clamp(row - visible / 2, 0, std::max(0, count - visible));
}

// ---------------------------------------------------------------------------------------------------------------------
// Profiles. The cvars are always the truth; options_ mirrors them for the renderer. A profile is its factory values plus
// the overrides saved in b3_perfil_<profile> ("key=value;key=value"). DOCK is an overlay (internal 720, output 1080).
// ---------------------------------------------------------------------------------------------------------------------
enum Key { kTempo, kInterna, kSaida, kSombras, kDesfoque, kBloom, kDof, kBoost, kCopias, kZ, kRam, kKeyCount };

struct ProfileKey {
  const char* id;
  const char* cvar;
};

constexpr ProfileKey kKeys[kKeyCount] = {
    {"tempo", "b3_tempo_jogo_fps"},
    {"interna", "b3_deko_resolucao_interna"},
    {"saida", "b3_deko_resolucao_saida"},
    {"sombras", "b3_deko_sombras_projetadas"},
    {"desfoque", "b3_deko_desfoque_leve"},
    {"bloom", "b3_deko_bloom"},
    {"dof", "b3_deko_desfoque_resolucao"},
    {"boost", "b3_gpu_boost"},
    {"copias", "b3_deko_reduzir_copias"},
    {"z", "b3_deko_restaurar_z"},
    {"ram", "b3_gpu_boost_ram1600"},
};

struct ProfileDef {
  const char* id;
  const char* name;
  const char* cvar;
  const char* help;
  const char* factory[kKeyCount];
};

//                                   tempo   interna saida   sombras  desfoque bloom    dof    boost    copias  z
constexpr ProfileDef kProfiles[] = {
    {"default", "DEFAULT", "b3_perfil_default",
     "BALANCED LOOK AND SPEED: 45 FPS, FIGHTER SHADOWS ON, NO BLOOM, 720P, GPU BOOST IN HANDHELD",
     {"45", "720", "1080", "true", "false", "false", "720", "true", "true", "true", "true"}},
    {"performance", "PERFORMANCE", "b3_perfil_performance",
     "LIGHTER LOAD: 40 FPS, AUTO INTERNAL RESOLUTION, NO SHADOWS OR BLOOM, LIGHTWEIGHT BLUR, GPU BOOST IN HANDHELD. USE IT IF DEFAULT DROPS FRAMES",
     {"40", "0", "1080", "false", "true", "false", "720", "true", "true", "true", "true"}},
    {"nooverclock", "NO OVERCLOCK", "b3_perfil_nooverclock",
     "FOR STOCK CLOCKS: AUTO INTERNAL RESOLUTION, 720P OUTPUT, 35 FPS, NO SHADOWS OR BLOOM, NO GPU BOOST",
     {"35", "0", "720", "false", "true", "false", "720", "false", "true", "true", "false"}},
};
constexpr int kProfileCount = int(std::size(kProfiles));

using Overrides = std::vector<std::pair<std::string, std::string>>;

Overrides ParseOverrides(std::string_view text) {
  Overrides result;
  while (!text.empty()) {
    const size_t end = text.find(';');
    const std::string_view item = text.substr(0, end);
    const size_t eq = item.find('=');
    if (eq != std::string_view::npos && eq > 0) {
      result.emplace_back(std::string(item.substr(0, eq)), std::string(item.substr(eq + 1)));
    }
    if (end == std::string_view::npos) break;
    text.remove_prefix(end + 1);
  }
  return result;
}

std::string JoinOverrides(const Overrides& overrides) {
  std::string text;
  for (const auto& [key, value] : overrides) {
    if (!text.empty()) text += ';';
    text += key + "=" + value;
  }
  return text;
}

rex::cvar::Source SourceOf(std::string_view name) {
  for (const auto& entry : rex::cvar::GetRegistry()) {
    if (entry.name == name) return entry.source;
  }
  return rex::cvar::Source::kDefault;
}

class Profiles {
 public:
  explicit Profiles(b3::DekoOptions& options) : options_(options) {}

  static bool IsDocked() { return appletGetOperationMode() == AppletOperationMode_Console; }
  bool Docked() const { return docked_; }
  int Active() const { return active_; }

  // Called before the renderer reads the cvars: migrates old configs, applies the active profile and the dock overlay.
  void Startup() {
    if (SourceOf("b3_tempo_jogo_fps") == rex::cvar::Source::kDefault) {
      const int speed = REXCVAR_GET(b3_velocidade_jogo);
      const int limit = REXCVAR_GET(b3_limite_fps_luta);
      const int tempo = speed == 0 ? 60 : (limit == 55 || limit == 50 || limit == 45 || limit == 40 || limit == 35 || limit == 30) ? limit : 45;
      REXCVAR_SET(b3_tempo_jogo_fps, tempo);
    }
    int active = Find(rex::cvar::GetFlagByName("b3_perfil"));
    if (active < 0) {
      // Config from before the profiles: keep what it explicitly set as overrides of DEFAULT.
      Overrides overrides;
      for (int k = 0; k < kKeyCount; ++k) {
        const std::string value = Current(k);
        const bool set = k == kTempo || SourceOf(kKeys[k].cvar) == rex::cvar::Source::kConfig;
        if (set && value != kProfiles[0].factory[k]) overrides.emplace_back(kKeys[k].id, value);
      }
      rex::cvar::SetFlagByName(kProfiles[0].cvar, JoinOverrides(overrides));
      active = 0;
      REXLOG_INFO("[B3 perfil] config antiga: perfil DEFAULT com {} ajustes", overrides.size());
    }
    Select(active);
    docked_ = IsDocked();
    if (docked_) ApplyDock();
    REXLOG_INFO("[B3 perfil] ativo={} dock={}", kProfiles[active_].id, docked_ ? "sim" : "nao");
  }

  // Dock detection, polled from the pad filter (every ~250 ms).
  void Poll() {
    const auto now = std::chrono::steady_clock::now();
    if (now - last_poll_ < std::chrono::milliseconds(250)) return;
    last_poll_ = now;
    const bool docked = IsDocked();
    if (docked == docked_) return;
    docked_ = docked;
    ApplyDock();
    REXLOG_INFO("[B3 perfil] dock={}", docked_ ? "sim" : "nao");
  }

  void Select(int index) {
    active_ = index;
    rex::cvar::SetFlagByName("b3_perfil", kProfiles[index].id);
    for (int k = 0; k < kKeyCount; ++k) Set(k, Effective(index, k));
    if (docked_) ApplyDock();
  }

  std::string Current(int key) const { return rex::cvar::GetFlagByName(kKeys[key].cvar); }

  // Live change of one profile value (cvar + renderer options).
  void Set(int key, const std::string& value) {
    if (key == kTempo) {
      const int fps = std::atoi(value.c_str());
      REXCVAR_SET(b3_tempo_jogo_fps, fps);
      REXCVAR_SET(b3_velocidade_jogo, fps == 60 ? 0 : 1);
      REXCVAR_SET(b3_limite_fps_luta, fps);
    } else if (key == kInterna) {
      b3::PedirResolucaoInterna(std::atoi(value.c_str()));
    } else {
      rex::cvar::SetFlagByName(kKeys[key].cvar, value);
    }
    Sync();
  }

  // Mirrors the bool cvars into the renderer's option bits.
  void Sync() {
    auto on = [](const char* name) { return rex::cvar::GetFlagByName(name) == "true"; };
    const uint32_t options = (on("b3_deko_reduzir_copias") ? uint32_t(b3::kReduzirCopias) : 0u) |
                             (on("b3_deko_desfoque_leve") ? uint32_t(b3::kDesfoqueLeve) : 0u) |
                             (on("b3_deko_bloom") ? uint32_t(b3::kBloom) : 0u) |
                             (on("b3_deko_restaurar_z") ? uint32_t(b3::kRestaurarZ) : 0u) |
                             (on("b3_deko_sombras_projetadas") ? uint32_t(b3::kSombrasProjetadas) : 0u) |
                             b3::DofResolutionOption(std::atoi(Current(kDof).c_str()));
    options_.store(options, std::memory_order_relaxed);
  }

  // Stores what differs from the factory values into the active profile, then writes the config file.
  void SaveActive(const std::filesystem::path& config_path) {
    const auto previous = ParseOverrides(rex::cvar::GetFlagByName(kProfiles[active_].cvar));
    Overrides overrides;
    for (int k = 0; k < kKeyCount; ++k) {
      std::string value = Current(k);
      if (docked_ && (k == kInterna || k == kSaida)) {
        // The dock overlay owns these two while docked: keep the handheld value already stored.
        const auto old = std::ranges::find(previous, std::string_view(kKeys[k].id), &Overrides::value_type::first);
        if (old == previous.end()) continue;
        value = old->second;
      }
      if (value != kProfiles[active_].factory[k]) overrides.emplace_back(kKeys[k].id, value);
    }
    rex::cvar::SetFlagByName(kProfiles[active_].cvar, JoinOverrides(overrides));
    rex::cvar::SetFlagByName("b3_perfil", kProfiles[active_].id);
    rex::cvar::SaveConfig(config_path);
  }

  void RestoreDefaults(const std::filesystem::path& config_path) {
    for (const auto& profile : kProfiles) rex::cvar::SetFlagByName(profile.cvar, "");
    Select(0);
    rex::cvar::SaveConfig(config_path);
  }

 private:
  static int Find(std::string_view id) {
    for (int i = 0; i < kProfileCount; ++i) {
      if (id == kProfiles[i].id) return i;
    }
    return -1;
  }

  std::string Effective(int profile, int key) const {
    for (const auto& [id, value] : ParseOverrides(rex::cvar::GetFlagByName(kProfiles[profile].cvar))) {
      if (id == kKeys[key].id) return value;
    }
    return kProfiles[profile].factory[key];
  }

  void ApplyDock() {
    if (docked_) {
      Set(kInterna, "720");
      Set(kSaida, "1080");
    } else {
      Set(kInterna, Effective(active_, kInterna));
      Set(kSaida, Effective(active_, kSaida));
    }
  }

  b3::DekoOptions& options_;
  int active_ = 0;
  bool docked_ = false;
  std::chrono::steady_clock::time_point last_poll_{};
};

// ---------------------------------------------------------------------------------------------------------------------
// Quick menu
// ---------------------------------------------------------------------------------------------------------------------
class PortMenu {
 public:
  PortMenu(Profiles& profiles, std::filesystem::path config_path)
      : profiles_(profiles), config_path_(std::move(config_path)) {}

  void Open() {
    music_independent_ = REXCVAR_GET(b3_deko_musica_independente);
    confirm_ = false;
    Show();
  }

  bool Press(uint64_t buttons) {
    const bool confirmed = confirm_ && row_ == kRestore && buttons == HidNpadButton_A;
    confirm_ = false;
    if (buttons & HidNpadButton_Up) row_ = NextIndex(row_, kRows, -1);
    if (buttons & HidNpadButton_Down) row_ = NextIndex(row_, kRows, 1);
    if (buttons & (HidNpadButton_A | HidNpadButton_Left | HidNpadButton_Right)) {
      if (row_ == kSave) {
        if (buttons & HidNpadButton_A) {
          Save();
          return false;
        }
      } else if (row_ == kRestore) {
        if (buttons & HidNpadButton_A) {
          if (confirmed) {
            profiles_.RestoreDefaults(config_path_);
            music_independent_ = REXCVAR_GET(b3_deko_musica_independente);
          } else {
            confirm_ = true;
          }
        }
      } else {
        Change(buttons & HidNpadButton_Left ? -1 : 1);
      }
    }
    Show();
    return true;
  }

 private:
  enum Row {
    kProfile, kTime, kInternal, kOutput, kUpscaler, kShadows, kBloomRow, kBlur, kBattleBlur, kFps, kMenuLegend, kLanguage,
    kMusic, kDelay, kSave, kRestore, kRows
  };
  static constexpr int kVisible = 13;
  static constexpr std::array<int, 7> kTimes{30, 35, 40, 45, 50, 55, 60};
  static constexpr std::array<int, 4> kInternals{720, 540, 360, 0};
  static constexpr std::array<int, 3> kDofs{720, 540, 360};
  static constexpr std::array<int, 5> kDockInternals{1080, 720, 540, 360, 0};
  static constexpr std::array<int, 5> kDockOutputs{1440, 1080, 900, 720, 540};
  static constexpr std::array<int, 3> kHandheldOutputs{720, 540, 360};
  static constexpr const char* kUpscalers[] = {"LINEAR", "CAS", "FSR1"};

  std::span<const int> Internals() const {
    return profiles_.Docked() ? std::span<const int>(kDockInternals) : std::span<const int>(kInternals);
  }

  std::span<const int> Outputs() const {
    return profiles_.Docked() ? std::span<const int>(kDockOutputs) : std::span<const int>(kHandheldOutputs);
  }

  int OutputHeight() const {
    const auto list = Outputs();
    const int value = std::atoi(rex::cvar::GetFlagByName("b3_deko_resolucao_saida").c_str());
    return std::ranges::find(list, value) == list.end() ? (profiles_.Docked() ? 1080 : list.front()) : value;
  }

  static std::string Resolution(int height) { return fmt::format("{}X{}", height * 16 / 9, height); }

  bool Flag(int key) const { return profiles_.Current(key) == "true"; }
  int Number(int key) const { return std::atoi(profiles_.Current(key).c_str()); }

  std::string Line(int row) const {
    switch (row) {
      case kProfile: return std::string("PROFILE: ") + kProfiles[profiles_.Active()].name + (profiles_.Docked() ? " + DOCK" : "");
      case kTime: {
        const int fps = Number(kTempo);
        return fps == 60 ? "GAME TIME: 60 FPS (ORIGINAL)" : fmt::format("GAME TIME: {} FPS", fps);
      }
      case kInternal: {
        const int height = Number(kInterna);
        return "INTERNAL RESOLUTION: " + (height == 0      ? std::string("AUTO")
                                          : height == 1080 ? std::string("1080P (EXPERIMENTAL)")
                                                           : Resolution(height));
      }
      case kOutput: {
        const int height = OutputHeight();
        if (height == 1440) {
          return std::string("OUTPUT RESOLUTION: ") +
                 (rex::ui::g_saida_1440_recusada.load() ? "1440P (REFUSED, 1080)" : "1440P (EXPERIMENTAL)");
        }
        return "OUTPUT RESOLUTION: " + Resolution(height);
      }
      case kUpscaler: {
        const int mode = std::clamp(REXCVAR_GET(b3_deko_ampliador), 0, 2);
        return std::string("UPSCALER: ") + kUpscalers[mode];
      }
      case kShadows: return std::string("FIGHTER SHADOWS: ") + (Flag(kSombras) ? "YES" : "NO");
      case kBloomRow: return std::string("BLOOM: ") + (Flag(kBloom) ? "YES" : "NO");
      case kBlur: return std::string("LIGHTWEIGHT BLUR: ") + (Flag(kDesfoque) ? "YES" : "NO");
      case kBattleBlur: return fmt::format("BATTLE BLUR: {}P", Number(kDof));
      case kFps: return std::string("FPS COUNTER: ") + (REXCVAR_GET(switch_contador_fps) ? "YES" : "NO");
      case kMenuLegend: return std::string("MAIN MENU LEGEND: ") + (REXCVAR_GET(b3_menu_legend) ? "YES" : "NO");
      case kLanguage:
        switch (b3::ptbr::GetStatus()) {
          case b3::ptbr::Status::kNotInstalled: return "LANGUAGE: ENGLISH (PT-BR NOT INSTALLED)";
          case b3::ptbr::Status::kUnavailable: return "LANGUAGE: ENGLISH (PT-BR UNAVAILABLE)";
          default:
            if (!b3::ptbr::Active()) {
              // Translation off at boot: turning it on only takes effect after a restart.
              return REXCVAR_GET(b3_traducao_ptbr) ? "LANGUAGE: PORTUGUESE (BR) - RESTART" : "LANGUAGE: ENGLISH";
            }
            return std::string("LANGUAGE: ") + (b3::ptbr::Enabled() ? "PORTUGUESE (BR)" : "ENGLISH");
        }
      case kMusic: return std::string("MUSIC: ") + (music_independent_ ? "NORMAL SPEED" : "MATCH GAME TIME") + " (R)";
      case kDelay: return "AUDIO DELAY: " + std::to_string(REXCVAR_GET(b3_audio_atraso_ms)) + " MS";
      case kSave: return "SAVE AND CLOSE";
      default: return confirm_ ? "PRESS A AGAIN TO CONFIRM" : "RESTORE DEFAULTS";
    }
  }

  std::string Help() const {
    switch (row_) {
      case kProfile: {
        std::string text = profiles_.Docked() ? "DOCK ON: INTERNAL 720P AND OUTPUT 1080P ARE SET AUTOMATICALLY. " : "";
        text += kProfiles[profiles_.Active()].help;
        if (!profiles_.Docked()) text += ". DOCK MODE ADDS 720P INTERNAL AND 1080P OUTPUT BY ITSELF";
        return text;
      }
      case kTime:
        return "BATTLES AND THE DRAGON UNIVERSE MAP RUN AT REAL SPEED CAPPED AT THIS FPS. CHOOSE 30, 35, 40, 45, 50, 55 OR 60. 40 FEELS SMOOTH, 30 SAVES THE MOST POWER, 60 KEEPS THE "
               "ORIGINAL TIMING. FRAME RATE CAP 30 TO 60 FPS. LEFT LOWERS, RIGHT RAISES.";
      case kInternal:
        return "SIZE THE GAME DRAWS AT. LOWER IS FASTER BUT SOFTER. AUTO PICKS 720P OR 540P BY GPU LOAD. NEVER ABOVE "
               "THE OUTPUT SIZE. 1080P (EXPERIMENTAL, DOCK ONLY): RENDERS AT 1920X1080, HEAVIER ON GPU; FALLS BACK TO 720 IF "
               "MEMORY IS SHORT.";
      case kOutput:
        return "SIZE OF THE IMAGE SENT TO THE SCREEN, SEPARATE FROM INTERNAL. IF IT IS BIGGER THAN INTERNAL THE "
               "UPSCALER STRETCHES IT. DOCKED 1080P LOOKS BEST. HANDHELD 720P IS NATIVE. 1440P (EXPERIMENTAL, DOCK ONLY): DRAWS 2560X1440; THE SWITCH HDMI SENDS MAX 1080P, "
               "SO IT IS SUPERSAMPLING (SHARPER, HEAVIER). FALLS BACK TO 1080 IF REFUSED.";
      case kUpscaler:
        return "USED WHEN INTERNAL IS LOWER THAN OUTPUT. LINEAR IS SOFT AND CHEAP, CAS SHARPENS (SEE SHARPNESS IN "
               "ADVANCED), FSR1 RESTORES THE MOST DETAIL BUT COSTS MORE GPU.";
      case kShadows:
        return "SHADOWS CAST BY THE FIGHTERS ON THE GROUND. TURN OFF TO SAVE GPU TIME IF BATTLES DROP FRAMES.";
      case kBloomRow:
        return "SOFT GLOW AROUND BRIGHT LIGHTS AND ENERGY. TURN OFF FOR A LITTLE MORE SPEED, THE IMAGE LOOKS FLATTER.";
      case kBlur:
        return "DEPTH BLUR USES 5 SAMPLES INSTEAD OF MORE. FASTER AND SLIGHTLY GRAINIER. RECOMMENDED WITH LOW POWER "
               "OR HOT CONSOLES.";
      case kBattleBlur:
        return "SIZE OF THE BLUR BUFFER IN BATTLES. 540P OR 360P SAVE GPU TIME WITH LITTLE VISIBLE CHANGE.";
      case kFps:
        return "SHOWS THE FPS AND THE WORST FRAME GAP AT THE TOP LEFT. HANDY TO TEST SETTINGS, TURN OFF TO PLAY.";
      case kMenuLegend:
        return "SHOWS THE QUICK MENU SHORTCUT ON THE GAME'S MAIN MENU.";
      case kLanguage: {
        std::string text = "BRAZILIAN PORTUGUESE FAN TRANSLATION (PS2). TURNING IT ON NEEDS A RESTART; WHILE ON, "
                           "SWITCHING LANGUAGE APPLIES FROM THE NEXT SCREEN.";
        if (b3::ptbr::GetStatus() == b3::ptbr::Status::kReady && !b3::ptbr::Active() && REXCVAR_GET(b3_traducao_ptbr)) {
          text += " SAVE AND CLOSE, THEN RESTART THE GAME TO APPLY.";
        }
        if (b3::ptbr::GetStatus() == b3::ptbr::Status::kNotInstalled) {
          text += " NOT INSTALLED: COPY PTBR.B3TR TO /SWITCH/B3DEKO/TRADUCAO/ AND RESTART.";
        } else if (b3::ptbr::GetStatus() == b3::ptbr::Status::kUnavailable) {
          text += " UNAVAILABLE: THE PACK DOES NOT MATCH YOUR GAME FILES, SEE THE LOG.";
        }
        return text;
      }
      case kMusic:
        return music_independent_
                   ? "MUSIC KEEPS ITS OWN TEMPO. RECOMMENDED WITH GAME TIME AT 60 FPS. OTHER SOUNDS MAY DRIFT OUT OF "
                     "SYNC."
                   : "MUSIC FOLLOWS GAME TIME. USE IT IF AUDIO DRIFTS. AT 60 FPS MUSIC MAY SLOW DOWN WITH THE GAME. "
                     "BEST AT 45 FPS OR LESS.";
      case kDelay:
        return "DELAYS THE SOUND EFFECTS MIX IN STEPS OF 5 MS. RAISE IT IF SOUND ARRIVES BEFORE THE PICTURE. 0 IS "
               "THE DEFAULT.";
      case kSave:
        return "STORES THESE OPTIONS IN THE ACTIVE PROFILE AND IN THE CONFIG FILE, THEN CLOSES. MUSIC NEEDS A "
               "RESTART.";
      default:
        return confirm_ ? "PRESS A AGAIN TO CONFIRM. ANY OTHER KEY CANCELS."
                        : "RESETS ALL PROFILES TO FACTORY VALUES AND SELECTS DEFAULT. NEEDS A SECOND PRESS OF A.";
    }
  }

  void Show() const {
    const int start = WindowStart(row_, kRows, kVisible);
    std::vector<std::string> rows;
    rows.push_back(fmt::format("BUDOKAI 3 / PORT MENU  {}/{}", row_ + 1, int(kRows)));
    for (int i = start; i < std::min(start + kVisible, int(kRows)); ++i) {
      rows.push_back(MenuText((i == row_ ? "> " : "  ") + Line(i), 44));
    }
    rows.emplace_back("ARROWS: ADJUST / A: CHANGE / B: CLOSE");
    rex::ui::FijarSuperposicion(std::move(rows));
    rex::ui::FijarLetreiro(Help());
  }

  template <typename Range>
  static int Cycle(const Range& values, int value, int direction) {
    const auto current = std::ranges::find(values, value);
    if (current == std::ranges::end(values)) return *std::ranges::begin(values);
    const int count = int(std::ranges::size(values));
    return values[(int(current - std::ranges::begin(values)) + count + direction) % count];
  }

  void Change(int direction) {
    auto toggle = [&](int key) { profiles_.Set(key, Flag(key) ? "false" : "true"); };
    switch (row_) {
      case kProfile: profiles_.Select(NextIndex(profiles_.Active(), kProfileCount, direction)); break;
      case kTime: profiles_.Set(kTempo, std::to_string(Cycle(kTimes, Number(kTempo), direction))); break;
      case kInternal: profiles_.Set(kInterna, std::to_string(Cycle(Internals(), Number(kInterna), direction))); break;
      case kOutput: profiles_.Set(kSaida, std::to_string(Cycle(Outputs(), OutputHeight(), direction))); break;
      case kUpscaler:
        rex::cvar::SetFlagByName("b3_deko_ampliador",
                                 std::to_string(NextIndex(std::clamp(REXCVAR_GET(b3_deko_ampliador), 0, 2), 3, direction)));
        break;
      case kShadows: toggle(kSombras); break;
      case kBloomRow: toggle(kBloom); break;
      case kBlur: toggle(kDesfoque); break;
      case kBattleBlur: profiles_.Set(kDof, std::to_string(Cycle(kDofs, Number(kDof), direction))); break;
      case kFps: REXCVAR_SET(switch_contador_fps, !REXCVAR_GET(switch_contador_fps)); break;
      case kMenuLegend: REXCVAR_SET(b3_menu_legend, !REXCVAR_GET(b3_menu_legend)); break;
      case kLanguage: {
        if (b3::ptbr::GetStatus() != b3::ptbr::Status::kReady) break;
        if (!b3::ptbr::Active()) {
          // Off at boot: only the saved choice changes (applies after a restart); English again just clears it.
          REXCVAR_SET(b3_traducao_ptbr, !REXCVAR_GET(b3_traducao_ptbr));
          REXLOG_INFO("[ptbr] LANGUAGE set to {} for the next start (translation was off at boot)",
                      REXCVAR_GET(b3_traducao_ptbr) ? "PORTUGUESE (BR)" : "ENGLISH");
          break;
        }
        const bool pt = !b3::ptbr::Enabled();
        REXCVAR_SET(b3_traducao_ptbr, pt);
        b3::ptbr::SetEnabled(pt);
        b3::SetMenuLegendLanguage(pt);
        break;
      }
      case kMusic: music_independent_ = !music_independent_; break;
      case kDelay: {
        const int delay = std::clamp(REXCVAR_GET(b3_audio_atraso_ms) + direction * kDelayStepMs,
                                     0, rex::audio::nx::kMaxOutputDelayMs);
        REXCVAR_SET(b3_audio_atraso_ms, delay);
        rex::audio::nx::SetOutputDelayMs(delay);
        REXLOG_INFO("[B3 audio] atraso={} ms", delay);
        break;
      }
      default: break;
    }
  }

  void Save() {
    REXCVAR_SET(b3_deko_musica_independente, music_independent_);
    profiles_.SaveActive(config_path_);
  }

  Profiles& profiles_;
  std::filesystem::path config_path_;
  int row_ = 0;
  bool confirm_ = false;
  bool music_independent_ = true;
};

// ---------------------------------------------------------------------------------------------------------------------
// Advanced menu: categories (level 1) and their options (level 2)
// ---------------------------------------------------------------------------------------------------------------------
struct SettingChoice {
  std::string_view value;
  std::string_view label;
};

constexpr SettingChoice kBoolean[] = {{"false", "NO"}, {"true", "YES"}};
constexpr SettingChoice kAnisotropy[] = {{"0", "DEFAULT"}, {"2", "2X"}, {"4", "4X"}, {"8", "8X"}};
constexpr SettingChoice kGeometrySize[] = {{"0", "OFF"}, {"32", "32 MB"}, {"64", "64 MB"}, {"96", "96 MB"}};
constexpr SettingChoice kTextureSize[] = {{"384", "384 MB"}, {"512", "512 MB"}, {"640", "640 MB"}};
constexpr SettingChoice kPartialSubmit[] = {{"0", "OFF"}, {"128", "128"}, {"256", "256"}, {"512", "512"}};
constexpr SettingChoice kUploadSize[] = {{"16", "16 MB"}, {"32", "32 MB"}, {"64", "64 MB"}};
constexpr SettingChoice kAudioQueue[] = {{"6", "6"}, {"8", "8"}, {"10", "10"}, {"12", "12"}, {"14", "14"}, {"16", "16"}};
constexpr SettingChoice kAudioFrames[] = {{"4", "4"}, {"8", "8"}, {"16", "16"}, {"32", "32"}};
constexpr SettingChoice kXmaThread[] = {{"0", "REQUESTING THREAD"}, {"1", "XMA THREAD"}, {"2", "WORKER"}};
constexpr SettingChoice kLogLevel[] = {{"info", "INFO"}, {"warn", "WARNINGS"}, {"error", "ERRORS"}};
constexpr SettingChoice kSharpness[] = {{"0", "0"}, {"25", "25"}, {"50", "50"}, {"75", "75"}, {"100", "100"}};
constexpr SettingChoice kShadowBias[] = {{"0", "0"},   {"16", "16"},   {"32", "32"},
                                         {"64", "64"}, {"128", "128"}, {"256", "256"}};
constexpr SettingChoice kRumble[] = {{"0.25", "25%"}, {"0.5", "50%"}, {"0.75", "75%"}, {"1", "100%"}};

struct AdvancedOption {
  const char* label;
  const char* cvar;
  const char* help;
  std::span<const SettingChoice> choices = kBoolean;
  bool restart = false;
};

constexpr AdvancedOption kGraphicsOptions[] = {
    {"SKIP FINAL FXAA", "b3_deko_copia_final",
     "PRESENTS THE IMAGE BEFORE THE GAME'S FINAL FXAA WHEN POSSIBLE. SAVES GPU TIME, EDGES LOOK A BIT ROUGHER.",
     kBoolean, true},
    {"MIPMAPS", "nfsmw_nativo_mipmaps",
     "USES THE GAME'S SMALLER TEXTURE LEVELS FOR DISTANT SURFACES. KEEP ON. OFF IS ONLY FOR TESTS.", kBoolean, true},
    {"ANISOTROPIC FILTERING", "nfsmw_nativo_anisotropico",
     "SHARPENS TEXTURES SEEN AT SHALLOW ANGLES, LIKE THE ARENA FLOOR. 4X IS A GOOD BALANCE, DEFAULT IS FASTEST.",
     kAnisotropy, true},
    {"UPSCALER SHARPNESS", "b3_deko_nitidez",
     "STRENGTH OF CAS SHARPENING, 0 TO 100. ONLY WITH UPSCALER CAS AND INTERNAL BELOW OUTPUT. 50 IS BALANCED.",
     kSharpness},
    {"FETCH INVALID AS TEXTURE", "b3_deko_fetch_invalido",
     "TREATS TEXTURE SLOTS MARKED INVALID AS TEXTURES. NEEDED FOR FIGHTER SHADOWS. LEAVE ON.", kBoolean},
    {"SHADOW DEPTH BIAS", "b3_deko_sombra_sesgo",
     "PULLS THE FIGHTERS' SHADOW TOWARDS THE CAMERA SO IT STOPS FLICKERING IN STRIPES ON THE GROUND. 0 = ORIGINAL BEHAVIOUR. LIVE.",
     kShadowBias},
    {"GAMMA RAMP", "nfsmw_nativo_rampa_gamma",
     "APPLIES THE GAME'S GAMMA CURVE LIKE THE ORIGINAL CONSOLE. OFF MAKES MIDTONES AND SHADOWS DARKER.", kBoolean,
     true},
};
constexpr AdvancedOption kPacingOptions[] = {
    {"VSYNC", "vsync",
     "SYNCS PRESENTATION WITH THE SCREEN. OFF CAN TEAR BUT MAY REDUCE LATENCY. KEEP ON FOR 30 AND 45 FPS."},
    {"RELAXED VSYNC", "b3_deko_vsync_relaxado",
     "WHEN A FRAME IS LATE AT A 30 FPS CAP IT SHOWS AT THE NEXT VBLANK INSTEAD OF WAITING. AVOIDS 33 TO 67 MS SPIKES."},
    {"GAME TIME ON DRAGON UNIVERSE", "b3_tempo_jogo_mundo",
     "APPLIES GAME TIME (REAL SPEED AND FPS CAP) ALSO ON THE DRAGON UNIVERSE MAP, WITH THE SAME VALUE AS BATTLES. TURN OFF IF THE MAP RUNS TOO FAST OR TOO SLOW."},
};
constexpr AdvancedOption kRenderOptions[] = {
    {"REDUCE GPU COPIES", "b3_deko_reduzir_copias",
     "SKIPS A REDUNDANT FINAL DRAW AND THE CPU READ OF THE 80X45 BLOOM. KEEP ON FOR SPEED."},
    {"RESTORE DEPTH", "b3_deko_restaurar_z",
     "RESTORES SCENE DEPTH AFTER A RESOLVE. NEEDED FOR CORRECT EFFECTS. LEAVE ON."},
    {"RESTORE STENCIL", "b3_deko_restaurar_stencil",
     "KEEPS THE STENCIL MASK WHEN THE DEPTH IS SAVED AND RESTORED. LEAVE ON."},
    {"COLOR HANDOVER", "b3_deko_handover_cor",
     "SWAPS IMAGES INSTEAD OF COPYING WHEN A WHOLE COLOR TARGET IS RESOLVED TO AN IDENTICAL TEXTURE. SAVES GPU TIME."},
    {"SKIP REPEATED RESOLVES", "b3_deko_pular_resolve_repetido",
     "SKIPS A RESOLVE IDENTICAL TO THE PREVIOUS ONE WHEN NOTHING CHANGED. SAVES GPU TIME."},
    {"RESOLVE BY SWAP", "nfsmw_nativo_resolver_sin_copia",
     "SWAPS IMAGES INSTEAD OF COPYING PIXELS WHEN RESOLVING A FULL TARGET TO A SAME SIZE TEXTURE. KEEP ON.", kBoolean,
     true},
    {"LAZY FRONT BUFFER", "nfsmw_nativo_frontal_perezoso",
     "THE SWAP DRAWS THE FRONT BUFFER FROM THE RENDER TARGET INSTEAD OF COPYING IT FIRST. SAME IMAGE, A LITTLE FASTER.",
     kBoolean},
    {"COLOR COMPRESSION", "nfsmw_dk_compresion_color", "REDUCES RGBA8 TARGET BANDWIDTH. KEEP ON.", kBoolean, true},
    {"DEPTH COMPRESSION", "nfsmw_dk_compresion_profundidad", "REDUCES DEPTH TARGET BANDWIDTH. KEEP ON.", kBoolean,
     true},
    {"CLEAR USED AREA", "nfsmw_dk_borrar_area_util", "CLEARS ONLY THE ROWS IN USE INSTEAD OF THE WHOLE TARGET."},
    {"DEFER COLOR CLEARS", "nfsmw_dk_borrado_color_aplazado",
     "WAITS UNTIL THE COLOR TARGET IS USED BEFORE CLEARING IT. SAVES WORK WHEN IT IS NEVER DRAWN."},
    {"DEFER DEPTH CLEARS", "nfsmw_dk_borrado_z_aplazado",
     "WAITS UNTIL THE DEPTH TARGET IS USED BEFORE CLEARING IT. SAVES WORK WHEN IT IS NEVER DRAWN."},
    {"PARTIAL COMMAND SUBMISSION", "nfsmw_dk_envio_parcial",
     "DRAW CALLS PER PARTIAL SUBMISSION TO THE GPU. SMALLER LETS THE GPU START EARLIER, 256 IS BALANCED.",
     kPartialSubmit},
    {"UPLOAD BUFFER", "nfsmw_dk_subida_mb",
     "UPLOAD MEMORY PER COMMAND SLOT. 32 MB IS ENOUGH. USE 16 MB IF MEMORY IS SHORT.", kUploadSize, true},
};
constexpr AdvancedOption kGpuOptions[] = {
    {"OFFICIAL GPU BOOST", "b3_gpu_boost",
     "HANDHELD ONLY. ASKS THE SYSTEM FOR ITS OFFICIAL 460 MHZ GPU MODE. DEFAULT AND PERFORMANCE USE IT IN HANDHELD, NO OVERCLOCK DOES NOT. DOES NOTHING DOCKED."},
    {"BOOST WITH RAM 1600", "b3_gpu_boost_ram1600",
     "OFFICIAL HANDHELD MODE GPU 460 + RAM 1600 (USED BY COMMERCIAL GAMES). CLEAN AUDIO AT STOCK CPU. APPLIES LIVE."},
    {"ZCULL", "b3_deko_zcull",
     "REJECTS HIDDEN PIXELS EARLY ON THE GPU. SAVES TIME IN MOST SCENES.", kBoolean, true},
    {"EARLY Z", "nfsmw_nativo_z_temprana", "TESTS DEPTH BEFORE SHADING SO HIDDEN PIXELS COST LESS."},
    {"SKIP INVISIBLE DRAWS", "nfsmw_nativo_saltar_invisibles", "SKIPS DRAWS THAT HAVE NO VISIBLE EFFECT."},
    {"SKIP REPEATED CLEARS", "nfsmw_nativo_saltar_borrados_repetidos", "AVOIDS CLEARING THE SAME AREA TWICE."},
    {"TEXTURE CACHE", "nfsmw_nativo_texturas_mb_max",
     "TEXTURE MEMORY KEPT BEFORE OLD ONES ARE DROPPED. 512 MB IS BALANCED, LOWER IF MEMORY RUNS OUT.", kTextureSize},
    {"GEOMETRY CACHE", "nfsmw_dk_geometria_mb",
     "REUSES VERTICES AND INDICES BETWEEN FRAMES. OFF SAVES MEMORY.", kGeometrySize, true},
    {"SAMPLE GEOMETRY", "nfsmw_dk_geometria_muestreo",
     "CHECKS ONLY PARTS OF CACHED GEOMETRY TO DETECT CHANGES. FASTER.", kBoolean, true},
    {"BACKGROUND SHADER COMPILE", "nfsmw_dk_compilar_en_fondo",
     "COMPILES NEW SHADER VARIANTS IN THE BACKGROUND TO AVOID HITCHES. OFF COMPILES ON THE SPOT.", kBoolean, true},
};
constexpr AdvancedOption kAudioOptions[] = {
    {"MUTE AUDIO", "audio_mute", "TURNS ALL SOUND OFF."},
    {"AUDIO PUMP", "audio_switch_bomba",
     "ASKS THE GAME FOR AUDIO AT REGULAR INTERVALS. KEEP ON TO AVOID ROBOTIC SOUND.", kBoolean, true},
    {"AUDIO QUEUE", "audio_switch_tramas_en_cola",
     "PCM BLOCKS QUEUED BY THE PUMP. MORE AVOIDS CUTS BUT ADDS DELAY. 10 IS BALANCED.", kAudioQueue, true},
    {"AUDIO FRAME LIMIT", "audio_maxqframes",
     "MAX BUFFERED AUDIO FRAMES. LOWER REDUCES DELAY BUT MAY CRACKLE.", kAudioFrames, true},
    {"XMA DECODE THREAD", "audio_xma_en_trabajador",
     "WHO DECODES XMA MUSIC AND VOICES. CHANGE IT ONLY IF AUDIO STUTTERS DURING BATTLES.", kXmaThread, true},
};
constexpr AdvancedOption kControlOptions[] = {
    {"BUTTONS BY POSITION", "input_xbox_layout",
     "MATCHES THE PHYSICAL XBOX BUTTON POSITIONS INSTEAD OF THE LETTERS. TURN ON IF BUTTONS FEEL SWAPPED."},
    {"RUMBLE", "input_vibracion", "ENABLES THE GAME'S RUMBLE. OFF SENDS NO VIBRATION."},
    {"RUMBLE STRENGTH", "input_vibracion_fuerza", "SCALES THE GAME'S RUMBLE. THE SWITCH MOTORS FEEL STRONGER.",
     kRumble},
};
constexpr AdvancedOption kLogOptions[] = {
    {"LOG LEVEL", "log_level", "HOW MUCH THE LOG FILE RECORDS. INFO IS BEST FOR BUG REPORTS.", kLogLevel},
    {"LOG TO FILE", "log_to_file", "WRITES THE LOG TO A FILE ON THE SD CARD. USEFUL FOR BUG REPORTS.", kBoolean, true},
    {"L3 + R3 CAPTURE", "b3_deko_diagnostico",
     "PRESS BOTH STICKS TO CAPTURE A FRAME AND ITS DATA FOR DIAGNOSIS.", kBoolean, true},
    {"FULL CAPTURE", "b3_deko_diagnostico_completo", "ADDS MORE DATA TO FRAME CAPTURES. LARGER FILES."},
};

struct AdvancedCategory {
  const char* name;
  const char* help;
  std::span<const AdvancedOption> options;
};

constexpr AdvancedCategory kCategories[] = {
    {"GRAPHICS", "IMAGE QUALITY: FINAL FILTER, TEXTURE FILTERING, UPSCALER SHARPNESS AND COLOR.", kGraphicsOptions},
    {"FRAME PACING", "HOW FRAMES REACH THE SCREEN: VSYNC AND LATE FRAME HANDLING.", kPacingOptions},
    {"DEKO RENDERING", "DEKO3D RENDERER INTERNALS: COPIES, RESOLVES, CLEARS AND COMMAND SUBMISSION.", kRenderOptions},
    {"GPU AND CACHE", "GPU BOOST, DEPTH CULLING AND TEXTURE, GEOMETRY AND SHADER CACHES.", kGpuOptions},
    {"AUDIO", "SOUND OUTPUT, BUFFERING AND DECODING.", kAudioOptions},
    {"CONTROLS", "BUTTON LAYOUT AND RUMBLE.", kControlOptions},
    {"LOGS AND DIAGNOSTICS", "LOG FILES AND FRAME CAPTURES FOR BUG REPORTS.", kLogOptions},
};
constexpr int kCategoryCount = int(std::size(kCategories));

class AdvancedSettings {
 public:
  AdvancedSettings(Profiles& profiles, std::filesystem::path config_path)
      : profiles_(profiles), config_path_(std::move(config_path)) {}

  void Open() {
    level_ = 1;
    saved_ = false;
    Show();
  }

  // B: back from the options to the categories. Returns false when the menu should close.
  bool Back() {
    if (level_ == 1) return false;
    level_ = 1;
    row_ = category_;
    saved_ = false;
    Show();
    return true;
  }

  bool Press(uint64_t pressed) {
    saved_ = false;
    if (level_ == 1) {
      if (pressed & HidNpadButton_Up) row_ = NextIndex(row_, kCategoryCount + 1, -1);
      if (pressed & HidNpadButton_Down) row_ = NextIndex(row_, kCategoryCount + 1, 1);
      if (pressed & HidNpadButton_A) {
        if (row_ == kCategoryCount) {
          Save();
        } else {
          category_ = row_;
          level_ = 2;
          row_ = 0;
        }
      }
      Show();
      return true;
    }
    if (pressed & (HidNpadButton_L | HidNpadButton_R)) {
      category_ = NextIndex(category_, kCategoryCount, pressed & HidNpadButton_L ? -1 : 1);
      row_ = 0;
    }
    const auto options = kCategories[category_].options;
    const int save = int(options.size());
    if (pressed & HidNpadButton_Up) row_ = NextIndex(row_, save + 1, -1);
    if (pressed & HidNpadButton_Down) row_ = NextIndex(row_, save + 1, 1);
    if (row_ == save) {
      if (pressed & HidNpadButton_A) Save();
    } else if (pressed & (HidNpadButton_A | HidNpadButton_Left | HidNpadButton_Right)) {
      const auto& option = options[row_];
      const int current = ChoiceIndex(option, rex::cvar::GetFlagByName(option.cvar));
      const int next = current < 0 ? 0 : NextIndex(current, int(option.choices.size()), pressed & HidNpadButton_Left ? -1 : 1);
      const auto value = option.choices[next].value;
      if (rex::cvar::SetFlagByName(option.cvar, value)) {
        profiles_.Sync();
        REXLOG_INFO("[B3 Deko menu] {}={}", option.cvar, value);
      }
    }
    Show();
    return true;
  }

 private:
  static bool SameValue(std::string_view a, std::string_view b) {
    if (a == b) return true;
    const std::string left(a), right(b);
    char* end_left = nullptr;
    char* end_right = nullptr;
    const double x = std::strtod(left.c_str(), &end_left);
    const double y = std::strtod(right.c_str(), &end_right);
    return !left.empty() && !right.empty() && *end_left == '\0' && *end_right == '\0' && std::abs(x - y) < 1e-6;
  }

  static int ChoiceIndex(const AdvancedOption& option, std::string_view value) {
    for (size_t i = 0; i < option.choices.size(); ++i) {
      if (SameValue(option.choices[i].value, value)) return int(i);
    }
    return -1;
  }

  void Save() {
    profiles_.SaveActive(config_path_);
    saved_ = true;
  }

  void Show() const {
    std::vector<std::string> lines;
    std::string help;
    if (level_ == 1) {
      lines.emplace_back("SETTINGS / CATEGORIES");
      for (int i = 0; i < kCategoryCount; ++i) {
        lines.push_back(std::string(i == row_ ? "> " : "  ") + kCategories[i].name);
      }
      lines.push_back(std::string(row_ == kCategoryCount ? "> " : "  ") + "SAVE TO CONFIG");
      lines.emplace_back("ARROWS: SELECT / A: OPEN / B: CLOSE");
      help = row_ == kCategoryCount ? "WRITES THE CURRENT SETTINGS TO THE CONFIG FILE ON THE SD CARD."
                                    : kCategories[row_].help;
    } else {
      const auto& category = kCategories[category_];
      const int count = int(category.options.size()) + 1;
      const int start = WindowStart(row_, count, 13);
      lines.push_back(fmt::format("SETTINGS / {}  {}/{}", category.name, row_ + 1, count));
      for (int i = start; i < std::min(start + 13, count); ++i) {
        if (i == count - 1) {
          lines.push_back(std::string(i == row_ ? "> " : "  ") + "SAVE TO CONFIG");
          continue;
        }
        const auto& option = category.options[i];
        const auto value = rex::cvar::GetFlagByName(option.cvar);
        const int selected = ChoiceIndex(option, value);
        std::string line = fmt::format("{}{}: {}", i == row_ ? "> " : "  ", option.label,
                                      selected < 0 ? std::string_view(value) : option.choices[selected].label);
        if (option.restart) line += " (R)";
        lines.push_back(MenuText(line, 44));
      }
      lines.emplace_back("A: CHANGE / L/R: CATEGORY / B: BACK");
      help = row_ == count - 1 ? "WRITES THE CURRENT SETTINGS TO THE CONFIG FILE ON THE SD CARD."
                               : std::string(category.options[row_].help) +
                                     (category.options[row_].restart ? " (R) NEEDS A RESTART TO APPLY." : "");
    }
    if (saved_) help = "SAVED TO THE CONFIG FILE. (R) OPTIONS APPLY AFTER A RESTART.";
    rex::ui::FijarSuperposicion(std::move(lines));
    rex::ui::FijarLetreiro(std::move(help));
  }

  Profiles& profiles_;
  std::filesystem::path config_path_;
  int level_ = 1;
  int category_ = 0;
  int row_ = 0;
  bool saved_ = false;
};

class AchievementsMenu {
 public:
  // Opens the menu on the list (never straight on a detail card).
  void Open(rex::system::AchievementManager& manager) {
    detail_ = false;
    Show(manager);
  }

  // B: closes the detail card back to the list (true: consumed); false when already on the list (the menu closes).
  bool Back(rex::system::AchievementManager& manager) {
    if (!detail_) return false;
    detail_ = false;
    Show(manager);
    return true;
  }

  void Show(rex::system::AchievementManager& manager, uint64_t pressed = 0) {
    const auto catalog = manager.ListAchievements();
    std::vector<std::string> lines{"BUDOKAI 3 / ACHIEVEMENTS"};
    std::vector<rex::ui::IconoEnLinea> icons;
    std::string marquee;
    if (catalog.empty()) {
      detail_ = false;
      lines.emplace_back("NO ACHIEVEMENTS LOADED");
    } else if (detail_) {
      // Detail card (0.1.50): left/right move to the previous/next achievement, B (Back) returns to the list.
      const int size = int(catalog.size());
      int steps = 0;
      if (pressed & HidNpadButton_Left) steps = -1;
      if (pressed & HidNpadButton_Right) steps = 1;
      row_ = std::clamp(row_ + steps, 0, size - 1);
      const auto& item = catalog[row_];
      const bool got = manager.IsUnlocked(item.id);
      lines.push_back(fmt::format("ACHIEVEMENT {} OF {}", row_ + 1, size));
      // Lines 2-7: the 96x96 icon (columns 2-9) and, from column 11, the name in the big font (two lines each, up to two
      // rows of 16 characters), then the state and the score. The big font leaves the line under it empty.
      std::string indent(11, ' ');
      if (!got) indent[2] = '\x02';
      const auto name = WrapWords(MenuText(item.label, 64), 16, 2);
      rex::ui::IconoCompartido icon = Icon(item.image_id, 96);
      if (icon) {
        rex::ui::IconoEnLinea entry;
        entry.linea = 2;
        entry.columna = 2;
        entry.filas = 6;
        entry.gris = !got;
        entry.icono = std::move(icon);
        icons.push_back(std::move(entry));
      }
      for (size_t i = 0; i < 2; ++i) {
        lines.push_back(i < name.size() ? indent + '\x03' + name[i] : indent);
        lines.push_back(indent);
      }
      lines.push_back(indent + fmt::format("{}  {} G", got ? "UNLOCKED" : "LOCKED", item.gamerscore));
      lines.push_back("");
      lines.push_back("");
      const std::string_view description = got ? item.description : item.unachieved_description;
      for (const auto& row : WrapWords(MenuText(description, 400), 42, 6)) {
        lines.push_back(std::string(got ? " " : "\x02") + row);
      }
      while (lines.size() < 15) lines.emplace_back();
      lines.emplace_back("L/R: PREVIOUS / NEXT / B: BACK");
      marquee = "B: BACK TO LIST";
    } else {
      // Console style (0.1.47): each entry takes two lines, its icon (32x32, grey when locked) over both, the name and
      // score on the first and the description in small type under it. The selected one's goes to the marquee too.
      constexpr int kPageRows = 6;
      const int size = int(catalog.size());
      int steps = 0;
      if (pressed & HidNpadButton_Up) steps = -1;
      if (pressed & HidNpadButton_Down) steps = 1;
      if (pressed & HidNpadButton_Left) steps = -kPageRows;
      if (pressed & HidNpadButton_Right) steps = kPageRows;
      row_ = std::clamp(row_ + steps, 0, size - 1);
      if (pressed & HidNpadButton_A) {
        detail_ = true;
        Show(manager);
        return;
      }
      int unlocked = 0;
      uint32_t earned = 0, total = 0;
      for (const auto& item : catalog) {
        total += item.gamerscore;
        if (manager.IsUnlocked(item.id)) {
          ++unlocked;
          earned += item.gamerscore;
        }
      }
      lines.push_back(fmt::format("{} OF {} UNLOCKED / {} OF {} G", unlocked, size, earned, total));
      const int start = row_ / kPageRows * kPageRows;
      for (int i = start; i < std::min(start + kPageRows, size); ++i) {
        const auto& item = catalog[i];
        const bool got = manager.IsUnlocked(item.id);
        rex::ui::IconoCompartido icon = Icon(item.image_id);
        // Columns 0-1: the cursor. Column 2 (under the icon): '\x02' dims the rest of a locked one. Columns 3-5: the
        // icon, or + / - without one (the text-only layout). The name from column 6.
        std::string line = i == row_ ? "> " : "  ";
        line += got ? ' ' : '\x02';
        line += icon ? "   " : (got ? " + " : " - ");
        line += fmt::format("{} {} G", MenuText(item.label, 30), item.gamerscore);
        if (icon) {
          rex::ui::IconoEnLinea entry;
          entry.linea = uint32_t(lines.size());
          entry.columna = 2;
          entry.filas = 2;
          entry.gris = !got;
          entry.icono = std::move(icon);
          icons.push_back(std::move(entry));
        }
        lines.push_back(std::move(line));
        // The description in the small font ('\x01') from column 6: two rows of ~76 characters.
        const std::string_view description = got ? item.description : item.unachieved_description;
        lines.push_back(std::string(got ? "      \x01" : "  \x02   \x01") + MenuText(description, 148));
      }
      const auto& item = catalog[row_];
      const std::string_view description = manager.IsUnlocked(item.id) ? item.description : item.unachieved_description;
      marquee = "A: DETAILS  ";
      marquee += description.empty() ? MenuText(item.label, 400)
                                     : MenuText(fmt::format("{}: {}", item.label, description), 400);
    }
    if (!detail_) lines.emplace_back("ARROWS: NAVIGATE / A: DETAILS / B: CLOSE");
    rex::ui::FijarLetreiro(std::move(marquee));
    rex::ui::FijarSuperposicionConIconos(std::move(lines), std::move(icons));
  }

  // The achievement's image from the game's XDBF (in the user's own XEX), decoded once and shared with the unlock
  // notification. Null when it has none (the text-only layout). Only with the game's module loaded (title_xdbf()
  // requires it): from a non-empty catalog (LoadAchievementsData read it from that XDBF) or from an unlock event.
  static rex::ui::IconoCompartido Icon(uint32_t image_id, uint32_t max_side = 32) {
    static std::mutex mutex;
    static std::vector<std::tuple<uint32_t, uint32_t, rex::ui::IconoCompartido>> cache;
    if (!image_id) return nullptr;
    std::lock_guard lock(mutex);
    for (const auto& [id, side, icon] : cache) {
      if (id == image_id && side == max_side) return icon;
    }
    rex::system::KernelState* kernel = rex::system::kernel_state();
    if (!kernel) return nullptr;
    rex::ui::IconoCompartido icon;
    const auto db = kernel->title_xdbf();
    if (db.is_valid()) {
      const auto block = db.GetEntry(rex::system::util::XdbfSection::kImage, image_id);
      if (block) icon = rex::ui::DecodificarIcono(image_id, block.buffer, block.size, max_side);
    }
    if (!icon) REXLOG_INFO("[B3 Deko menu] achievement without icon (image {}): text only", image_id);
    cache.emplace_back(image_id, max_side, icon);  // a failure is remembered too
    return icon;
  }

 private:
  // Words wrapped to `width` columns, at most `max_rows` rows (the last ends in "..." if the text is cut). A word longer
  // than a row is broken.
  static std::vector<std::string> WrapWords(std::string_view text, size_t width, size_t max_rows) {
    std::vector<std::string> rows;
    std::string current;
    bool cut = false;
    size_t pos = 0;
    while (pos < text.size() && !cut) {
      while (pos < text.size() && text[pos] == ' ') ++pos;
      const size_t end = std::min(text.find(' ', pos), text.size());
      std::string word(text.substr(pos, end - pos));
      pos = end;
      while (!word.empty()) {
        if (!current.empty() && current.size() + 1 + word.size() <= width) {
          current += ' ' + word;
          word.clear();
        } else if (current.empty() && word.size() <= width) {
          current = std::move(word);
          word.clear();
        } else {
          if (!current.empty()) {
            rows.push_back(std::move(current));
            current.clear();
          } else {
            rows.push_back(word.substr(0, width));
            word.erase(0, width);
          }
          if (rows.size() >= max_rows) {
            cut = true;
            break;
          }
        }
      }
    }
    if (!cut && !current.empty()) rows.push_back(std::move(current));
    if (rows.size() > max_rows) rows.resize(max_rows);
    if (cut && !rows.empty()) {
      auto& last = rows.back();
      if (last.size() + 3 > width) last.resize(width - 3);
      last += "...";
    }
    return rows;
  }

  int row_ = 0;
  bool detail_ = false;
};

class DekoXenosApp final : public rex::ReXApp {
 public:
  static std::unique_ptr<rex::ui::WindowedApp> Create(rex::ui::WindowedAppContext& context) {
    return std::unique_ptr<DekoXenosApp>(new DekoXenosApp(context));
  }

 private:
  explicit DekoXenosApp(rex::ui::WindowedAppContext& context)
      : rex::ReXApp(context, "budokai3", PPCImageConfig) {}

  std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override {
    std::lock_guard lock(menu_mutex_);
    ToggleMenu(Menu::kAchievements);
    return nullptr;
  }

  std::unique_ptr<rex::ui::AchievementNotificationDialog> CreateAchievementNotificationDialog() override {
    // Notifications are painted directly by Deko, without a UI-thread drawer.
    return nullptr;
  }

  void OnPreLaunchModule() override {
    achievement_notice_listener_ = achievements().RegisterNotificationCallback(
        [this](const rex::system::AchievementEvent& event) {
          std::lock_guard lock(menu_mutex_);
          if (menu_owner_ != this) return;
          const auto& item = event.achievement;
          // Console style (0.1.47), top right for ~4 s: the icon over the first two lines (columns 0-2), the header
          // and the name beside it, the description in small type under them. Without an icon, the same text from
          // column 0.
          rex::ui::IconoEnLinea notice_icon;
          notice_icon.linea = 0;
          notice_icon.columna = 0;
          notice_icon.filas = 2;
          notice_icon.icono = AchievementsMenu::Icon(item.image_id);
          const std::string indent = notice_icon.icono ? "   " : "";
          std::vector<std::string> lines{
              indent + fmt::format("ACHIEVEMENT UNLOCKED  {} G", item.gamerscore),
              indent + MenuText(item.label, 44 - indent.size())};
          if (!item.description.empty()) {
            // Two small rows as wide as the box (two small characters per column), broken at a space.
            const size_t columns = std::max(lines[0].size(), lines[1].size());
            const size_t width = std::max<size_t>(24, (columns - indent.size()) * 2);
            std::string text = MenuText(item.description, width * 2);
            if (text.size() > width) {
              const size_t space = text.rfind(' ', width);
              if (space != std::string::npos && space > 0) text[space] = '\n';
            }
            lines.push_back(indent + "\x01" + text);
          }
          const bool with_icon = bool(notice_icon.icono);
          rex::ui::AvisarSuperposicionConIcono(std::move(lines), std::move(notice_icon));
          REXLOG_INFO("[B3 Deko menu] aviso de conquista {} (icone {})", item.id, with_icon ? "sim" : "nao");
        });
  }

  void OnConfigurePaths(rex::PathConfig& paths) override {
    const auto executable = rex::filesystem::GetExecutableFolder();
    package_ = executable;
    const std::array candidates{executable, executable / "B3deko",
                                executable / "switch" / "B3deko"};
    for (const auto& candidate : candidates) {
      std::error_code ec;
      if (std::filesystem::is_regular_file(candidate / "game_root" / "default.xex", ec) &&
          std::filesystem::is_regular_file(candidate / "b3_shaders.nfsp", ec)) {
        package_ = candidate;
        break;
      }
    }
    paths.config_path = package_ / "budokai3.toml";
    paths.game_data_root = package_ / "game_root";
    if (paths.user_data_root.empty()) paths.user_data_root = package_ / "userdata";
    if (paths.cache_root.empty()) paths.cache_root = package_ / "cache";
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    // The US/Japan guest registers its music renderer as client 0. Its PCM
    // matched the original ADX track in the Switch pre-mix capture.
    const bool separate_music = REXCVAR_GET(b3_deko_musica_independente);
    rex::audio::nx::SetMusicRenderClient(separate_music ? 0 : SIZE_MAX);
    const int configured_delay = std::clamp(REXCVAR_GET(b3_audio_atraso_ms),
                                            0, rex::audio::nx::kMaxOutputDelayMs);
    const int delay = (configured_delay + kDelayStepMs / 2) / kDelayStepMs * kDelayStepMs;
    REXCVAR_SET(b3_audio_atraso_ms, delay);
    rex::audio::nx::SetOutputDelayMs(delay);
    REXLOG_INFO("[B3 audio] modo={} atraso={} ms",
                separate_music ? "musica_independente" : "mistura_0.1.32", delay);
    profiles_.Startup();
    // Build 0.1.52: the PT-BR overlay must be in place before the runtime mounts game_root (sizes of the archives).
    b3::ptbr::Initialize(package_ / "traducao" / "ptbr.b3tr", package_ / "game_root" / "us",
                         REXCVAR_GET(b3_traducao_ptbr_max_growth_mb), REXCVAR_GET(b3_traducao_ptbr));
    b3::ptbr::SetEnabled(REXCVAR_GET(b3_traducao_ptbr));
    // Concealment of late guest effect frames is on in every profile (0.1.48): the late frames happen at stock CPU
    // in all of them. The cvar (audio_switch_ocultar_faltas, default true) can still turn it off.
    config.graphics = b3::CreateXenosDeko(package_ / "b3_shaders.nfsp", options_);
    REXLOG_INFO("B3: standard ReXGlue Xenos command processor, Deko3D backend, package {}",
                package_.string());
  }

  void OnCreateDialogs(rex::ui::ImGuiDrawer*) override {
    // Handle the chords in the same input filter that suppresses guest input.
    REXCVAR_SET(switch_atajo_perfil_app, false);
    const auto config_path = package_ / "budokai3.toml";
    {
      std::lock_guard lock(menu_mutex_);
      menu_owner_ = this;
      port_menu_ = std::make_unique<PortMenu>(profiles_, config_path);
      advanced_ = std::make_unique<AdvancedSettings>(profiles_, config_path);
    }
    rex::input::FijarFiltroMando(&DekoXenosApp::FilterPad);
    b3::SetMenuLegendLanguage(b3::ptbr::Enabled());  // Portuguese legend only while the translation is really on
    rex::ui::RegisterBind(kProfilesBind, "F5", "Budokai 3 port menu", [this] {
      std::lock_guard lock(menu_mutex_);
      ToggleMenu(Menu::kQuick);
    });
    rex::ui::RegisterBind(kSettingsBind, "F6", "Budokai 3 settings", [this] {
      std::lock_guard lock(menu_mutex_);
      ToggleMenu(Menu::kAdvanced);
    });
  }

  void OnShutdown() override {
    rex::ui::UnregisterBind(kProfilesBind);
    rex::ui::UnregisterBind(kSettingsBind);
    rex::input::FijarFiltroMando(nullptr);
    if (achievement_notice_listener_ && runtime() && runtime()->kernel_state()) {
      achievements().UnregisterCallback(achievement_notice_listener_);
    }
    std::lock_guard lock(menu_mutex_);
    menu_owner_ = nullptr;
    if (menu_ != Menu::kClosed) CloseMenu();
    rex::ui::AvisarSuperposicion({});
    port_menu_.reset();
    advanced_.reset();
  }

  void CloseMenu() {
    menu_ = Menu::kClosed;
    b3::g_port_menu_open.store(false, std::memory_order_relaxed);
    release_buttons_.fill(true);
    rex::ui::FijarSuperposicion({});
    rex::ui::FijarLetreiro({});
    REXLOG_INFO("[B3 Deko menu] fechado");
  }

  void ToggleMenu(Menu menu) {
    if (menu_ == menu) {
      CloseMenu();
      return;
    }
    if (menu == Menu::kAchievements && (!runtime() || !runtime()->kernel_state())) return;
    menu_ = menu;
    b3::g_port_menu_open.store(true, std::memory_order_relaxed);
    previous_buttons_.fill(UINT64_MAX);
    release_buttons_.fill(false);
    switch (menu_) {
      case Menu::kQuick: port_menu_->Open(); break;
      case Menu::kAdvanced: advanced_->Open(); break;
      case Menu::kAchievements: achievements_menu_.Open(achievements()); break;
      case Menu::kClosed: return;
    }
    REXLOG_INFO("[B3 Deko menu] {} aberto", menu_ == Menu::kQuick ? "rapido" :
                menu_ == Menu::kAdvanced ? "ajustes" : "conquistas");
  }

  static bool FilterPad(size_t slot, uint64_t buttons) {
    std::lock_guard lock(menu_mutex_);
    if (!menu_owner_) return false;
    auto& app = *menu_owner_;
    app.profiles_.Poll();
    if (slot >= app.previous_buttons_.size()) return app.menu_ != Menu::kClosed;
    const uint64_t previous = std::exchange(app.previous_buttons_[slot], buttons);
    if (app.release_buttons_[slot]) {
      if (buttons == 0) app.release_buttons_[slot] = false;
      return true;
    }
    for (const auto& shortcut : kMenuShortcuts) {
      if ((buttons & shortcut.chord) == shortcut.chord) {
        if ((previous & shortcut.chord) != shortcut.chord) app.ToggleMenu(shortcut.menu);
        return true;
      }
    }
    if (app.menu_ == Menu::kClosed) return false;
    constexpr uint64_t controls = HidNpadButton_Up | HidNpadButton_Down | HidNpadButton_Left |
                                  HidNpadButton_Right | HidNpadButton_A | HidNpadButton_B |
                                  HidNpadButton_L | HidNpadButton_R;
    const uint64_t pressed = (buttons & ~previous) & controls;
    if (pressed & HidNpadButton_B) {
      // B on an achievement's detail card goes back to the list (0.1.50); only B on the list closes the menu.
      if (app.menu_ == Menu::kAchievements && app.achievements_menu_.Back(app.achievements())) {
        // consumed by the detail card
      } else if (app.menu_ != Menu::kAdvanced || !app.advanced_->Back()) {
        app.CloseMenu();
      }
    } else if (pressed) {
      if (app.menu_ == Menu::kQuick && !app.port_menu_->Press(pressed)) app.CloseMenu();
      if (app.menu_ == Menu::kAdvanced && !app.advanced_->Press(pressed)) app.CloseMenu();
      if (app.menu_ == Menu::kAchievements) app.achievements_menu_.Show(app.achievements(), pressed);
    }
    return true;
  }

  static inline std::mutex menu_mutex_;
  static inline DekoXenosApp* menu_owner_ = nullptr;
  std::filesystem::path package_;
  b3::DekoOptions options_{0};
  Profiles profiles_{options_};
  std::unique_ptr<PortMenu> port_menu_;
  std::unique_ptr<AdvancedSettings> advanced_;
  AchievementsMenu achievements_menu_;
  rex::system::AchievementListenerHandle achievement_notice_listener_ = 0;
  std::array<uint64_t, 8> previous_buttons_{};
  std::array<bool, 8> release_buttons_{};
  Menu menu_ = Menu::kClosed;
};
}  // namespace

REX_DEFINE_APP(budokai3, DekoXenosApp::Create)
