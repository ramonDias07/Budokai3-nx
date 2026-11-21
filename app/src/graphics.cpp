#include "graphics.h"
#include "game_speed.h"
#include "dragon_rush_layout.h"

#include "nfsmw_nativo_shaders.h"
#include "nfsmw_shader_library.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <pthread.h>

#include <rex/audio/audio_system.h>
#include <rex/cvar.h>
#include <rex/graphics/command_processor.h>
#include <rex/graphics/graphics_system.h>
#include <rex/graphics/pipeline/shader/interpreter.h>
#include <rex/graphics/pipeline/shader/shader.h>
#include <rex/logging.h>
#include <rex/system/xmemory.h>
#include <rex/ui/deko3d/glsl.h>
#include <rex/ui/deko3d/presentador.h>
#include <rex/ui/switch_apm.h>
#include <rex/ui/switch_saltynx.h>
#include <rex/x360/deko/render.h>
#include <rex/x360/shader_entry.h>
#include <rex/x360/xenos.h>

#include <switch.h>
#include <xxhash.h>

REXCVAR_DEFINE_BOOL(b3_deko_diagnostico, true, "Budokai 3",
                    "Habilita a captura manual de quadros e o registro de shaders Xenos.");
REXCVAR_DEFINE_BOOL(b3_deko_diagnostico_completo, false, "Budokai 3",
                    "Inclui sondas de vertices e capturas intermediarias extras ao pressionar L3 + R3.");
REXCVAR_DEFINE_BOOL(b3_deko_reduzir_copias, true, "Budokai 3",
                    "Evita o desenho final redundante e a leitura pelo CPU do bloom 80x45.");
REXCVAR_DEFINE_BOOL(b3_deko_bloom, true, "Budokai 3", "Mistura o brilho do bloom na cena.");
REXCVAR_DEFINE_INT32(b3_deko_desfoque_resolucao, 720, "Budokai 3",
                     "Altura do alvo do desfoque de combate (360, 540 ou 720).");
REXCVAR_DEFINE_BOOL(b3_deko_restaurar_z, true, "Budokai 3", "Restaura a profundidade da cena apos um resolve.");
REXCVAR_DEFINE_BOOL(b3_deko_desfoque_leve, true, "Budokai 3",
                    "Usa cinco amostras no desfoque de profundidade original.");
REXCVAR_DEFINE_BOOL(b3_deko_sombras_projetadas, true, "Budokai 3",
                    "Compoe as sombras projetadas pelos lutadores.");
// 0.1.46: the projection of the fighter shadows redraws the ground over the restored depth with another vertex shader;
// on the Switch the Z does not match bit for bit and the shadow fails in rows that change every frame (the flicker,
// captures 4246/4300/4496 of the 0.1.44 test). A small bias towards the camera only on that draw.
REXCVAR_DEFINE_INT32(b3_deko_sombra_sesgo, 64, "Budokai 3",
                     "Desloca a projecao da sombra dos lutadores para a camera (unidades de profundidade; a "
                     "inclinacao acompanha). 0 = como antes (sombra piscando em listras). Ao vivo.");
REXCVAR_DEFINE_INT32(b3_limite_fps_luta, 60, "Budokai 3",
                     "Teto de FPS na luta: 60 (sem teto), 55, 50, 45, 40, 35 ou 30.");

extern "C" void RexSwitchPerfIntervaloSwap(unsigned vblanks);

// Build 0.1.43. b3_deko_resolucao_interna (SDK, destinos.cpp) is live now; b3::PedirResolucaoInterna sets it.
REXCVAR_DECLARE(int32_t, b3_deko_resolucao_interna);
// Official GPU boost (460.8 MHz handheld profile through apm, RAM clock untouched; switch_apm.cpp). Live.
REXCVAR_DEFINE_BOOL(b3_gpu_boost, false, "Budokai 3",
                    "Perfil oficial de GPU 460.8 MHz (apm) so no portatil; no dock nao faz nada. Desligar restaura o "
                    "perfil anterior. Ao vivo.");
REXCVAR_DEFINE_BOOL(b3_gpu_boost_ram1600, true, "Budokai 3",
                    "GPU boost uses the official handheld mode GPU 460 + RAM 1600 (fixes audio crackle at stock CPU). Live.");

REXCVAR_DEFINE_BOOL(b3_deko_copia_final, true, "Budokai 3",
                    "Usa a imagem anterior ao FXAA na copia final do Budokai 3.");

namespace b3 {

void PedirResolucaoInterna(int altura) {
  // 1080: experimental, docked only (the renderer falls back to 720 in handheld or short of memory).
  if (altura != 0 && altura != 1080 && altura != 720 && altura != 540 && altura != 360) {
    REXLOG_WARN("[B3 Deko escala] PedirResolucaoInterna({}) invalida: ignorada", altura);
    return;
  }
  REXCVAR_SET(b3_deko_resolucao_interna, altura);  // read at each frame boundary by the renderer
}

namespace {
std::atomic<bool> g_gpu_boost_ocupado{false};
}  // namespace

bool AplicarGpuBoost(bool ligar) {
  const bool dock = rex::ui::switch_saltynx::ModoBase(appletGetOperationMode() == AppletOperationMode_Console);
  if (!ligar) {
    // A request to turn it on is still being applied in the background: "off" now would find nothing to restore and
    // the thread would turn it on afterwards, stuck until restart (0.1.44, log 005: true->false, then "ligado").
    // Return false so TickGpuBoost tries again 30 swaps later.
    if (g_gpu_boost_ocupado.load()) {
      return false;
    }
    if (!RexSwitchApmAtivo()) {
      return true;  // nothing to restore
    }
    if (g_gpu_boost_ocupado.exchange(true)) {
      return false;
    }
    // Restaurar sleeps ~0.5 s and reads clkrst: keep it off the swap thread (same pthread mechanism as the on path).
    pthread_t hilo_off;
    const int rc_off = pthread_create(&hilo_off, nullptr, [](void*) -> void* {
      if (RexSwitchApmRestaurar() == 1) {
        REXLOG_INFO("[B3 gpu boost] desligado: perfil anterior restaurado");
      } else {
        REXLOG_WARN("[B3 gpu boost] desligado, mas o perfil anterior foi RECHAZADO pelo apm (ver [apm] no log)");
      }
      g_gpu_boost_ocupado.store(false);
      return nullptr;
    }, nullptr);
    if (rc_off != 0) {
      REXLOG_WARN("[B3 gpu boost] pthread_create falhou ({}): boost nao restaurado", rc_off);
      g_gpu_boost_ocupado.store(false);
      return false;
    }
    pthread_detach(hilo_off);
    return true;
  }
  if (dock) {
    REXLOG_INFO("[B3 gpu boost] ignorado: console no dock (so vale no portatil)");
    return false;
  }
  if (g_gpu_boost_ocupado.load()) {
    return false;  // an apply or a restore is still running in the background; retry next tick
  }
  if (RexSwitchApmAtivo()) {
    return true;
  }
  if (g_gpu_boost_ocupado.exchange(true)) {
    return false;  // a request is still being checked (it polls the clocks for ~1.5 s)
  }
  // Off the swap thread: RexSwitchApmAplicar waits for the clocks (up to a few seconds) and checks the memory clock
  // did not move; if it did, it falls back to the original profile (see switch_apm.cpp).
  // pthread, not std::thread: std::thread throws std::system_error "Function not implemented" on this toolchain
  // (the 0.1.43 NO OVERCLOCK crash, rex_crash.log: std::terminate in AplicarGpuBoost).
  pthread_t hilo;
  RexSwitchApmPermitirRam1600(REXCVAR_GET(b3_gpu_boost_ram1600) ? 1 : 0);
  const int rc = pthread_create(&hilo, nullptr, [](void*) -> void* {
    RexSwitchApmPedirGpuMhz(460);
    RexSwitchApmAplicar();
    REXLOG_INFO("[B3 gpu boost] ligado: {}", RexSwitchApmAtivo() ? "GPU 460.8 MHz ativa (memoria intacta)"
                                                                 : "nao aplicado (ver [apm] no log: sem perfil seguro)");
    if (!REXCVAR_GET(b3_gpu_boost) && RexSwitchApmAtivo()) {
      // Turned off from the menu while this was being applied.
      RexSwitchApmRestaurar();
      REXLOG_INFO("[B3 gpu boost] desligado logo apos ligar (pedido durante a aplicacao): perfil anterior restaurado");
    }
    g_gpu_boost_ocupado.store(false);
    return nullptr;
  }, nullptr);
  if (rc != 0) {
    REXLOG_WARN("[B3 gpu boost] pthread_create falhou ({}): boost nao aplicado", rc);
    g_gpu_boost_ocupado.store(false);
    return false;
  }
  pthread_detach(hilo);
  return true;
}

namespace {

using namespace rex::graphics;
namespace x360 = rex::x360;
using Provider = rex::deko3d::ProveedorDeko3d;
using ShaderEntry = nfsmw::nativo::EntradaShader;
constexpr uint64_t kCaptureButtons = HidNpadButton_StickL | HidNpadButton_StickR;

// Battle frame cap. 30 uses the exact swap interval of two vblanks; 55/50/45/40/35 present
// as fast as allowed and sleep to a monotonic deadline (no catch-up bursts).
class BattleFramePacer {
 public:
  void Pace() {
    const int phase = PublishedPhase();
    const int requested = REXCVAR_GET(b3_limite_fps_luta);
    const int limit = TimedScene(phase) &&
                              (requested == 30 || requested == 35 || requested == 40 || requested == 45 || requested == 50 || requested == 55)
                          ? requested
                          : 60;
    if (limit != limit_) {
      REXLOG_INFO("[B3 limite fps] efetivo={} (fase={} pedido={})", limit, phase, requested);
      limit_ = limit;
      deadline_ns_ = 0;
      RexSwitchPerfIntervaloSwap(limit == 30 ? 2 : 0);
    }
    if (limit == 60 || limit == 30) return;
    const uint64_t period = 1000000000ull / uint64_t(limit);
    const uint64_t now = NowNs();
    if (deadline_ns_ == 0 || now > deadline_ns_ + period) {
      deadline_ns_ = now + period;  // late or first frame: restart, no catch-up
      return;
    }
    if (now < deadline_ns_) svcSleepThread(int64_t(deadline_ns_ - now));
    deadline_ns_ += period;
  }

 private:
  static uint64_t NowNs() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count());
  }
  int limit_ = 60;
  uint64_t deadline_ns_ = 0;

