// nfsmw - native renderer, part C5a (see nfsmw_nativo_shaders.h).
//
// Xenos shader-container format used by B3 (36-byte header); all fields are big-endian.

#include "nfsmw_nativo_shaders.h"

#include "nfsmw_shader_library.h"

#include <rex/logging.h>

#include <algorithm>
#include <exception>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <unordered_map>

/*
 * xxHash reads with memcpy (XXH_FORCE_MEMORY_ACCESS 0). With the method it picks for GCC (1) it reads through
 * 64- and 32-bit pointers without may_alias, and GCC may move that read ahead of the store of the data being
 * hashed (strict aliasing). With that, the texture key read claves[4] before writing it and the same texture was
 * created several times (see docs/toolchain.md). Same fingerprint values; on AArch64, the same LDR.
 */
#if defined(XXH_IMPLEM_13a8737387)
#error "xxhash.h ya se ha incluido con su implementacion antes de este punto: XXH_FORCE_MEMORY_ACCESS 0 llegaria tarde"
#endif
#undef XXH_FORCE_MEMORY_ACCESS
#define XXH_FORCE_MEMORY_ACCESS 0
#define XXH_INLINE_ALL
#include <xxhash.h>

namespace nfsmw::nativo {
namespace {

// Bits of a vertex fetch instruction that D3D does not touch
// (VertexFetchInstruction in XenosRecomp shader_code.h): opcode, source and
// destination registers, destination swizzle and predicate. The rest (fetch
// constant, format, stride, offset and numeric modes) comes from the declaration.
constexpr uint32_t kFetchConserva[3] = {0x0007FFFF, 0x80000FFF, 0x80000000};

constexpr uint32_t kMaxAvisos = 48;

constexpr const char* kUsos[] = {"posicion", "peso",     "indices",    "normal",
                                 "tam_punto", "texcoord", "tangente",   "binormal",
                                 "teselado",  "posicion_t", "color",    "niebla",
                                 "profundidad", "muestra", "uso14",     "uso15"};

struct Contenedor {
  const std::vector<uint8_t>& o;

