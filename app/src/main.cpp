#include "graphics.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iterator>
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

extern const rex::PPCImageInfo PPCImageConfig;
REXCVAR_DECLARE(bool, switch_atajo_perfil_app);
REXCVAR_DECLARE(bool, switch_contador_fps);
REXCVAR_DEFINE_BOOL(b3_deko_musica_independente, true, "Budokai 3",
                    "Musica em tempo real. Desligue para comparar com a mistura da 0.1.32. Requer reiniciar.");
REXCVAR_DEFINE_INT32(b3_audio_atraso_ms, 0, "Budokai 3", "Atraso da mistura de efeitos em milissegundos.");

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

struct Preset {
  const char* name;
  uint32_t options;
};

constexpr uint32_t kBaseOptions = b3::kReduzirCopias | b3::kRestaurarZ;
constexpr Preset kPresets[] = {
    {"QUALIDADE",
     kBaseOptions | b3::kBloom | b3::kSombrasProjetadas},
    {"EQUILIBRADO",
     kBaseOptions | b3::kBloom | b3::kSombrasProjetadas | b3::kDesfoqueLeve},
    {"DESEMPENHO",
     kBaseOptions | b3::kDesfoqueLeve},
};

struct GraphicsSetting {
  const char* label;
  const char* cvar;
  b3::DekoOption option;
};