 public:
  // The frame period the cap aims at, for the automatic internal resolution (16.7 ms without a cap).
  double PeriodMs() const { return limit_ >= 30 && limit_ < 60 ? 1000.0 / double(limit_) : 16.7; }
};
constexpr uint64_t kAudioCaptureButtons = kCaptureButtons | HidNpadButton_ZL | HidNpadButton_ZR;
constexpr size_t kMaxPasses = 128;
constexpr size_t kMaxCopyRows = 96;
constexpr uint32_t kMaxShaderDumps = 24;
constexpr uint64_t kCompositeVertexContainer = 0x2C44067E6D06DD69ULL;
constexpr uint64_t kCompositePixel = 0xF5F4D761A981B87DULL;
constexpr uint64_t kBlurPixel = 0x4DFCBC49057E5721ULL;
constexpr uint64_t kShadowProjectVertex = 0x4B41B10E837C6871ULL;
constexpr uint64_t kShadowProjectPixel = 0x4228617C65DB9D17ULL;
constexpr uint64_t kStagePreviewPixel = 0x046E7D32000FC3F3ULL;
constexpr uint64_t kHudVertex = 0xF37B3AC56DA38C91ULL;
constexpr uint64_t kHudPixel = 0x1BFD0840B053E193ULL;
constexpr uint64_t kSkinnedOutlineVertex = 0x9B2CDBD500DB8645ULL;
constexpr uint64_t kSkinnedMaterialVertex = 0xF3AC1AA2FE3EA253ULL;
constexpr uint64_t kFriezaSkinnedVertex = 0x2DD4268D7EE12280ULL;
constexpr uint64_t kFriezaRigidVertex = 0x07096A25A5490DE0ULL;
constexpr std::array<uint64_t, 5> kModelEffectPixels{
    0x5F27AACEB38B1088ULL, 0xA4A965C189287B99ULL, 0x052330CFA0039030ULL,
    0x4EABC3D0340E935CULL, 0xF37B5A4DE697AE01ULL,
};
constexpr uint64_t kBloomBlurPixel = 0x555EBAAEF8F61839ULL;
constexpr uint64_t kDepthRestoreVertex = 0x95DD91E3E9AE3A74ULL;
constexpr uint64_t kDepthRestorePixel = 0xADCA7FBBCBF53B57ULL;
constexpr uint64_t kCopyPixel = 0x6E57A6E305D5D69BULL;
constexpr uint64_t kFullScreenStrip = 0x54728E4E265F591FULL;
constexpr uint64_t kMapDistortionPixel = 0xB2A76EBF50959AADULL;
constexpr uint64_t kFightEffectVertex = 0x432273B438E1AB50ULL;
using PostCapture = x360::RenderDk::CapturaPosProcesso;
constexpr uint32_t kMaxShaderWords = 4096;
constexpr uint64_t kClearVertex = 0x1E6883FCCDE1F688ULL;
constexpr uint64_t kColorPixel = 0xA4A965C189287B99ULL;

struct FrameCounters {
  uint32_t draws = 0;
  uint32_t failed_draws = 0;
  uint32_t unknown_vs = 0;
  uint32_t unknown_ps = 0;
  uint32_t copies = 0;
  uint32_t failed_copies = 0;
  uint32_t draws_3d = 0;
  uint32_t missing_vs_3d = 0;
  bool combat_post = false;
  uint64_t failed_vs_hash = 0;
  uint64_t failed_ps_hash = 0;
};

struct DrawPassKey {
  uint64_t vs, ps;
  uint32_t edram, color, color_mask, depth_target, viewport, depth, primitive, after_copy;
  uint32_t color_control, blend0, alpha_ref;
  bool operator==(const DrawPassKey&) const = default;
};

struct DrawPass {
  DrawPassKey key;
  uint32_t draws = 0, recorded = 0, failed = 0;
  uint32_t first_draw = 0;
  uint32_t raster = 0, clip = 0, surface = 0;
  uint32_t scissor_tl = 0, scissor_br = 0;
  uint32_t screen_tl = 0, screen_br = 0, stencil = 0, stencil_bf = 0;
  uint32_t fetch0 = 0, fetch1 = 0, fetch2 = 0, fetch3 = 0;
  uint32_t vs_export = 0, ps_export = 0;
};

struct CopyPass {
  uint32_t after_draw, base, pitch, info, control, color, depth;
  bool success, substituted;
};

class DekoShader final : public Shader {
 public:
  using Shader::Shader;
  const x360::EntradaShader* deko_entry = nullptr;
  uint8_t reported_scene_usage = 0;
  bool vertex_probe_checked = false;
  bool vertex_probe_supported = false;
};

class DekoCommandProcessor final : public CommandProcessor {
 public:
  DekoCommandProcessor(GraphicsSystem* system, rex::system::KernelState* kernel,
                       const std::filesystem::path& shader_path, DekoOptions& options)
      : CommandProcessor(system, kernel), shader_path_(shader_path), options_(options) {}

  void IssueSwap(uint32_t frontbuffer, uint32_t width, uint32_t height) override {
    if (!render_ || !graphics_system_->presenter()) return;

    FlushCompositeDraw();
    if (gamma_dirty_) {
      std::array<std::array<uint16_t, 3>, 256> gamma{};
      const auto* source = gamma_ramp_256_entry_table();
      for (size_t i = 0; i < gamma.size(); ++i) {
        gamma[i] = {uint16_t(source[i].color_10_red), uint16_t(source[i].color_10_green),
                    uint16_t(source[i].color_10_blue)};
      }
      render_->RampaGamma(gamma);
      gamma_dirty_ = false;
    }

    auto swap = x360::LeerTexturaSwap(register_file_->values);
    const uint32_t fetch_base = swap.dword[1] & 0x1ffff000u;
    // The standard Vulkan path reads fetch slot zero. Keep that descriptor
    // authoritative when it names a source; the Swap packet supplies the visible extent.
    if (!fetch_base && frontbuffer) {
      swap.dword[1] = (swap.dword[1] & 0xfffu) | (frontbuffer & 0x1ffff000u);
    }
    ++swaps_;
    const bool captured_scene = swaps_ == scene_capture_;
    if (captured_scene || swaps_ <= state_capture_until_) {
      REXLOG_INFO("[B3 Deko quadro] swap={} packet={:08X} fetch={:08X} "
                  "fetch_words={:08X}/{:08X}/{:08X}/{:08X}/{:08X}/{:08X} size={}x{} "
                  "draws={} failed={} unknown_vs={} unknown_ps={} last_failed={:016X}/{:016X} "
                  "copies={} copy_failures={}",
                  swaps_, frontbuffer, fetch_base, swap.dword[0], swap.dword[1], swap.dword[2],
                  swap.dword[3], swap.dword[4], swap.dword[5], width, height, frame_.draws,
                  frame_.failed_draws, frame_.unknown_vs, frame_.unknown_ps, frame_.failed_vs_hash,
                  frame_.failed_ps_hash, frame_.copies, frame_.failed_copies);
      LogPasses();
      if (captured_scene) scene_capture_ = 0;
    }
    const bool presented = render_->Presentar(graphics_system_->presenter(), swap, width, height);
    if (!presented) ++missed_swaps_;
    pacer_.Pace();
    render_->FijarPeriodoAlvo(pacer_.PeriodMs());
    TickGpuBoost();
    const bool manual_scene = ManualCaptureRequested();
    detailed_capture_ = manual_scene ? swaps_ + 1 : 0;
    if (manual_scene) {
      world_shadow_logs_ = 0;
      state_capture_until_ = swaps_ + (REXCVAR_GET(b3_deko_diagnostico_completo) ? 3 : 1);
      scene_capture_ = swaps_ + 1;
      REXLOG_INFO("[B3 Deko captura] quadro={} gatilho=controle desenhos_3d={} VS_3d_ausentes={}",
                  scene_capture_, frame_.draws_3d, frame_.missing_vs_3d);
      render_->PedirVolcadoEncadeado(scene_capture_, REXCVAR_GET(b3_deko_diagnostico_completo));
    }
    draw_passes_.clear();
    copy_passes_.clear();
    unsampled_draws_ = 0;
    attempted_draws_ += frame_.draws;
    failed_draws_ += frame_.failed_draws;
    unknown_vs_draws_ += frame_.unknown_vs;
    unknown_ps_draws_ += frame_.unknown_ps;
    copy_attempts_ += frame_.copies;
    failed_copies_ += frame_.failed_copies;
    previous_combat_frame_ = frame_.combat_post;
    frame_ = {};
    bloom_blur_draw_ = false;
    shadow_resolve_ = 0;
    model_redraw_ = false;
    model_checkpoint_ = false;
    hud_traces_ = 0;
    effect_traces_ = 0;
    probes_per_pass_.clear();
    captured_inputs_.fill(0);
    dialogue_sprites_captured_ = 0;
    hud_dumps_ = 0;
    dragon_rush_dumps_ = 0;
    scene_checkpoints_ = 0;
    captured_effects_ = 0;
    UpdateOptions();
    last_post_pass_.reset();
    composite_source_ = 0;
    if (swaps_ == 1 || swaps_ % 300 == 0) {
      const auto stats = render_->EstadisticasDeDibujos();
      REXLOG_INFO("Xenos/Deko: swaps={} missed={} draws={}/{} failed={} rejected={} unknown_shaders={} "
                  "draw_unknown_vs={} draw_unknown_ps={} copies={} copies_failed={} composite_copies={}/{} edram_clears={}",
                  swaps_, missed_swaps_, stats.dibujados, attempted_draws_, failed_draws_,
                  stats.rechazados, unknown_shaders_, unknown_vs_draws_, unknown_ps_draws_,
                  copy_attempts_, failed_copies_, composite_copies_, composite_attempts_, scene_clears_);
      REXLOG_INFO("[B3 Deko desempenho] swap={} finais_omitidos={} leituras_bloom_omitidas={} "
                  "bloom_omitido={} z_restaurado={} dof_leve_pedidos={} sombras_omitidas={} sombras_com_sesgo={}",
                  swaps_, desenhos_finais_omitidos_, leituras_bloom_omitidas_, bloom_omitidos_, restaura_z_,
                  dof_leve_pedidos_, sombras_omitidas_, sombras_com_sesgo_);
    }
  }

 protected:
  bool SetupContext() override {
    auto* provider = static_cast<Provider*>(graphics_system_->provider());
    if (!provider || !library_.Cargar(shader_path_)) {
      REXLOG_ERROR("Xenos/Deko: failed to load shader library {}", shader_path_.string());
      return false;
    }
    render_ = std::make_unique<x360::RenderDk>(*provider, memory_);
    diagnostic_ = REXCVAR_GET(b3_deko_diagnostico);
    REXLOG_INFO("[B3 Deko build] 0.1.50: achievements detail card (A opens, B back); audio from 0.1.49 (one Audio Worker per client)");
    direct_composite_ = REXCVAR_GET(b3_deko_copia_final);
    REXLOG_INFO("Xenos/Deko: copia final sem FXAA {}", direct_composite_ ? "ativada" : "desativada");
    if (diagnostic_) {
      draw_passes_.reserve(kMaxPasses);
      copy_passes_.reserve(kMaxCopyRows);
      if (R_SUCCEEDED(hidInitialize())) {
        padInitializeAny(&capture_pad_);
        capture_pad_ready_ = true;
      }
    }
    REXLOG_INFO("Xenos/Deko: diagnostico {} (captura manual: {})",
                diagnostic_ ? "ativado" : "desativado", capture_pad_ready_ ? "L3+R3" : "indisponivel");
    const uint32_t options =
        (REXCVAR_GET(b3_deko_reduzir_copias) ? uint32_t(kReduzirCopias) : 0u) |
        (REXCVAR_GET(b3_deko_desfoque_leve) ? uint32_t(kDesfoqueLeve) : 0u) |
        (REXCVAR_GET(b3_deko_bloom) ? uint32_t(kBloom) : 0u) |
        (REXCVAR_GET(b3_deko_restaurar_z) ? uint32_t(kRestaurarZ) : 0u) |
        (REXCVAR_GET(b3_deko_sombras_projetadas) ? uint32_t(kSombrasProjetadas) : 0u) |
        DofResolutionOption(REXCVAR_GET(b3_deko_desfoque_resolucao));
    options_.store(options, std::memory_order_relaxed);
    UpdateOptions();
    return true;
  }

  void ShutdownContext() override {
    if (capture_pad_ready_) {
      hidExit();
      capture_pad_ready_ = false;
    }
    render_.reset();
  }

  void OnGammaRamp256EntryTableValueWritten() override { gamma_dirty_ = true; }

  void OnPrimaryBufferEnd() override { FlushCompositeDraw(); }