  bool Hay(size_t posicion, size_t bytes) const {
    return posicion <= o.size() && bytes <= o.size() - posicion;
  }
  uint32_t U32(size_t p) const {
    return uint32_t(o[p]) << 24 | uint32_t(o[p + 1]) << 16 | uint32_t(o[p + 2]) << 8 | o[p + 3];
  }
  uint16_t U16(size_t p) const { return uint16_t(uint32_t(o[p]) << 8 | o[p + 1]); }
};

// nfsmw_nativo_sombra_minimo. The zero-terminated string starting at `posicion` is `nombre`.
bool NombreEs(const Contenedor& c, size_t posicion, const char* nombre) {
  const size_t n = std::strlen(nombre);
  return c.Hay(posicion, n + 1) && std::memcmp(c.o.data() + posicion, nombre, n) == 0 && c.o[posicion + n] == 0;
}

// Reads the same 36-byte container and shader structures used by XenosRecomp's
// shader.h and the local reNut reference. Container +0x18 is shaderOffset;
// +0x14 is definitionTableOffset and must not be treated as the shader header.
// Shader +0/+4 select the microcode range inside the physical part. For a VS,
// +0x18 is the word offset to vertexElementsAndInterpolators and +0x1C is the
// number of vertex elements. Returns the reason if the record cannot be read.
const char* Leer(const nfsmw::native::Shader& shader, EntradaShader& e) {
  const Contenedor c{shader.original};
  if (!c.Hay(0, 36)) return "contenedor demasiado corto";
  const uint32_t virtuales = c.U32(4);
  const uint32_t fisicos = c.U32(8);
  const uint32_t tabla = c.U32(16);
  const uint32_t definiciones = c.U32(20);
  const uint32_t shader_offset = c.U32(24);
  if (virtuales < 36 || !fisicos || (fisicos % 4) || !c.Hay(virtuales, fisicos)) {
    return "microcodigo fuera del contenedor";
  }
  if (shader_offset > virtuales || size_t(virtuales - shader_offset) < (shader.vertices ? 36 : 32)) {
    return "cabecera del shader fuera de la parte virtual";
  }

  const uint32_t micro_offset = c.U32(shader_offset);
  const uint32_t micro_bytes = c.U32(size_t(shader_offset) + 4);
  if (!micro_bytes || (micro_bytes & 3) || micro_offset > fisicos || micro_bytes > fisicos - micro_offset) {
    return "microcodigo fuera de la parte fisica";
  }

  e.vertices = shader.vertices;
  e.microcodigo.resize(micro_bytes / 4);
  for (size_t i = 0; i < e.microcodigo.size(); ++i) {
    e.microcodigo[i] = c.U32(size_t(virtuales) + micro_offset + i * 4);
  }

  if (e.vertices) {
    const uint32_t previos = c.U32(size_t(shader_offset) + 24);
    const uint32_t cantidad = c.U32(size_t(shader_offset) + 28);
    const size_t comienzo = size_t(shader_offset) + 36 + size_t(previos) * 4;
    if (cantidad > 64 || previos > 1024 || comienzo > virtuales ||
        size_t(cantidad) > (size_t(virtuales) - comienzo) / 4) {
      return "elementos de vertices fuera de la parte virtual";
    }
    for (uint32_t i = 0; i < cantidad; ++i) {
      const uint32_t valor = c.U32(comienzo + size_t(i) * 4);
      ElementoVertice elemento;
      elemento.instruccion = uint16_t(valor & 0xFFF);
      elemento.uso = uint8_t((valor >> 12) & 0xF);
      elemento.indice_uso = uint8_t((valor >> 16) & 0xF);
      if ((size_t(elemento.instruccion) + 1) * 3 > e.microcodigo.size()) {
        return "elemento de vertices apunta fuera del microcodigo";
      }
      e.elementos.push_back(elemento);
      for (size_t j = 0; j < 3; ++j) {
        e.microcodigo[size_t(elemento.instruccion) * 3 + j] &= kFetchConserva[j];
      }
    }
  } else {
    e.salidas = c.U32(size_t(shader_offset) + 28);
  }

  // Constant table: only the samplers (register and type).
  if (!tabla || !c.Hay(tabla + 4, 28)) return "sin tabla de constantes";
  const size_t base = size_t(tabla) + 4;
  const uint32_t constantes = c.U32(base + 12);
  const uint32_t info = c.U32(base + 16);
  if (constantes > 1024 || !c.Hay(base + info, size_t(constantes) * 20)) {
    return "tabla de constantes fuera del contenedor";
  }
  // Float registers the SPIR-V reads: the highest in the table, or up to the end of the
  // buffer if there is an array with relative indexing (XenosRecomp shader_recompiler.cpp:
  // 1172-1183: tailCount = 256 in VS and 224 in PS).
  uint32_t registros_float = 0;
  for (uint32_t i = 0; i < constantes; ++i) {
    const size_t p = base + info + size_t(i) * 20;
    if (c.U16(p + 4) == 2) {  // RegisterSet::Float4
      const uint32_t indice = c.U16(p + 6);
      const uint32_t cuantos = c.U16(p + 8);
      registros_float = std::max(registros_float, cuantos > 1 ? (e.vertices ? 256u : 224u)
                                                              : indice + 1);
    }
  }
  // reNut may emit literal components as constant-buffer reads. Restore the
  // values from this shader's definition table, including PS c253-c255.
  if (definiciones) {
    if (definiciones > virtuales || virtuales - definiciones < 24) {
      return "tabla de definiciones fuera del contenedor";
    }
    for (size_t p = size_t(definiciones) + 20; ; p += 8) {
      if (p > virtuales || virtuales - p < 4) return "tabla de definiciones sin terminador";
      if (!c.U32(p)) break;
      if (virtuales - p < 8) return "definicion float truncada";
      const uint32_t primero = c.U16(p);
      const uint32_t cantidad = (uint32_t(c.U16(p + 2)) + 3) / 4;
      const uint32_t datos = c.U32(p + 4);
      const uint32_t banco = e.vertices ? 0u : 256u;
      if (primero < banco || primero > banco + 256 || cantidad > banco + 256 - primero ||
          datos > fisicos || cantidad * 16 > fisicos - datos) {
        return "definicion float fuera del banco del shader";
      }
      for (uint32_t i = 0; i < cantidad; ++i) {
        const size_t src = size_t(virtuales) + datos + i * 16;
        const uint32_t reg = primero - banco + i;
        const std::array valor{c.U32(src), c.U32(src + 4), c.U32(src + 8), c.U32(src + 12)};
        // Only components with a non-immediate mantissa can read the UBO.
        if (std::ranges::any_of(valor, [](uint32_t v) { return (v & 0xFFFu) != 0; })) {
          e.literais.push_back({reg, valor});
          registros_float = std::max(registros_float, reg + 1);
        }
      }
    }
  }
  e.constantes_bytes = std::min<uint32_t>(std::max<uint32_t>(registros_float, 1), 256) * 16;
  for (uint32_t i = 0; i < constantes; ++i) {
    const size_t p = base + info + size_t(i) * 20;
    if (c.U16(p + 4) != 3) continue;  // RegisterSet::Sampler
    SamplerShader sampler;
    sampler.registro = c.U16(p + 6);
    const uint32_t tipo = c.U32(p + 12);
    sampler.tipo = c.Hay(base + tipo, 4) ? c.U16(base + tipo + 2) : 0;
    // nfsmw_nativo_sombra_minimo. The name is at that distance from the start of the table, like the type
    // (XenosRecomp shader_recompiler.cpp: constantTableData + constantInfo->name).
    sampler.mapa_sombras = NombreEs(c, base + size_t(c.U32(p)), "SHADOWMAP_SAMPLER");
    e.samplers.push_back(sampler);
  }

  e.huella = XXH3_64bits(e.microcodigo.data(), e.microcodigo.size() * sizeof(uint32_t));
  return nullptr;
}

struct ClaveCruda {
  uint64_t huella;
  uint32_t palabras;
  bool vertices;
  bool operator==(const ClaveCruda&) const = default;
};

struct HashClaveCruda {
  size_t operator()(const ClaveCruda& c) const {
    return size_t(c.huella ^ (uint64_t(c.palabras) << 1) ^ uint64_t(c.vertices));
  }
};

}  // namespace

struct ShadersNativos::Datos {
  nfsmw::native::BibliotecaShaders biblioteca;
  std::vector<EntradaShader> entradas;
  std::unordered_map<const nfsmw::native::Shader*, uint32_t> por_shader;
  // (vertices, palabras) -> entradas candidatas.
  std::map<std::pair<bool, uint32_t>, std::vector<uint32_t>> candidatos;
  std::unordered_map<ClaveCruda, const EntradaShader*, HashClaveCruda> cache;
  std::vector<uint32_t> temporal;
  EstadisticasShaders estadisticas;
  uint32_t avisos = 0;
  bool cargada = false;
};

ShadersNativos::ShadersNativos() : datos_(std::make_unique<Datos>()) {}
ShadersNativos::~ShadersNativos() = default;

bool ShadersNativos::cargada() const {
  return datos_->cargada;
}

bool FetchCoherentes(const EntradaShader& vs, std::span<const uint32_t> parcheado) {
  if (!vs.vertices || parcheado.size() != vs.microcodigo.size()) {
    return false;
  }
  for (const ElementoVertice& elemento : vs.elementos) {
    const uint32_t registro = (vs.microcodigo[size_t(elemento.instruccion) * 3] >> 12) & 0x3F;
    bool encontrado = false;
    for (const ElementoVertice& otro : vs.elementos) {
      const uint32_t d0 = parcheado[size_t(otro.instruccion) * 3];
      if (((d0 >> 12) & 0x3F) == registro && (d0 & 0x1F) == 0) {
        encontrado = true;
        break;
      }
    }
    if (!encontrado) {
      return false;
    }
  }
  return true;
}

// nfsmw_nativo_sombra_minimo. The library has tfetch2DSombraMin if the pixel shader's SPIR-V contains the
// NFSMW_MARCA_SOMBRA_MINIMO constant from shader_common.h (a 32-bit OpConstant): nothing else uses it.
constexpr uint32_t kMarcaSombraMinimo = 0x5E3B1A84u;
static bool TieneMarcaSombraMinimo(const std::vector<uint32_t>& spirv) {
  if (spirv.size() < 5 || spirv[0] != 0x07230203u) {
    return false;
  }
  for (size_t i = 5; i < spirv.size();) {
    const uint32_t palabras = spirv[i] >> 16;
    if (palabras == 0 || i + palabras > spirv.size()) {
      return false;
    }
    if ((spirv[i] & 0xFFFF) == 43 && palabras == 4 && spirv[i + 3] == kMarcaSombraMinimo) {
      return true;
    }
    i += palabras;
  }
  return false;
}

// OpKill, OpTerminateInvocation and OpDemoteToHelperInvocation in the module. If the SPIR-V cannot be walked
// it returns 2, the conservative answer (the fragment stage is kept).
static uint32_t ContarKills(const std::vector<uint32_t>& spirv) {
  if (spirv.size() < 5 || spirv[0] != 0x07230203u) {
    return 2;
  }
  uint32_t kills = 0;
  for (size_t i = 5; i < spirv.size();) {
    const uint32_t palabras = spirv[i] >> 16;
    const uint32_t codigo = spirv[i] & 0xFFFF;
    if (palabras == 0 || i + palabras > spirv.size()) {
      return 2;
    }
    if (codigo == 252 || codigo == 4416 || codigo == 5380) {
      ++kills;
    }
    i += palabras;
  }
  return kills;
}

bool ShadersNativos::Cargar(const std::filesystem::path& archivo) {
  Datos& d = *datos_;
  try {
    d.biblioteca.Cargar(archivo);
  } catch (const std::exception& e) {
    REXLOG_WARN("[nativo] C5a: biblioteca de shaders no disponible ({}): {}", archivo.string(),
                e.what());
    return false;
  }
  const auto& shaders = d.biblioteca.shaders();
  uint32_t con_kill = 0, sin_kill = 0;
  uint32_t con_mapa_sombras = 0, con_minimo = 0;  // nfsmw_nativo_sombra_minimo
  d.entradas.clear();
  d.entradas.reserve(shaders.size());
  uint32_t vertex = 0, pixel = 0;
  for (uint32_t i = 0; i < shaders.size(); ++i) {
    EntradaShader e;
    e.shader = &shaders[i];
    e.numero = i;
    if (const char* motivo = Leer(shaders[i], e)) {
      REXLOG_WARN("[nativo] C5a: contenedor {} de la biblioteca ignorado: {}", i, motivo);
      continue;
    }
    if (!e.vertices) {
      e.kills = ContarKills(shaders[i].spirv);
      e.descarta = e.kills != 1;                // 1 = only the alpha test one
      (e.descarta ? con_kill : sin_kill) += 1;
      // nfsmw_nativo_sombra_minimo. Whether its SPIR-V can take the shadow map minimum.
      e.sombra_minimo = TieneMarcaSombraMinimo(shaders[i].spirv);
      for (const SamplerShader& s : e.samplers) {
        if (s.mapa_sombras) {
          ++con_mapa_sombras;
          con_minimo += e.sombra_minimo ? 1 : 0;
          break;
        }
      }
    }
    (e.vertices ? vertex : pixel) += 1;
    d.entradas.push_back(std::move(e));
  }
  d.candidatos.clear();
  d.por_shader.clear();
  std::map<std::tuple<bool, uint32_t, uint64_t>, std::vector<uint32_t>> grupos;
  for (uint32_t i = 0; i < d.entradas.size(); ++i) {
    const EntradaShader& e = d.entradas[i];
    const uint32_t palabras = uint32_t(e.microcodigo.size());
    d.candidatos[{e.vertices, palabras}].push_back(i);
    d.por_shader[e.shader] = i;
    grupos[{e.vertices, palabras, e.huella}].push_back(i);
  }
  uint32_t repetidos = 0, repetidos_distintos = 0;
  for (const auto& [clave, miembros] : grupos) {
    if (miembros.size() < 2) continue;
    ++repetidos;
    const auto& spirv = d.entradas[miembros[0]].shader->spirv;
    for (uint32_t m : miembros) {
      if (d.entradas[m].shader->spirv != spirv) {
        ++repetidos_distintos;
        break;
      }
    }
  }
  d.cache.clear();
  d.cargada = !d.entradas.empty();
  REXLOG_INFO("[nativo] C5a: biblioteca con {} shaders ({} vertex, {} pixel); {} grupos con el "
              "mismo microcodigo, {} de ellos con SPIR-V distinto",
              d.entradas.size(), vertex, pixel, repetidos, repetidos_distintos);
  REXLOG_INFO("[nativo] C5a: pixel shaders que pueden descartar pixeles {} de {} (el resto solo llevan el "
              "kill de la prueba de alfa: sin color que escribir, su etapa se puede quitar)",
              con_kill, con_kill + sin_kill);
  REXLOG_INFO("[nativo] C5a: sombra por minimo (build 184): {} pixel shaders muestrean el mapa de sombras "
              "(SHADOWMAP_SAMPLER) y {} traen tfetch2DSombraMin{}",
              con_mapa_sombras, con_minimo,
              con_minimo && con_minimo == con_mapa_sombras
                  ? " (biblioteca con el minimo)"
                  : " (sin el minimo: el mapa de sombras se sigue copiando como siempre)");
  return d.cargada;
}

const EntradaShader* ShadersNativos::Identificar(bool vertices,
                                                 std::span<const uint32_t> microcodigo) {
  Datos& d = *datos_;
  ++d.estadisticas.cargas;
  const ClaveCruda clave{XXH3_64bits(microcodigo.data(), microcodigo.size_bytes()),
                         uint32_t(microcodigo.size()), vertices};
  if (auto it = d.cache.find(clave); it != d.cache.end()) {
    return it->second;
  }
  ++d.estadisticas.distintos;

  const EntradaShader* elegido = nullptr;
  uint32_t coincidencias = 0;
  if (auto c = d.candidatos.find({vertices, uint32_t(microcodigo.size())});
      c != d.candidatos.end()) {
    for (uint32_t indice : c->second) {
      const EntradaShader& e = d.entradas[indice];
      d.temporal.assign(microcodigo.begin(), microcodigo.end());
      for (const ElementoVertice& elemento : e.elementos) {
        for (size_t j = 0; j < 3; ++j) {
          d.temporal[size_t(elemento.instruccion) * 3 + j] &= kFetchConserva[j];
        }
      }
      if (XXH3_64bits(d.temporal.data(), d.temporal.size() * sizeof(uint32_t)) != e.huella ||
          d.temporal != e.microcodigo) {
        continue;
      }
      if (!elegido) {
        elegido = &e;
      }
      ++coincidencias;
    }
  }

  // Unidentified shaders (vertex or pixel): report the closest containers of the same length, so a
  // near-identical variant (a few words nulled by D3D or by a different pixel shader) is easy to recipe
  // in tools/shader_layout.json ("template" + "xor" over those words).
  const bool avisar = d.avisos < kMaxAvisos;
  if (elegido) {
    ++d.estadisticas.identificados;
    if (coincidencias > 1) {
      ++d.estadisticas.ambiguos;
    }
  } else {
    ++d.estadisticas.sin_identificar;
    if (avisar) {
      ++d.avisos;
      const auto c = d.candidatos.find({vertices, uint32_t(microcodigo.size())});
      REXLOG_WARN("[nativo] C5a: {} shader sin identificar: {} palabras, huella {:016X} "
                  "({} contenedores de ese tipo y longitud)",
                  vertices ? "vertex" : "pixel", microcodigo.size(), clave.huella,
                  c != d.candidatos.end() ? c->second.size() : 0);
      // Diagnostics: which words change against the containers of the same length (masked incoming
      // microcode / container microcode). Only the closest two are reported.
      std::vector<std::pair<uint32_t, std::string>> cercanos;
      for (size_t k = 0; c != d.candidatos.end() && k < c->second.size(); ++k) {
        const EntradaShader& e = d.entradas[c->second[k]];
        std::string diferencias;
        uint32_t cuantas = 0;
        for (size_t i = 0; i < microcodigo.size(); ++i) {
          uint32_t entrante = microcodigo[i];
          for (const ElementoVertice& elemento : e.elementos) {
            const size_t p = size_t(elemento.instruccion) * 3;
            if (i >= p && i < p + 3) {
              entrante &= kFetchConserva[i - p];
            }
          }
          if (entrante == e.microcodigo[i]) {
            continue;
          }
          if (++cuantas <= 24) {
            diferencias += fmt::format(" {}:{:08X}/{:08X}", i, microcodigo[i], e.microcodigo[i]);
          }
        }
        std::string fetch;
        for (const ElementoVertice& elemento : e.elementos) {
          fetch += fmt::format(" {}", elemento.instruccion);
        }
        cercanos.emplace_back(cuantas, fmt::format("frente a n{}: {} palabras distintas{}; fetch en las "
                                                   "instrucciones{}:{}",
                                                   e.numero, cuantas,
                                                   cuantas <= 8 ? " (variante casi identica: receta en shader_layout.json)" : "",
                                                   fetch, diferencias));
      }
      std::stable_sort(cercanos.begin(), cercanos.end(),
                       [](const auto& x, const auto& y) { return x.first < y.first; });
      for (size_t k = 0; k < cercanos.size() && k < 2; ++k) {
        REXLOG_WARN("[nativo] C5a:   {}", cercanos[k].second);
      }
    }
  }
  d.cache.emplace(clave, elegido);
  return elegido;
}

const EntradaShader* ShadersNativos::IdentificarContenedor(
    std::span<const uint8_t> contenedor, bool* coincidencia_microcodigo) const {
  if (coincidencia_microcodigo) {
    *coincidencia_microcodigo = false;
  }
  const Datos& d = *datos_;
  if (!d.cargada) {
    return nullptr;
  }
  const nfsmw::native::Shader* shader = d.biblioteca.Buscar(contenedor);
  if (shader) {
    const auto it = d.por_shader.find(shader);
    return it != d.por_shader.end() ? &d.entradas[it->second] : nullptr;
  }

  // The B3 constructors may copy or normalize the container before creating the object. If its
  // full bytes differ, resolve only when the executable microcode and shader interface still match.
  if (contenedor.size() < 24) {
    return nullptr;
  }
  nfsmw::native::Shader original;
  original.original.assign(contenedor.begin(), contenedor.end());
  Contenedor c{original.original};
  const uint32_t firma = c.U32(0);
  if ((firma & ~1u) != 0x102A1100u) {
    return nullptr;
  }
  original.vertices = (firma & 1u) != 0;
  EntradaShader runtime;
  if (Leer(original, runtime)) {
    return nullptr;
  }
  const auto candidatas = d.candidatos.find({runtime.vertices, uint32_t(runtime.microcodigo.size())});
  if (candidatas == d.candidatos.end()) {
    return nullptr;
  }
  const auto mismos_elementos = [](const std::vector<ElementoVertice>& a,
                                   const std::vector<ElementoVertice>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
      if (a[i].instruccion != b[i].instruccion || a[i].uso != b[i].uso ||
          a[i].indice_uso != b[i].indice_uso) return false;
    }
    return true;
  };
  const auto mismos_samplers = [](const std::vector<SamplerShader>& a,
                                  const std::vector<SamplerShader>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
      if (a[i].registro != b[i].registro || a[i].tipo != b[i].tipo ||
          a[i].mapa_sombras != b[i].mapa_sombras) return false;
    }
    return true;
  };
  const EntradaShader* elegida = nullptr;
  for (uint32_t indice : candidatas->second) {
    const EntradaShader& e = d.entradas[indice];
    if (e.microcodigo != runtime.microcodigo || !mismos_elementos(e.elementos, runtime.elementos) ||
        !mismos_samplers(e.samplers, runtime.samplers) || e.salidas != runtime.salidas ||
        e.constantes_bytes != runtime.constantes_bytes) {
      continue;
    }
    if (elegida && elegida->shader->spirv != e.shader->spirv) {
      return nullptr;  // ambiguous translations are unsafe to bind to a live object
    }
    elegida = &e;
  }
  if (elegida && coincidencia_microcodigo) {
    *coincidencia_microcodigo = true;
  }
  return elegida;
}

