# OpenSCP

Explorador de archivos de **dos paneles** con objetivo de SFTP multiplataforma (macOS/Linux).

## Estado
- GUI mínima (Qt 6) con dos paneles locales (copiar/mover/borrar).

## Proximamente...

Panel derecho remoto (SFTP) usando libssh2.

## Cómo compilar (macOS)
```bash
export CMAKE_PREFIX_PATH="$HOME/Qt/6.9.2/macos"
cmake -S . -B build
cmake --build build
./build/openscp_hello

