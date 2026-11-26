// nfsmw - native renderer, part C5a: identify in the NFSSPV library the
// shaders the game's D3D uploads to the ring (PM4_IM_LOAD and
// IM_LOAD_IMMEDIATE).
//
// The ring only carries the microcode. The library stores the original
// compiled container (header, constant table, definitions and microcode)
// with its SPIR-V translation made by XenosRecomp.
//
// Pixel shaders arrive as is and are identified by their microcode. Vertex
// shaders do not: D3D reorders the fetches, changes their swizzles and
// nulls the outputs the pixel shader does not read (measured), so their
// identity comes from the device objects (nfsmw_nativo_ganchos.h) and only
// the vertex input is read from the patched microcode.

#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace nfsmw::native {
struct Shader;
}

namespace nfsmw::nativo {

// Vertex declaration element of the container (XenosRecomp shader.h).
struct ElementoVertice {
  uint16_t instruccion = 0;  // index of the fetch instruction in the microcode
  uint8_t uso = 0;           // DeclUsage: 0 posicion, 3 normal, 5 texcoord, 10 color...
  uint8_t indice_uso = 0;
};

// Sampler from the constant table: the register is the texture's fetch constant
// in pixel shaders.
struct SamplerShader {
  uint16_t registro = 0;
  uint16_t tipo = 0;  // D3DXPARAMETER_TYPE: 10-12 = 2D, 13 = 3D, 14 = cubo
  // It is called SHADOWMAP_SAMPLER in the constant table: it is the shadow map sampling the library rewrites
  // (tfetch2DSombra and tfetch2DSombraMin). nfsmw_nativo_sombra_minimo.
  bool mapa_sombras = false;
};

struct LiteralFloat4 {
  uint32_t registro;
  std::array<uint32_t, 4> valor;
};

struct EntradaShader {
  const nfsmw::native::Shader* shader = nullptr;  // contenedor original y SPIR-V
  bool vertices = false;
  uint32_t numero = 0;  // position in the library, for the reports
  std::vector<uint32_t> microcodigo;  // in host byte order, with the fetches masked
  uint64_t huella = 0;                // XXH3 of that masked microcode
  std::vector<ElementoVertice> elementos;
  std::vector<SamplerShader> samplers;
  uint32_t salidas = 0;  // pixel shader: bits COLOR0..3 y DEPTH (PixelShaderOutputs)
  // OpKill in the SPIR-V. XenosRecomp puts one in every pixel shader for the alpha test (guarded by
  // SPEC_CONSTANT_ALPHA_TEST), so it only really discards if there is more than one.
  uint32_t kills = 0;
  bool descarta = false;
  // Its SPIR-V has tfetch2DSombraMin (the NFSMW_MARCA_SOMBRA_MINIMO constant from shader_common.h): with
  // kSpecSombraMinimo it takes the minimum of the shadow map and its pair. nfsmw_nativo_sombra_minimo.
  bool sombra_minimo = false;
  // Bytes of its float constant buffer that the SPIR-V reads (16 per register).
  uint32_t constantes_bytes = 256 * 16;
  std::vector<LiteralFloat4> literais;
};

class ShadersNativos {
 public:
  ShadersNativos();
  ~ShadersNativos();

  // false, with a warning in the log, if the library is missing or damaged.
  bool Cargar(const std::filesystem::path& archivo);
  bool cargada() const;

  // Microcode already in host byte order. nullptr if it is not in the library.
  // Only the ring thread uses it.
  const EntradaShader* Identificar(bool vertices, std::span<const uint32_t> microcodigo);

 private:
  struct Datos;
  std::unique_ptr<Datos> datos_;
};

}  // namespace nfsmw::nativo