  void PrepareForWait() override {
    FlushCompositeDraw();
    // The guest may reuse an upload range once the standard CP returns from a
    // fence. Tell the shared renderer to forget per-range upload reuse.
    x360::g_sincronizaciones_anillo.fetch_add(1, std::memory_order_relaxed);
    if (render_) render_->EnviarOclusiones();
  }

  void WriteRegister(uint32_t index, uint32_t value) override {
    if (index < RegisterFile::kRegisterCount && (*register_file_)[index] != value) {
      if (index >= 0x4000 && index < 0x4400) ++vs_constants_;
      if (index >= 0x4400 && index < 0x4800) ++ps_constants_;
      if (index >= 0x4800 && index < 0x48c0) ++fetch_;
      // RenderDk caches the viewport and scissor per render pass. Draw
      // initiators and other unrelated GPU writes do not invalidate it.
      if ((index >= XE_GPU_REG_PA_SC_WINDOW_OFFSET && index <= XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR) ||
          (index >= XE_GPU_REG_PA_CL_VPORT_XSCALE && index <= XE_GPU_REG_PA_CL_VPORT_ZOFFSET) ||
          (index >= XE_GPU_REG_PA_CL_CLIP_CNTL && index <= XE_GPU_REG_PA_CL_VTE_CNTL) ||
          index == XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL || index == XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR ||
          index == XE_GPU_REG_PA_SU_VTX_CNTL || index == XE_GPU_REG_PA_SU_SC_MODE_CNTL) {
        ++raster_;
      }
    }
    CommandProcessor::WriteRegister(index, value);
  }

  Shader* LoadShader(xenos::ShaderType stage, uint32_t guest_address, const uint32_t* words,
                     uint32_t count) override {
    if (!count) return nullptr;
    const size_t bytes = size_t(count) * sizeof(uint32_t);
    std::string key(1, char(stage));
    key.append(reinterpret_cast<const char*>(words), bytes);
    if (auto it = shaders_.find(key); it != shaders_.end()) return it->second.get();

    auto shader = std::make_unique<DekoShader>(stage, XXH3_64bits(words, bytes), words, count);
    const bool vertex = stage == xenos::ShaderType::kVertex;
    const auto* match = library_.Identificar(vertex, shader->ucode_data());
    shader->deko_entry = ConvertShader(match);
    if (diagnostic_ && vertex && shader->deko_entry && matched_vertices_ < kMaxShaderDumps) {
      ++matched_vertices_;
      REXLOG_INFO("[B3 Deko shader] VS {:016X} -> {:016X}, inputs={:08X}", shader->ucode_data_hash(),
                  shader->deko_entry->huella_contenedor, shader->deko_entry->mascara_entradas_renut);
    }
    if (!shader->deko_entry) {
      ++unknown_shaders_;
      if (unknown_shaders_ <= kMaxShaderDumps) {
        REXLOG_WARN("Xenos/Deko: unidentified {} shader, {} words, hash {:016X}, guest {:08X}",
                    vertex ? "vertex" : "pixel", count, shader->ucode_data_hash(), guest_address);
      }
      if (diagnostic_ && shader_dumps_ < kMaxShaderDumps && count <= kMaxShaderWords) {
        DumpShader(vertex, shader->ucode_data_hash(), words, count);
        ++shader_dumps_;
      }
    }
    return shaders_.emplace(std::move(key), std::move(shader)).first->second.get();
  }