const EntradaShader* ShadersNativos::IdentificarMicrocodigoEnDatos(
    bool vertices, std::span<const uint8_t> datos) const {
  const Datos& d = *datos_;
  if (!d.cargada || datos.size() < 4 || (datos.size() & 3)) {
    return nullptr;
  }

  const size_t palabras_datos = datos.size() / 4;
  const EntradaShader* elegida = nullptr;
  for (const EntradaShader& e : d.entradas) {
    if (e.vertices != vertices || e.microcodigo.empty() || e.microcodigo.size() > palabras_datos) {
      continue;
    }
    const size_t palabras_shader = e.microcodigo.size();
    for (size_t inicio = 0; inicio <= palabras_datos - palabras_shader; ++inicio) {
      bool coincide = true;
      for (size_t i = 0; i < palabras_shader; ++i) {
        const size_t indice = inicio + i;
        const size_t b = indice * 4;
        uint32_t palabra = uint32_t(datos[b]) << 24 | uint32_t(datos[b + 1]) << 16 |
                           uint32_t(datos[b + 2]) << 8 | datos[b + 3];
        if (vertices) {
          for (const ElementoVertice& elemento : e.elementos) {
            const size_t fetch = size_t(elemento.instruccion) * 3;
            if (i >= fetch && i < fetch + 3) {
              palabra &= kFetchConserva[i - fetch];
              break;
            }
          }
        }
        if (palabra != e.microcodigo[i]) {
          coincide = false;
          break;
        }
      }
      if (!coincide) {
        continue;
      }
      if (elegida && elegida->shader->spirv != e.shader->spirv) {
        return nullptr;
      }
      if (!elegida) {
        elegida = &e;
      }
    }
  }
  return elegida;
}
// Entries are in library order (Cargar), with gaps where a container was skipped.
const EntradaShader* ShadersNativos::PorNumero(uint32_t numero) const {
  const Datos& d = *datos_;
  if (!d.cargada) {
    return nullptr;
  }
  const auto it = std::lower_bound(d.entradas.begin(), d.entradas.end(), numero,
                                   [](const EntradaShader& e, uint32_t n) { return e.numero < n; });
  return it != d.entradas.end() && it->numero == numero ? &*it : nullptr;
}

const char* NombreUso(uint8_t uso) {
  return kUsos[uso & 0xF];
}

EstadisticasShaders ShadersNativos::Estadisticas() const {
  return datos_->estadisticas;
}

}  // namespace nfsmw::nativo