constexpr GraphicsSetting kSettings[] = {
    {"SOMBRAS PROJETADAS", "b3_deko_sombras_projetadas", b3::kSombrasProjetadas},
    {"BRILHO DA CENA", "b3_deko_bloom", b3::kBloom},
    {"DESFOQUE LEVE", "b3_deko_desfoque_leve", b3::kDesfoqueLeve},
    {"REDUZIR COPIAS DA GPU", "b3_deko_reduzir_copias", b3::kReduzirCopias},
    {"RESTAURAR PROFUNDIDADE", "b3_deko_restaurar_z", b3::kRestaurarZ},
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

class PortMenu {
 public:
  PortMenu(b3::DekoOptions& options, const std::filesystem::path& config_path)
      : options_(options), config_path_(config_path) {}

  void Open() {
    music_independent_ = REXCVAR_GET(b3_deko_musica_independente);
    Show();
  }

  bool Press(uint64_t buttons) {
    if (buttons & HidNpadButton_Up) row_ = (row_ + kRows - 1) % kRows;
    if (buttons & HidNpadButton_Down) row_ = (row_ + 1) % kRows;
    if (buttons & (HidNpadButton_A | HidNpadButton_Left | HidNpadButton_Right)) {
      if (row_ == kSave) {
        if (buttons & HidNpadButton_A) {
          Save();
          return false;
        }
      } else {
        Change(buttons & HidNpadButton_Left ? -1 : 1);
      }
    }
    Show();
    return true;
  }

 private:
  static constexpr int kResolution = 1 + std::size(kSettings);
  static constexpr int kFps = kResolution + 1;
  static constexpr int kMusic = kFps + 1;
  static constexpr int kAudioDelay = kMusic + 1;
  static constexpr int kSave = kAudioDelay + 1;
  static constexpr int kRows = kSave + 1;
  static constexpr uint32_t kIndependentOptions = b3::kDofResolutionMask;

  void Show() const {
    const uint32_t options = options_.load(std::memory_order_relaxed);
    std::vector<std::string> rows{"BUDOKAI 3 / MENU DO PORT"};
    const int preset = CurrentPreset(options);
    rows.push_back(std::string("PERFIL: ") + (preset >= 0 ? kPresets[preset].name : "PERSONALIZADO"));
    for (const auto& setting : kSettings) {
      rows.push_back(std::string(setting.label) + ": " + ((options & setting.option) ? "SIM" : "NAO"));
    }
    rows.push_back("DESFOQUE DA LUTA: " + std::to_string(b3::DofResolutionHeight(options)) + "P");
    rows.push_back(std::string("CONTADOR DE FPS: ") + (REXCVAR_GET(switch_contador_fps) ? "SIM" : "NAO"));
    rows.emplace_back(music_independent_ ? "MUSICA: RITMO NORMAL" : "MUSICA: JUNTO COM A LUTA");
    rows.push_back("ATRASO DO AUDIO: " + std::to_string(REXCVAR_GET(b3_audio_atraso_ms)) + " MS");
    rows.emplace_back("SALVAR E FECHAR");
    for (int i = 0; i < kRows; ++i) rows[i + 1].insert(0, i == row_ ? "> " : "  ");
    rows.emplace_back("SETAS: AJUSTAR / A: ALTERAR / B: FECHAR");
    if (row_ == kMusic) rows.emplace_back("MUSICA: SALVE E REABRA O JOGO");
    rex::ui::FijarSuperposicion(std::move(rows));
  }

  static int CurrentPreset(uint32_t options) {
    for (int i = 0; i < int(std::size(kPresets)); ++i) {
      if ((options & ~kIndependentOptions) == kPresets[i].options) return i;
    }
    return -1;
  }

  template <size_t N>
  static int Cycle(const std::array<int, N>& values, int value, int direction) {
    const auto current = std::ranges::find(values, value);
    if (current == values.end()) return values.front();
    const int count = int(N);
    return values[(int(current - values.begin()) + count + direction) % count];
  }

  void Change(int direction) {
    uint32_t options = options_.load(std::memory_order_relaxed);
    if (row_ == 0) {
      const int current = CurrentPreset(options);
      const int count = int(std::size(kPresets));
      const int next = current < 0 ? 1 : (current + count + direction) % count;
      options = kPresets[next].options | (options & kIndependentOptions);
    } else if (row_ < kResolution) {
      options ^= kSettings[row_ - 1].option;
    } else if (row_ == kResolution) {
      const int height = Cycle(std::array{720, 540, 360}, b3::DofResolutionHeight(options), direction);
      options = (options & ~b3::kDofResolutionMask) | b3::DofResolutionOption(height);
    } else if (row_ == kFps) {
      REXCVAR_SET(switch_contador_fps, !REXCVAR_GET(switch_contador_fps));
    } else if (row_ == kMusic) {
      music_independent_ = !music_independent_;
    } else if (row_ == kAudioDelay) {
      const int delay = std::clamp(REXCVAR_GET(b3_audio_atraso_ms) + direction * kDelayStepMs,
                                   0, rex::audio::nx::kMaxOutputDelayMs);
      REXCVAR_SET(b3_audio_atraso_ms, delay);
      rex::audio::nx::SetOutputDelayMs(delay);
      REXLOG_INFO("[B3 audio] atraso={} ms", delay);
    }
    options_.store(options, std::memory_order_relaxed);
  }

  void Save() const {
    const uint32_t options = options_.load(std::memory_order_relaxed);
    for (const auto& setting : kSettings) {
      rex::cvar::SetFlagByName(setting.cvar, (options & setting.option) ? "true" : "false");
    }
    rex::cvar::SetFlagByName("b3_deko_desfoque_resolucao", std::to_string(b3::DofResolutionHeight(options)));
    rex::cvar::SetFlagByName("switch_contador_fps", REXCVAR_GET(switch_contador_fps) ? "true" : "false");
    REXCVAR_SET(b3_deko_musica_independente, music_independent_);
    rex::cvar::SaveConfig(config_path_);
  }

  b3::DekoOptions& options_;
  std::filesystem::path config_path_;
  int row_ = 0;
  bool music_independent_ = true;
};

struct SettingChoice {
  std::string_view value;
  std::string_view label;
};

constexpr SettingChoice kBoolean[] = {{"false", "NAO"}, {"true", "SIM"}};
constexpr SettingChoice kAnisotropy[] = {{"0", "PADRAO"}, {"2", "2X"}, {"4", "4X"}, {"8", "8X"}};
constexpr SettingChoice kGeometrySize[] = {{"0", "OFF"}, {"32", "32 MB"}, {"64", "64 MB"}, {"96", "96 MB"}};
constexpr SettingChoice kTextureSize[] = {{"384", "384 MB"}, {"512", "512 MB"}, {"640", "640 MB"}};
constexpr SettingChoice kAudioQueue[] = {{"6", "6"}, {"8", "8"}, {"10", "10"}, {"12", "12"}, {"16", "16"}};
constexpr SettingChoice kLogLevel[] = {{"info", "INFO"}, {"warn", "AVISOS"}, {"error", "ERROS"}};

struct AdvancedOption {
  const char* label;
  const char* cvar;
  const char* description;
  std::span<const SettingChoice> choices = kBoolean;
  bool restart = false;
};

constexpr AdvancedOption kDisplayOptions[] = {
    {"VSYNC", "vsync", "SINCRONIZA A APRESENTACAO DOS QUADROS"},
    {"PULAR FXAA FINAL", "b3_deko_copia_final", "EVITA O FILTRO FINAL QUANDO COMPATIVEL", kBoolean, true},
    {"MIPMAPS", "nfsmw_nativo_mipmaps", "USA OS NIVEIS DE TEXTURA DO JOGO", kBoolean, true},
    {"FILTRO ANISOTROPICO", "nfsmw_nativo_anisotropico", "NITIDEZ DE TEXTURAS VISTAS DE LADO", kAnisotropy, true},
    {"ZCULL", "b3_deko_zcull", "DESCARTA PIXELS COBERTOS NA GPU", kBoolean, true},
};
constexpr AdvancedOption kGpuOptions[] = {
    {"Z ANTECIPADO", "nfsmw_nativo_z_temprana", "TESTA PROFUNDIDADE ANTES DO SHADER"},
    {"PULAR DESENHOS INVISIVEIS", "nfsmw_nativo_saltar_invisibles", "EVITA DESENHOS SEM EFEITO VISUAL"},
    {"PULAR LIMPEZAS REPETIDAS", "nfsmw_nativo_saltar_borrados_repetidos", "EVITA REPETIR LIMPEZAS DA MESMA AREA"},
    {"RESTAURAR STENCIL", "b3_deko_restaurar_stencil", "PRESERVA A MASCARA DA CENA NAS COPIAS"},
    {"CACHE DE TEXTURAS", "nfsmw_nativo_texturas_mb_max", "LIMITE ANTES DE LIBERAR TEXTURAS ANTIGAS", kTextureSize},
    {"CACHE DE GEOMETRIA", "nfsmw_dk_geometria_mb", "REUTILIZA VERTICES E INDICES", kGeometrySize, true},
    {"AMOSTRAR GEOMETRIA", "nfsmw_dk_geometria_muestreo", "CONFERE PARTES DA GEOMETRIA EM CACHE", kBoolean, true},
};
constexpr AdvancedOption kSystemOptions[] = {
    {"SILENCIAR AUDIO", "audio_mute", "DESLIGA A SAIDA DE SOM"},
    {"BOMBA DE AUDIO", "audio_switch_bomba", "SOLICITA AUDIO EM INTERVALOS REGULARES", kBoolean, true},
    {"FILA DE AUDIO", "audio_switch_tramas_en_cola", "QUADROS PCM NA FILA DA BOMBA", kAudioQueue, true},
    {"BOTOES POR POSICAO", "input_xbox_layout", "USA A POSICAO FISICA DOS BOTOES XBOX"},
    {"NIVEL DO LOG", "log_level", "ESCOLHE O DETALHE DOS REGISTROS", kLogLevel},
    {"CAPTURA L3 + R3", "b3_deko_diagnostico", "GRAVA UM QUADRO E SEUS DADOS", kBoolean, true},
    {"CAPTURA COMPLETA", "b3_deko_diagnostico_completo", "GRAVA MAIS DADOS NA CAPTURA DE QUADRO"},
};

struct AdvancedPage {
  const char* name;
  std::span<const AdvancedOption> options;
};

constexpr AdvancedPage kAdvancedPages[] = {
    {"IMAGEM", kDisplayOptions}, {"GPU E CACHE", kGpuOptions}, {"SOM, CONTROLES E LOGS", kSystemOptions},
};

class AdvancedSettings {
 public:
  explicit AdvancedSettings(std::filesystem::path config_path) : config_path_(std::move(config_path)) {}

  void Open() const { Show(); }

  bool Press(uint64_t pressed) {
    if (pressed & (HidNpadButton_L | HidNpadButton_R)) {
      page_ = NextIndex(page_, std::size(kAdvancedPages), pressed & HidNpadButton_L ? -1 : 1);
      row_ = 0;
    }
    const auto options = kAdvancedPages[page_].options;
    const int save = int(options.size());
    if (pressed & HidNpadButton_Up) row_ = NextIndex(row_, save + 1, -1);
    if (pressed & HidNpadButton_Down) row_ = NextIndex(row_, save + 1, 1);
    if (row_ == save) {
      if (pressed & HidNpadButton_A) {
        rex::cvar::SaveConfig(config_path_);
        return false;
      }
    } else if (pressed & (HidNpadButton_A | HidNpadButton_Left | HidNpadButton_Right)) {
      const auto& option = options[row_];
      const int current = ChoiceIndex(option, rex::cvar::GetFlagByName(option.cvar));
      const int next = current < 0 ? 0 : NextIndex(current, option.choices.size(), pressed & HidNpadButton_Left ? -1 : 1);
      const auto value = option.choices[next].value;
      if (rex::cvar::SetFlagByName(option.cvar, value)) {
        REXLOG_INFO("[B3 Deko menu] {}={}", option.cvar, value);
      }
    }
    Show();
    return true;
  }

 private:
  static int ChoiceIndex(const AdvancedOption& option, std::string_view value) {
    const auto item = std::ranges::find(option.choices, value, &SettingChoice::value);
    return item == option.choices.end() ? -1 : int(item - option.choices.begin());
  }

  void Show() const {
    const auto& page = kAdvancedPages[page_];
    std::vector<std::string> lines{std::string("AJUSTES / ") + page.name};
    for (size_t i = 0; i < page.options.size(); ++i) {
      const auto& option = page.options[i];
      const auto value = rex::cvar::GetFlagByName(option.cvar);
      const int selected = ChoiceIndex(option, value);
      std::string line = fmt::format("{}{}: {}", int(i) == row_ ? "> " : "  ", option.label,
                                    selected < 0 ? std::string_view(value) : option.choices[selected].label);
      if (option.restart) line += " (R)";
      lines.push_back(MenuText(line, 44));
    }
    const bool save_row = size_t(row_) == page.options.size();
    lines.emplace_back(save_row ? "> SALVAR E FECHAR" : "  SALVAR E FECHAR");
    lines.emplace_back(save_row ? "GRAVA OS AJUSTES NO CARTAO" : page.options[row_].description);
    lines.emplace_back("L/R ABAS / SETAS E A AJUSTAM / B FECHA");
    lines.emplace_back("(R) SALVE E REABRA PARA APLICAR");
    rex::ui::FijarSuperposicion(std::move(lines));
  }

  std::filesystem::path config_path_;
  int page_ = 0;
  int row_ = 0;
};

class AchievementsMenu {
 public:
  void Show(rex::system::AchievementManager& manager, uint64_t pressed = 0) {
    const auto catalog = manager.ListAchievements();
    std::vector<std::string> lines{"BUDOKAI 3 / CONQUISTAS"};
    if (catalog.empty()) {
      lines.emplace_back("SEM CONQUISTAS CARREGADAS");
    } else {
      constexpr int kPageRows = 8;
      const int size = int(catalog.size());
      int steps = 0;
      if (pressed & HidNpadButton_Up) steps = -1;
      if (pressed & HidNpadButton_Down) steps = 1;
      if (pressed & HidNpadButton_Left) steps = -kPageRows;
      if (pressed & HidNpadButton_Right) steps = kPageRows;
      row_ = std::clamp(row_ + steps, 0, size - 1);
      const auto unlocked = std::ranges::count_if(catalog, [&](const auto& item) {
        return manager.IsUnlocked(item.id);
      });
      lines.push_back(fmt::format("{} DE {} DESBLOQUEADAS", unlocked, size));
      const int start = row_ / kPageRows * kPageRows;
      for (int i = start; i < std::min(start + kPageRows, size); ++i) {
        const auto& item = catalog[i];
        const auto label = MenuText(item.label, 28);
        lines.push_back(fmt::format("{}{} {} {} G", i == row_ ? "> " : "  ",
                                    manager.IsUnlocked(item.id) ? "+" : "-", label, item.gamerscore));
      }
      const auto& item = catalog[row_];
      std::string_view description = manager.IsUnlocked(item.id) ? item.description : item.unachieved_description;
      for (int i = 0; i < 2 && !description.empty(); ++i) {
        const size_t space = description.size() > 44 ? description.rfind(' ', 44) : std::string_view::npos;
        const size_t split = i == 0 && space != std::string_view::npos ? space : description.size();
        lines.push_back(MenuText(description.substr(0, split), 44));
        description.remove_prefix(std::min(split + 1, description.size()));
      }
    }
    lines.emplace_back("SETAS NAVEGAM / B FECHA");
    rex::ui::FijarSuperposicion(std::move(lines));
  }

 private:
  int row_ = 0;
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
          rex::ui::AvisarSuperposicion({fmt::format("CONQUISTA DESBLOQUEADA / {} G", item.gamerscore),
                                       MenuText(item.label, 44)});
          REXLOG_INFO("[B3 Deko menu] aviso de conquista {}", item.id);
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
      port_menu_ = std::make_unique<PortMenu>(options_, config_path);
      advanced_ = std::make_unique<AdvancedSettings>(config_path);
    }
    rex::input::FijarFiltroMando(&DekoXenosApp::FilterPad);
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
    release_buttons_.fill(true);
    rex::ui::FijarSuperposicion({});
    REXLOG_INFO("[B3 Deko menu] fechado");
  }

  void ToggleMenu(Menu menu) {
    if (menu_ == menu) {
      CloseMenu();
      return;
    }
    if (menu == Menu::kAchievements && (!runtime() || !runtime()->kernel_state())) return;
    menu_ = menu;
    previous_buttons_.fill(UINT64_MAX);
    release_buttons_.fill(false);
    switch (menu_) {
      case Menu::kQuick: port_menu_->Open(); break;
      case Menu::kAdvanced: advanced_->Open(); break;
      case Menu::kAchievements: achievements_menu_.Show(achievements()); break;
      case Menu::kClosed: return;
    }
    REXLOG_INFO("[B3 Deko menu] {} aberto", menu_ == Menu::kQuick ? "rapido" :
                menu_ == Menu::kAdvanced ? "ajustes" : "conquistas");
  }

  static bool FilterPad(size_t slot, uint64_t buttons) {
    std::lock_guard lock(menu_mutex_);
    if (!menu_owner_) return false;
    auto& app = *menu_owner_;
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
      app.CloseMenu();
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
  b3::DekoOptions options_{kPresets[1].options};
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