  bool IssueDraw(xenos::PrimitiveType primitive, uint32_t count, IndexBufferInfo*, bool) override {
    if (!render_) return false;
    const auto mode = register_file_->Get<reg::RB_MODECONTROL>().edram_mode;
    if (mode == xenos::EdramMode::kCopy) return IssueCopy();
    if (mode == xenos::EdramMode::kNoOperation || !count) return true;
    FlushCompositeDraw();
    composite_source_ = 0;
    bloom_blur_draw_ = false;

    auto* vs = static_cast<DekoShader*>(active_vertex_shader());
    const auto* ps = static_cast<const DekoShader*>(active_pixel_shader());
    if (vs != last_vs_) {
      last_vs_ = vs;
      ++vs_generation_;
    }

    x360::PeticionDibujo draw{};
    draw.registros = register_file_->values;
    draw.vs = vs ? vs->deko_entry : nullptr;
    draw.ps = ps ? ps->deko_entry : nullptr;
    if (vs) draw.vs_microcodigo = vs->ucode_data();
    draw.generacion_vs = vs_generation_;
    draw.generacion_constantes_vs = vs_constants_;
    draw.generacion_constantes_ps = ps_constants_;
    draw.generacion_fetch = fetch_;
    draw.generacion_encuadre = raster_;
    ++frame_.draws;
    if (vs && !draw.vs) ++frame_.unknown_vs;
    if (mode == xenos::EdramMode::kColorDepth && ps && !draw.ps) ++frame_.unknown_ps;
    const auto& r = *register_file_;
    const bool geometry = diagnostic_ && count >= 12 &&
                          (r[XE_GPU_REG_RB_DEPTHCONTROL] & 3u) != 0 &&
                          (r[XE_GPU_REG_PA_CL_VTE_CNTL] & 0xFu) != 0;
    if (geometry) {
      ++frame_.draws_3d;
      if (vs && !draw.vs) {
        ++frame_.missing_vs_3d;
        const uint8_t mode_flag = mode == xenos::EdramMode::kDepthOnly ? 2 : 1;
        if (!(vs->reported_scene_usage & mode_flag) && shader_examples_ < 2 * kMaxShaderDumps) {
          vs->reported_scene_usage |= mode_flag;
          ++shader_examples_;
          LogMissingVertex(*vs, ps);
        }
      }
    }
    const bool sampling = IsSamplingFrame();
    const uint64_t recorded_before = sampling ? render_->DibujosGrabados() : 0;
    const bool cleared = TrySceneClear(primitive, count, vs, ps);
    if (diagnostic_ && !cleared && vs && vs->ucode_data_hash() == kClearVertex &&
        (r[XE_GPU_REG_RB_SURFACE_INFO] & 0x3FFFFu) == 0x20280u && scene_clear_fallbacks_++ < 4) {
      const auto fetch = r.GetVertexFetch(0);
      REXLOG_INFO("[B3 Deko EDRAM] 4x fallback swap={} mode={} draw={:08X} fetch={:08X}/{:08X} "
                  "depth={:08X} scissor={:08X}/{:08X} offset={:08X}",
                  swaps_ + 1, uint32_t(mode), r[XE_GPU_REG_VGT_DRAW_INITIATOR], fetch.dword_0, fetch.dword_1,
                  r[XE_GPU_REG_RB_DEPTHCONTROL], r[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL],
                  r[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR], r[XE_GPU_REG_PA_SC_WINDOW_OFFSET]);
    }
    const uint64_t vs_hash = vs ? vs->ucode_data_hash() : 0;
    const uint64_t ps_hash = ps ? ps->ucode_data_hash() : 0;
    const auto fetch0 = r.GetTextureFetch(0);
    const uint32_t source = fetch0.dword_1 & 0x1ffff000u;
    const bool full_screen_strip = vs_hash == kFullScreenStrip &&
                                   primitive == xenos::PrimitiveType::kTriangleStrip &&
                                   mode == xenos::EdramMode::kColorDepth;
    if (full_screen_strip && ps_hash == kMapDistortionPixel &&
        draw.ps && count == 4 && (r[XE_GPU_REG_RB_SURFACE_INFO] & 0x3FFFFu) == 1280u &&
        r[XE_GPU_REG_RB_COLOR_INFO] == 0 && r[XE_GPU_REG_RB_COLOR_MASK] == 0xF &&
        (r[XE_GPU_REG_RB_DEPTHCONTROL] & 3u) == 0 &&
        (r[XE_GPU_REG_RB_BLENDCONTROL0] & 0x1FFF1FFFu) == 0x00010001u) {
      // The shader scales its mask displacement with pixel constant c0.x.
      // Zero the displacement while keeping the scene copy and the next resolve.
      std::memcpy(postprocess_registers_.data(), r.values, sizeof(r.values));
      postprocess_registers_[0x4400] = 0;
      draw.registros = postprocess_registers_.data();
      draw.generacion_constantes_ps ^= 1ull << 63;
      if (!reported_map_distortion_) {
        REXLOG_INFO("[B3 Deko campanha] distorcao desativada swap={} draw={}", swaps_ + 1, frame_.draws);
        reported_map_distortion_ = true;
      }
    }
    draw.dof_reduzido = desfoque_leve_ && full_screen_strip && ps_hash == kBlurPixel &&
                        (r[XE_GPU_REG_RB_SURFACE_INFO] & 0x3FFFFu) == 1280u;
    if (dof_height_ != 720 && full_screen_strip && ps_hash == kBlurPixel && count == 4 &&
        (r[XE_GPU_REG_RB_SURFACE_INFO] & 0x3FFFFu) == 1280u &&
        r[XE_GPU_REG_RB_COLOR_INFO] == 0 && r[XE_GPU_REG_RB_COLOR_MASK] == 0xF &&
        (r[XE_GPU_REG_RB_DEPTHCONTROL] & 3u) == 0 && (r[XE_GPU_REG_RB_COLORCONTROL] & 0x8u) == 0 &&
        (r[XE_GPU_REG_RB_BLENDCONTROL0] & 0x1FFF1FFFu) == 0x00010001u &&
        (fetch0.dword_2 & 0x03FFFFFFu) == 0x0059E4FFu && r[XE_GPU_REG_PA_SC_WINDOW_OFFSET] == 0 &&
        r[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL] == 0 &&
        r[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR] == 0x02D00500u) {
      draw.altura_desfoque_b3 = dof_height_;
    }
    dof_leve_pedidos_ += draw.dof_reduzido;
    const bool compose_bloom = frame_.combat_post && full_screen_strip && ps_hash == kCopyPixel &&
                               r[XE_GPU_REG_RB_COLOR_INFO] == 0 && r[XE_GPU_REG_RB_COLOR_MASK] == 0xF &&
                               (r[XE_GPU_REG_RB_SURFACE_INFO] & 0x3FFFFu) == 1280u &&
                               (r[XE_GPU_REG_RB_DEPTHCONTROL] & 7u) == 0 &&
                               (r[XE_GPU_REG_RB_BLENDCONTROL0] & 0x1FFF1FFFu) == 0x01010106u &&
                               (fetch0.dword_2 & 0x03FFFFFFu) == 0x0005804Fu;
    const bool final_composite = direct_composite_ && draw.vs && draw.ps &&
                                 draw.vs->huella_contenedor == kCompositeVertexContainer &&
                                 ps_hash == kCompositePixel && mode == xenos::EdramMode::kColorDepth &&
                                 primitive == xenos::PrimitiveType::kRectangleList && count == 3 &&
                                 (r[XE_GPU_REG_RB_DEPTHCONTROL] & 3u) == 0;
    // This draw can be omitted only after its immediately following copy is
    // known to take all its pixels from the same frame's pre-FXAA texture.
    const bool defer_composite = reduzir_copias_ && final_composite && source && !sampling &&
                                r[XE_GPU_REG_RB_COLOR_INFO] == 0 && r[XE_GPU_REG_RB_COLOR_MASK] == 0xF &&
                                (r[XE_GPU_REG_RB_SURFACE_INFO] & 0x3FFFFu) == 1280u &&
                                (r[XE_GPU_REG_RB_BLENDCONTROL0] & 0x1FFF1FFFu) == 0x00010001u;
    if (defer_composite) {
      std::memcpy(postprocess_registers_.data(), r.values, sizeof(r.values));
      composite_draw_ = draw;
      composite_draw_->registros = postprocess_registers_.data();
      composite_source_ = source;
      last_post_pass_.reset();
      return true;
    }
    bool restored_depth = false;
    const uint32_t depth_alias = r[XE_GPU_REG_RB_DEPTH_INFO];
    const uint32_t surface_width = r[XE_GPU_REG_RB_SURFACE_INFO] & 0x3FFFFu;
    const bool detailed = swaps_ + 1 == detailed_capture_;
    const bool full_capture = detailed && REXCVAR_GET(b3_deko_diagnostico_completo);
    const bool redraw_after_shadow = shadow_resolve_ && frame_.copies == 3 && surface_width == 1280 &&
        depth_alias == 0x530 && mode == xenos::EdramMode::kColorDepth &&
        r[XE_GPU_REG_RB_COLOR_INFO] == 0 && r[XE_GPU_REG_RB_COLOR_MASK] == 7 &&
        r[XE_GPU_REG_RB_DEPTHCONTROL] == 0x00710236u;
    const bool outline_after_shadow = redraw_after_shadow && vs_hash == kSkinnedOutlineVertex && ps_hash == kColorPixel;
    if (detailed && redraw_after_shadow) {
      if (outline_after_shadow && !model_redraw_) {
        render_->VolcarCena(0, surface_width, frame_.draws, false);
        model_redraw_ = true;
        REXLOG_INFO("[B3 Deko modelo] swap={} draw={} comeca_contorno_apos_sombra",
                    swaps_ + 1, frame_.draws);
      } else if (model_redraw_ && !model_checkpoint_ && vs_hash == kSkinnedMaterialVertex &&
                 ps_hash == kModelEffectPixels.front()) {
        render_->VolcarCena(0, surface_width, frame_.draws, false);
        model_checkpoint_ = true;
        REXLOG_INFO("[B3 Deko modelo] swap={} draw={} comeca_material_apos_contorno",
                    swaps_ + 1, frame_.draws);
      }
    }
    const uint32_t depth_height = depth_alias == 0x530 && surface_width == 1280 ? 720 :
                                  depth_alias == 0x514 && surface_width == 640 ? 360 :
                                  depth_alias == 0x514 && surface_width == 320 ? 180 : 0;
    if (restaurar_z_ && mode == xenos::EdramMode::kColorDepth && vs_hash == kDepthRestoreVertex &&
        ps_hash == kDepthRestorePixel && primitive == xenos::PrimitiveType::kRectangleList && count == 3 &&
        depth_height && r[XE_GPU_REG_RB_COLOR_INFO] == depth_alias &&
        r[XE_GPU_REG_RB_COLOR_MASK] == 0xF && (r[XE_GPU_REG_RB_DEPTHCONTROL] & 3u) == 0 &&
        r[XE_GPU_REG_PA_SC_WINDOW_OFFSET] == 0 && r[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL] == 0 &&
        r[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR] == ((depth_height << 16) | surface_width) &&
        (fetch0.dword_2 & 0x03FFFFFFu) == (((depth_height - 1) << 13) | (surface_width - 1))) {
      if (full_capture && depth_alias == 0x530 && frame_.draws_3d >= 20 && scene_checkpoints_ < 2) {
        render_->VolcarCena(0, surface_width, frame_.draws, false);
        ++scene_checkpoints_;
      }
      restored_depth = render_->RestaurarZDesdeResuelta(source, depth_alias, surface_width, depth_height);
      restaura_z_ += restored_depth;
    }
    const uint32_t depth_control = r[XE_GPU_REG_RB_DEPTHCONTROL];
    if (detailed && (depth_control & 1u) && world_shadow_logs_ < 64) {
      ++world_shadow_logs_;
      REXLOG_INFO("[B3 Deko sombra mundo] swap={} draw={} vs={:016X} ps={:016X} depth={:08X} stencil={:08X}/{:08X} cor_mask={:X}",
                  swaps_ + 1, frame_.draws, vs_hash, ps_hash, depth_control, r[XE_GPU_REG_RB_STENCILREFMASK],
                  r[XE_GPU_REG_RB_STENCILREFMASK_BF], r[XE_GPU_REG_RB_COLOR_MASK]);
    }
    // Keep the shadow pass's depth/stencil tests and omit only its color writes.
    const bool skip_shadow = !sombras_projetadas_ && vs_hash == kShadowProjectVertex &&
                             ps_hash == kShadowProjectPixel && shadow_resolve_ && source == shadow_resolve_ &&
                             r[XE_GPU_REG_RB_SURFACE_INFO] == 0x14000500 &&
                             r[XE_GPU_REG_RB_COLOR_INFO] == 0 && r[XE_GPU_REG_RB_COLOR_MASK] == 7;
    draw.omitir_cor = skip_shadow;
    {
      // Shadow projection (Z LEQUAL + stencil EQUAL 1 over the restored depth): pull it towards the camera.
      const int sesgo = std::clamp(REXCVAR_GET(b3_deko_sombra_sesgo), 0, 4096);
      const bool projecao = sesgo && vs_hash == kShadowProjectVertex && ps_hash == kShadowProjectPixel &&
                            (depth_control & 2u);
      draw.sesgo_b3[0] = projecao ? -float(sesgo) : 0.0f;
      draw.sesgo_b3[1] = projecao ? -2.0f : 0.0f;
      sombras_com_sesgo_ += projecao;
    }
    draw.texturas_dinamicas = ps_hash == kStagePreviewPixel;
    if (draw.texturas_dinamicas) {
      const auto u = r.GetTextureFetch(1);
      const auto v = r.GetTextureFetch(2);
      draw.validar_planos_yuv =
          fetch0.format == xenos::TextureFormat::k_8 && u.format == xenos::TextureFormat::k_8 &&
          v.format == xenos::TextureFormat::k_8 && source && u.base_address && v.base_address &&
          u.type == xenos::FetchConstantType::kTexture && v.type == xenos::FetchConstantType::kTexture;
    }
    sombras_omitidas_ += skip_shadow;
    const bool skip_bloom = compose_bloom && !bloom_;
    bloom_omitidos_ += skip_bloom;
    if (full_capture && full_screen_strip && ps_hash == kBlurPixel) {
      render_->VolcarPosProcesso(source, PostCapture::EntradaDesfoque);
    }
    if (full_capture && compose_bloom) {
      render_->VolcarPosProcesso(source, PostCapture::EntradaBloom);
    }
    if (detailed && frame_.combat_post && vs_hash == kHudVertex &&
        ps_hash == kHudPixel && depth_alias == 0x530 && surface_width == 1280) {
      if (depth_control == 0x00708772) render_->VolcarMascaraHud(false);
      if (depth_control == 0x0070C273) render_->VolcarMascaraHud(true);
    }
    const uint32_t color_info = r[XE_GPU_REG_RB_COLOR_INFO];
    uint32_t capture_effect = 0;
    if (full_capture && color_info == 0 && surface_width == 1280) {
      if (vs_hash == kFullScreenStrip && ps_hash == kMapDistortionPixel) capture_effect = 1;
      if (vs_hash == kFightEffectVertex && ps_hash == kHudPixel) capture_effect = 2;
    }
    const bool capture_effect_here = capture_effect && !(captured_effects_ & capture_effect);
    if (capture_effect_here) {
      captured_effects_ |= capture_effect;
      const auto f1 = r.GetTextureFetch(1);
      REXLOG_INFO("[B3 Deko efeito] swap={} draw={} vs={:016X} ps={:016X} tex0={:08X}/{:08X}/{:08X}/{:08X} "
                  "tex1={:08X}/{:08X}/{:08X}/{:08X} c0={:08X}/{:08X}/{:08X}/{:08X}",
                  swaps_ + 1, frame_.draws, vs_hash, ps_hash, fetch0.dword_0, fetch0.dword_1, fetch0.dword_2,
                  fetch0.dword_3, f1.dword_0, f1.dword_1, f1.dword_2, f1.dword_3,
                  r.values[0x4400], r.values[0x4401], r.values[0x4402], r.values[0x4403]);
      if (capture_effect == 1) {
        render_->VolcarEntrada(source, frame_.draws, 0);
        render_->VolcarEntrada(f1.dword_1 & 0x1FFFF000u, frame_.draws, 1);
      } else {
        render_->VolcarCena(color_info, surface_width, frame_.draws, false);
      }
    }
    const bool combat_hud = frame_.combat_post && vs_hash == kHudVertex && ps_hash == kHudPixel &&
                            surface_width == 1280;
    const bool dialogue_sprite = detailed && !frame_.combat_post && draw.vs && draw.ps &&
        vs_hash == kHudVertex && ps_hash == kHudPixel && surface_width == 1280 && color_info == 0 &&
        depth_alias == 0x530 && (depth_control == 0x00708736 || depth_control == 0x00708772) &&
        (frame_.copies == 2 || frame_.copies == 4) && primitive == xenos::PrimitiveType::kTriangleList &&
        dialogue_sprites_captured_ < 6;
    if (dialogue_sprite) {
      if (dialogue_sprites_captured_++ == 0) render_->VolcarCena(color_info, surface_width, frame_.draws, false);
      SaveDrawInputs(*vs, *ps, count, false, false);
      draw.diagnostico_texturas_b3 = frame_.draws;
      REXLOG_INFO("[B3 Deko dialogo] swap={} draw={} vertices={} z={:08X} tex={:08X}/{:08X}/{:08X}",
                  swaps_ + 1, frame_.draws, count, depth_control, fetch0.dword_0, fetch0.dword_1, fetch0.dword_2);
    }
    DragonRushIconVertices rush_icon;
    if (combat_hud && draw.vs && draw.ps && color_info == 0 &&
        primitive == xenos::PrimitiveType::kTriangleList && count == 6 &&
        depth_control == 0x00708772 && fetch0.type == xenos::FetchConstantType::kTexture &&
        fetch0.format == xenos::TextureFormat::k_8_8_8_8 &&
        (fetch0.dword_2 & 0x03FFFFFFu) == 0x003FE0FFu && ReadDragonRushIcon(rush_icon)) {
      draw.ranura_vertices_modificados = 95;
      draw.vertices_modificados = rush_icon;
      if (capture_rush_audio_) {
        capture_rush_audio_ = false;
        rex::audio::RequestRenderClientAudioCapture();
        REXLOG_INFO("[B3 audio fonte] Dragon Rush swap={} draw={}, captura de 4 s por cliente",
                    swaps_ + 1, frame_.draws);
      }
      if (!reported_switch_rush_) {
        REXLOG_INFO("[B3 Deko Dragon Rush] layout Switch aplicado swap={} draw={}", swaps_ + 1, frame_.draws);
        reported_switch_rush_ = true;
      }
    }
    const bool skinned_outline = vs_hash == kSkinnedOutlineVertex && ps_hash == kColorPixel;
    const bool skinned_material = vs_hash == kSkinnedMaterialVertex && ps_hash == kModelEffectPixels.front();
    const bool combat_model = surface_width == 1280 && depth_alias == 0x530 && color_info == 0 &&
                              (skinned_outline || skinned_material);
    if (detailed && draw.vs && draw.ps && ps_hash == kModelEffectPixels.front() &&
        (vs_hash == kFriezaSkinnedVertex || vs_hash == kFriezaRigidVertex)) {
      auto& captures = captured_inputs_[vs_hash == kFriezaSkinnedVertex ? 5 : 6];
      if (captures < 4) {
        ++captures;
        SaveDrawInputs(*vs, *ps, count, vs_hash == kFriezaSkinnedVertex, false);
      }
    }
    if (detailed && (combat_hud || combat_model) && vs && ps && vs->deko_entry && ps->deko_entry) {
      const bool after_shadow = shadow_resolve_ && frame_.copies == 3;
      if (combat_hud || frame_.copies == 0 || after_shadow) {
        const size_t group = combat_hud ? 4u : size_t(after_shadow) * 2u + size_t(skinned_material);
        if (captured_inputs_[group] < (combat_hud ? 48u : (after_shadow ? 32u : 16u))) {
          ++captured_inputs_[group];
          SaveDrawInputs(*vs, *ps, count, combat_model, after_shadow);
          if ((skinned_material || combat_hud) && captured_inputs_[group] <= (combat_hud ? 4u : 2u)) {
            draw.diagnostico_texturas_b3 = frame_.draws;
          }
        }
      }
    }
    const bool hud_capture = full_capture && combat_hud;
    // The first quads draw the clock and masks. Capture the later sprites that actually fill the bars.
    const bool hud_checkpoint = hud_capture &&
        ((hud_dumps_ >= 7 && hud_dumps_ < 27) || (hud_dumps_ < 48 && hud_dumps_ % 4 == 0));
    if (hud_capture && !hud_dumps_) render_->VolcarCorHud(color_info, 1280, frame_.draws, false);
    const bool model_capture = geometry && color_info == 0 && (surface_width == 1280 || surface_width == 640) &&
        (std::ranges::find(kModelEffectPixels, ps_hash) != kModelEffectPixels.end() ||
         vs_hash == 0xFDF960B5D7869030ULL || vs_hash == 0x4D5BDBCCEDEB1BB5ULL || vs_hash == 0x34E48B1366739F9BULL);
    if (full_capture && vs && draw.vs && (hud_capture || model_capture)) {
      if (!vs->vertex_probe_checked) {
        rex::string::StringBuffer disassembly;
        vs->AnalyzeUcode(disassembly);
        vs->vertex_probe_supported = ShaderInterpreter::CanInterpretShader(*vs);
        vs->vertex_probe_checked = true;
      }
      if (vs->vertex_probe_supported) {
        const uint32_t stream = hud_capture ? 0 : r.GetVertexFetch(95).dword_0;
        const std::array<uint64_t, 5> key{vs_hash, ps_hash, stream, surface_width, depth_control};
        auto pass = probes_per_pass_.find(key);
        if (pass == probes_per_pass_.end() && probes_per_pass_.size() < 40) {
          pass = probes_per_pass_.emplace(key, 0).first;
        }
        if (pass != probes_per_pass_.end() && pass->second < (hud_capture ? 6u : 2u)) {
          ++pass->second;
          draw.diagnostico_dibujo = frame_.draws;
        }
      }
    }
    const bool accepted = cleared || restored_depth || skip_bloom || render_->Dibujar(draw);
    if (detailed && !full_capture && frame_.combat_post && color_info == 0 && surface_width == 1280 &&
        draw.vs && draw.ps && vs && ps && mode == xenos::EdramMode::kColorDepth && source &&
        !full_screen_strip && render_->DibujosGrabados() != recorded_before && dragon_rush_dumps_ < 48) {
      ++dragon_rush_dumps_;
      render_->VolcarDragonRush(surface_width, frame_.draws);
      if (!combat_hud) SaveDrawInputs(*vs, *ps, count, false, false);
      REXLOG_INFO("[B3 Deko Dragon Rush] swap={} draw={} vs={:016X} ps={:016X} count={} tex={:08X}/{:08X}",
                  swaps_ + 1, frame_.draws, vs_hash, ps_hash, count, fetch0.dword_1, fetch0.dword_2);
    }
    if (accepted && capture_effect_here) render_->VolcarCena(color_info, surface_width, frame_.draws, true);
    if (accepted && dialogue_sprite) render_->VolcarCena(color_info, surface_width, frame_.draws, true);
    if (hud_capture) {
      if (hud_checkpoint) render_->VolcarCorHud(color_info, 1280, frame_.draws, true);
      ++hud_dumps_;
    }
    if (sampling && skip_shadow && (depth_control & 1u)) {
      REXLOG_INFO("[B3 Deko sombra] swap={} draw={} cor=omitida stencil_gravado={}", swaps_ + 1, frame_.draws,
                  render_->DibujosGrabados() != recorded_before);
    }
    bloom_blur_draw_ = accepted && full_screen_strip && ps_hash == kBloomBlurPixel &&
                       (r[XE_GPU_REG_RB_SURFACE_INFO] & 0x3FFFFu) == 80u &&
                       r[XE_GPU_REG_RB_COLOR_INFO] == 0 && r[XE_GPU_REG_RB_DEPTH_INFO] == 0x514;
    if (sampling) {
      if (draw.ps && !draw.ps->literais.empty() && sampled_literals_.insert(ps_hash).second) {
        REXLOG_INFO("[B3 Deko constantes] swap={} draw={} ps={:016X} cbuf={} literais={} ultimo=c{}",
                    swaps_ + 1, frame_.draws, ps_hash, draw.ps->constantes_bytes,
                    draw.ps->literais.size(), draw.ps->literais.back().registro);
      }
      const bool recorded = cleared || restored_depth || render_->DibujosGrabados() != recorded_before;
      RecordDraw(primitive, vs, ps, accepted, recorded);
      if (geometry && std::ranges::find(kModelEffectPixels, ps_hash) != kModelEffectPixels.end() &&
          effect_traces_++ < 48) {
        const auto tex1 = r.GetTextureFetch(1);
        const auto stream = r.GetVertexFetch(95);
        const uint64_t vertex_constants = XXH3_64bits(r.values + 0x4000, 0x400 * sizeof(uint32_t));
        REXLOG_INFO("[B3 Deko modelo] swap={} draw={} vs={:016X} ps={:016X} count={} "
                    "index={:08X} stream={:08X}/{:08X} cbuf_vs={:016X} "
                    "tex={:08X}/{:08X} ps_c2={:08X}/{:08X}/{:08X}/{:08X} "
                    "depth={:08X} stencil={:08X} copies={} gravado={}",
                    swaps_ + 1, frame_.draws, vs_hash, ps_hash, count, r[XE_GPU_REG_VGT_DMA_BASE],
                    stream.dword_0, stream.dword_1, vertex_constants, fetch0.dword_1, tex1.dword_1,
                    r.values[0x4408], r.values[0x4409], r.values[0x440A], r.values[0x440B],
                    depth_control, r[XE_GPU_REG_RB_STENCILREFMASK], frame_.copies, recorded);
      }
      if (ps_hash == kStagePreviewPixel && hud_traces_++ < 24) {
        const auto u = r.GetTextureFetch(1);
        const auto v = r.GetTextureFetch(2);
        REXLOG_INFO("[B3 Deko miniatura] swap={} draw={} y={:08X}/{:08X}/{:08X} "
                    "u={:08X}/{:08X}/{:08X} v={:08X}/{:08X}/{:08X} luma_validada={} cbuf_ps={} literais_ps={}",
                    swaps_ + 1, frame_.draws, fetch0.dword_0, fetch0.dword_1, fetch0.dword_2,
                    u.dword_0, u.dword_1, u.dword_2, v.dword_0, v.dword_1, v.dword_2,
                    draw.validar_planos_yuv && fetch0.type == xenos::FetchConstantType::kInvalidTexture,
                    draw.ps ? draw.ps->constantes_bytes : 0, draw.ps ? draw.ps->literais.size() : 0);
      }
      if (frame_.combat_post && vs_hash == kHudVertex && ps_hash == kHudPixel && hud_traces_++ < 32) {
        REXLOG_INFO("[B3 Deko HUD] swap={} draw={} prim={} count={} src={:08X} stencil={:08X}/{:08X} "
                    "depth={:08X} blend={:08X} initiator={:08X} raster={:08X} reset={:08X} "
                    "index_size={:08X} scissor={:08X}/{:08X} window={:08X}/{:08X} "
                    "color={:08X} alpha={:08X}",
                    swaps_ + 1, frame_.draws, uint32_t(primitive), count, source,
                    r[XE_GPU_REG_RB_STENCILREFMASK], r[XE_GPU_REG_RB_STENCILREFMASK_BF], depth_control,
                    r[XE_GPU_REG_RB_BLENDCONTROL0], r[XE_GPU_REG_VGT_DRAW_INITIATOR],
                    r[XE_GPU_REG_PA_SU_SC_MODE_CNTL], r[XE_GPU_REG_VGT_MULTI_PRIM_IB_RESET_INDX],
                    r[XE_GPU_REG_VGT_DMA_SIZE], r[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL],
                    r[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR], r[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL],
                    r[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR], r[XE_GPU_REG_RB_COLORCONTROL],
                    r[XE_GPU_REG_RB_ALPHA_REF]);
      }
    }
    last_post_pass_.reset();
    if (accepted && full_screen_strip && (r[XE_GPU_REG_RB_SURFACE_INFO] & 0x3FFFu) == 1280u) {
      if (ps_hash == kBlurPixel) {
        frame_.combat_post = true;
        last_post_pass_ = PostCapture::Desfoque;
        if (sampling) {
          const auto color = r.GetTextureFetch(0);
          const auto depth = r.GetTextureFetch(1);
          REXLOG_INFO("[B3 Deko combate] swap={} desfoque cor={:08X} profundidade={:08X} "
                      "parametros={:08X}/{:08X}/{:08X}/{:08X}",
                      swaps_ + 1, color.dword_1 & 0x1ffff000u, depth.dword_1 & 0x1ffff000u,
                      r.values[0x4404], r.values[0x4405], r.values[0x4406], r.values[0x4407]);
        }
      } else if (compose_bloom) {
        last_post_pass_ = PostCapture::Composicao;
      }
    }
    if (accepted && final_composite) {
      // The FXAA shader samples fetch slot zero. A resolve written this frame
      // validates the address, even if the guest marked the fetch invalid.
      const auto fetch = x360::LeerTexturaSwap(register_file_->values);
      composite_source_ = fetch.dword[1] & 0x1ffff000u;
      if (sampling) {
        REXLOG_INFO("[B3 Deko FXAA] swap={} fonte={:08X} fetch={:08X}/{:08X}/{:08X}/{:08X}/{:08X}/{:08X}",
                    swaps_ + 1, composite_source_, fetch.dword[0], fetch.dword[1], fetch.dword[2],
                    fetch.dword[3], fetch.dword[4], fetch.dword[5]);
      }
    }
    if (!accepted) {
      ++frame_.failed_draws;
      frame_.failed_vs_hash = vs ? vs->ucode_data_hash() : 0;
      frame_.failed_ps_hash = ps ? ps->ucode_data_hash() : 0;
    }
    // Let the standard command processor continue reading subsequent commands.
    return true;
  }

