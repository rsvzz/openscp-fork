#include "openscp/Libssh2SftpClient.hpp"
#include <libssh2.h>
#include <libssh2_sftp.h>

#include <cstring>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <cstdlib>

// POSIX sockets
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>

namespace openscp {

static bool g_libssh2_inited = false;

Libssh2SftpClient::Libssh2SftpClient() {
  if (!g_libssh2_inited) {
    int rc = libssh2_init(0);
    (void)rc;
    g_libssh2_inited = true;
  }
}

Libssh2SftpClient::~Libssh2SftpClient() {
  disconnect();
}

bool Libssh2SftpClient::tcpConnect(const std::string& host, uint16_t port, std::string& err) {
  struct addrinfo hints{};
  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  char portStr[16];
  snprintf(portStr, sizeof(portStr), "%u", static_cast<unsigned>(port));

  struct addrinfo* res = nullptr;
  int gai = getaddrinfo(host.c_str(), portStr, &hints, &res);
  if (gai != 0) {
    err = std::string("getaddrinfo: ") + gai_strerror(gai);
    return false;
  }

  int s = -1;
  for (auto rp = res; rp != nullptr; rp = rp->ai_next) {
    s = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (s == -1) continue;
    if (::connect(s, rp->ai_addr, rp->ai_addrlen) == 0) {
      // conectado
      sock_ = s;
      freeaddrinfo(res);
      return true;
    }
    ::close(s);
    s = -1;
  }
  freeaddrinfo(res);
  err = "No se pudo conectar al host/puerto.";
  return false;
}

bool Libssh2SftpClient::sshHandshakeAuth(const SessionOptions& opt, std::string& err) {
  session_ = libssh2_session_init();
  if (!session_) { err = "libssh2_session_init falló"; return false; }

  // Handshake
  if (libssh2_session_handshake(session_, sock_) != 0) {
    err = "SSH handshake falló";
    return false;
  }

  // Verificación de host key según política de known_hosts
  if (opt.known_hosts_policy != openscp::KnownHostsPolicy::Off) {
    LIBSSH2_KNOWNHOSTS* nh = libssh2_knownhost_init(session_);
    if (!nh) { err = "No se pudo inicializar known_hosts"; return false; }

    // Ruta efectiva
    std::string khPath;
    if (opt.known_hosts_path.has_value()) khPath = *opt.known_hosts_path;
    else {
      const char* home = std::getenv("HOME");
      if (home) khPath = std::string(home) + "/.ssh/known_hosts";
    }

    bool khLoaded = false;
    if (!khPath.empty()) {
      khLoaded = (libssh2_knownhost_readfile(nh, khPath.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH) >= 0);
    }
    if (!khLoaded && opt.known_hosts_policy == openscp::KnownHostsPolicy::Strict) {
      libssh2_knownhost_free(nh);
      err = "known_hosts no disponible o ilegible (política estricta)";
      return false;
    }

    size_t keylen = 0; int keytype = 0;
    const char* hostkey = (const char*)libssh2_session_hostkey(session_, &keylen, &keytype);
    if (!hostkey || keylen == 0) {
      libssh2_knownhost_free(nh);
      err = "No se pudo obtener host key";
      return false;
    }

    int alg = 0;
    switch (keytype) {
      case LIBSSH2_HOSTKEY_TYPE_RSA:
        alg = LIBSSH2_KNOWNHOST_KEY_SSHRSA; break;
      case LIBSSH2_HOSTKEY_TYPE_DSS:
        alg = LIBSSH2_KNOWNHOST_KEY_SSHDSS; break;
      case LIBSSH2_HOSTKEY_TYPE_ECDSA_256:
#ifdef LIBSSH2_KNOWNHOST_KEY_ECDSA_256
        alg = LIBSSH2_KNOWNHOST_KEY_ECDSA_256; break;
#else
        alg = 0; break;
#endif
      case LIBSSH2_HOSTKEY_TYPE_ECDSA_384:
#ifdef LIBSSH2_KNOWNHOST_KEY_ECDSA_384
        alg = LIBSSH2_KNOWNHOST_KEY_ECDSA_384; break;
#else
        alg = 0; break;
#endif
      case LIBSSH2_HOSTKEY_TYPE_ECDSA_521:
#ifdef LIBSSH2_KNOWNHOST_KEY_ECDSA_521
        alg = LIBSSH2_KNOWNHOST_KEY_ECDSA_521; break;
#else
        alg = 0; break;
#endif
      case LIBSSH2_HOSTKEY_TYPE_ED25519:
#ifdef LIBSSH2_KNOWNHOST_KEY_ED25519
        alg = LIBSSH2_KNOWNHOST_KEY_ED25519; break;
#else
        alg = 0; break;
#endif
      default:
        alg = 0; break;
    }

    int typemask_plain = LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | alg;
    int typemask_hash  = LIBSSH2_KNOWNHOST_TYPE_SHA1  | LIBSSH2_KNOWNHOST_KEYENC_RAW | alg;

    struct libssh2_knownhost* host = nullptr;
    int check = libssh2_knownhost_checkp(nh, opt.host.c_str(), opt.port,
                                         hostkey, keylen, typemask_plain, &host);
    if (check != LIBSSH2_KNOWNHOST_CHECK_MATCH) {
      check = libssh2_knownhost_checkp(nh, opt.host.c_str(), opt.port,
                                       hostkey, keylen, typemask_hash, &host);
    }

    if (check == LIBSSH2_KNOWNHOST_CHECK_MATCH) {
      libssh2_knownhost_free(nh);
    } else if (opt.known_hosts_policy == openscp::KnownHostsPolicy::AcceptNew && check == LIBSSH2_KNOWNHOST_CHECK_NOTFOUND) {
      // TOFU: agregar nuevo host y escribir archivo
      if (khPath.empty()) { libssh2_knownhost_free(nh); err = "Ruta known_hosts no definida"; return false; }
      int addMask = LIBSSH2_KNOWNHOST_TYPE_PLAIN | LIBSSH2_KNOWNHOST_KEYENC_RAW | alg;
      int addrc = libssh2_knownhost_addc(nh, opt.host.c_str(), nullptr,
                                         hostkey, (size_t)keylen,
                                         nullptr, 0, addMask, nullptr);
      if (addrc != 0 || libssh2_knownhost_writefile(nh, khPath.c_str(), LIBSSH2_KNOWNHOST_FILE_OPENSSH) != 0) {
        libssh2_knownhost_free(nh);
        err = "No se pudo agregar/escribir host en known_hosts";
        return false;
      }
      libssh2_knownhost_free(nh);
    } else {
      libssh2_knownhost_free(nh);
      err = (check == LIBSSH2_KNOWNHOST_CHECK_MISMATCH)
            ? "Host key no coincide con known_hosts"
            : "Host desconocido en known_hosts";
      return false;
    }
  }

  // Autenticación
  // Preferimos clave si está presente; si no, password.
  if (opt.private_key_path.has_value()) {
    const char* passphrase = opt.private_key_passphrase ? opt.private_key_passphrase->c_str() : nullptr;
    int rc = libssh2_userauth_publickey_fromfile(session_,
          opt.username.c_str(),
          NULL,  // public key path (NULL: se deriva de la privada)
          opt.private_key_path->c_str(),
          passphrase);
    if (rc != 0) {
      err = "Auth por clave falló";
      return false;
    }
  } else if (opt.password.has_value()) {
    int rc = libssh2_userauth_password(session_,
          opt.username.c_str(),
          opt.password->c_str());
    if (rc != 0) {
      err = "Auth por password falló";
      return false;
    }
  } else {
    err = "Sin credenciales: provee password o ruta de clave privada.";
    return false;
  }

  // SFTP init
  sftp_ = libssh2_sftp_init(session_);
  if (!sftp_) {
    err = "No se pudo inicializar SFTP";
    return false;
  }

  return true;
}

bool Libssh2SftpClient::connect(const SessionOptions& opt, std::string& err) {
  if (connected_) { err = "Ya conectado"; return false; }
  if (!tcpConnect(opt.host, opt.port, err)) return false;
  if (!sshHandshakeAuth(opt, err)) return false;

  connected_ = true;
  return true;
}

void Libssh2SftpClient::disconnect() {
  if (sftp_) {
    libssh2_sftp_shutdown(sftp_);
    sftp_ = nullptr;
  }
  if (session_) {
    libssh2_session_disconnect(session_, "bye");
    libssh2_session_free(session_);
    session_ = nullptr;
  }
  if (sock_ != -1) {
    ::close(sock_);
    sock_ = -1;
  }
  connected_ = false;
}

bool Libssh2SftpClient::list(const std::string& remote_path,
                             std::vector<FileInfo>& out,
                             std::string& err) {
  if (!connected_ || !sftp_) { err = "No conectado"; return false; }

  std::string path = remote_path.empty() ? "/" : remote_path;

  LIBSSH2_SFTP_HANDLE* dir = libssh2_sftp_opendir(sftp_, path.c_str());
  if (!dir) {
    err = "sftp_opendir falló para: " + path;
    return false;
  }

  out.clear();
  out.reserve(64);

  char filename[512];
  char longentry[1024];
  LIBSSH2_SFTP_ATTRIBUTES attrs;

  while (true) {
    memset(&attrs, 0, sizeof(attrs));
    int rc = libssh2_sftp_readdir_ex(dir,
              filename, sizeof(filename),
              longentry, sizeof(longentry),
              &attrs);
    if (rc > 0) {
      // rc = nombre_len
      FileInfo fi{};
      fi.name = std::string(filename, rc);
      fi.is_dir = (attrs.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) ?
                  ((attrs.permissions & LIBSSH2_SFTP_S_IFMT) == LIBSSH2_SFTP_S_IFDIR) : false;
      if (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE)  fi.size  = attrs.filesize;
      if (attrs.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) fi.mtime = attrs.mtime;
      if (fi.name == "." || fi.name == "..") continue;
      out.push_back(std::move(fi));
    } else if (rc == 0) {
      // fin de directorio
      break;
    } else {
      err = "sftp_readdir_ex falló";
      libssh2_sftp_closedir(dir);
      return false;
    }
  }

  libssh2_sftp_closedir(dir);
  return true;
}

// Download a remote file to local path using libssh2_sftp.
// Reports incremental progress and supports cooperative cancellation.
bool Libssh2SftpClient::get(const std::string& remote,
                            const std::string& local,
                            std::string& err,
                            std::function<void(std::size_t,std::size_t)> progress,
                            std::function<bool()> shouldCancel) {
  if (!connected_ || !sftp_) { err = "No conectado"; return false; }

  // Tamaño remoto (para progreso)
  LIBSSH2_SFTP_ATTRIBUTES st{};
  if (libssh2_sftp_stat_ex(sftp_, remote.c_str(), (unsigned)remote.size(),
                           LIBSSH2_SFTP_STAT, &st) != 0) {
    err = "No se pudo obtener stat remoto";
    return false;
  }
  std::size_t total = (st.flags & LIBSSH2_SFTP_ATTR_SIZE) ? (std::size_t)st.filesize : 0;

  // Abrir remoto para lectura
  LIBSSH2_SFTP_HANDLE* rh = libssh2_sftp_open_ex(
      sftp_, remote.c_str(), (unsigned)remote.size(),
      LIBSSH2_FXF_READ, 0, LIBSSH2_SFTP_OPENFILE);
  if (!rh) { err = "No se pudo abrir remoto para lectura"; return false; }

  // Abrir local para escritura (trunc)
  FILE* lf = ::fopen(local.c_str(), "wb");
  if (!lf) {
    libssh2_sftp_close(rh);
    err = "No se pudo abrir archivo local para escribir";
    return false;
  }

  const std::size_t CHUNK = 64 * 1024;
  std::vector<char> buf(CHUNK);
  std::size_t done = 0;

  while (true) {
    if (shouldCancel && shouldCancel()) {
      err = "Cancelado por usuario";
      std::fclose(lf);
      libssh2_sftp_close(rh);
      return false;
    }
    ssize_t n = libssh2_sftp_read(rh, buf.data(), (size_t)buf.size());
    if (n > 0) {
      if (std::fwrite(buf.data(), 1, (size_t)n, lf) != (size_t)n) {
        err = "Escritura local falló";
        std::fclose(lf);
        libssh2_sftp_close(rh);
        return false;
      }
      done += (std::size_t)n;
      if (progress && total) progress(done, total);
    } else if (n == 0) {
      break; // EOF
    } else {
      err = "Lectura remota falló";
      std::fclose(lf);
      libssh2_sftp_close(rh);
      return false;
    }
  }

  std::fclose(lf);
  libssh2_sftp_close(rh);
  return true;
}

// Upload a local file to remote path (create/truncate).
// Reports incremental progress and supports cooperative cancellation.
bool Libssh2SftpClient::put(const std::string& local,
                            const std::string& remote,
                            std::string& err,
                            std::function<void(std::size_t,std::size_t)> progress,
                            std::function<bool()> shouldCancel) {
  if (!connected_ || !sftp_) { err = "No conectado"; return false; }

  // Abrir local para lectura
  FILE* lf = ::fopen(local.c_str(), "rb");
  if (!lf) { err = "No se pudo abrir archivo local para lectura"; return false; }

  // Tamaño local
  std::fseek(lf, 0, SEEK_END);
  long fsz = std::ftell(lf);
  std::fseek(lf, 0, SEEK_SET);
  std::size_t total = fsz > 0 ? (std::size_t)fsz : 0;

  // Abrir remoto para escritura (crea/trunca)
  LIBSSH2_SFTP_HANDLE* wh = libssh2_sftp_open_ex(
      sftp_, remote.c_str(), (unsigned)remote.size(),
      LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT | LIBSSH2_FXF_TRUNC,
      0644, LIBSSH2_SFTP_OPENFILE);
  if (!wh) {
    std::fclose(lf);
    err = "No se pudo abrir remoto para escritura";
    return false;
  }

  const std::size_t CHUNK = 64 * 1024;
  std::vector<char> buf(CHUNK);
  std::size_t done = 0;

  while (true) {
    size_t n = std::fread(buf.data(), 1, buf.size(), lf);
    if (n > 0) {
      char* p = buf.data();
      size_t remain = n;
      while (remain > 0) {
        if (shouldCancel && shouldCancel()) {
          err = "Cancelado por usuario";
          libssh2_sftp_close(wh);
          std::fclose(lf);
          return false;
        }
        ssize_t w = libssh2_sftp_write(wh, p, remain);
        if (w < 0) {
          err = "Escritura remota falló";
          libssh2_sftp_close(wh);
          std::fclose(lf);
          return false;
        }
        remain -= (size_t)w;
        p += w;
        done += (size_t)w;
        if (progress && total) progress(done, total);
      }
    } else {
      if (std::ferror(lf)) {
        err = "Lectura local falló";
        libssh2_sftp_close(wh);
        std::fclose(lf);
        return false;
      }
      break; // EOF
    }
  }

  libssh2_sftp_close(wh);
  std::fclose(lf);
  return true;
}

// Lightweight existence check using sftp_stat.
bool Libssh2SftpClient::exists(const std::string& remote_path,
                               bool& isDir,
                               std::string& err) {
  isDir = false;
  if (!connected_ || !sftp_) { err = "No conectado"; return false; }

  LIBSSH2_SFTP_ATTRIBUTES st{};
  int rc = libssh2_sftp_stat_ex(
      sftp_, remote_path.c_str(), (unsigned)remote_path.size(),
      LIBSSH2_SFTP_STAT, &st);

  if (rc == 0) {
    if (st.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) {
      isDir = ((st.permissions & LIBSSH2_SFTP_S_IFMT) == LIBSSH2_SFTP_S_IFDIR);
    }
    return true; // existe
  }

  unsigned long sftp_err = libssh2_sftp_last_error(sftp_);
  if (sftp_err == LIBSSH2_FX_NO_SUCH_FILE || sftp_err == LIBSSH2_FX_FAILURE) {
    err.clear();
    return false; // no existe
  }

  err = "stat remoto falló";
  return false;
}

// Detailed remote metadata (like stat). Returns false if the path doesn't exist.
bool Libssh2SftpClient::stat(const std::string& remote_path,
                             FileInfo& info,
                             std::string& err) {
  if (!connected_ || !sftp_) { err = "No conectado"; return false; }

  LIBSSH2_SFTP_ATTRIBUTES st{};
  int rc = libssh2_sftp_stat_ex(
      sftp_, remote_path.c_str(), (unsigned)remote_path.size(),
      LIBSSH2_SFTP_STAT, &st);
  if (rc != 0) {
    unsigned long sftp_err = libssh2_sftp_last_error(sftp_);
    if (sftp_err == LIBSSH2_FX_NO_SUCH_FILE || sftp_err == LIBSSH2_FX_FAILURE) {
      err.clear();
      return false; // no existe
    }
    err = "stat remoto falló";
    return false;
  }
  info.name.clear();
  info.is_dir = (st.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) ?
                ((st.permissions & LIBSSH2_SFTP_S_IFMT) == LIBSSH2_SFTP_S_IFDIR) : false;
  info.size  = (st.flags & LIBSSH2_SFTP_ATTR_SIZE) ? (std::uint64_t)st.filesize : 0;
  info.mtime = (st.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) ? (std::uint64_t)st.mtime : 0;
  return true;
}

bool Libssh2SftpClient::mkdir(const std::string& remote_dir,
                              std::string& err,
                              unsigned int mode) {
  if (!connected_ || !sftp_) { err = "No conectado"; return false; }
  int rc = libssh2_sftp_mkdir(sftp_, remote_dir.c_str(), mode);
  if (rc != 0) { err = "sftp_mkdir falló"; return false; }
  return true;
}

bool Libssh2SftpClient::removeFile(const std::string& remote_path,
                                   std::string& err) {
  if (!connected_ || !sftp_) { err = "No conectado"; return false; }
  int rc = libssh2_sftp_unlink(sftp_, remote_path.c_str());
  if (rc != 0) { err = "sftp_unlink falló"; return false; }
  return true;
}

bool Libssh2SftpClient::removeDir(const std::string& remote_dir,
                                  std::string& err) {
  if (!connected_ || !sftp_) { err = "No conectado"; return false; }
  int rc = libssh2_sftp_rmdir(sftp_, remote_dir.c_str());
  if (rc != 0) { err = "sftp_rmdir falló (¿directorio no vacío?)"; return false; }
  return true;
}

bool Libssh2SftpClient::rename(const std::string& from,
                               const std::string& to,
                               std::string& err,
                               bool /*overwrite*/) {
  if (!connected_ || !sftp_) { err = "No conectado"; return false; }
  int rc = libssh2_sftp_rename(sftp_, from.c_str(), to.c_str());
  if (rc != 0) { err = "sftp_rename falló"; return false; }
  return true;
}
} // namespace openscp
