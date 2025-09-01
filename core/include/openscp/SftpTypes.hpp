#pragma once
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace openscp {

// Política de validación de known_hosts
enum class KnownHostsPolicy {
  Strict,     // Requiere coincidencia en known_hosts (falla si no está o no coincide)
  AcceptNew,  // Acepta y guarda nuevos hosts; rechaza cambios de clave
  Off         // Sin verificación (no recomendado)
};

struct FileInfo {
  std::string name;
  bool        is_dir = false;
  std::uint64_t size  = 0;
  std::uint64_t mtime = 0; // epoch segundos (aprox)
};

struct SessionOptions {
  std::string host;
  std::uint16_t port = 22;
  std::string username;

  std::optional<std::string> password;
  std::optional<std::string> private_key_path;
  std::optional<std::string> private_key_passphrase;

  // Seguridad SSH
  std::optional<std::string> known_hosts_path; // por defecto: ~/.ssh/known_hosts
  KnownHostsPolicy known_hosts_policy = KnownHostsPolicy::Strict;
};

} // namespace openscp