  bool IssueCopy() override {
    const auto& r = *register_file_;
    x360::RegistrosCopia copy{};
    copy.rb_surface_info = r[XE_GPU_REG_RB_SURFACE_INFO];
    copy.rb_color_info[0] = r[XE_GPU_REG_RB_COLOR_INFO];
    copy.rb_color_info[1] = r[XE_GPU_REG_RB_COLOR1_INFO];
    copy.rb_color_info[2] = r[XE_GPU_REG_RB_COLOR2_INFO];
    copy.rb_color_info[3] = r[XE_GPU_REG_RB_COLOR3_INFO];
    copy.rb_depth_info = r[XE_GPU_REG_RB_DEPTH_INFO];
    copy.rb_copy_control = r[XE_GPU_REG_RB_COPY_CONTROL];
    copy.rb_copy_dest_base = r[XE_GPU_REG_RB_COPY_DEST_BASE];
    copy.rb_copy_dest_pitch = r[XE_GPU_REG_RB_COPY_DEST_PITCH];
    copy.rb_copy_dest_info = r[XE_GPU_REG_RB_COPY_DEST_INFO];
    copy.rb_color_clear = r[XE_GPU_REG_RB_COLOR_CLEAR];
    copy.rb_color_clear_lo = r[XE_GPU_REG_RB_COLOR_CLEAR_LO];
    copy.rb_depth_clear = r[XE_GPU_REG_RB_DEPTH_CLEAR];
    copy.pa_sc_window_offset = r[XE_GPU_REG_PA_SC_WINDOW_OFFSET];
    copy.pa_sc_window_scissor_tl = r[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL];
    copy.pa_sc_window_scissor_br = r[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR];
    copy.pa_su_sc_mode_cntl = r[XE_GPU_REG_PA_SU_SC_MODE_CNTL];
    copy.pa_su_vtx_cntl = r[XE_GPU_REG_PA_SU_VTX_CNTL];
    copy.fetch_vertices[0] = r[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_0];
    copy.fetch_vertices[1] = r[XE_GPU_REG_SHADER_CONSTANT_FETCH_00_1];
    copy.omitir_leitura_cpu = reduzir_copias_ && bloom_blur_draw_ && copy.rb_copy_control == 0x00100000 &&
                              copy.rb_copy_dest_pitch == 0x002D0060 && copy.rb_copy_dest_info == 0x01000302 &&
                              copy.rb_color_info[0] == 0 && copy.rb_depth_info == 0x514;
    bloom_blur_draw_ = false;
    leituras_bloom_omitidas_ += copy.omitir_leitura_cpu;
    if (composite_draw_) {
      if (render_->PodeSubstituirCopiaFrontal(copy, composite_source_)) {
        composite_draw_.reset();
        ++desenhos_finais_omitidos_;
      } else {
        FlushCompositeDraw();
      }
    }
    if (swaps_ + 1 == detailed_capture_ && (copy.rb_surface_info & 0x3FFFFu) == 1280) {
      if (model_checkpoint_ && frame_.copies == 3) {
        render_->VolcarCena(0, 1280, frame_.draws, true);
        REXLOG_INFO("[B3 Deko modelo] swap={} draw={} termina_material_antes_da_copia",
                    swaps_ + 1, frame_.draws);
      }
      if (hud_dumps_ && copy.rb_color_info[0] == 0 && copy.rb_copy_control == 0x00100100 &&
          (copy.rb_copy_dest_base & 0x1FFFFFFFu) == 0x1F35F000u) {
        render_->VolcarCorHud(0, 1280, frame_.draws, true);
        REXLOG_INFO("[B3 Deko HUD] swap={} draw={} hud_composto_antes_do_fxaa",
                    swaps_ + 1, frame_.draws);
      }
    }
    ++frame_.copies;
    bool substituted = false;
    const bool success = composite_source_ ? render_->CopiarDesdeResuelta(copy, composite_source_, substituted)
                                           : render_->Copiar(copy);
    if (!success) ++frame_.failed_copies;
    if (success && copy.rb_surface_info == 0x08000230 && copy.rb_color_info[0] == 0 &&
        copy.rb_copy_control == 0x00100000 && copy.rb_copy_dest_pitch == 0x02000200 &&
        copy.rb_copy_dest_info == 0x01000302) {
      shadow_resolve_ = copy.rb_copy_dest_base & 0x1FFFFFFFu;
      if (swaps_ + 1 == detailed_capture_) {
        render_->VolcarEntrada(shadow_resolve_, frame_.draws, 0);
        REXLOG_INFO("[B3 Deko mascara 512] swap={} draw={} endereco={:08X}",
                    swaps_ + 1, frame_.draws, shadow_resolve_);
      }
    }
    const bool sampling = IsSamplingFrame();
    if (composite_source_) {
      ++composite_attempts_;
      if (substituted) ++composite_copies_;
      if (composite_attempts_ <= 3 || sampling) {
        REXLOG_INFO("[B3 Deko copia final] swap={} fonte={:08X} destino={:08X} aplicada={}",
                    swaps_ + 1, composite_source_, copy.rb_copy_dest_base & 0x1fffffffu, substituted);
      }
      composite_source_ = 0;
    }
    if (swaps_ + 1 == scene_capture_ && success && last_post_pass_ && (copy.rb_copy_control & 7u) == 0 &&
        (copy.rb_copy_dest_pitch & 0x3FFFu) == 1280u) {
      const uint32_t base = copy.rb_copy_dest_base & 0x1fffffffu;
      render_->VolcarPosProcesso(base, *last_post_pass_);
      REXLOG_INFO("[B3 Deko combate] swap={} etapa={} destino={:08X}", swaps_ + 1,
                  uint32_t(*last_post_pass_), base);
    }
    if (sampling && copy_passes_.size() < kMaxCopyRows) {
      copy_passes_.push_back({frame_.draws, copy.rb_copy_dest_base, copy.rb_copy_dest_pitch,
                              copy.rb_copy_dest_info, copy.rb_copy_control,
                              copy.rb_color_info[copy.rb_copy_control & 3u], copy.rb_depth_info,
                              success, substituted});
    }
    return true;
  }

