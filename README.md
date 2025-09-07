# OpenSCP (Pre-alpha)

**OpenSCP** es un explorador de archivos estilo *two-panel commander* escrito en **C++/Qt**, con soporte para **SFTP remoto** basado en `libssh2`.

El objetivo del proyecto es ofrecer una alternativa ligera y multiplataforma a herramientas como WinSCP, enfocada en la simplicidad y en un código abierto y extensible.

---

## Características actuales (v0.5.0)

### Exploración en dos paneles
- Panel izquierdo y derecho navegables de manera independiente.  
- Cada panel con su propia barra de herramientas (botón **Arriba** incluido).  
- Arrastrar-y-soltar entre paneles para copiar/mover.  
- Menú contextual remoto con acciones: Descargar, Subir, Renombrar, Borrar, Nueva carpeta y Cambiar permisos.  

### Operaciones locales
- Copiar (F5), mover (F6) y eliminar (Supr) de forma recursiva.  
- Manejo de conflictos: sobrescribir, omitir, renombrar, aplicar a todos.  

### SFTP (libssh2)
- Conexión con:
  - Usuario/contraseña.  
  - Clave privada con passphrase.  
  - **keyboard-interactive** (ej. OTP/2FA).  
  - **ssh-agent** (clave ya cargada en agente).  
- Validación de host key con política seleccionable:
  - Estricto.  
  - Aceptar nuevo (TOFU).  
  - Sin verificación.  
- Guardado automático en `known_hosts` cuando procede.  
- Navegación remota completa con doble clic para previsualizar archivos (descarga temporal).  
- Crear carpeta, renombrar, y borrar y cambiar permisos (chmod) con opción recursiva.  
- Comprobación de permisos de escritura antes de habilitar acciones.  

### Transferencias
- **Cola visible de transferencias** con controles: pausar, reanudar, cancelar y reintentar.  
- Soporte de reanudación por archivo.  
- Límites de velocidad globales y por tarea.  
- Barra de progreso global (descargas) y progreso por archivo (subidas/descargas).  
- Reconexión automática con backoff durante transferencias.  

### UX / Interfaz Qt
- Splitter central ajustable.  
- Barra de estado con mensajes más detallados.  
- Columnas remotas ordenables y con tamaños ajustados automáticamente.  
- Atajos de teclado:  
  - **F5**: Copiar / Subir.  
  - **F6**: Mover.  
  - **F7**: Descargar.  
  - **Supr**: Eliminar.  

### Gestor de sitios
- Lista de servidores con credenciales guardadas.  
- Soporte de almacenamiento en **Keychain (macOS)**.  
- Migración desde configuraciones antiguas.  

---

## Roadmap (corto plazo)

- **Protocolos adicionales**: añadir soporte para SCP; planificar FTP/FTPS/WebDAV.  
- **Concurrencia real**: múltiples conexiones simultáneas en la cola (sin bloqueo global por `sftpMutex_`).  
- **Secretos en Linux**: integración con Libsecret/Secret Service.  
- **Proxy / Jump host**: soporte SOCKS5, HTTP CONNECT y “ProxyJump”.  
- **Sincronización**: modo comparar+sincronizar y “mantener actualizado” con filtros/ignorados.  
- **Robustez de cola**: persistir estado en disco, reanudación tras reinicios, checksums opcionales.  
- **Seguridad avanzada**: hashes en `known_hosts`, mostrar algoritmos/KEX acordados, más políticas.  

---

## Requisitos

- Qt 6.x  
- libssh2  
- CMake 3.16+  
- Compilador con soporte de C++17  

---

## Compilación

```bash
git clone https://github.com/tuusuario/openscp.git
cd openscp
cmake -S . -B build
cmake --build build
./build/openscp_hello
```

## Localización (i18n)

- Archivos de traducción (`.ts`): `src/openscp_ts/openscp_es.ts`, `src/openscp_ts/openscp_en.ts`.
- Durante la compilación, CMake/Qt genera los `.qm` y los incrusta como recursos en `:/i18n`.
- Para actualizar traducciones, edita los `.ts` y recompila; Qt ejecuta `lrelease` automáticamente.
