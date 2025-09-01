# OpenSCP (Pre-alpha)

**OpenSCP** es un explorador de archivos estilo *two-panel commander* escrito en **C++/Qt**, con soporte para **SFTP remoto** basado en `libssh2`.

El objetivo del proyecto es ofrecer una alternativa ligera y multiplataforma a herramientas como *WinSCP*, enfocada en la simplicidad y en un código abierto y extensible.

---

## Características (v0.4.0)

- **Exploración en dos paneles**  
  - Panel izquierdo y derecho, navegables de manera independiente.  
  - Cada panel tiene su propia **toolbar** con botón **Arriba** para retroceder al directorio padre.  

- **Operaciones locales**  
  - Copiar (`F5`) y mover (`F6`) recursivo.  
  - Eliminar (`Supr`).  
  - Manejo de conflictos: sobrescribir, omitir, renombrar y aplicar a todos.

- **Soporte SFTP (libssh2)**  
  - Conexión con usuario/contraseña o clave privada.  
  - Validación de `known_hosts` (estricto por defecto).  
  - Navegación de directorios remotos.  
  - Descargar (F7) archivos y carpetas (recursivo) con barra de progreso global, cancelación y resolución de colisiones (sobrescribir/omitir/renombrar/… todo).  
  - Subir (F5) archivos y carpetas (recursivo) con progreso y cancelación.  
  - Crear carpeta, renombrar y borrar (incluye borrado recursivo) en remoto.  

- **Interfaz Qt**  
  - Splitter central ajustable.  
  - Barra de estado con mensajes.  
  - Atajos de teclado para todas las operaciones básicas.  

---

## Roadmap

- [ ] Descarga recursiva con estimación total de tamaño y ETA.  
- [ ] Reintentos, colas avanzadas y “reanudación” (resume).  
- [ ] Vista de permisos/propietarios y edición chmod/chown.  
- [ ] Preferencias: selector/política de `known_hosts` desde UI.  
- [ ] Mejoras de UX (drag & drop, menú contextual).  

---

## Requisitos

- [Qt 6.x](https://www.qt.io/download) (módulos **Core**, **Widgets**, **Gui**)  
- [libssh2](https://www.libssh2.org/)  
- [CMake 3.16+](https://cmake.org/download/)  
- Compilador con soporte de **C++17** o superior

---

## Compilación

### Linux / macOS

```bash
# Clonar el repositorio
git clone https://github.com/tuusuario/OpenSCP-hello.git
cd OpenSCP-hello

# Generar los archivos de build
cmake -S . -B build

# Compilar
cmake --build build

# Ejecutar
./build/openscp_hello
```

## Estado
Este release (v0.4.0) marca una versión temprana usable (pre‑alpha) con transferencias recursivas y validación de known_hosts.
Se recomienda solo para pruebas y retroalimentación temprana.