 private:
  // b3_gpu_boost, live: re-evaluated every 30 swaps and on a dock change (handheld only; docked it turns itself off, and
  // comes back on undocking while the cvar is on).
  void TickGpuBoost() {
    if (swaps_ % 30 != 1) return;
    const bool dock = rex::ui::switch_saltynx::ModoBase(appletGetOperationMode() == AppletOperationMode_Console);
    const bool quer = REXCVAR_GET(b3_gpu_boost) && !dock;
    const bool ram = REXCVAR_GET(b3_gpu_boost_ram1600);
    if (quer && gpu_boost_estado_ && RexSwitchApmAtivo() && ram != gpu_boost_ram_aplicada_) {
      // The RAM 1600 cvar changed while the boost is on: restore now, and the next tick (30 swaps later) applies again.
      if (AplicarGpuBoost(false)) {
        REXLOG_INFO("[B3 gpu boost] b3_gpu_boost_ram1600 mudou ({}): boost reaplicado no proximo ciclo", ram);
        gpu_boost_estado_ = false;
      }
      return;
    }
    if (quer == gpu_boost_estado_) return;
    if (AplicarGpuBoost(quer)) {
      gpu_boost_estado_ = quer;
      if (quer) gpu_boost_ram_aplicada_ = ram;
    }
  }
  bool gpu_boost_estado_ = false;
  bool gpu_boost_ram_aplicada_ = false;

  void UpdateOptions() {
    const uint32_t options = options_.load(std::memory_order_relaxed);
    const bool reduzir = (options & kReduzirCopias) != 0;
    const bool bloom = (options & kBloom) != 0;
    const bool restaurar = (options & kRestaurarZ) != 0;
    const bool desfoque = (options & kDesfoqueLeve) != 0;
    const bool sombras = (options & kSombrasProjetadas) != 0;
    const int height = DofResolutionHeight(options);
    if (!swaps_ || reduzir != reduzir_copias_ || bloom != bloom_ || restaurar != restaurar_z_ ||
        desfoque != desfoque_leve_ || sombras != sombras_projetadas_ || height != dof_height_) {
      REXLOG_INFO("[B3 Deko opcoes] swap={} reduzir_copias={} bloom={} restaurar_z={} desfoque_leve={} sombras_projetadas={} desfoque={}p",
                  swaps_ + 1, reduzir, bloom, restaurar, desfoque, sombras, height);
    }
    reduzir_copias_ = reduzir;
    bloom_ = bloom;
    restaurar_z_ = restaurar;
    desfoque_leve_ = desfoque;
    sombras_projetadas_ = sombras;
    dof_height_ = height;
  }

  bool ReadDragonRushIcon(DragonRushIconVertices& vertices) {
    const auto& r = *register_file_;
    const auto initiator = r.Get<reg::VGT_DRAW_INITIATOR>();
    const auto fetch = r.GetVertexFetch(95);
    const uint64_t address = uint64_t(fetch.address) * 4;
    if (initiator.source_select != xenos::SourceSelect::kAutoIndex ||
        r[XE_GPU_REG_VGT_INDX_OFFSET] != 0 ||
        fetch.type != xenos::FetchConstantType::kVertex ||
        fetch.endian != xenos::Endian::k8in32 || fetch.size != vertices.size() ||
        address + sizeof(vertices) > x360::kMemoriaFisica) return false;
    std::memcpy(vertices.data(), memory_->TranslatePhysical(uint32_t(address)), sizeof(vertices));
    for (uint32_t& word : vertices) word = xenos::GpuSwap(word, fetch.endian);
    return RepositionDragonRushIcon(vertices) && !REXCVAR_QUERY(bool, input_xbox_layout);
  }

  void FlushCompositeDraw() {
    if (!composite_draw_) return;
    if (!render_->Dibujar(*composite_draw_)) {
      ++frame_.failed_draws;
      frame_.failed_ps_hash = kCompositePixel;
    }
    composite_draw_.reset();
  }

