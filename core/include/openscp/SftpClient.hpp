#pragma once
#include "SftpTypes.hpp"
#include <functional>

namespace openscp {

class SftpClient {
public:
  using ProgressCB = std::function<void(double)>; // 0..1

  virtual ~SftpClient() = default;

  virtual bool connect(const SessionOptions& opt, std::string& err) = 0;
  virtual void disconnect() = 0;
  virtual bool isConnected() const = 0;

  virtual bool list(const std::string& remote_path,
                    std::vector<FileInfo>& out,
                   std::string& err) = 0;

  // Al final de la clase SftpClient (interfaz)
  virtual bool get(const std::string& remote,
                 const std::string& local,
                 std::string& err,
                 std::function<void(std::size_t /*done*/, std::size_t /*total*/)> progress = {},
                 std::function<bool()> shouldCancel = {}) = 0;

  virtual bool put(const std::string& local,
                 const std::string& remote,
                 std::string& err,
                 std::function<void(std::size_t /*done*/, std::size_t /*total*/)> progress = {},
                 std::function<bool()> shouldCancel = {}) = 0;

  // ¿Existe el path remoto? Devuelve true si existe; pone isDir si se pudo determinar.
  // En caso de "no existe", devuelve false y deja err vacío.
  // En caso de error real (permisos, etc.), devuelve false y llena err.
  virtual bool exists(const std::string& remote_path,
                      bool& isDir,
                      std::string& err) = 0;

  // Información detallada de un path remoto (como stat). Devuelve true si existe.
  virtual bool stat(const std::string& remote_path,
                    FileInfo& info,
                    std::string& err) = 0;

  // Operaciones de archivos/carpetas (lado remoto)
  virtual bool mkdir(const std::string& remote_dir,
                     std::string& err,
                     unsigned int mode = 0755) = 0;

  virtual bool removeFile(const std::string& remote_path,
                          std::string& err) = 0;

  virtual bool removeDir(const std::string& remote_dir,
                         std::string& err) = 0;

  virtual bool rename(const std::string& from,
                      const std::string& to,
                      std::string& err,
                      bool overwrite = false) = 0;




};

} // namespace openscp
