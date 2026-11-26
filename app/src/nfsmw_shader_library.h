#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace nfsmw::native {

// The original container also identifies constants and interface. The physical
// code alone is not enough. It is looked up before the driver creates its object.
struct Shader {
  std::vector<uint8_t> original;
  std::vector<uint32_t> spirv;
  uint64_t huella = 0;
  bool vertices = false;
};

class BibliotecaShaders {
 public:
  // Transactional load: an invalid file does not replace the library.
  void Cargar(std::span<const uint8_t> archivo);
  void Cargar(const std::filesystem::path& archivo);
  const std::vector<Shader>& shaders() const { return shaders_; }

 private:
  std::vector<Shader> shaders_;
};

}  // namespace nfsmw::native