  bool TrySceneClear(xenos::PrimitiveType primitive, uint32_t count,
                     const DekoShader* vs, const DekoShader* ps) {
    if (primitive != xenos::PrimitiveType::kRectangleList || count != 3 ||
        !vs || vs->ucode_data_hash() != kClearVertex) return false;

    const auto& r = *register_file_;
    const auto surface = r.Get<reg::RB_SURFACE_INFO>();
    const auto mode = r.Get<reg::RB_MODECONTROL>().edram_mode;
    const bool color = mode == xenos::EdramMode::kColorDepth;
    const uint32_t color_info = r[XE_GPU_REG_RB_COLOR_INFO];
    const uint32_t depth_info = r[XE_GPU_REG_RB_DEPTH_INFO];
    const auto depth = r.Get<reg::RB_DEPTHCONTROL>();
    const auto stencil = r.Get<reg::RB_STENCILREFMASK>();
    // The B3 frame begins with a 4x reinterpretation of its main 1x
    // EDRAM buffers. Limit the conversion to the observed untextured clears.
    if (surface.msaa_samples != xenos::MsaaSamples::k4X || surface.surface_pitch != 640 ||
        color_info != 0 || depth_info != 0x530 || r[XE_GPU_REG_PA_CL_VTE_CNTL] != 0x300 ||
        r[XE_GPU_REG_PA_CL_CLIP_CNTL] != 0x10000 || r[XE_GPU_REG_PA_SU_SC_MODE_CNTL] != 0x10000 ||
        r[XE_GPU_REG_PA_SC_WINDOW_OFFSET] != 0 || r[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL] != 0 ||
        r[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR] != 0x20002000 ||
        r[XE_GPU_REG_VGT_INDX_OFFSET] != 0 || r[XE_GPU_REG_RB_DEPTHCONTROL] != 0x8777) return false;

    if (color && (!ps || ps->ucode_data_hash() != kColorPixel ||
        (r[XE_GPU_REG_RB_COLOR_MASK] & 0xF) != 0xF ||
        (r[XE_GPU_REG_RB_BLENDCONTROL0] & 0x1FFF1FFF) != 0x00010001 ||
        (r[XE_GPU_REG_RB_COLORCONTROL] & 0x18) != 0)) return false;

    const auto initiator = r.Get<reg::VGT_DRAW_INITIATOR>();
    const auto fetch = r.GetVertexFetch(0);
    constexpr uint32_t kStride = 28, kWordsPerVertex = kStride / sizeof(uint32_t);
    const uint64_t address = uint64_t(fetch.address) * 4;
    if (initiator.source_select != xenos::SourceSelect::kAutoIndex ||
        fetch.type != xenos::FetchConstantType::kVertex || fetch.size < count * kWordsPerVertex ||
        address + count * kStride > x360::kMemoriaFisica) return false;

    const auto* data = memory_->TranslatePhysical(uint32_t(address));
    std::array<std::array<float, kWordsPerVertex>, 3> vertices{};
    for (size_t i = 0; i < vertices.size(); ++i) {
      for (size_t j = 0; j < kWordsPerVertex; ++j) {
        uint32_t bits;
        std::memcpy(&bits, data + i * kStride + j * sizeof(bits), sizeof(bits));
        const float value = std::bit_cast<float>(xenos::GpuSwap(bits, fetch.endian));
        if (!std::isfinite(value)) return false;
        vertices[i][j] = value;
      }
    }
    const auto [x0, x1] = std::minmax({vertices[0][0], vertices[1][0], vertices[2][0]});
    const auto [y0, y1] = std::minmax({vertices[0][1], vertices[1][1], vertices[2][1]});
    const float z = vertices[0][2];
    if (x0 < -8192 || y0 < -8192 || x1 > 8192 || y1 > 8192 ||
        x0 == x1 || y0 == y1 || z < 0 || z > 1) return false;
    unsigned corners = 0;
    for (const auto& v : vertices) {
      if ((v[0] != x0 && v[0] != x1) || (v[1] != y0 && v[1] != y1) || v[2] != z ||
          (color && !std::equal(v.begin() + 3, v.end(), vertices[0].begin() + 3))) return false;
      corners |= 1u << ((v[0] == x1 ? 1u : 0u) | (v[1] == y1 ? 2u : 0u));
    }
    if (std::popcount(corners) != 3) return false;

    const float pixel_bias = (r[XE_GPU_REG_PA_SU_VTX_CNTL] & 1u) ? 0.0f : 0.5f;
    const auto edge = [pixel_bias](float v) { return int32_t(std::ceil(v + pixel_bias - 0.5f)); };
    if (!render_->BorrarSuperficie4x(color_info, depth_info, surface.surface_pitch,
                                    edge(x0), edge(y0), edge(x1), edge(y1),
                                    color ? vertices[0].data() + 3 : nullptr, z, depth.z_write_enable,
                                    uint8_t(stencil.stencilwritemask), uint8_t(stencil.stencilref))) return false;
    if (++scene_clears_ <= 4 || IsSamplingFrame()) {
      REXLOG_INFO("[B3 Deko EDRAM] swap={} 4x->1x color={} area=({},{} {}x{}) z={} stencil={:02X}/{:02X}",
                  swaps_ + 1, color, edge(x0), edge(y0), edge(x1) - edge(x0), edge(y1) - edge(y0),
                  z, uint32_t(stencil.stencilref), uint32_t(stencil.stencilwritemask));
    }
    return true;
  }

  bool IsSamplingFrame() const {
    return swaps_ + 1 <= state_capture_until_ || swaps_ + 1 == scene_capture_;
  }

  bool ManualCaptureRequested() {
    if (!capture_pad_ready_) return false;
    padUpdate(&capture_pad_);
    const uint64_t buttons = padGetButtons(&capture_pad_);
    const bool held = (buttons & kCaptureButtons) == kCaptureButtons;
    const bool pressed = held && !capture_buttons_held_;
    capture_buttons_held_ = held;
    if (pressed && (buttons & kAudioCaptureButtons) == kAudioCaptureButtons) {
      capture_rush_audio_ = !capture_rush_audio_;
      REXLOG_INFO("[B3 audio fonte] captura do proximo Dragon Rush {}",
                  capture_rush_audio_ ? "armada" : "cancelada");
      return false;
    }
    return pressed;
  }

  void LogMissingVertex(const DekoShader& vs, const DekoShader* ps) const {
    const auto& r = *register_file_;
    REXLOG_INFO("[B3 Deko VS 3D] swap={} vs={:016X} ps={:016X} ps_container={:016X} "
                "program={:08X} edram={:08X} depth={:08X} color={:08X} mask={:08X} "
                "draw={:08X} index={:08X}/{:08X} interp={:08X} ctx={:08X}",
                swaps_ + 1, vs.ucode_data_hash(), ps ? ps->ucode_data_hash() : 0,
                ps && ps->deko_entry ? ps->deko_entry->huella_contenedor : 0, r[XE_GPU_REG_SQ_PROGRAM_CNTL],
                r[XE_GPU_REG_RB_MODECONTROL], r[XE_GPU_REG_RB_DEPTHCONTROL], r[XE_GPU_REG_RB_COLOR_INFO],
                r[XE_GPU_REG_RB_COLOR_MASK], r[XE_GPU_REG_VGT_DRAW_INITIATOR], r[XE_GPU_REG_VGT_DMA_BASE],
                r[XE_GPU_REG_VGT_DMA_SIZE], r[XE_GPU_REG_SQ_INTERPOLATOR_CNTL], r[XE_GPU_REG_SQ_CONTEXT_MISC]);
    for (const uint32_t slot : {94u, 95u}) {
      const auto fetch = r.GetVertexFetch(slot);
      REXLOG_INFO("[B3 Deko stream] vs={:016X} slot={} fetch={:08X}/{:08X} bytes={} endian={}",
                  vs.ucode_data_hash(), slot, fetch.dword_0, fetch.dword_1, fetch.size * 4u, uint32_t(fetch.endian));
    }

    // Little-endian B3RS v1: header, then the draw and shader register ranges.
    // Retain the original values so the captured microcode can be checked against them offline.
    constexpr uint32_t kDrawStart = 0x2000, kDrawCount = 0x400;
    constexpr uint32_t kShaderStart = 0x4000, kShaderCount = 0x908;
    static_assert(kDrawStart + kDrawCount <= RegisterFile::kRegisterCount &&
                  kShaderStart + kShaderCount <= RegisterFile::kRegisterCount);
    const std::array<uint32_t, 8> header{0x53523342, 1, uint32_t(swaps_ + 1), frame_.draws,
                                          kDrawStart, kDrawCount, kShaderStart, kShaderCount};
    std::vector<uint32_t> data;
    data.reserve(header.size() + kDrawCount + kShaderCount);
    data.insert(data.end(), header.begin(), header.end());
    data.insert(data.end(), r.values + kDrawStart, r.values + kDrawStart + kDrawCount);
    data.insert(data.end(), r.values + kShaderStart, r.values + kShaderStart + kShaderCount);
    const auto name = fmt::format("vs_{:016X}_mode{}_swap{}.bin", vs.ucode_data_hash(),
                                  r[XE_GPU_REG_RB_MODECONTROL] & 7u, swaps_ + 1);
    const auto path = shader_path_.parent_path() / "b3_deko_diag" / "registros" / name;
    x360::EncolarEscrituraSd([path, data = std::move(data)] {
      std::error_code ec;
      std::filesystem::create_directories(path.parent_path(), ec);
      if (!ec) {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size() * sizeof(uint32_t)));
        out.close();
        if (out) return;
      }
      REXLOG_WARN("[B3 Deko registros] falha ao escrever {}", path.string());
    });
  }

  void SaveDrawInputs(const DekoShader& vs, const DekoShader& ps, uint32_t count, bool model, bool after_shadow) {
    const auto& r = *register_file_;
    const auto init = r.Get<reg::VGT_DRAW_INITIATOR>();
    const uint32_t bytes_per_index = (r[XE_GPU_REG_VGT_DRAW_INITIATOR] & (1u << 11)) ? 4 : 2;
    const uint32_t index_bytes = init.source_select == xenos::SourceSelect::kDMA ? count * bytes_per_index : 0;
    const uint32_t index_base = r[XE_GPU_REG_VGT_DMA_BASE] & (0x1FFFFFFFu & ~(bytes_per_index - 1));
    if (index_bytes && (count > (r[XE_GPU_REG_VGT_DMA_SIZE] & 0xFFFFFF) ||
                        uint64_t(index_base) + index_bytes > x360::kMemoriaFisica)) return;

    const auto bone = r.GetVertexFetch(94);
    const auto mesh = r.GetVertexFetch(95);
    const uint32_t bone_bytes = model && bone.type == xenos::FetchConstantType::kVertex ? bone.size * 4u : 0;
    const uint32_t mesh_bytes = mesh.type == xenos::FetchConstantType::kVertex ? mesh.size * 4u : 0;
    constexpr uint32_t kMaxStreamBytes = 256 * 1024;
    if (!mesh_bytes || bone_bytes > kMaxStreamBytes || mesh_bytes > kMaxStreamBytes ||
        uint64_t(bone.address) * 4 + bone_bytes > x360::kMemoriaFisica ||
        uint64_t(mesh.address) * 4 + mesh_bytes > x360::kMemoriaFisica) return;

    // B3IN v1: header and register words are little endian. Guest memory
    // and microcode keep their original byte order for offline replay.
    const std::span<const uint32_t> code = vs.ucode_data();
    constexpr uint32_t kDrawStart = 0x2000, kDrawCount = 0x400;
    constexpr uint32_t kShaderStart = 0x4000, kShaderCount = 0x908;
    const std::array<uint32_t, 14> header{0x4E493342, 1, uint32_t(swaps_ + 1), frame_.draws, frame_.copies,
        kDrawStart, kDrawCount, kShaderStart, kShaderCount, uint32_t(code.size_bytes()),
        index_base, index_bytes, bone_bytes, mesh_bytes};
    std::vector<uint8_t> data;
    data.reserve(sizeof(header) + (kDrawCount + kShaderCount) * 4 + code.size_bytes() + index_bytes +
                 bone_bytes + mesh_bytes);
    const auto append = [&data](const void* bytes, size_t size) {
      const auto* first = static_cast<const uint8_t*>(bytes);
      data.insert(data.end(), first, first + size);
    };
    append(header.data(), sizeof(header));
    append(r.values + kDrawStart, kDrawCount * 4);
    append(r.values + kShaderStart, kShaderCount * 4);
    append(code.data(), code.size_bytes());
    if (index_bytes) append(memory_->TranslatePhysical(index_base), index_bytes);
    if (bone_bytes) append(memory_->TranslatePhysical(bone.address * 4u), bone_bytes);
    append(memory_->TranslatePhysical(mesh.address * 4u), mesh_bytes);
    const auto name = fmt::format("swap_{:06}_draw_{:04}_{:016X}_{:016X}.bin", swaps_ + 1, frame_.draws,
                                  vs.ucode_data_hash(), ps.ucode_data_hash());
    const auto path = shader_path_.parent_path() / "b3_deko_diag" / "entradas" / name;
    REXLOG_INFO("[B3 Deko entrada] {} apos_sombra={} indices={} ossos={:08X}/{} malha={:08X}/{}", name,
                after_shadow, index_bytes, bone.address * 4u, bone_bytes, mesh.address * 4u, mesh_bytes);
    x360::EncolarEscrituraSd([path, data = std::move(data)] {
      std::error_code ec;
      std::filesystem::create_directories(path.parent_path(), ec);
      std::ofstream out(path, std::ios::binary);
      out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
      out.close();
      if (!out) REXLOG_WARN("[B3 Deko entrada] falha ao escrever {}", path.string());
    });
  }

  void RecordDraw(xenos::PrimitiveType primitive, const DekoShader* vs, const DekoShader* ps,
                  bool accepted, bool recorded) {
    const auto& r = *register_file_;
    const DrawPassKey key{vs ? vs->ucode_data_hash() : 0, ps ? ps->ucode_data_hash() : 0,
                         r[XE_GPU_REG_RB_MODECONTROL], r[XE_GPU_REG_RB_COLOR_INFO],
                         r[XE_GPU_REG_RB_COLOR_MASK], r[XE_GPU_REG_RB_DEPTH_INFO],
                         r[XE_GPU_REG_PA_CL_VTE_CNTL], r[XE_GPU_REG_RB_DEPTHCONTROL], uint32_t(primitive),
                         frame_.copies, r[XE_GPU_REG_RB_COLORCONTROL], r[XE_GPU_REG_RB_BLENDCONTROL0],
                         r[XE_GPU_REG_RB_ALPHA_REF]};
    auto pass = std::find_if(draw_passes_.begin(), draw_passes_.end(),
                             [&](const DrawPass& row) { return row.key == key; });
    if (pass == draw_passes_.end()) {
      if (draw_passes_.size() >= kMaxPasses) {
        ++unsampled_draws_;
        return;
      }
      const auto fetch = r.GetTextureFetch(0);
      draw_passes_.push_back({.key = key, .first_draw = frame_.draws,
                             .raster = r[XE_GPU_REG_PA_SU_SC_MODE_CNTL], .clip = r[XE_GPU_REG_PA_CL_CLIP_CNTL],
                             .surface = r[XE_GPU_REG_RB_SURFACE_INFO],
                             .scissor_tl = r[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_TL],
                             .scissor_br = r[XE_GPU_REG_PA_SC_WINDOW_SCISSOR_BR],
                             .screen_tl = r[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_TL],
                             .screen_br = r[XE_GPU_REG_PA_SC_SCREEN_SCISSOR_BR],
                             .stencil = r[XE_GPU_REG_RB_STENCILREFMASK],
                             .stencil_bf = r[XE_GPU_REG_RB_STENCILREFMASK_BF],
                             .fetch0 = fetch.dword_0, .fetch1 = fetch.dword_1, .fetch2 = fetch.dword_2,
                             .fetch3 = fetch.dword_3,
                             .vs_export = r[XE_GPU_REG_SQ_PROGRAM_CNTL],
                             .ps_export = ps && ps->deko_entry ? ps->deko_entry->salidas : 0});
      pass = std::prev(draw_passes_.end());
    }
    ++pass->draws;
    if (recorded) ++pass->recorded;
    if (!accepted) ++pass->failed;
  }

  void LogPasses() const {
    REXLOG_INFO("[B3 Deko cena] swap={} passes={} copies={}/{} draws_3d={} unknown_vs_3d={} overflow={}",
                swaps_, draw_passes_.size(), copy_passes_.size(), frame_.copies, frame_.draws_3d,
                frame_.missing_vs_3d, unsampled_draws_);
    for (const DrawPass& p : draw_passes_) {
      const auto& k = p.key;
      REXLOG_INFO("[B3 Deko passe] swap={} draw={} after_copies={} vs={:016X} ps={:016X} "
                  "draws={} recorded={} failed={} mode={:08X} prim={} surface={:08X} color={:08X}/{:08X} "
                  "depth={:08X}/{:08X} vte={:08X} clip={:08X} raster={:08X} scissor={:08X}/{:08X} "
                  "tex0={:08X}/{:08X}/{:08X}/{:08X} program={:08X} ps_outputs={:02X} "
                  "color_control={:08X} blend0={:08X} alpha_ref={:08X} "
                  "screen={:08X}/{:08X} stencil={:08X}/{:08X}",
                  swaps_, p.first_draw, k.after_copy, k.vs, k.ps, p.draws, p.recorded, p.failed, k.edram,
                  k.primitive, p.surface, k.color, k.color_mask, k.depth_target, k.depth, k.viewport, p.clip,
                  p.raster, p.scissor_tl, p.scissor_br, p.fetch0, p.fetch1, p.fetch2, p.fetch3, p.vs_export, p.ps_export,
                  k.color_control, k.blend0, k.alpha_ref, p.screen_tl, p.screen_br, p.stencil, p.stencil_bf);
    }
    for (const CopyPass& p : copy_passes_) {
      REXLOG_INFO("[B3 Deko copia] swap={} after_draw={} base={:08X} pitch={:08X} info={:08X} "
                  "control={:08X} color={:08X} depth={:08X} ok={} substituted={}",
                  swaps_, p.after_draw, p.base, p.pitch, p.info, p.control, p.color, p.depth, p.success,
                  p.substituted);
    }
  }

  void DumpShader(bool vertex, uint64_t hash, const uint32_t* words, uint32_t count) const {
    char name[64];
    std::snprintf(name, sizeof(name), "%s_%016llX_%u.bin", vertex ? "vs" : "ps",
                  static_cast<unsigned long long>(hash), count);
    const auto path = shader_path_.parent_path() / "b3_deko_diag" / "shaders" / name;
    const auto* first = reinterpret_cast<const char*>(words);
    std::vector<char> data(first, first + size_t(count) * sizeof(uint32_t));
    x360::EncolarEscrituraSd([path, data = std::move(data)] {
      std::error_code ec;
      std::filesystem::create_directories(path.parent_path(), ec);
      if (!ec) {
        std::ofstream out(path, std::ios::binary);
        out.write(data.data(), std::streamsize(data.size()));
        out.close();
        if (out) return;
      }
      REXLOG_WARN("[B3 Deko shader] falha ao escrever {}", path.string());
    });
  }

  const x360::EntradaShader* ConvertShader(const ShaderEntry* entry) {
    if (!entry || !entry->shader) return nullptr;
    auto [it, inserted] = entries_.try_emplace(entry);
    auto& output = it->second;
    if (inserted) {
      output.spirv = entry->shader->spirv;
      output.vertices = entry->vertices;
      if (output.vertices) {
        output.entradas_renut_compactas = true;
        output.mascara_entradas_renut = rex::deko3d::MascaraEntradasVertice(output.spirv);
      }
      output.numero = entry->numero;
      output.microcodigo = entry->microcodigo;
      output.huella = entry->huella;
      output.huella_contenedor = entry->shader->huella;
      output.salidas = entry->salidas;
      output.kills = entry->kills;
      output.descarta = entry->descarta;
      output.sombra_minimo = entry->sombra_minimo;
      output.constantes_bytes = entry->constantes_bytes;
      output.literais.reserve(entry->literais.size());
      for (const auto& literal : entry->literais) {
        output.literais.push_back({literal.registro, literal.valor});
      }
      // The compiled shaders fetch their final three declaration entries at a bone index.
      size_t first_bone_fetch = entry->elementos.size();
      switch (output.huella_contenedor) {
        case 0x7986F49BCDC63751ULL: first_bone_fetch = 4; break;
        case 0xC3A1B0CCE460AC24ULL:
        case 0xBF19A616AA4E9A60ULL: first_bone_fetch = 5; break;
        case 0x2D51DC2FA95CF583ULL:
        case 0xCCA4EBADCEBFE7B0ULL:
        case 0x634031451DF2B733ULL: first_bone_fetch = 6; break;
      }
      for (size_t i = 0; i < entry->elementos.size(); ++i) {
        const auto& element = entry->elementos[i];
        output.elementos.push_back({element.instruccion, element.uso, element.indice_uso, i >= first_bone_fetch});
      }
      for (const auto& sampler : entry->samplers) {
        output.samplers.push_back({sampler.registro, sampler.tipo, sampler.mapa_sombras});
      }
    }
    return &output;
  }

  std::filesystem::path shader_path_;
  DekoOptions& options_;
  bool reduzir_copias_ = true, bloom_ = true, restaurar_z_ = true, bloom_blur_draw_ = false;
  uint64_t desenhos_finais_omitidos_ = 0, leituras_bloom_omitidas_ = 0, bloom_omitidos_ = 0, restaura_z_ = 0;
  bool desfoque_leve_ = true;
  int dof_height_ = 720;
  bool sombras_projetadas_ = true;
  bool reported_map_distortion_ = false, reported_switch_rush_ = false;
  uint64_t dof_leve_pedidos_ = 0, sombras_omitidas_ = 0, sombras_com_sesgo_ = 0;
  bool model_redraw_ = false, model_checkpoint_ = false;
  bool previous_combat_frame_ = false;
  uint32_t shadow_resolve_ = 0, hud_traces_ = 0, effect_traces_ = 0;
  std::array<uint32_t, RegisterFile::kRegisterCount> postprocess_registers_;
  std::optional<x360::PeticionDibujo> composite_draw_;
  nfsmw::nativo::ShadersNativos library_;
  std::unordered_map<const ShaderEntry*, x360::EntradaShader> entries_;
  std::unordered_map<std::string, std::unique_ptr<DekoShader>> shaders_;
  std::unique_ptr<x360::RenderDk> render_;
  const DekoShader* last_vs_ = nullptr;
  uint64_t vs_generation_ = 1;
  uint64_t vs_constants_ = 1, ps_constants_ = 1, fetch_ = 1, raster_ = 1;
  uint64_t swaps_ = 0, missed_swaps_ = 0, unknown_shaders_ = 0;
  uint64_t attempted_draws_ = 0, failed_draws_ = 0, unknown_vs_draws_ = 0, unknown_ps_draws_ = 0;
  uint64_t copy_attempts_ = 0, failed_copies_ = 0, scene_clears_ = 0;
  FrameCounters frame_;
  uint64_t composite_copies_ = 0, composite_attempts_ = 0;
  uint32_t composite_source_ = 0;
  bool direct_composite_ = false;
  uint32_t shader_dumps_ = 0, matched_vertices_ = 0, shader_examples_ = 0;
  uint32_t unsampled_draws_ = 0, scene_clear_fallbacks_ = 0;
  uint64_t scene_capture_ = 0;
  uint64_t detailed_capture_ = 0;
  BattleFramePacer pacer_;
  uint32_t world_shadow_logs_ = 0;
  uint32_t hud_dumps_ = 0;
  uint32_t dragon_rush_dumps_ = 0;
  uint32_t scene_checkpoints_ = 0;
  uint32_t captured_effects_ = 0;
  uint32_t dialogue_sprites_captured_ = 0;
  std::array<uint8_t, 7> captured_inputs_{};
  std::map<std::array<uint64_t, 5>, uint32_t> probes_per_pass_;
  uint64_t state_capture_until_ = 0;
  std::optional<PostCapture> last_post_pass_;
  std::vector<DrawPass> draw_passes_;
  std::unordered_set<uint64_t> sampled_literals_;
  std::vector<CopyPass> copy_passes_;
  PadState capture_pad_{};
  bool capture_pad_ready_ = false, capture_buttons_held_ = false;
  bool capture_rush_audio_ = false;
  bool diagnostic_ = false;
  bool gamma_dirty_ = true;
};

class DekoGraphicsSystem final : public GraphicsSystem {
 public:
  DekoGraphicsSystem(const std::filesystem::path& shaders, DekoOptions& options)
      : shaders_(shaders), options_(options) {}
  std::string name() const override { return "Xenos / Deko3D"; }

 protected:
  void CreateProvider(bool) override { provider_ = Provider::Crear(); }

  std::unique_ptr<CommandProcessor> CreateCommandProcessor() override {
    return std::make_unique<DekoCommandProcessor>(this, kernel_state(), shaders_, options_);
  }

 private:
  std::filesystem::path shaders_;
  DekoOptions& options_;
};
}  // namespace

std::unique_ptr<rex::system::IGraphicsSystem> CreateXenosDeko(
    const std::filesystem::path& shader_library, DekoOptions& options) {
  return std::make_unique<DekoGraphicsSystem>(shader_library, options);
}
}  // namespace b3
